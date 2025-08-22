#include <gtk/gtk.h>
#include <stdlib.h>

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
 * funcion de accion para abrir widget de pending
 */
static void launch_pending(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    int rc = system("./bin/pending &");
    if (rc == -1)
        g_printerr("No se pudo lanzar pending\n");
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
    GtkCssProvider *style_provider;
    style_provider = gtk_css_provider_new();

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

    GError *err = NULL;

    GtkBuilder *builder = gtk_builder_new();

    if (!gtk_builder_add_from_file(builder, "ui/menu.glade", &err))
    {
        g_printerr("Error cargando menu.glade: %s\n", err->message);
        g_error_free(err);
        return 1;
    }

    // definicion de widgets
    GtkWidget *win = GTK_WIDGET(gtk_builder_get_object(builder, "menu_window"));
    GtkWidget *b1 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo1"));
    GtkWidget *b2 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo2"));
    GtkWidget *b3 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo3"));
    GtkWidget *b4 = GTK_WIDGET(gtk_builder_get_object(builder, "btn_algo4"));
    GtkWidget *bout = GTK_WIDGET(gtk_builder_get_object(builder, "btn_salir"));

    // definicion de clases de css para cada widget
    gtk_style_context_add_class(gtk_widget_get_style_context(win), "bg-main");
    gtk_style_context_add_class(gtk_widget_get_style_context(b1), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b2), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b3), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(b4), "option");

    // nombre de widget para clase css específica
    gtk_widget_set_name(bout, "btn_salir");

    if (!win || !b1 || !b2 || !bout)
    {
        g_printerr("IDs no encontrados en menu.glade\n");
        return 1;
    }

    gtk_widget_set_tooltip_text(b1, "Ejecuta Algoritmo 1 (pendiente)");
    gtk_widget_set_tooltip_text(b2, "Ejecuta Algoritmo 2 (pendiente)");
    gtk_widget_set_tooltip_text(b3, "Ejecuta Algoritmo 3 (pendiente)");
    gtk_widget_set_tooltip_text(b4, "Ejecuta Algoritmo 4 (pendiente)");
    gtk_widget_set_tooltip_text(bout, "Salir del menú");


    // acciones 
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(b1, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b2, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b3, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(b4, "clicked", G_CALLBACK(launch_pending), NULL);
    g_signal_connect(bout, "clicked", G_CALLBACK(on_quit_clicked), win);

    gtk_widget_show_all(win);
    gtk_main();

    g_object_unref(builder);
    return 0;
}
