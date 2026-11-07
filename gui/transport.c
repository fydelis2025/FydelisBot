#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "transport.h"

#define CONNECT_TIMEOUT_MS     2000
#define SEND_TIMEOUT_MS         500
#define KEEPALIVE_INTERVAL_MS   500
#define BACKOFF_MIN_MS          100
#define BACKOFF_MAX_MS         2000

struct _FydelisTransport {
    gchar   *host;
    guint16  port;
    GMutex   mutex;
    GCond    cond;
    GHashTable *pending;
    GQueue  *raw;
    gboolean dirty;
    gboolean running;
    GThread *thread;
    guint32  seq;
    FydelisLinkStateFunc link_cb;
    gpointer             link_ud;
    gboolean             link_state;
};

static gint64 now_us(void) { return g_get_monotonic_time(); }

typedef struct { FydelisTransport *t; gboolean connected; } LinkEvent;

static gboolean emit_link(gpointer data)
{
    LinkEvent *e = data;
    FydelisLinkStateFunc cb = NULL;
    gpointer ud = NULL;
    g_mutex_lock(&e->t->mutex);
    cb = e->t->link_cb;
    ud = e->t->link_ud;
    g_mutex_unlock(&e->t->mutex);
    if (cb) cb(e->connected, ud);
    g_free(e);
    return G_SOURCE_REMOVE;
}

static void set_link_state(FydelisTransport *t, gboolean connected)
{
    g_mutex_lock(&t->mutex);
    if (t->link_state == connected) {
        g_mutex_unlock(&t->mutex);
        return;
    }
    t->link_state = connected;
    g_mutex_unlock(&t->mutex);
    LinkEvent *e = g_new0(LinkEvent, 1);
    e->t = t;
    e->connected = connected;
    g_main_context_invoke(NULL, emit_link, e);
}

static int connect_to(const gchar *host, guint16 port)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL, *rp;
    gchar portstr[8];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    g_snprintf(portstr, sizeof portstr, "%u", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return -1;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        if (errno == EINPROGRESS || errno == EINTR) {
            struct pollfd p = { .fd = fd, .events = POLLOUT };
            if (poll(&p, 1, CONNECT_TIMEOUT_MS) == 1) {
                int err = 0;
                socklen_t len = sizeof err;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)
                    break;
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    }
    return fd;
}

static gboolean send_all(int fd, const gchar *buf, gsize len)
{
    gsize off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n > 0) { off += n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd p = { .fd = fd, .events = POLLOUT };
            if (poll(&p, 1, SEND_TIMEOUT_MS) <= 0) return FALSE;
            continue;
        }
        return FALSE;
    }
    return TRUE;
}

static gboolean peer_gone(int fd)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int rc = poll(&p, 1, 0);
    if (rc < 0) return errno != EINTR;
    if (rc == 0) return FALSE;
    gchar tmp[256];
    ssize_t n = recv(fd, tmp, sizeof tmp, 0);
    if (n == 0) return TRUE;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return FALSE;
    return n < 0;
}

static void session(FydelisTransport *t, int fd)
{
    GString *payload = g_string_new(NULL);
    gboolean handshaked = FALSE;
    while (TRUE) {
        g_mutex_lock(&t->mutex);
        while (t->running && !t->dirty)
            if (!g_cond_wait_until(&t->cond, &t->mutex,
                                   now_us() + KEEPALIVE_INTERVAL_MS * 1000))
                break;
        if (!t->running) {
            g_mutex_unlock(&t->mutex);
            break;
        }
        const gboolean has_cmds = t->dirty;
        g_string_truncate(payload, 0);
        if (!handshaked) {
            g_string_append(payload, "HELLO 1\n");
            handshaked = TRUE;
        }
        for (GList *l = t->raw->head; l != NULL; l = l->next)
            g_string_append_printf(payload, "%s\n", (const gchar *)l->data);
        g_queue_clear_full(t->raw, g_free);
        if (has_cmds) {
            t->dirty = FALSE;
            GHashTableIter it;
            gpointer k, v;
            gchar num[32];
            g_hash_table_iter_init(&it, t->pending);
            while (g_hash_table_iter_next(&it, &k, &v)) {
                g_ascii_formatd(num, sizeof num, "%.4f", *(double *)v);
                g_string_append_printf(payload, "SET %u %s %s\n",
                                       ++t->seq, (const gchar *)k, num);
            }
            g_hash_table_remove_all(t->pending);
        }
        g_mutex_unlock(&t->mutex);
        if (payload->len > 0 && !send_all(fd, payload->str, payload->len))
            break;
        if (!has_cmds && peer_gone(fd))
            break;
    }
    g_string_free(payload, TRUE);
}

static gpointer worker(gpointer data)
{
    FydelisTransport *t = data;
    guint backoff = BACKOFF_MIN_MS;
    for (;;) {
        g_mutex_lock(&t->mutex);
        const gboolean run = t->running;
        g_mutex_unlock(&t->mutex);
        if (!run) break;
        int fd = connect_to(t->host, t->port);
        if (fd < 0) {
            set_link_state(t, FALSE);
            g_usleep((gulong)backoff * 1000);
            backoff = (backoff * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : backoff * 2;
            continue;
        }
        backoff = BACKOFF_MIN_MS;
        set_link_state(t, TRUE);
        session(t, fd);
        close(fd);
        set_link_state(t, FALSE);
        g_mutex_lock(&t->mutex);
        g_hash_table_remove_all(t->pending);
        t->dirty = FALSE;
        g_mutex_unlock(&t->mutex);
    }
    return NULL;
}

FydelisTransport *fydelis_transport_new(const gchar *host, guint16 port)
{
    FydelisTransport *t = g_new0(FydelisTransport, 1);
    t->host = g_strdup(host ? host : "127.0.0.1");
    t->port = port;
    t->pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    t->raw = g_queue_new();
    g_mutex_init(&t->mutex);
    g_cond_init(&t->cond);
    return t;
}

void fydelis_transport_set_link_handler(FydelisTransport *t,
                                        FydelisLinkStateFunc cb, gpointer user_data)
{
    g_mutex_lock(&t->mutex);
    t->link_cb = cb;
    t->link_ud = user_data;
    g_mutex_unlock(&t->mutex);
}

gboolean fydelis_transport_start(FydelisTransport *t, GError **error)
{
    g_mutex_lock(&t->mutex);
    if (t->running) {
        g_mutex_unlock(&t->mutex);
        return TRUE;
    }
    t->running = TRUE;
    g_mutex_unlock(&t->mutex);
    t->thread = g_thread_try_new("fydelis-link", worker, t, error);
    if (!t->thread) {
        g_mutex_lock(&t->mutex);
        t->running = FALSE;
        g_mutex_unlock(&t->mutex);
        return FALSE;
    }
    return TRUE;
}

void fydelis_transport_stop(FydelisTransport *t)
{
    if (!t) return;
    g_mutex_lock(&t->mutex);
    t->running = FALSE;
    t->link_cb = NULL;
    t->link_ud = NULL;
    g_cond_broadcast(&t->cond);
    g_mutex_unlock(&t->mutex);
    if (t->thread) {
        g_thread_join(t->thread);
        t->thread = NULL;
    }
}

void fydelis_transport_set_joint(FydelisTransport *t, const gchar *joint, gdouble radians)
{
    g_return_if_fail(t && joint);
    g_mutex_lock(&t->mutex);
    double *slot = g_hash_table_lookup(t->pending, joint);
    if (!slot) {
        slot = g_new(double, 1);
        g_hash_table_insert(t->pending, g_strdup(joint), slot);
    }
    *slot = radians;
    t->dirty = TRUE;
    g_cond_signal(&t->cond);
    g_mutex_unlock(&t->mutex);
}

static void queue_raw(FydelisTransport *t, const gchar *line)
{
    g_mutex_lock(&t->mutex);
    g_queue_push_tail(t->raw, g_strdup(line));
    t->dirty = TRUE;
    g_cond_signal(&t->cond);
    g_mutex_unlock(&t->mutex);
}

void fydelis_transport_estop(FydelisTransport *t)  { queue_raw(t, "ESTOP"); }
void fydelis_transport_enable(FydelisTransport *t) { queue_raw(t, "ENABLE"); }

void fydelis_transport_free(FydelisTransport *t)
{
    if (!t) return;
    fydelis_transport_stop(t);
    g_hash_table_destroy(t->pending);
    g_queue_free_full(t->raw, g_free);
    g_cond_clear(&t->cond);
    g_mutex_clear(&t->mutex);
    g_free(t->host);
    g_free(t);
}

