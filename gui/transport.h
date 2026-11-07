#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _FydelisTransport FydelisTransport;

/* Chamado na main thread (main context do GTK). */
typedef void (*FydelisLinkStateFunc)(gboolean connected, gpointer user_data);

FydelisTransport *fydelis_transport_new(const gchar *host, guint16 port);
void              fydelis_transport_free(FydelisTransport *t);

void     fydelis_transport_set_link_handler(FydelisTransport *t,
                                            FydelisLinkStateFunc cb,
                                            gpointer user_data);

gboolean fydelis_transport_start(FydelisTransport *t, GError **error);
void     fydelis_transport_stop(FydelisTransport *t);

/* Coalescido: enviar 1000x/s é seguro. */
void fydelis_transport_set_joint(FydelisTransport *t, const gchar *joint, gdouble radians);

/* Comandos de segurança (enviados mesmo se a ligação estiver em baixo). */
void fydelis_transport_estop(FydelisTransport *t);
void fydelis_transport_enable(FydelisTransport *t);

G_END_DECLS
