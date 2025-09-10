// p1/src/floyd.c
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#define INF 9999999

/* --- UI --- */
static GtkWidget *matrix_grid;      /* donde van los GtkEntry */
static GtkWidget *result_view;      /* GtkTreeView con el resultado */
static int        node_count = 0;
static GtkWidget ***entries = NULL;

/* =========================================================
 * Utilidades
 * ========================================================= */
static gboolean token_is_inf(const char *t) {
    if (!t) return TRUE;
    if (g_ascii_strcasecmp(t, "INF") == 0) return TRUE;
    if (g_strcmp0(t, "∞") == 0) return TRUE;
    while (*t==' '||*t=='\t'||*t=='\n'||*t=='\r') t++;
    return *t == '\0';
}

static void clear_treeview_columns(GtkTreeView *tv) {
    GList *cols = gtk_tree_view_get_columns(tv);
    for (GList *l = cols; l; l = l->next)
        gtk_tree_view_remove_column(tv, GTK_TREE_VIEW_COLUMN(l->data));
    g_list_free(cols);
}

static void free_entries(void) {
    if (!entries) return;
    for (int i = 0; i < node_count; i++) free(entries[i]);
    free(entries);
    entries = NULL;
}

static char **default_labels(int n) {
    char **lbl = g_new0(char*, n);
    for (int i = 0; i < n; i++) {
        lbl[i] = g_strdup_printf("%c", 'A' + i); /* A, B, C... */
    }
    return lbl;
}

static void free_labels(char **lbl, int n) {
    if (!lbl) return;
    for (int i=0;i<n;i++) g_free(lbl[i]);
    g_free(lbl);
}

/* =========================================================
 * Reconstrucción de rutas usando P (next-hop)
 * ========================================================= */
static void build_path_str(int src, int dst, int **P, int n, char **labels, GString *out) {
    if (src == dst) { g_string_append(out, labels[src]); return; }
    if (P[src][dst] < 0) { g_string_append(out, "No ruta"); return; }

    int cur = src;
    g_string_append(out, labels[src]);
    while (cur != dst) {
        int nx = P[cur][dst];
        if (nx < 0 || nx >= n) { g_string_append(out, " -> ?"); return; }
        g_string_append_printf(out, " → %s", labels[nx]);
        cur = nx;
        /* pequeña salvaguarda ante bucles patológicos */
        static const int MAX_STEPS = 256;
        static int steps = 0;
        if (++steps > MAX_STEPS) { g_string_append(out, " (loop?)"); break; }
    }
}

/* =========================================================
 * Generación de LaTeX
 * ========================================================= */
static void tex_write_preamble(FILE *f, const char *title) {
    fprintf(f,
        "\\documentclass[11pt]{article}\n"
        "\\usepackage[margin=2.2cm]{geometry}\n"
        "\\usepackage{booktabs}\n"
        "\\usepackage{array}\n"
        "\\usepackage{longtable}\n"
        "\\usepackage{float}\n"
        "\\usepackage[table]{xcolor}\n"
        "\\usepackage{hyperref}\n"
        "\\newcommand{\\INF}{$\\infty$}\n"
        "\\title{%s}\n"
        "\\date{\\today}\n"
        "\\begin{document}\n"
        "\\maketitle\n", title);
}

static void tex_write_labels_row(FILE *f, char **labels, int n) {
    fprintf(f, " & ");
    for (int j=0;j<n;j++) {
        fprintf(f, "\\textbf{%s}%s", labels[j], (j+1<n)?" & ":"\\\\\\midrule\n");
    }
}

static void tex_table_D(FILE *f, const char *caption, int **M, int **Prev, int n, char **labels, gboolean highlight) {
    fprintf(f, "\\begin{table}[H]\\centering\n");
    fprintf(f, "\\caption{%s}\n", caption);
    fprintf(f, "\\rowcolors{2}{white}{white}\n");
    fprintf(f, "\\begin{tabular}{l");
    for (int j=0;j<n;j++) fprintf(f," r"); /* columnas numéricas */
    fprintf(f, "}\n\\toprule\n");
    tex_write_labels_row(f, labels, n);

    for (int i=0;i<n;i++) {
        fprintf(f, "\\textbf{%s}", labels[i]);
        for (int j=0;j<n;j++) {
            int v = M[i][j];
            gboolean changed = FALSE;
            if (highlight && Prev) {
                int pv = Prev[i][j];
                changed = (pv != v);
            }
            fprintf(f, " & ");
            if (changed) fprintf(f, "\\cellcolor{yellow!30}");
            if (v >= INF/2) fprintf(f, "\\INF");
            else fprintf(f, "%d", v);
        }
        fprintf(f, " \\\\\n");
    }
    fprintf(f, "\\bottomrule\n\\end{tabular}\n\\end{table}\n\n");
}

static void tex_table_P(FILE *f, const char *caption, int **P, int **PrevP, int n, char **labels, gboolean highlight) {
    fprintf(f, "\\begin{table}[H]\\centering\n");
    fprintf(f, "\\caption{%s}\n", caption);
    fprintf(f, "\\rowcolors{2}{white}{white}\n");
    fprintf(f, "\\begin{tabular}{l");
    for (int j=0;j<n;j++) fprintf(f," c");
    fprintf(f, "}\n\\toprule\n");
    tex_write_labels_row(f, labels, n);

    for (int i=0;i<n;i++) {
        fprintf(f, "\\textbf{%s}", labels[i]);
        for (int j=0;j<n;j++) {
            int v = P[i][j];
            gboolean changed = FALSE;
            if (highlight && PrevP) {
                int pv = PrevP[i][j];
                changed = (pv != v);
            }
            fprintf(f, " & ");
            if (changed) fprintf(f, "\\cellcolor{yellow!30}");
            if (v < 0) fprintf(f, "-");
            else fprintf(f, "%s", labels[v]);
        }
        fprintf(f, " \\\\\n");
    }
    fprintf(f, "\\bottomrule\n\\end{tabular}\n\\end{table}\n\n");
}

static void tex_write_all(FILE *f,
                          int ****Dsnaps, int ****Psnaps, /* (k) -> [n][n] */
                          int n, int K,
                          char **labels)
{
    /* Portada / introducción mínima */
    fprintf(f, "\\section*{Descripción}\n");
    fprintf(f, "Reporte automático del algoritmo de Floyd–Warshall. Se muestran D(0) y P(0), ");
    fprintf(f, "todas las tablas intermedias D(k) y P(k) con cambios resaltados, y el resultado final.\n\n");

    /* D(0) y P(0) */
    tex_table_D(f, "D(0) – matriz de distancias inicial", (*Dsnaps)[0], NULL, n, labels, FALSE);
    tex_table_P(f, "P(0) – matriz de siguiente salto inicial", (*Psnaps)[0], NULL, n, labels, FALSE);

    /* Intermedias D(k), P(k) */
    for (int k=1; k<=K; k++) {
        gchar *cd = g_strdup_printf("D(%d)", k);
        gchar *cp = g_strdup_printf("P(%d)", k);
        tex_table_D(f, cd, (*Dsnaps)[k], (*Dsnaps)[k-1], n, labels, TRUE);
        tex_table_P(f, cp, (*Psnaps)[k], (*Psnaps)[k-1], n, labels, TRUE);
        g_free(cd); g_free(cp);
    }

    /* Resultado final */
    fprintf(f, "\\section*{Distancias y rutas óptimas}\n");
    tex_table_D(f, "D(final)", (*Dsnaps)[K], (*Dsnaps)[K-1], n, labels, FALSE);
    tex_table_P(f, "P(final)", (*Psnaps)[K], (*Psnaps)[K-1], n, labels, FALSE);

    fprintf(f, "\\subsection*{Listado de rutas (todas las parejas i \\neq j)}\n");
    fprintf(f, "\\begin{longtable}{llp{0.65\\textwidth}}\n");
    fprintf(f, "\\toprule\n");
    fprintf(f, "\\textbf{Origen} & \\textbf{Destino} & \\textbf{Ruta óptima (con saltos)}\\\\\\midrule\\\\[-1ex]\n");

    int **Df = (*Dsnaps)[K];
    int **Pf = (*Psnaps)[K];
    for (int i=0;i<n;i++) for (int j=0;j<n;j++) if (i!=j) {
        fprintf(f, "%s & %s & ", labels[i], labels[j]);
        if (Df[i][j] >= INF/2 || Pf[i][j] < 0) {
            fprintf(f, "No existe ruta.\\\\\n");
        } else {
            GString *path = g_string_new(NULL);
            build_path_str(i, j, Pf, n, labels, path);
            fprintf(f, "%s (distancia = %d)\\\\\n", path->str, Df[i][j]);
            g_string_free(path, TRUE);
        }
    }

    fprintf(f, "\\bottomrule\n\\end{longtable}\n");
    fprintf(f, "\\end{document}\n");
}

/* =========================================================
 * Crear directorio reportes y compilar
 * ========================================================= */
static gchar* make_report_dir(void) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    gchar ts[32];
    strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", &tmv);
    gchar *dir = g_strdup_printf("reports/floyd-%s", ts);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static void compile_and_open_pdf(const char *dir, const char *texname) {
    gchar *cmd = g_strdup_printf(
        "sh -c 'cd %s && pdflatex -interaction=nonstopmode -halt-on-error %s >/dev/null 2>&1 && "
        "(xdg-open %s.pdf >/dev/null 2>&1 || evince %s.pdf >/dev/null 2>&1 || true)'",
        dir, texname, texname, texname);
    (void)system(cmd);
    g_free(cmd);
}

/* =========================================================
 * Callbacks de UI
 * ========================================================= */
static void create_matrix(GtkButton *button, gpointer user_data) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(user_data);
    node_count = gtk_spin_button_get_value_as_int(spin);

    /* Limpia grid anterior si existe */
    if (GTK_IS_GRID(matrix_grid)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(matrix_grid));
        for (GList *it = children; it; it = it->next)
            gtk_widget_destroy(GTK_WIDGET(it->data));
        g_list_free(children);
    }

    free_entries();

    entries = malloc(node_count * sizeof(GtkWidget **));
    for (int i = 0; i < node_count; i++) {
        entries[i] = malloc(node_count * sizeof(GtkWidget *));
        for (int j = 0; j < node_count; j++) {
            GtkWidget *entry = gtk_entry_new();
            gtk_widget_set_size_request(entry, 50, 30);
            gtk_entry_set_text(GTK_ENTRY(entry), (i==j) ? "0" : "INF");
            gtk_grid_attach(GTK_GRID(matrix_grid), entry, j, i, 1, 1);
            entries[i][j] = entry;
        }
    }

    gtk_widget_show_all(matrix_grid);
    if (GTK_IS_TREE_VIEW(result_view))
        clear_treeview_columns(GTK_TREE_VIEW(result_view));
}

static void run_floyd(GtkButton *button, gpointer user_data) {
    if (node_count <= 0 || !entries) return;
    int n = node_count;

    /* Lee matriz desde la UI */
    int **dist = g_new0(int*, n);
    for (int i = 0; i < n; i++) {
        dist[i] = g_new0(int, n);
        for (int j = 0; j < n; j++) {
            const char *text = gtk_entry_get_text(GTK_ENTRY(entries[i][j]));
            dist[i][j] = token_is_inf(text) ? INF : atoi(text);
        }
    }

    /* Etiquetas (A..H por ahora) */
    char **labels = default_labels(n);

    /* Snapshots: D(0..n), P(0..n) */
    int ***D = g_new0(int**, n+1);
    int ***P = g_new0(int**, n+1);
    for (int k=0;k<=n;k++) {
        D[k] = g_new0(int*, n);
        P[k] = g_new0(int*, n);
        for (int i=0;i<n;i++) {
            D[k][i] = g_new0(int, n);
            P[k][i] = g_new0(int, n);
        }
    }

    /* D(0) y P(0) */
    for (int i=0;i<n;i++) for (int j=0;j<n;j++) {
        D[0][i][j] = dist[i][j];
        if (i==j || dist[i][j] >= INF/2) P[0][i][j] = -1;
        else P[0][i][j] = j; /* next hop inicial: ir directo a j */
    }

    /* Floyd con snapshots por k */
    for (int k=0; k<n; k++) {
        for (int i=0; i<n; i++) {
            if (D[k][i][k] >= INF/2) continue;
            for (int j=0; j<n; j++) {
                if (D[k][k][j] >= INF/2) continue;
                int via = D[k][i][k] + D[k][k][j];
                if (via < D[k][i][j]) {
                    /* actualiza sobre una copia para D[k+1] */
                    D[k+1][i][j] = via;
                    /* para P: el siguiente salto hacia j pasa a ser el primero hacia k */
                    int nxt = (P[k][i][k] >= 0) ? P[k][i][k] : -1;
                    P[k+1][i][j] = (nxt >= 0) ? nxt : P[k][i][j];
                }
            }
        }
        /* completa D[k+1], P[k+1] con valores no cambiados respecto a D[k], P[k] */
        for (int i=0;i<n;i++) for (int j=0;j<n;j++) {
            if (D[k+1][i][j] == 0) D[k+1][i][j] = D[k][i][j];
            if (k==0 && P[k+1][i][j] == 0 && !(i==j && P[0][i][j]==-1))
                P[k+1][i][j] = P[k][i][j];
            if (k>0 && P[k+1][i][j] == 0) P[k+1][i][j] = P[k][i][j];
        }
    }

    /* Muestra D(final) en el TreeView */
    int **Df = D[n];
    GType *types = g_new0(GType, n);
    for (int c = 0; c < n; c++) types[c] = G_TYPE_STRING;
    GtkListStore *store = gtk_list_store_newv(n, types);
    g_free(types);

    for (int i = 0; i < n; i++) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        for (int j = 0; j < n; j++) {
            if (Df[i][j] >= INF/2)
                gtk_list_store_set(store, &it, j, "INF", -1);
            else {
                gchar *s = g_strdup_printf("%d", Df[i][j]);
                gtk_list_store_set(store, &it, j, s, -1);
                g_free(s);
            }
        }
    }
    gtk_tree_view_set_model(GTK_TREE_VIEW(result_view), GTK_TREE_MODEL(store));
    g_object_unref(store);
    clear_treeview_columns(GTK_TREE_VIEW(result_view));
    for (int j = 0; j < n; j++) {
        gchar *title = g_strdup_printf("Node %s", labels[j]);
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col =
            gtk_tree_view_column_new_with_attributes(title, renderer, "text", j, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(result_view), col);
        g_free(title);
    }

    /* === Generar LaTeX === */
    gchar *dir = make_report_dir();
    gchar *texpath = g_build_filename(dir, "floyd.tex", NULL);
    FILE *f = fopen(texpath, "w");
    if (f) {
        tex_write_preamble(f, "Proyecto 1 – Floyd–Warshall");
        int ****Dsnaps = (int****)&D;
        int ****Psnaps = (int****)&P;
        tex_write_all(f, Dsnaps, Psnaps, n, n, labels);
        fclose(f);

        /* compilar y abrir */
        compile_and_open_pdf(dir, "floyd");
    } else {
        g_printerr("No se pudo escribir %s\n", texpath);
    }

    /* liberar */
    g_free(texpath); g_free(dir);
    for (int i=0;i<n;i++) g_free(dist[i]);
    g_free(dist);
    for (int k=0;k<=n;k++) {
        for (int i=0;i<n;i++) { g_free(D[k][i]); g_free(P[k][i]); }
        g_free(D[k]); g_free(P[k]);
    }
    g_free(D); g_free(P);
    free_labels(labels, n);
}

static void on_save_clicked(GtkButton *b, gpointer user_data) {
    (void)b; (void)user_data;
    if (node_count <= 0 || !entries) return;

    g_mkdir_with_parents("cases", 0755);

    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Save Floyd Case", NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "cases");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "case.floyd");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        FILE *f = fopen(fname, "w");
        if (!f) {
            GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                  "Could not open file for writing:\n%s", fname);
            gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
            g_free(fname); gtk_widget_destroy(dlg); return;
        }
        fprintf(f, "# FloydCase v1\n");
        fprintf(f, "N=%d\n", node_count);
        fprintf(f, "MATRIX\n");
        for (int i=0;i<node_count;i++) {
            for (int j=0;j<node_count;j++) {
                const char *txt = gtk_entry_get_text(GTK_ENTRY(entries[i][j]));
                fputs(token_is_inf(txt) ? "INF" : ((txt && *txt) ? txt : "0"), f);
                if (j+1<node_count) fputc(' ', f);
            }
            fputc('\n', f);
        }
        fprintf(f, "END\n");
        fclose(f);
        g_free(fname);
    }
    gtk_widget_destroy(dlg);
}

static void on_load_clicked(GtkButton *b, gpointer user_data) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(user_data);

    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Load Floyd Case", NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "cases");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        FILE *f = fopen(fname, "r");
        if (!f) {
            GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                  "Could not open file:\n%s", fname);
            gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
            g_free(fname); gtk_widget_destroy(dlg); return;
        }

        int n = 0;
        long pos = ftell(f);
        char first[64] = {0};
        if (fgets(first, sizeof first, f) && g_str_has_prefix(first, "# FloydCase")) {
            char line[128] = {0};
            if (!fgets(line, sizeof line, f) || sscanf(line, "N=%d", &n) != 1 || n <= 0) {
                GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                    "Invalid file (missing or bad N=).");
                gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
                fclose(f); g_free(fname); gtk_widget_destroy(dlg); return;
            }
            /* salta MATRIX */
            fgets(first, sizeof first, f);
        } else {
            fseek(f, pos, SEEK_SET);
            if (fscanf(f, "%d", &n) != 1 || n <= 0) {
                GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                    "Invalid file format (missing N).");
                gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
                fclose(f); g_free(fname); gtk_widget_destroy(dlg); return;
            }
        }

        gtk_spin_button_set_value(spin, n);
        create_matrix(NULL, spin);

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                char tok[64] = {0};
                if (fscanf(f, "%63s", tok) != 1) {
                    GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                        "Invalid file at row %d, col %d.", i, j);
                    gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
                    fclose(f); g_free(fname); gtk_widget_destroy(dlg); return;
                }
                gtk_entry_set_text(GTK_ENTRY(entries[i][j]), token_is_inf(tok) ? "INF" : tok);
            }
        }

        fclose(f);
        g_free(fname);
    }
    gtk_widget_destroy(dlg);
}

/* =========================================================
 * main: crea/encuentra widgets de Glade y conecta señales
 * ========================================================= */
int main(int argc, char *argv[]) {
    /* si nos lanzaron desde el menú con FLOYD_PIDFILE, escribe tu PID */
    const char *pidpath = g_getenv("FLOYD_PIDFILE");
    if (pidpath && *pidpath) {
        FILE *pf = fopen(pidpath, "w");
        if (pf) { fprintf(pf, "%ld\n", (long)getpid()); fclose(pf); }
    }

    gtk_init(&argc, &argv);

    if (argc < 2) {
        g_printerr("Usage: %s <glade_file>\n", argv[0]);
        return 1;
    }

    g_message("Cargando UI: %s", argv[1]);
    GtkBuilder *builder = gtk_builder_new_from_file(argv[1]);
    if (!builder) { g_printerr("Error: Could not load UI file %s\n", argv[1]); return 1; }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) { g_printerr("No se encontró 'main_window' en el .glade\n"); return 1; }

    /* matrix_grid: si falta, créalo dentro de matrix_view o matrix_scroll_window */
    matrix_grid = GTK_WIDGET(gtk_builder_get_object(builder, "matrix_grid"));
    if (!GTK_IS_GRID(matrix_grid)) {
        g_message("[FLOYD] 'matrix_grid' no encontrado o no es GtkGrid. Creando uno nuevo...");
        matrix_grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(matrix_grid), 4);
        gtk_grid_set_column_spacing(GTK_GRID(matrix_grid), 4);

        GtkWidget *viewport = GTK_WIDGET(gtk_builder_get_object(builder, "matrix_view"));
        GtkWidget *scroll   = GTK_WIDGET(gtk_builder_get_object(builder, "matrix_scroll_window"));
        if (GTK_IS_CONTAINER(viewport)) {
            gtk_container_add(GTK_CONTAINER(viewport), matrix_grid);
        } else if (GTK_IS_CONTAINER(scroll)) {
            gtk_container_add(GTK_CONTAINER(scroll), matrix_grid);
        } else {
            g_printerr("[FLOYD] No hay contenedor para la matriz (matrix_view/matrix_scroll_window).\n");
        }
    }

    /* result_view: asegúrate de tener un TreeView dentro de result_scroll */
    result_view = GTK_WIDGET(gtk_builder_get_object(builder, "result_view"));
    if (!GTK_IS_TREE_VIEW(result_view)) {
        GtkWidget *scroll = GTK_WIDGET(gtk_builder_get_object(builder, "result_scroll"));
        if (GTK_IS_CONTAINER(scroll)) {
            result_view = gtk_tree_view_new();
            gtk_container_add(GTK_CONTAINER(scroll), result_view);
        } else {
            g_printerr("[FLOYD] No se encontró TreeView ni contenedor 'result_scroll'.\n");
            result_view = gtk_tree_view_new();
        }
    }

    /* botones y spin */
    GtkWidget *create_button = GTK_WIDGET(gtk_builder_get_object(builder, "create_button"));
    GtkWidget *run_button    = GTK_WIDGET(gtk_builder_get_object(builder, "run_button"));
    GtkWidget *save_button   = GTK_WIDGET(gtk_builder_get_object(builder, "save_button"));
    GtkWidget *load_button   = GTK_WIDGET(gtk_builder_get_object(builder, "load_button"));
    GtkWidget *nodes_spin    = GTK_WIDGET(gtk_builder_get_object(builder, "nodes_spin"));

    if (create_button) g_signal_connect(create_button, "clicked", G_CALLBACK(create_matrix), nodes_spin);
    if (run_button)    g_signal_connect(run_button,    "clicked", G_CALLBACK(run_floyd),   NULL);
    if (save_button)   g_signal_connect(save_button,   "clicked", G_CALLBACK(on_save_clicked), NULL);
    if (load_button)   g_signal_connect(load_button,   "clicked", G_CALLBACK(on_load_clicked), nodes_spin);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
