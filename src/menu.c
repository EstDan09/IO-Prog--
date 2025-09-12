// menu.c
#include <gtk/gtk.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>

/* Mapa: botón -> PID del proceso lanzado */
static GHashTable *pending_map = NULL;

/* ===== Prototipos ===== */
static void on_destroy(GtkWidget *w, gpointer data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static void on_pending_exit(GPid pid, gint status, gpointer data);
static void child_setup_func(gpointer user_data);
static void launch_pending(GtkButton *button, gpointer user_data);
static void launch_floyd(GtkButton *button, gpointer user_data);

/* Lee un PID desde un archivo temporal (pidfile) */
static GPid read_pidfile(const char *path)
{
    gchar *content = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &content, &len, NULL))
        return 0;
    char *end = NULL;
    long long v = g_ascii_strtoll(content, &end, 10);
    g_free(content);
    if (!end || (*end != '\0' && *end != '\n'))
        return 0;
    if (v <= 0 || v > INT_MAX)
        return 0;
    return (GPid)v;
}

/* ===== Callbacks para procesos lanzados con g_spawn_async (pending) ===== */
static void on_pending_exit(GPid pid, gint status, gpointer data)
{
    (void)status;
    GtkButton *button = GTK_BUTTON(data);

    if (g_hash_table_lookup(pending_map, button) == GINT_TO_POINTER(pid))
    {
        g_hash_table_remove(pending_map, button);
    }
    g_spawn_close_pid(pid);
}

/* El hijo se vuelve líder de su propio grupo para poder matar su descendencia */
static void child_setup_func(gpointer user_data)
{
    (void)user_data;
    setpgid(0, 0); /* En el hijo: nuevo grupo de procesos con PGID = PID del hijo */
}

/* ===== Cierre del menú: matar todo lo que quede vivo ===== */
static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, pending_map);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        GPid pid = GPOINTER_TO_INT(value);
        if (pid > 0)
        {
            /* -pid => señal al grupo completo */
            kill(-pid, SIGTERM);
        }
    }
    gtk_main_quit();
}

/* ===== Lanzar 'pending' con g_spawn_async (placeholders) ===== */
static void launch_pending(GtkButton *button, gpointer user_data)
{
    (void)user_data;


    char *argv[] = {"./bin/pending", NULL};
    GError *error = NULL;
    GPid pid = 0;

    gboolean ok = g_spawn_async(
        NULL,
        argv,
        NULL,
        G_SPAWN_DO_NOT_REAP_CHILD,
        child_setup_func,
        NULL,
        &pid,
        &error);

    if (!ok)
    {
        g_printerr("No se pudo lanzar pending: %s\n",
                   (error && error->message) ? error->message : "error desconocido");
        if (error)
            g_clear_error(&error);
        return;
    }

    /* Guardar el PID asociado a este botón y registrar watch */
    // g_hash_table_insert(pending_map, button, GINT_TO_POINTER(pid));
    // g_child_watch_add(pid, on_pending_exit, button);
}

/* ===== Lanzar Floyd con system() + setsid + pidfile ===== */
static void launch_floyd(GtkButton *button, gpointer user_data)
{
    (void)button;      // unused
    (void)user_data;   // unused

    const char *bin   = "./bin/floyd";
    const char *glade = "p1/ui/floyd.glade";

    // Launch the program in the background detached from this GTK app
    char cmd[512];
    g_snprintf(cmd, sizeof(cmd),
               "setsid '%s' '%s' </dev/null >/dev/null 2>&1 &",
               bin, glade);

    int ret = system(cmd);
    if (ret == -1) {
        g_printerr("Error launching floyd: %s\n", g_strerror(errno));
    } else {
        g_message("Floyd lanzado correctamente.");
    }
}


/* ===== Botón Salir ===== */
static void on_quit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWindow *win = GTK_WINDOW(user_data);
    gtk_window_close(win);
}

/* ===== MAIN ===== */
int main(int argc, char *argv[])
{

        gtk_init(&argc, &argv);

    /* Proveedor de estilos */
    GtkCssProvider *style_provider = gtk_css_provider_new();
    GError *error_style = NULL;

    pending_map = g_hash_table_new(g_direct_hash, g_direct_equal);

    gtk_css_provider_load_from_path(style_provider, "src/style.css", &error_style);
    if (error_style)
    {
        g_printerr("Error en carga de CSS: %s\n", error_style->message);
        g_clear_error(&error_style);
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(style_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);

    GError *err = NULL;
    GtkBuilder *builder = gtk_builder_new();

    if (!gtk_builder_add_from_file(builder, "ui/menu.glade", &err))
    {
        g_printerr("Error cargando menu.glade: %s\n", err->message);
        g_error_free(err);
        g_object_unref(style_provider);
        g_object_unref(builder);
        return 1;
    }

    /* Widgets */
    GtkWidget *win = GTK_WIDGET(gtk_builder_get_object(builder, "menu_window"));
    GtkWidget *b1 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo1"));
    GtkWidget *b2 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo2"));
    GtkWidget *b3 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo3"));
    GtkWidget *b4 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo4"));
    GtkWidget *bout = GTK_WIDGET(gtk_builder_get_object(builder, "btn_salir"));

    if (!win || !b1 || !b2 || !b3 || !b4 || !bout)
    {
        g_printerr("IDs no encontrados en menu.glade\n");
        g_object_unref(style_provider);
        g_object_unref(builder);
        return 1;
    }

    /* Clases CSS */
    gtk_style_context_add_class(gtk_widget_get_style_context(win), "bg-main");
    gtk_style_context_add_class(gtk_widget_get_style_context(b1), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b2), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b3), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b4), "option");
    gtk_widget_set_name(bout, "btn_salir");

    /* Tooltips */
    gtk_widget_set_tooltip_text(b1,
                                "Algoritmo de Floyd–Warshall: calcula distancias mínimas entre todos los pares.\n"
                                "Muestra D(0) y resultado final; integra guardado/carga y puede generar reporte PDF.");
    gtk_widget_set_tooltip_text(b2, "Ejecuta Algoritmo 2 (pending)");
    gtk_widget_set_tooltip_text(b3, "Ejecuta Algoritmo 3 (pending)");
    gtk_widget_set_tooltip_text(b4, "Ejecuta Algoritmo 4 (pending)");
    gtk_widget_set_tooltip_text(bout, "Salir del menú");

    /* Acciones */
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(b1, "clicked", G_CALLBACK(launch_floyd), NULL); /* Floyd con system() */
    g_signal_connect(b2, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b3, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b4, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(bout, "clicked", G_CALLBACK(on_quit_clicked), win);

    gtk_widget_show_all(win);
    gtk_main();

    g_object_unref(style_provider);
    g_object_unref(builder);
    return 0;
}
