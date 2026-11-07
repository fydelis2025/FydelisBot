#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "transport.h"

#define APP_ID    "com.fydelis.humanoidbridge"
#define APP_TITLE "FydelisBot Humanoid — Consola de Juntas"

typedef struct {
    const gchar *name;
    const gchar *label;
    gdouble      min;
    gdouble      max;
    gdouble      step;
    gdouble      init;
} JointSpec;

static const JointSpec k_joints[] = {
    { "neck_joint",           "Rotação do Pescoço", -1.57, 1.57, 0.01, 0.0 },
    { "right_shoulder_joint", "Ombro Direito",      -3.14, 3.14, 0.01, 0.0 },
    { "left_shoulder_joint",  "Ombro Esquerdo",     -3.14, 3.14, 0.01, 0.0 },
    { "right_hip_joint",      "Anca Direita",       -1.57, 1.57, 0.01, 0.0 },
    { "left_hip_joint",       "Anca Esquerda",      -1.57, 1.57, 0.01, 0.0 },
};

typedef struct _App App;

typedef struct {
    App                *app;
    const JointSpec    *spec;
    GtkWidget          *scale;
    GtkWidget          *value_label;
} JointRow;

struct _App {
    GtkApplication    *app;
    GtkWidget         *window;
    GtkWidget         *status_label;
    GtkWidget         *estop_button;
    GtkWidget         *zero_button;
    FydelisTransport  *transport;
    JointRow           rows[G_N_ELEMENTS(k_joints)];
    gboolean           estopped;
};

static gchar *format_rad(gdouble v) {
    return g_strdup_printf("%+.2f rad", v);
}

static void sync_all(App *self) {
    if (self->estopped) return;
    for (gsize i = 0; i < G_N_ELEMENTS(self->rows); i++)
        fydelis_transport_set_joint(self->transport,
                                    self->rows[i].spec->name,
                                    gtk_range_get_value(GTK_RANGE(self->rows[i].scale)));
}

static void on_scale_changed(GtkRange *range, gpointer user_data) {
    JointRow *row = user_data;
    gdouble value = gtk_range_get_value(range);
    gchar *txt = format_rad(value);
    gtk_label_set_text(GTK_LABEL(row->value_label), txt);
    g_free(txt);
    if (row->app->estopped) return;
    fydelis_transport_set_joint(row->app->transport, row->spec->name, value);
}

static void set_estopped(App *self, gboolean on) {
    self->estopped = on;
    for (gsize i = 0; i < G_N_ELEMENTS(self->rows); i++)
        gtk_widget_set_sensitive(self->rows[i].scale, !on);
    gtk_widget_set_sensitive(self->zero_button, !on);

    if (on) {
        fydelis_transport_estop(self->transport);
        gtk_button_set_label(GTK_BUTTON(self->estop_button), "Rearmar");
        gtk_widget_add_css_class(self->estop_button, "suggested-action");
    } else {
        fydelis_transport_enable(self->transport);
        gtk_button_set_label(GTK_BUTTON(self->estop_button), "PARAGEM DE EMERGÊNCIA");
        gtk_widget_remove_css_class(self->estop_button, "suggested-action");
        sync_all(self);
    }
}

static void on_confirm_rearm(GObject *src, GAsyncResult *res, gpointer user_data) {
    GtkAlertDialog *dlg = GTK_ALERT_DIALOG(src);
    App *self = user_data;
    GError *err = NULL;
    int choice = gtk_alert_dialog_choose_finish(dlg, res, &err);
    if (err) { g_error_free(err); return; }
    if (choice == 1) set_estopped(self, FALSE);
}

static void on_estop_clicked(GtkButton *btn, gpointer user_data) {
    App *self = user_data;
    if (!self->estopped) {
        set_estopped(self, TRUE);
        return;
    }
    GtkAlertDialog *dlg = gtk_alert_dialog_new(
        "Rearmar o robô?\n\nOs valores dos sliders serão reenviados.");
    gtk_alert_dialog_set_buttons(dlg, (const char *[]){ "Cancelar", "Rearmar", NULL });
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_default_button(dlg, 0);
    gtk_alert_dialog_choose(dlg, GTK_WINDOW(self->window), NULL, on_confirm_rearm, self);
    g_object_unref(dlg);
}

static void on_zero_clicked(GtkButton *btn, gpointer user_data) {
    App *self = user_data;
    for (gsize i = 0; i < G_N_ELEMENTS(self->rows); i++)
        gtk_range_set_value(GTK_RANGE(self->rows[i].scale), self->rows[i].spec->init);
}

static void on_link_state(gboolean connected, gpointer user_data) {
    App *self = user_data;
    gtk_label_set_text(GTK_LABEL(self->status_label),
        connected ? "● Ligado ao simulador" : "● Sem ligação — a tentar…");
    gtk_widget_remove_css_class(self->status_label, connected ? "status-off" : "status-ok");
    gtk_widget_add_css_class(self->status_label, connected ? "status-ok" : "status-off");
    if (connected && !self->estopped) sync_all(self);
}

static void install_css(void) {
    static const gchar css[] =
        ".status-ok   { color: #2ec27e; font-weight: 600; }"
        ".status-off  { color: #e01b24; font-weight: 600; }"
        ".joint-name  { font-weight: 600; }"
        ".value-read  { font-family: monospace; color: #5e5c64; }"
        "scale trough { min-height: 6px; }";
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void activate(GtkApplication *app, gpointer user_data) {
    App *self = user_data;
    GtkWidget *header, *grid, *box;

    self->app = app;
    self->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(self->window), APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(self->window), 560, 380);

    header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(self->window), header);
    self->status_label = gtk_label_new("● A ligar…");
    gtk_widget_add_css_class(self->status_label, "status-off");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), self->status_label);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_window_set_child(GTK_WINDOW(self->window), box);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_box_append(GTK_BOX(box), grid);

    for (gsize i = 0; i < G_N_ELEMENTS(k_joints); i++) {
        JointRow *row = &self->rows[i];
        row->app = self;
        row->spec = &k_joints[i];

        GtkWidget *name = gtk_label_new(k_joints[i].label);
        gtk_widget_add_css_class(name, "joint-name");
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), name, 0, (int)i, 1, 1);

        row->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
            k_joints[i].min, k_joints[i].max, k_joints[i].step);
        gtk_range_set_value(GTK_RANGE(row->scale), k_joints[i].init);
        gtk_scale_set_draw_value(GTK_SCALE(row->scale), FALSE);
        gtk_scale_add_mark(GTK_SCALE(row->scale), 0.0, GTK_POS_BOTTOM, NULL);
        gtk_widget_set_hexpand(row->scale, TRUE);
        gtk_grid_attach(GTK_GRID(grid), row->scale, 1, (int)i, 1, 1);

        row->value_label = gtk_label_new("+0.00 rad");
        gtk_widget_add_css_class(row->value_label, "value-read");
        gtk_widget_set_size_request(row->value_label, 90, -1);
        gtk_label_set_xalign(GTK_LABEL(row->value_label), 1.0f);
        gtk_grid_attach(GTK_GRID(grid), row->value_label, 2, (int)i, 1, 1);

        g_signal_connect(row->scale, "value-changed", G_CALLBACK(on_scale_changed), row);
    }

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), actions);

    self->zero_button = gtk_button_new_with_label("Postura neutra");
    g_signal_connect(self->zero_button, "clicked", G_CALLBACK(on_zero_clicked), self);
    gtk_box_append(GTK_BOX(actions), self->zero_button);

    self->estop_button = gtk_button_new_with_label("PARAGEM DE EMERGÊNCIA");
    gtk_widget_add_css_class(self->estop_button, "destructive-action");
    g_signal_connect(self->estop_button, "clicked", G_CALLBACK(on_estop_clicked), self);
    gtk_box_append(GTK_BOX(actions), self->estop_button);

    install_css();
    gtk_window_present(GTK_WINDOW(self->window));

    fydelis_transport_set_link_handler(self->transport, on_link_state, self);
    GError *err = NULL;
    if (!fydelis_transport_start(self->transport, &err)) {
        g_printerr("Erro ao iniciar transporte: %s\n", err->message);
        g_error_free(err);
    }
}

int main(int argc, char **argv) {
    static App self = {0};
    const gchar *env_host = g_getenv("FYDELIS_HOST");
    const gchar *env_port = g_getenv("FYDELIS_PORT");
    const gchar *host = (env_host && *env_host) ? env_host : "127.0.0.1";
    guint16 port = (env_port && *env_port) ?
        (guint16)g_ascii_strtoull(env_port, NULL, 10) : 9090;

    self.transport = fydelis_transport_new(host, port);
    self.estopped = FALSE;

    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &self);

    gint status = g_application_run(G_APPLICATION(app), argc, argv);

    fydelis_transport_free(self.transport);
    g_object_unref(app);
    return status;
}

