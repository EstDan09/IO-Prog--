#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define INF 9999999

static GtkWidget *matrix_grid;
static GtkWidget *result_view;
static int node_count = 0;
static GtkWidget ***entries = NULL;

static void create_matrix(GtkButton *button, gpointer user_data) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(user_data);
    node_count = gtk_spin_button_get_value_as_int(spin);

    // Clear previous matrix
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(matrix_grid));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    // Allocate entry grid
    entries = malloc(node_count * sizeof(GtkWidget**));
    for (int i = 0; i < node_count; i++) {
        entries[i] = malloc(node_count * sizeof(GtkWidget*));
        for (int j = 0; j < node_count; j++) {
            GtkWidget *entry = gtk_entry_new();
            gtk_widget_set_size_request(entry, 50, 30);

            if (i == j)
                gtk_entry_set_text(GTK_ENTRY(entry), "0");
            else
                gtk_entry_set_text(GTK_ENTRY(entry), "INF");

            gtk_grid_attach(GTK_GRID(matrix_grid), entry, j, i, 1, 1);
            entries[i][j] = entry;
        }
    }

    gtk_widget_show_all(matrix_grid);
}

static void run_floyd(GtkButton *button, gpointer user_data) {
    if (node_count == 0) return;

    // Read matrix
    int **dist = malloc(node_count * sizeof(int*));
    for (int i = 0; i < node_count; i++) {
        dist[i] = malloc(node_count * sizeof(int));
        for (int j = 0; j < node_count; j++) {
            const char *text = gtk_entry_get_text(GTK_ENTRY(entries[i][j]));
            if (strcmp(text, "INF") == 0)
                dist[i][j] = INF;
            else
                dist[i][j] = atoi(text);
        }
    }

    // Floyd–Warshall
    for (int k = 0; k < node_count; k++) {
        for (int i = 0; i < node_count; i++) {
            for (int j = 0; j < node_count; j++) {
                if (dist[i][k] + dist[k][j] < dist[i][j]) {
                    dist[i][j] = dist[i][k] + dist[k][j];
                }
            }
        }
    }

    // Show result in TreeView
    GtkListStore *store = gtk_list_store_new(node_count, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    for (int i = 0; i < node_count; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);

        gchar *row[node_count];
        for (int j = 0; j < node_count; j++) {
            if (dist[i][j] >= INF/2) row[j] = g_strdup("INF");
            else row[j] = g_strdup_printf("%d", dist[i][j]);
        }
        gtk_list_store_set(store, &iter,
            0, row[0],
            1, (node_count>1?row[1]:""),
            2, (node_count>2?row[2]:""),
            3, (node_count>3?row[3]:""),
            4, (node_count>4?row[4]:""),
            5, (node_count>5?row[5]:""),
            6, (node_count>6?row[6]:""),
            7, (node_count>7?row[7]:""),
            -1);
        for (int j = 0; j < node_count; j++) g_free(row[j]);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(result_view), GTK_TREE_MODEL(store));
    g_object_unref(store);

    // Add columns if not yet added
    if (gtk_tree_view_get_n_columns(GTK_TREE_VIEW(result_view)) == 0) {
        for (int j = 0; j < node_count; j++) {
            gchar *title = g_strdup_printf("Node %d", j);
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(title, renderer, "text", j, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(result_view), col);
            g_free(title);
        }
    }

    // Free memory
    for (int i = 0; i < node_count; i++) free(dist[i]);
    free(dist);
}
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    if (argc < 2) {
        g_printerr("Usage: %s <glade_file>\n", argv[0]);
        return 1;
    }

    GtkBuilder *builder = gtk_builder_new_from_file(argv[1]);
    if (!builder) {
        g_printerr("Error: Could not load UI file %s\n", argv[1]);
        return 1;
    }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    matrix_grid = GTK_WIDGET(gtk_builder_get_object(builder, "matrix_grid"));
    result_view = GTK_WIDGET(gtk_builder_get_object(builder, "result_view"));

    GtkWidget *create_button = GTK_WIDGET(gtk_builder_get_object(builder, "create_button"));
    GtkWidget *run_button = GTK_WIDGET(gtk_builder_get_object(builder, "run_button"));
    GtkWidget *nodes_spin = GTK_WIDGET(gtk_builder_get_object(builder, "nodes_spin"));

    g_signal_connect(create_button, "clicked", G_CALLBACK(create_matrix), nodes_spin);
    g_signal_connect(run_button, "clicked", G_CALLBACK(run_floyd), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
