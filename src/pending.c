#include <gtk/gtk.h>

static void on_close_clicked(GtkButton *button, gpointer user_data) {
    GtkWindow *win = GTK_WINDOW(user_data);
    gtk_window_close(win);
}

static void on_destroy(GtkWidget *w, gpointer data) {
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GError *err = NULL;
    GtkBuilder *builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, "ui/pending.glade", &err)) {
        g_printerr("Error cargando pending.glade: %s\n", err->message);
        g_error_free(err);
        return 1;
    }

    GtkWidget *win = GTK_WIDGET(gtk_builder_get_object(builder, "pending_window"));
    GtkWidget *btn = GTK_WIDGET(gtk_builder_get_object(builder, "close_button"));
    if (!win || !btn) {
        g_printerr("IDs no encontrados en pending.glade\n");
        return 1;
    }

    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_close_clicked), win);

    gtk_widget_show_all(win);
    gtk_main();

    g_object_unref(builder);
    return 0;
}
