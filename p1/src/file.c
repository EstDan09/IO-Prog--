#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

// Prototypes
static void on_save_clicked(GtkButton *button, gpointer user_data);
static void on_load_clicked(GtkButton *button, gpointer user_data);
static void on_destroy(GtkWidget *widget, gpointer data);

typedef struct {
    GtkEntry *entry_filename;
    GtkSpinButton *spin_num1;
    GtkSpinButton *spin_num2;
} AppWidgets;

/**
 * Save file with filename and numbers
 */
static void on_save_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;

    const char *filename = gtk_entry_get_text(app->entry_filename);
    int n1 = gtk_spin_button_get_value_as_int(app->spin_num1);
    int n2 = gtk_spin_button_get_value_as_int(app->spin_num2);

    FILE *f = fopen(filename, "w");
    if (!f) {
        g_printerr("Could not open file %s for writing\n", filename);
        return;
    }
    fprintf(f, "%d %d\n", n1, n2);
    fclose(f);

    g_print("Saved %s: %d %d\n", filename, n1, n2);
}

/**
 * Load file and update inputs
 */
static void on_load_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;

    const char *filename = gtk_entry_get_text(app->entry_filename);
    FILE *f = fopen(filename, "r");
    if (!f) {
        g_printerr("Could not open file %s for reading\n", filename);
        return;
    }
    int n1, n2;
    if (fscanf(f, "%d %d", &n1, &n2) == 2) {
        gtk_spin_button_set_value(app->spin_num1, n1);
        gtk_spin_button_set_value(app->spin_num2, n2);
        g_print("Loaded %s: %d %d\n", filename, n1, n2);
    } else {
        g_printerr("Invalid format in file %s\n", filename);
    }
    fclose(f);
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkBuilder *builder = gtk_builder_new_from_file("p1/ui/file.glade");

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    GtkEntry *entry_filename = GTK_ENTRY(gtk_builder_get_object(builder, "entry_filename"));
    GtkSpinButton *spin_num1 = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_num1"));
    GtkSpinButton *spin_num2 = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_num2"));
    GtkButton *btn_save = GTK_BUTTON(gtk_builder_get_object(builder, "btn_save"));
    GtkButton *btn_load = GTK_BUTTON(gtk_builder_get_object(builder, "btn_load"));

    AppWidgets *app = g_slice_new(AppWidgets);
    app->entry_filename = entry_filename;
    app->spin_num1 = spin_num1;
    app->spin_num2 = spin_num2;

    // Connect signals
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), app);
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_load_clicked), app);

    gtk_widget_show_all(window);
    gtk_main();

    g_slice_free(AppWidgets, app);
    g_object_unref(builder);
    return 0;
}
