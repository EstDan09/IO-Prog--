// main.c — Simplex Solver GUI (GTK + LaTeX + Evince Presentation)
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
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
static GtkWidget ***entries_A = NULL;
static GtkWidget **entries_c = NULL;
static GtkWidget **entries_b = NULL;
static GtkWidget **entries_varnames = NULL;
static GtkWidget **combo_ineq = NULL;

// ------------------------------------------------------------
// Utility helpers
// ------------------------------------------------------------
static void show_error(const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                               "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void clear_container(GtkWidget *container) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
}

static void generate_and_open_pdf(const char *tex_path) {
    if (!tex_path || !*tex_path) return;

    char pdf_path[512];
    const char *dot = strrchr(tex_path, '.');
    if (dot)
        snprintf(pdf_path, sizeof(pdf_path), "%.*s.pdf", (int)(dot - tex_path), tex_path);
    else
        snprintf(pdf_path, sizeof(pdf_path), "%s.pdf", tex_path);

    // Compile .tex → .pdf silently
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
             "pdflatex -interaction=nonstopmode -halt-on-error \"%s\" > /dev/null 2>&1",
             tex_path);
    (void)system(cmd);

    if (access(pdf_path, F_OK) == 0) {
        snprintf(cmd, sizeof(cmd), "evince --presentation \"%s\" &", pdf_path);
        (void)system(cmd);
    } else {
        show_error("Error: PDF file not generated. Check LaTeX compilation output.");
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
        show_error("Variables and constraints must be between 2 and 15.");
        return;
    }

    // Clear existing grids
    clear_container(grid_varnames);
    clear_container(grid_objective);
    clear_container(grid_constraints);

    // Allocate fresh entries
    entries_varnames = g_new0(GtkWidget*, n_vars);
    entries_c = g_new0(GtkWidget*, n_vars);
    entries_b = g_new0(GtkWidget*, n_cons);
    entries_A = g_new0(GtkWidget**, n_cons);
    combo_ineq = g_new0(GtkWidget*, n_cons);

    for (int i = 0; i < n_cons; i++)
        entries_A[i] = g_new0(GtkWidget*, n_vars);

    // Variable name inputs
    for (int j = 0; j < n_vars; j++) {
        entries_varnames[j] = gtk_entry_new();
        char defname[24];
        snprintf(defname, sizeof(defname), "$x_%d$", j + 1);
        gtk_entry_set_text(GTK_ENTRY(entries_varnames[j]), defname);
        gtk_entry_set_width_chars(GTK_ENTRY(entries_varnames[j]), 8);
        gtk_grid_attach(GTK_GRID(grid_varnames), entries_varnames[j], j, 0, 1, 1);
    }

    // Objective coefficients
    for (int j = 0; j < n_vars; j++) {
        entries_c[j] = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(entries_c[j]), 8);
        gtk_grid_attach(GTK_GRID(grid_objective), entries_c[j], j, 0, 1, 1);
    }

    // Constraints grid
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
        gtk_widget_set_sensitive(combo_ineq[i], FALSE);
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
        show_error("Please enter and generate fields first.");
        return;
    }

    double *c = g_new0(double, n_vars);
    double *A = g_new0(double, n_vars * n_cons);
    double *b = g_new0(double, n_cons);

    // Parse objective coefficients
    for (int j = 0; j < n_vars; j++) {
        const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_c[j]));
        c[j] = (txt && *txt) ? atof(txt) : 0.0;
    }

    // Parse constraint coefficients
    for (int i = 0; i < n_cons; i++) {
        for (int j = 0; j < n_vars; j++) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_A[i][j]));
            A[i * n_vars + j] = (txt && *txt) ? atof(txt) : 0.0;
        }
        const char *tb = gtk_entry_get_text(GTK_ENTRY(entries_b[i]));
        b[i] = (tb && *tb) ? atof(tb) : 0.0;
        if (b[i] < 0) {
            show_error("All RHS values (b) must be ≥ 0 for this version.");
            g_free(c); g_free(A); g_free(b);
            return;
        }
    }

    ObjectiveSense sense =
        (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_sense)) == 0)
        ? SIMPLEX_MAXIMIZE : SIMPLEX_MINIMIZE;

    SimplexProblem p = { n_vars, n_cons, sense, c, A, b };
    SimplexResult r = {0};
    SimplexTrace tr;
    simplex_trace_init(&tr);

    p.var_names = g_new0(char*, n_vars);
for (int j = 0; j < n_vars; j++) {
    const char *txt = gtk_entry_get_text(GTK_ENTRY(entries_varnames[j]));
    p.var_names[j] = g_strdup(txt && *txt ? txt : "$x$");
}
p.problem_name = gtk_entry_get_text(GTK_ENTRY(entry_problem_name));

    // Solve
    simplex_solve_with_trace(&p, &r, 1000, &tr);

    // Write LaTeX report
    const char *problem_name = gtk_entry_get_text(GTK_ENTRY(entry_problem_name));
    char report_filename[256];
    if (problem_name && *problem_name)
        snprintf(report_filename, sizeof(report_filename), "%s_report.tex", problem_name);
    else
        snprintf(report_filename, sizeof(report_filename), "reporte_simplex.tex");

    gboolean show_steps = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_show_steps));
    (void)show_steps; // currently unused — but could be passed to modify detail level

    simplex_write_latex_report(report_filename, &p, &r, &tr);
    generate_and_open_pdf(report_filename);

    // Display result summary
    GString *buf = g_string_new("");
    g_string_append_printf(buf, "Estado: %s\nZ* = %.3f\n\nReporte LaTeX generado: %s\n\n",
                           simplex_status_str(r.status), r.z, report_filename);
    for (int j = 0; j < n_vars; j++)
        g_string_append_printf(buf, "x%d = %.3f\n", j + 1, r.x[j]);

    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_output));
    gtk_text_buffer_set_text(tb, buf->str, -1);
    g_string_free(buf, TRUE);

    // Cleanup
    simplex_free_result(&r);
    simplex_trace_free(&tr);
    g_free(c); g_free(A); g_free(b);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkBuilder *builder = gtk_builder_new_from_file("simplex.glade");
    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));

    entry_problem_name   = GTK_WIDGET(gtk_builder_get_object(builder, "entry_problem_name"));
    entry_num_vars       = GTK_WIDGET(gtk_builder_get_object(builder, "entry_num_vars"));
    entry_num_constraints= GTK_WIDGET(gtk_builder_get_object(builder, "entry_num_constraints"));
    combo_sense          = GTK_WIDGET(gtk_builder_get_object(builder, "combo_sense"));
    grid_varnames        = GTK_WIDGET(gtk_builder_get_object(builder, "grid_varnames"));
    grid_objective       = GTK_WIDGET(gtk_builder_get_object(builder, "grid_objective"));
    grid_constraints     = GTK_WIDGET(gtk_builder_get_object(builder, "grid_constraints"));
    check_show_steps     = GTK_WIDGET(gtk_builder_get_object(builder, "check_show_steps"));
    text_output          = GTK_WIDGET(gtk_builder_get_object(builder, "text_output"));

    g_signal_connect(gtk_builder_get_object(builder, "btn_generate"), "clicked",
                     G_CALLBACK(on_generate_clicked), NULL);
    g_signal_connect(gtk_builder_get_object(builder, "btn_solve"), "clicked",
                     G_CALLBACK(on_solve_clicked), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
