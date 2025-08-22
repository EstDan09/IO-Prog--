// menu.c
#include <gtk/gtk.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

/* PID del proceso 'pending' (0 = no hay proceso corriendo) */
static GPid pending_pid = 0;

//prototipos
static void on_destroy(GtkWidget *w, gpointer data);
static void on_quit_clicked(GtkButton *button, gpointer user_data);
static void on_pending_exit(GPid pid, gint status, gpointer data);
static gboolean kill_pending_cb(gpointer data);
static void terminate_pending_if_running(void);
static void launch_pending(GtkButton *button, gpointer user_data);
static void child_setup_func(gpointer user_data);

//antiTorres
static void on_pending_exit(GPid pid, gint status, gpointer data) {
    (void)status;
    (void)data;
    if (pid == pending_pid) {
        g_spawn_close_pid(pid);
        pending_pid = 0;
    }
}

//antiTorres2.0
static gboolean kill_pending_cb(gpointer data) {
    (void)data;
    if (pending_pid > 0) {
        kill(-pending_pid, SIGKILL);
    }
    return G_SOURCE_REMOVE;
}

/* Envía SIGTERM al pending (y luego SIGKILL en 1s si sigue vivo) */
static void terminate_pending_if_running(void) {
    if (pending_pid > 0) {
        kill(-pending_pid, SIGTERM);
        g_timeout_add(1000, kill_pending_cb, NULL);
    }
}

/* El hijo se vuelve líder de su propio grupo para poder matar su descendencia */
static void child_setup_func(gpointer user_data) {
    (void)user_data;
    /* En el hijo: nuevo grupo de procesos con PGID = PID del hijo */
    setpgid(0, 0);
}

/**
 * funcion destructura de ventana
 */
static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    terminate_pending_if_running();
    gtk_main_quit();
}

/**
 * funcion de accion para abrir widget de pending
 */
static void launch_pending(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    if (pending_pid > 0) {
        g_print("Ya hay un 'pending' corriendo (PID=%d)\n", pending_pid);
        return;
    }

    char *argv[] = {"./bin/pending", NULL};
    GError *error = NULL;

    gboolean ok = g_spawn_async(
         NULL, // directorio
         argv,
         NULL,
         G_SPAWN_DO_NOT_REAP_CHILD, // setup del child 
         child_setup_func, // mata el grupo 
         NULL,
         &pending_pid, // pid del child
         &error
    );
           
    if (!ok) {
        g_printerr("No se pudo lanzar pending: %s\n",
                   (error && error->message) ? error->message : "error desconocido");
        if (error) g_clear_error(&error);
        pending_pid = 0;
        return;
    }

    /* Recolectar cuando termine para no dejar zombi */
    g_child_watch_add(pending_pid, on_pending_exit, NULL);
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

    // load de proveedor de styling
    gtk_css_provider_load_from_path(style_provider, "src/style.css", &error_style);
    if (error_style) {
        g_printerr("Error en carga de CSS: %s\n", error_style->message);
        g_clear_error(&error_style);
    }

    // adjunta proveedor de css con la pantalla
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(style_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );

    GError *err = NULL;
    GtkBuilder *builder = gtk_builder_new();

    if (!gtk_builder_add_from_file(builder, "ui/menu.glade", &err)) {
        g_printerr("Error cargando menu.glade: %s\n", err->message);
        g_error_free(err);
        g_object_unref(style_provider);
        g_object_unref(builder);
        return 1;
    }

    // definicion de widgets
    GtkWidget *win  = GTK_WIDGET(gtk_builder_get_object(builder, "menu_window"));
    GtkWidget *b1   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo1"));
    GtkWidget *b2   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo2"));
    GtkWidget *b3   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo3"));
    GtkWidget *b4   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo4"));
    GtkWidget *bout = GTK_WIDGET(gtk_builder_get_object(builder, "btn_salir"));

    if (!win || !b1 || !b2 || !b3 || !b4 || !bout) {
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

    gtk_widget_set_tooltip_text(b1,  "Ejecuta Algoritmo 1 (pendiente)");
    gtk_widget_set_tooltip_text(b2,  "Ejecuta Algoritmo 2 (pendiente)");
    gtk_widget_set_tooltip_text(b3,  "Ejecuta Algoritmo 3 (pendiente)");
    gtk_widget_set_tooltip_text(b4,  "Ejecuta Algoritmo 4 (pendiente)");
    gtk_widget_set_tooltip_text(bout,"Salir del menú");

    // acciones
    g_signal_connect(win,  "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(b1,   "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b2,   "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b3,   "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b4,   "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(bout, "clicked", G_CALLBACK(on_quit_clicked), win);

    gtk_widget_show_all(win);
    gtk_main();

    g_object_unref(style_provider);
    g_object_unref(builder);
    return 0;
}

