/* tests/ui_load.c -- does GtkBuilder take this .ui? (s024 m3)
 *
 *   ui_load FILE.ui          exit 0 = it loaded; otherwise GtkBuilder's error
 *   ui_load --show FILE.ui   ...and show its window (close it to quit) -- to
 *                            compare by eye with Mortimer's canvas
 *
 * GtkBuilder refuses an unknown class, property, enum value or signal name --
 * so this is the judge of an exported form, not a reading of it. Handlers are
 * accepted by any name (a scope that hands back a do-nothing closure): the
 * signal NAMES are still checked by GTK; the hooks live in the app's code.
 * Needs a display (proof.sh uses xvfb-run when there's none). */
#include <gtk/gtk.h>
#include <string.h>

typedef struct { GObject parent; } AnyScope;
typedef struct { GObjectClass parent; } AnyScopeClass;
static void any_scope_iface(GtkBuilderScopeInterface *i);
G_DEFINE_TYPE_WITH_CODE(AnyScope, any_scope, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_BUILDER_SCOPE, any_scope_iface))
static void nothing(void) {}
static GClosure *any_closure(GtkBuilderScope *s, GtkBuilder *b, const char *name,
                             GtkBuilderClosureFlags flags, GObject *object, GError **error) {
    (void)s; (void)b; (void)name; (void)flags; (void)object; (void)error;
    return g_cclosure_new(G_CALLBACK(nothing), NULL, NULL);
}
/* Types the way GTK's own scope finds them (gtk_window_get_type() and so on). */
static GtkBuilderScope *g_types;
static GType any_type(GtkBuilderScope *s, GtkBuilder *b, const char *name) {
    (void)s;
    return GTK_BUILDER_SCOPE_GET_IFACE(g_types)->get_type_from_name(g_types, b, name);
}
static void any_scope_iface(GtkBuilderScopeInterface *i) {
    i->create_closure = any_closure;
    i->get_type_from_name = any_type;
}
static void any_scope_class_init(AnyScopeClass *c) { (void)c; }
static void any_scope_init(AnyScope *s) { (void)s; }

/* A property GtkBuilder couldn't set is only a WARNING to GTK -- here it fails.
 * (GTK logs structured, so this is a writer, not a g_log_set_handler.) */
static int g_bad = 0;
static GLogWriterOutput on_log(GLogLevelFlags level, const GLogField *fields, gsize n, gpointer data) {
    if (level & (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR)) {
        const char *msg = "";
        for (gsize i = 0; i < n; ++i)
            if (strcmp(fields[i].key, "MESSAGE") == 0) msg = fields[i].value;
        if (!strstr(msg, "session bus")) ++g_bad;   /* no dbus (a headless run): not the .ui */
        else return G_LOG_WRITER_HANDLED;
    }
    return g_log_writer_default(level, fields, n, data);
}

int main(int argc, char **argv) {
    const gboolean show = argc == 3 && strcmp(argv[1], "--show") == 0;
    if (argc != 2 && !show) { g_printerr("usage: ui_load [--show] FILE.ui\n"); return 2; }
    const char *path = argv[argc - 1];
    g_log_set_writer_func(on_log, NULL, NULL);
    gtk_init();
    /* every class the catalog may name -- registered before the builder asks */
    g_types = gtk_builder_cscope_new();
    GtkBuilder *b = gtk_builder_new();
    GtkBuilderScope *scope = g_object_new(any_scope_get_type(), NULL);
    gtk_builder_set_scope(b, scope);
    GError *err = NULL;
    if (!gtk_builder_add_from_file(b, path, &err)) {
        g_printerr("%s\n", err->message);
        return 1;
    }
    GSList *all = gtk_builder_get_objects(b);
    g_print("loaded: %u objects%s\n", g_slist_length(all), g_bad ? " -- with the warnings above" : "");
    GtkWindow *win = NULL;
    for (GSList *o = all; o && !win; o = o->next)
        if (GTK_IS_WINDOW(o->data)) win = GTK_WINDOW(o->data);
    g_slist_free(all);
    if (show && win) {
        gtk_window_present(win);
        while (g_list_model_get_n_items(gtk_window_get_toplevels()) > 0) g_main_context_iteration(NULL, TRUE);
    }
    return g_bad ? 1 : 0;
}
