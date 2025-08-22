#include <gtk/gtk.h>

/**
 * funcion destructura de ventana
 */
static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    gtk_main_quit();
}

/**
 * funcion de accion para cerrar la ventana / widget
 */
static void on_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWindow *win = GTK_WINDOW(user_data);
    gtk_window_close(win);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    // proveedor de styling
    GtkCssProvider *style_provider;
    style_provider = gtk_css_provider_new();

    GError *err = NULL;
    GError *error_style = NULL;

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

    GtkBuilder *builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, "ui/pending.glade", &err))
    {
        g_printerr("Error cargando pending.glade: %s\n", err->message);
        g_error_free(err);
        return 1;
    }

    // definicion de widgets 

    GtkWidget *win = GTK_WIDGET(gtk_builder_get_object(builder, "pending_window"));
    GtkWidget *btn = GTK_WIDGET(gtk_builder_get_object(builder, "close_button"));

    // definicion de clases de css para cada widget
    gtk_style_context_add_class(gtk_widget_get_style_context(win), "bg-pending");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "pending-button");

    if (!win || !btn)
    {
        g_printerr("IDs no encontrados en pending.glade\n");
        return 1;
    }

    // acciones
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_close_clicked), win);

    gtk_widget_show_all(win);
    gtk_main();

    g_object_unref(builder);
    return 0;
}
