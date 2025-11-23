// simplex_gui.c — Simplex Solver GUI (GTK + LaTeX + Evince Presentation)
#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <json-c/json.h>   // libjson-c-dev
#include "simplex_report.h"

// === Widget references ===
static GtkWidget *entry_problem_name;
static GtkWidget *entry_num_vars, *entry_num_constraints;
static GtkWidget *combo_sense;
static GtkWidget *grid_varnames, *grid_objective, *grid_constraints;
static GtkWidget *check_show_steps;
static GtkWidget *text_output;

// === Dynamic widgets ===
static int n_vars = 0, n_cons = 0;
static GtkWidget ***entries_A = NULL;      // [n_cons][n_vars]
static GtkWidget **entries_c = NULL;       // [n_vars]
static GtkWidget **entries_b = NULL;       // [n_cons]
static GtkWidget **entries_varnames = NULL;// [n_vars]
static GtkWidget **combo_ineq = NULL;      // [n_cons] (por ahora deshabilitados)

// ------------------------------------------------------------
// Utility helpers
// ------------------------------------------------------------
static void show_error(const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s", msg
    );
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void clear_container(GtkWidget *container) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
}

// Para mostrar al usuario dónde quedó el PDF (mismo criterio que simplex_write_latex_report)
static void build_report_folder(const char *problem_name,
                                char *folder,
                                size_t folder_sz)
{
    if (problem_name && *problem_name) {
        size_t k = 0;
        for (const char *p = problem_name; *p && k < folder_sz - 1; ++p) {
            char c = *p;
            if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                folder[k++] = '_';
            else
                folder[k++] = c;
        }
        folder[k] = '\0';
    } else {
        g_strlcpy(folder, "Reporte_Simplex", folder_sz);
    }
}

// ------------------------------------------------------------
// "Generate Fields" button callback
// ------------------------------------------------------------
static void on_generate_clicked(GtkButton *btn, gpointer user_data) {
    const char *txt_n = gtk_entry_get_text(GTK_ENTRY(entry_num_vars));
    const char *txt_m = gtk_entry_get_text(GTK_ENTRY(entry_num_constraints));
    n_vars = (txt_n && *txt_n) ? atoi(txt_n) : 0;
    n_cons = (txt_m && *txt_m) ? atoi(txt_m) : 0;

    if (n_vars < 2 || n_vars > 15 || n_cons < 2 || n_cons > 15) {
        show_error("Las variables y restricciones deben ser entre 2 y 15");
        return;
    }

    // Limpiar grillas anteriores
    clear_container(grid_varnames);
    clear_container(grid_objective);
    clear_container(grid_constraints);

    // (No liberamos los punteros anteriores, fugas mínimas aceptables
    //  para esta herramienta didáctica. Si quieres, luego hacemos free.)

    // Reservar nuevos punteros
    entries_varnames = g_new0(GtkWidget*, n_vars);
    entries_c        = g_new0(GtkWidget*, n_vars);
    entries_b        = g_new0(GtkWidget*, n_cons);
    entries_A        = g_new0(GtkWidget**, n_cons);
    combo_ineq       = g_new0(GtkWidget*, n_cons);

    for (int i = 0; i < n_cons; i++)
        entries_A[i] = g_new0(GtkWidget*, n_vars);

    // Nombres de variables
    for (int j = 0; j < n_vars; j++) {
        entries_varnames[j] = gtk_entry_new();
        char defname[24];
        snprintf(defname, sizeof(defname), "x%d", j + 1);   // sin '_' para no romper LaTeX
        gtk_entry_set_text(GTK_ENTRY(entries_varnames[j]), defname);
        gtk_entry_set_width_chars(GTK_ENTRY(entries_varnames[j]), 8);
        gtk_grid_attach(GTK_GRID(grid_varnames), entries_varnames[j], j, 0, 1, 1);
    }

    // Coeficientes de la función objetivo
    for (int j = 0; j < n_vars; j++) {
        entries_c[j] = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(entries_c[j]), 8);
        gtk_grid_attach(GTK_GRID(grid_objective), entries_c[j], j, 0, 1, 1);
    }

    // Restricciones
    for (int i = 0; i < n_cons; i++) {
        for (int j = 0; j < n_vars; j++) {
            entries_A[i][j] = gtk_entry_new();
            gtk_entry_set_width_chars(GTK_ENTRY(entries_A[i][j]), 8);
            gtk_grid_attach(GTK_GRID(grid_constraints), entries_A[i][j], j, i, 1, 1);
        }

        combo_ineq[i] = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ineq[i]), "≤");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ineq[i]), "=");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ineq[i]), "≥");
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ineq[i]), 0);
        gtk_widget_set_sensitive(combo_ineq[i], FALSE);   // Proyecto 4: solo usamos ≤
        gtk_grid_attach(GTK_GRID(grid_constraints), combo_ineq[i], n_vars, i, 1, 1);

        entries_b[i] = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(entries_b[i]), 8);
        gtk_grid_attach(GTK_GRID(grid_constraints), entries_b[i], n_vars + 1, i, 1, 1);
    }

    gtk_widget_show_all(grid_varnames);
    gtk_widget_show_all(grid_objective);
    gtk_widget_show_all(grid_constraints);
}

// ------------------------------------------------------------
// "Solve" button callback
// ------------------------------------------------------------
static void on_solve_clicked(GtkButton *btn, gpointer user_data) {
    if (n_vars == 0 || n_cons == 0) {
        show_error("Debe ingresar y generar los campos primero.");
        return;
    }

    double *c = g_new0(double, n_vars);
    double *A = g_new0(double, n_vars * n_cons);
    double *b = g_new0(double, n_cons);

    // Coeficientes de Z
    for (int j = 0; j < n_vars; j++) {
        const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_c[j]));
        c[j] = (txt && *txt) ? atof(txt) : 0.0;
    }

    // Matriz A y RHS b
    for (int i = 0; i < n_cons; i++) {
        for (int j = 0; j < n_vars; j++) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_A[i][j]));
            A[i * n_vars + j] = (txt && *txt) ? atof(txt) : 0.0;
        }
        const char *tb = gtk_entry_get_text(GTK_ENTRY(entries_b[i]));
        b[i] = (tb && *tb) ? atof(tb) : 0.0;
        if (b[i] < 0) {
            show_error("Todas las b_i (lado derecho) deben ser ≥ 0 en esta versión.");
            g_free(c); g_free(A); g_free(b);
            return;
        }
    }

    ObjectiveSense sense =
        (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_sense)) == 0)
        ? SIMPLEX_MAXIMIZE
        : SIMPLEX_MINIMIZE;

    SimplexProblem p = {0};
    p.n     = n_vars;
    p.m     = n_cons;
    p.sense = sense;
    p.c     = c;
    p.A     = A;
    p.b     = b;
    p.ctype = NULL; // solo <=

    // Nombres de variables para LaTeX
    p.var_names = g_new0(char*, n_vars);
    for (int j = 0; j < n_vars; j++) {
        const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_varnames[j]));
        p.var_names[j] = g_strdup((txt && *txt) ? txt : "x");
    }
    p.problem_name = gtk_entry_get_text(GTK_ENTRY(entry_problem_name));

    SimplexResult r = (SimplexResult){0};
    SimplexTrace  tr;
    simplex_trace_init(&tr);

    simplex_solve_with_trace(&p, &r, 1000, &tr);

    // Generar reporte LaTeX (esta función ya corre pdflatex y abre evince)
    const char *problem_name = p.problem_name;
    char base_name[256];
    if (problem_name && *problem_name)
        snprintf(base_name, sizeof(base_name), "%s_report", problem_name);
    else
        snprintf(base_name, sizeof(base_name), "reporte_simplex");

    simplex_write_latex_report(base_name, &p, &r, &tr);

    // Construir ruta amigable para informar al usuario
    char folder[256];
    build_report_folder(problem_name, folder, sizeof(folder));

    // Mostrar resumen
    GString *buf = g_string_new("");
    g_string_append_printf(buf,
        "Estado: %s\nZ* = %.3f\n\nReporte LaTeX generado en carpeta: %s (reporte_simplex.pdf)\n\n",
        simplex_status_str(r.status),
        r.z,
        folder
    );
    for (int j = 0; j < n_vars; j++) {
        g_string_append_printf(buf, "x%d = %.3f\n", j + 1, r.x ? r.x[j] : 0.0);
    }

    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_output));
    gtk_text_buffer_set_text(tb, buf->str, -1);
    g_string_free(buf, TRUE);

    // Limpieza
    simplex_free_result(&r);
    simplex_trace_free(&tr);

    if (p.var_names) {
        for (int j = 0; j < n_vars; j++)
            g_free(p.var_names[j]);
        g_free(p.var_names);
    }
    g_free(c);
    g_free(A);
    g_free(b);
}

// ------------------------------------------------------------
// Guardar problema (.simplex JSON)
// ------------------------------------------------------------
static void on_save_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Guardar problema",
        NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancelar", GTK_RESPONSE_CANCEL,
        "_Guardar",  GTK_RESPONSE_ACCEPT,
        NULL
    );

    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "problema.simplex");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.simplex");
    gtk_file_filter_set_name(filter, "Archivos Simplex (*.simplex)");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        json_object *root = json_object_new_object();
        json_object_object_add(root, "name",
            json_object_new_string(gtk_entry_get_text(GTK_ENTRY(entry_problem_name))));
        json_object_object_add(root, "sense",
            json_object_new_string(gtk_combo_box_get_active(GTK_COMBO_BOX(combo_sense)) == 0 ? "max" : "min"));
        json_object_object_add(root, "n", json_object_new_int(n_vars));
        json_object_object_add(root, "m", json_object_new_int(n_cons));

        // varnames
        json_object *varnames = json_object_new_array();
        for (int j = 0; j < n_vars; ++j) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_varnames[j]));
            json_object_array_add(varnames, json_object_new_string(txt ? txt : ""));
        }
        json_object_object_add(root, "varnames", varnames);

        // c
        json_object *jc = json_object_new_array();
        for (int j = 0; j < n_vars; ++j) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_c[j]));
            json_object_array_add(jc,
                json_object_new_double(txt && *txt ? atof(txt) : 0.0));
        }
        json_object_object_add(root, "c", jc);

        // A
        json_object *jA = json_object_new_array();
        for (int i = 0; i < n_cons; ++i) {
            json_object *row = json_object_new_array();
            for (int j = 0; j < n_vars; ++j) {
                const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_A[i][j]));
                json_object_array_add(row,
                    json_object_new_double(txt && *txt ? atof(txt) : 0.0));
            }
            json_object_array_add(jA, row);
        }
        json_object_object_add(root, "A", jA);

        // b
        json_object *jb = json_object_new_array();
        for (int i = 0; i < n_cons; ++i) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_b[i]));
            json_object_array_add(jb,
                json_object_new_double(txt && *txt ? atof(txt) : 0.0));
        }
        json_object_object_add(root, "b", jb);

        FILE *f = fopen(filename, "w");
        if (f) {
            fputs(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY), f);
            fclose(f);
        }

        json_object_put(root);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

// ------------------------------------------------------------
// Cargar problema (.simplex JSON)
// ------------------------------------------------------------
static void on_load_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Cargar problema",
        NULL,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancelar", GTK_RESPONSE_CANCEL,
        "_Abrir",    GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.simplex");
    gtk_file_filter_set_name(filter, "Archivos Simplex (*.simplex)");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        json_object *root = json_object_from_file(filename);
        if (root) {
            json_object *jn = NULL, *jm = NULL;
            json_object_object_get_ex(root, "n", &jn);
            json_object_object_get_ex(root, "m", &jm);

            n_vars = jn ? json_object_get_int(jn) : 0;
            n_cons = jm ? json_object_get_int(jm) : 0;

            if (jn) gtk_entry_set_text(GTK_ENTRY(entry_num_vars),
                                       json_object_get_string(jn));
            if (jm) gtk_entry_set_text(GTK_ENTRY(entry_num_constraints),
                                       json_object_get_string(jm));

            // Reconstruir grillas
            on_generate_clicked(NULL, NULL);

            json_object *name;
            if (json_object_object_get_ex(root, "name", &name))
                gtk_entry_set_text(GTK_ENTRY(entry_problem_name),
                                   json_object_get_string(name));

            json_object *sense;
            if (json_object_object_get_ex(root, "sense", &sense)) {
                const char *s = json_object_get_string(sense);
                gtk_combo_box_set_active(GTK_COMBO_BOX(combo_sense),
                    (s && strcmp(s, "max") == 0) ? 0 : 1);
            }

            json_object *varnames;
            if (json_object_object_get_ex(root, "varnames", &varnames)) {
                int len = json_object_array_length(varnames);
                for (int j = 0; j < n_vars && j < len; ++j) {
                    json_object *v = json_object_array_get_idx(varnames, j);
                    gtk_entry_set_text(GTK_ENTRY(entries_varnames[j]),
                                       json_object_get_string(v));
                }
            }

            json_object *jc;
            if (json_object_object_get_ex(root, "c", &jc)) {
                int len = json_object_array_length(jc);
                for (int j = 0; j < n_vars && j < len; ++j) {
                    json_object *v = json_object_array_get_idx(jc, j);
                    gtk_entry_set_text(GTK_ENTRY(entries_c[j]),
                                       json_object_get_string(v));
                }
            }

            json_object *jA;
            if (json_object_object_get_ex(root, "A", &jA)) {
                int rows = json_object_array_length(jA);
                for (int i = 0; i < n_cons && i < rows; ++i) {
                    json_object *row = json_object_array_get_idx(jA, i);
                    int len = json_object_array_length(row);
                    for (int j = 0; j < n_vars && j < len; ++j) {
                        json_object *v = json_object_array_get_idx(row, j);
                        gtk_entry_set_text(GTK_ENTRY(entries_A[i][j]),
                                           json_object_get_string(v));
                    }
                }
            }

            json_object *jb;
            if (json_object_object_get_ex(root, "b", &jb)) {
                int len = json_object_array_length(jb);
                for (int i = 0; i < n_cons && i < len; ++i) {
                    json_object *v = json_object_array_get_idx(jb, i);
                    gtk_entry_set_text(GTK_ENTRY(entries_b[i]),
                                       json_object_get_string(v));
                }
            }

            json_object_put(root);
        }

        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkBuilder *builder = gtk_builder_new_from_file("simplex.glade");
    GtkWidget  *window  = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));

    entry_problem_name    = GTK_WIDGET(gtk_builder_get_object(builder, "entry_problem_name"));
    entry_num_vars        = GTK_WIDGET(gtk_builder_get_object(builder, "entry_num_vars"));
    entry_num_constraints = GTK_WIDGET(gtk_builder_get_object(builder, "entry_num_constraints"));
    combo_sense           = GTK_WIDGET(gtk_builder_get_object(builder, "combo_sense"));
    grid_varnames         = GTK_WIDGET(gtk_builder_get_object(builder, "grid_varnames"));
    grid_objective        = GTK_WIDGET(gtk_builder_get_object(builder, "grid_objective"));
    grid_constraints      = GTK_WIDGET(gtk_builder_get_object(builder, "grid_constraints"));
    check_show_steps      = GTK_WIDGET(gtk_builder_get_object(builder, "check_show_steps"));
    text_output           = GTK_WIDGET(gtk_builder_get_object(builder, "text_output"));

    GtkWidget *btn_generate = GTK_WIDGET(gtk_builder_get_object(builder, "btn_generate"));
    GtkWidget *btn_solve    = GTK_WIDGET(gtk_builder_get_object(builder, "btn_solve"));
    GtkWidget *btn_save     = GTK_WIDGET(gtk_builder_get_object(builder, "btn_save"));
    GtkWidget *btn_load     = GTK_WIDGET(gtk_builder_get_object(builder, "btn_load"));

    g_signal_connect(btn_generate, "clicked",
                     G_CALLBACK(on_generate_clicked), NULL);
    g_signal_connect(btn_solve, "clicked",
                     G_CALLBACK(on_solve_clicked), NULL);
    g_signal_connect(btn_save, "clicked",
                     G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(btn_load, "clicked",
                     G_CALLBACK(on_load_clicked), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}

