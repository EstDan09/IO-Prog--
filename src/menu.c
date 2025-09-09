// menu.c
#include <gtk/gtk.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

/* PID del proceso 'pending' (0 = no hay proceso corriendo) */
// static GPid pending_pid = 0;

// Información obtenida de: https://docs.gtk.org/glib/struct.HashTable.html
static GHashTable *pending_map = NULL;

// prototipos
static void on_destroy(GtkWidget *w, gpointer data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static void on_pending_exit(GPid pid, gint status, gpointer data);
static void launch_pending(GtkButton *button, gpointer user_data);
static void child_setup_func(gpointer user_data);

/* El hijo se vuelve líder de su propio grupo para poder matar su descendencia */
// static void child_setup_func(gpointer user_data) {
//     (void)user_data;
//     /* En el hijo: nuevo grupo de procesos con PGID = PID del hijo */
//     setpgid(0, 0);
// }

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

/**
 * funcion destructura de ventana
 */
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
            kill(-pid, SIGTERM);
        }
    }

    gtk_main_quit();
}

/**
 * funcion de accion para abrir widget de pending usando system()
 */
static void launch_pending(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    // Llamada a pending.glade, ahora con system
    int ret = system("./bin/pending &");

    if (ret == -1)
    {
        g_printerr("Error al ejecutar pending con system()\n");
    }
}

// Lanza ./bin/floyd con system(), aislado en nueva sesión, y guarda el PID
static void launch_floyd(GtkButton *button, gpointer user_data)
{
    (void)user_data;

    // pidfile único por botón y por PID del menú (para no chocar con otras instancias)
    char pidfile[256];
    g_snprintf(pidfile, sizeof pidfile, "/tmp/floyd_%ld_%p.pid", (long)getpid(), (void *)button);

    // 1) borrar pidfile previo (si existe)
    g_unlink(pidfile);

    // 2) comando:
    // - setsid: hace a floyd líder de nueva sesión / grupo (luego kill(-pid, ...) mata a todo su grupo)
    // - redirecciones: desconectar stdio
    // - & : en background
    // - echo $! > pidfile : escribe el PID del proceso lanzado
    char cmd[512];
    g_snprintf(
        cmd, sizeof cmd,
        "sh -c 'setsid ./bin/floyd </dev/null >/dev/null 2>&1 & echo $! > %s'",
        pidfile);

    int ret = system(cmd);
    if (ret == -1)
    {
        g_printerr("Error al ejecutar system(): %s\n", g_strerror(errno));
        return;
    }

    // 3) leer el PID del pidfile y guardarlo en el mapa
    GPid pid = read_pidfile(pidfile);
    g_unlink(pidfile);

    if (pid <= 0)
    {
        g_printerr("No se pudo leer el PID de floyd (pidfile vacío o inválido)\n");
        return;
    }

    // Si había uno anterior en este botón, opcional: marcar para cierre (aquí solo lo sobreescribimos)
    g_hash_table_insert(pending_map, button, GINT_TO_POINTER(pid));
    g_message("floyd lanzado con PID %d", pid);
}

/**
 * quit para ventana de menu
 */
static void on_quit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWindow *win = GTK_WINDOW(user_data);
    gtk_window_close(win);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    // proveedor de styling
    GtkCssProvider *style_provider = gtk_css_provider_new();
    GError *error_style = NULL;

    pending_map = g_hash_table_new(g_direct_hash, g_direct_equal);

    // load de proveedor de styling
    gtk_css_provider_load_from_path(style_provider, "src/style.css", &error_style);
    if (error_style)
    {
        g_printerr("Error en carga de CSS: %s\n", error_style->message);
        g_clear_error(&error_style);
    }

    // adjunta proveedor de css con la pantalla
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

    // definicion de widgets
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

    // definicion de clases de css para cada widget
    gtk_style_context_add_class(gtk_widget_get_style_context(win), "bg-main");
    gtk_style_context_add_class(gtk_widget_get_style_context(b1), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b2), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b3), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b4), "option");

    // nombre de widget para clase css específica
    gtk_widget_set_name(bout, "btn_salir");

    gtk_widget_set_tooltip_text(b1, "Ejecuta Algoritmo 1 (pendiente)");
    gtk_widget_set_tooltip_text(b2, "Ejecuta Algoritmo 2 (pendiente)");
    gtk_widget_set_tooltip_text(b3, "Ejecuta Algoritmo 3 (pendiente)");
    gtk_widget_set_tooltip_text(b4, "Ejecuta Algoritmo 4 (pendiente)");
    gtk_widget_set_tooltip_text(bout, "Salir del menú");

    // acciones
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(b1, "clicked", G_CALLBACK(launch_floyd), NULL);
    gtk_widget_set_tooltip_text(
        b1,
        "Floyd–Warshall: calcula todas las distancias mínimas entre pares de nodos.\n"
        "Muestra D(0), tablas intermedias D(k)/P(k) y rutas óptimas.\n"
        "Genera reporte LaTeX con grafo y tablas.");
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
