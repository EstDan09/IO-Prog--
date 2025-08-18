#include <gtk/gtk.h>
#include <stdlib.h>

static void on_destroy(GtkWidget *w, gpointer data) {
    (void)w; (void)data;           // silencia warnings
    gtk_main_quit();
}

static void launch_pending(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data; // silencia warnings
    int rc = system("./bin/pending &");   // lanza en paralelo
    if (rc == -1) g_printerr("No se pudo lanzar pending\n");
}

// <<< renombrada: antes se llamaba on_exit >>>
static void on_quit_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWindow *win = GTK_WINDOW(user_data);
    gtk_window_close(win);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GError *err = NULL;
    GtkBuilder *builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, "ui/menu.glade", &err)) {
        g_printerr("Error cargando menu.glade: %s\n", err->message);
        g_error_free(err);
        return 1;
    }

    GtkWidget *win  = GTK_WIDGET(gtk_builder_get_object(builder, "menu_window"));
    GtkWidget *b1   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo1"));
    GtkWidget *b2   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo2"));
    GtkWidget *bout = GTK_WIDGET(gtk_builder_get_object(builder, "btn_salir"));

    if (!win || !b1 || !b2 || !bout) {
        g_printerr("IDs no encontrados en menu.glade\n");
        return 1;
    }

    gtk_widget_set_tooltip_text(b1, "Ejecuta Algoritmo 1 (pendiente)");
    gtk_widget_set_tooltip_text(b2, "Ejecuta Algoritmo 2 (pendiente)");
    gtk_widget_set_tooltip_text(bout, "Salir del menú");

    g_signal_connect(win,  "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(b1,   "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b2,   "clicked", G_CALLBACK(launch_pending), NULL);
    // <<< aquí cambia el callback >>>
    g_signal_connect(bout, "clicked", G_CALLBACK(on_quit_clicked), win);

    gtk_widget_show_all(win);
    gtk_main();

    g_object_unref(builder);
    return 0;
}
