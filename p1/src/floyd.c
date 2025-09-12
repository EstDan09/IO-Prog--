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
static GtkWidget **row_user = NULL;
static GtkWidget **col_user = NULL; // new enabezados de fil col editables

static void on_header_changed(GtkEditable *editable, gpointer user_data);
static gchar *escape_latex(const char *input);
static void tex_write_graph(FILE *f, int n, int **Df, int **Pf, char **labels);



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
    if (entries) {
        for (int i = 0; i < node_count; i++) {
            free(entries[i]);
        }
        free(entries);
        entries = NULL;
    }
    // update para que libere encabezados as well 
    if (row_user) {
        free(row_user);
        row_user = NULL;
    }

    if (col_user) {
        free(col_user);
        col_user = NULL;
    }
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

/**
 * tex
 * Preamble: crea el "encabezado" del archivo de latex, junto con la portada. 
 * También incluye la info sobre Floyd ahora
 */
static void tex_write_preamble(FILE *f, const char *title,
                               const char *course, const char *semester) {
    fprintf(f,
        "\\documentclass{article}\n"
        "\\usepackage[margin=2.2cm]{geometry}\n"
        "\\usepackage{booktabs}\n"
        "\\usepackage{array}\n"
        "\\usepackage{longtable}\n"
        "\\usepackage{float}\n"
        "\\usepackage[table]{xcolor}\n"
        "\\usepackage{hyperref}\n"
        "\\usepackage[utf8]{inputenc}\n"
        "\\usepackage{tikz}\n"
        "\\newcommand{\\INF}{$\\infty$}\n"
        "\\begin{document}\n"
        "\\begin{titlepage}\n"
        "  \\centering\n"
        "  \\vfill\n"
        "  {\\Huge %s}\\par\n"                     
        "  \\vspace{1cm}\n"
        "  {\\Large Curso: %s}\\par\n"            
        "  {\\Large Semestre: %s}\\par\n"         
        "  \\vfill\n"
        "  {\\Large Autores: Fabian Bustos - Esteban Secaida}\\par\n"
        "  \\vspace{1cm}\n"
        "  {\\large Fecha: \\today}\\par\n"
        "  \\vfill\n"
        "\\end{titlepage}\n\n",
        title, course, semester
    );
    /* Algoritmo de Floyd section */
    fprintf(f, "\\section*{Algoritmo de Floyd}\n");
    fprintf(f, "El algoritmo de Floyd, también conocido como Floyd--Warshall, "
            "es un método para encontrar las distancias más cortas entre todos "
            "los pares de nodos en un grafo ponderado, dirigido o no dirigido. "
            "Funciona de manera iterativa, actualizando las distancias considerando "
            "cada nodo como un posible punto intermedio entre pares de nodos.\n\n");

    fprintf(f, "El algoritmo fue propuesto por Robert W. Floyd en 1962, quien "
            "contribuyó significativamente al campo de la informática teórica y "
            "la optimización de algoritmos de grafos. La esencia de su trabajo "
            "reside en su simplicidad y eficacia para grafos densos.\n\n");

}

/**
 * tex
 * Write de labels de columnas y filas
 */
static void tex_write_labels_row(FILE *f, char **labels, int n) {
    fprintf(f, " & ");
    for (int j=0;j<n;j++) {
        fprintf(f, "\\textbf{%s}%s", labels[j], (j+1<n)?" & ":"\\\\\\midrule\n");
    }
}
/**
 * tex
 * Write de tabla D 
 */
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

/**
 * tex
 * Write de Tabla P
 */
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

/**
 * tex
 * Write de Grafo de Problema Floyd 
 * new: función de que detecte rutas "mutuas" para curvar la flecha 
 * para evitar que los pesos de cada ruta queden uno encima del otro 
 * inspo: https://latexdraw.com/tikz-shapes-circle/
 */
static void tex_write_graph(FILE *f, int n, int **Df, int **Pf, char **labels) {
    fprintf(f, "\\section*{Problema: Grafo de rutas}\n");
    fprintf(f, "\\begin{tikzpicture}[->, >=stealth, node distance=2cm, every node/.style={circle, draw}]\n");

    // posiciona los nodos en un círculo
    for (int i = 0; i < n; i++) {
        fprintf(f, "\\node (%d) at (%d*360/%d:3cm) {%s};\n", 
                i, i, n, labels[i]);
    }

    // dibujar edges de grafo
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            if (Df[i][j] < INF/2) {
                if (Df[j][i] < INF/2 && i < j) {
                    // si ruta es mutua, "curvear" la flechas para cada lado para que no choquen
                    fprintf(f, "\\draw (%d) to[bend left] node[above] {%d} (%d);\n", i, Df[i][j], j);
                    fprintf(f, "\\draw (%d) to[bend left] node[below] {%d} (%d);\n", j, Df[j][i], i);
                } else if (Df[j][i] >= INF/2) {
                    // ruta singular, peso va encima
                    fprintf(f, "\\draw (%d) -- node[above] {%d} (%d);\n", i, Df[i][j], j);
                }
            }
        }
    }

    fprintf(f, "\\end{tikzpicture}\n");
}


/**
 * tex
 * Función que nació porque pensé que ls caracteres "especiales" estaban quebrando el latex
 * Resultó que no era eso, pero quedó como validador por si acaso
 * Culpable: https://stackoverflow.com/questions/2541616/how-to-escape-strip-special-characters-in-the-latex-document
 * y yo, por usar stackoverflow
 */
static gchar *escape_latex(const char *input) {
    if (!input) return g_strdup("");
    GString *out = g_string_new(NULL);
    for (const char *p = input; *p; p++) {
        switch (*p) {
            case '#': g_string_append(out, "\\#"); break;
            case '$': g_string_append(out, "\\$"); break;
            case '%': g_string_append(out, "\\%"); break;
            case '&': g_string_append(out, "\\&"); break;
            case '_': g_string_append(out, "\\_"); break;
            case '{': g_string_append(out, "\\{"); break;
            case '}': g_string_append(out, "\\}"); break;
            case '~': g_string_append(out, "\\textasciitilde{}"); break;
            case '^': g_string_append(out, "\\^{}"); break;
            case '\\': g_string_append(out, "\\textbackslash{}"); break;
            default: g_string_append_c(out, *p); break;
        }
    }
    return g_string_free(out, FALSE);
}

/***
 * tex
 * Write del cuerpo principal del documento Latex
 */
static void tex_write_all(FILE *f,
                          int ****Dsnaps, int ****Psnaps,
                          int n, int K,
                          char **labels)
{
    // DIBUJO DE GRAFO ACA PARA INICIAL
    tex_write_graph(f, n, (*Dsnaps)[0], (*Psnaps)[0], labels);
    
    /* Introducción */
    fprintf(f, "\\section*{Tablas Iniciales}\n");
    fprintf(f, "Reporte automático del algoritmo de Floyd--Warshall. Se muestran D(0) y P(0), ");
    fprintf(f, "todas las tablas intermedias D(k) y P(k) con cambios resaltados, y el resultado final.\n\n");
    /* D(0) y P(0) */
    tex_table_D(f, "D(0) -- matriz de distancias inicial", (*Dsnaps)[0], NULL, n, labels, FALSE);
    tex_table_P(f, "P(0) -- matriz de siguiente salto inicial", (*Psnaps)[0], NULL, n, labels, FALSE);
    
    fprintf(f, "\\section*{Tablas Intermedias}\n");

    /* Tablas intermedias D(k), P(k) */
    for (int k = 1; k <= K; k++) {
        gchar *cd = g_strdup_printf("D(%d)", k);
        gchar *cp = g_strdup_printf("P(%d)", k);
        tex_table_D(f, cd, (*Dsnaps)[k], (*Dsnaps)[k-1], n, labels, TRUE);
        tex_table_P(f, cp, (*Psnaps)[k], (*Psnaps)[k-1], n, labels, TRUE);
        g_free(cd);
        g_free(cp);
    }

    /* Resultado final */
    fprintf(f, "\\section*{Distancias y rutas óptimas}\n");
    tex_table_D(f, "D(final)", (*Dsnaps)[K], (*Dsnaps)[K-1], n, labels, FALSE);
    tex_table_P(f, "P(final)", (*Psnaps)[K], (*Psnaps)[K-1], n, labels, FALSE);

    /* Listado de rutas */
    fprintf(f, "\\subsection*{Listado de rutas (todas las parejas i $\\neq$ j)}\n");
    fprintf(f, "\\begin{longtable}{llp{0.65\\textwidth}}\n");
    fprintf(f, "\\toprule\n");
    fprintf(f, "\\textbf{Origen} & \\textbf{Destino} & \\textbf{Ruta óptima (con saltos)}\\\\\\midrule\n");

    int **Df = (*Dsnaps)[K];
    int **Pf = (*Psnaps)[K];

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            fprintf(f, "%s & %s & ", escape_latex(labels[i]), escape_latex(labels[j]));

            if (Df[i][j] >= INF/2 || Pf[i][j] < 0) {
                fprintf(f, "No existe ruta.\\\\\n");
            } else {
                GString *path = g_string_new(NULL);
                build_path_str(i, j, Pf, n, labels, path);

                /* Replace Unicode arrow with LaTeX math arrow */
                gchar *safe_path = g_strdup(path->str);
                // g_strreplace_all(safe_path, "→", "$\\to$");

                fprintf(f, "%s (distancia = %d)\\\\\n", safe_path, Df[i][j]);
                g_string_free(path, TRUE);
                g_free(safe_path);
            }
        }
    }

    fprintf(f, "\\bottomrule\n\\end{longtable}\n\\end{document}");
}



/* =========================================================
 * Crear directorio reportes y compilar
 * ========================================================= */

/**
 * doc
 * genera el directorio donde se hacen los reportes de floyd
 * Utiliza fecha y hora actual
 */
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

/**
 * doc
 * compila el latex y lo abre en modo presentación
 */
static void compile_and_open_pdf(const char *dir, const char *texname) {
    gchar *pdfpath = g_build_filename(dir, g_strconcat(texname, ".pdf", NULL), NULL);

    // compila latex
    gchar *cmd_compile = g_strdup_printf(
        "cd '%s' && pdflatex -interaction=nonstopmode -halt-on-error '%s.tex'",
        dir, texname
    );
    int ret = system(cmd_compile);
    g_free(cmd_compile);

    if (ret != 0) {
        g_printerr("Error: pdflatex falló: %d\n", ret);
        g_free(pdfpath);
        return;
    }

    // usa evince para abrir doc en modo presentación 
    gchar *cmd_open = g_strdup_printf(
    "evince --presentation '%s' >/dev/null 2>&1 &",
    pdfpath
);
    system(cmd_open);
    g_free(cmd_open);
    g_free(pdfpath);
}


/* =========================================================
 * Callbacks de UI
 * ========================================================= */
/**
 * UI
 * Crea la matriz para el glade 
 */
static void create_matrix(GtkButton *button, gpointer user_data) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(user_data);
    node_count = gtk_spin_button_get_value_as_int(spin);

    // libera grid viejo si existe 
    if (GTK_IS_GRID(matrix_grid)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(matrix_grid));
        for (GList *it = children; it; it = it->next)
            gtk_widget_destroy(GTK_WIDGET(it->data));
        g_list_free(children);
    }

    // libera entries 
    free_entries();

    // libera headers por si acaso
    if (row_user) { free(row_user); row_user = NULL; }
    if (col_user) { free(col_user); col_user = NULL; }

    // crea headers que puedan ser modificados por el usuario 
    row_user = malloc(node_count * sizeof(GtkWidget *));
    col_user = malloc(node_count * sizeof(GtkWidget *));

    // alloc de entradas de la matriz
    entries = malloc(node_count * sizeof(GtkWidget **));
    for (int i = 0; i < node_count; i++) {
        entries[i] = malloc(node_count * sizeof(GtkWidget *));
    }

    // creación de encabezados de columnas
    for (int j = 0; j < node_count; j++) {
        char label[8];

        // esto escribe el nombre en un formato correcto para que latex no se queje 
        snprintf(label, sizeof(label), "%c", 'A' + j);
        GtkWidget *header = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(header), label);
        gtk_editable_set_editable(GTK_EDITABLE(header), TRUE);
        gtk_widget_set_size_request(header, 50, 30);
        gtk_grid_attach(GTK_GRID(matrix_grid), header, j + 1, 0, 1, 1);
        col_user[j] = header;

        // detector de cambios en los nombres de encabezados 
        g_signal_connect(header, "changed", G_CALLBACK(on_header_changed), GINT_TO_POINTER(j | 0x1000)); 
        // 0x1000 para columna
    }

    // creación de encabezados de filas
    for (int i = 0; i < node_count; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%c", 'A' + i);
        GtkWidget *header = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(header), label);
        gtk_editable_set_editable(GTK_EDITABLE(header), TRUE);
        gtk_widget_set_size_request(header, 50, 30);
        gtk_grid_attach(GTK_GRID(matrix_grid), header, 0, i + 1, 1, 1);
        row_user[i] = header;

        g_signal_connect(header, "changed", G_CALLBACK(on_header_changed), GINT_TO_POINTER(i));
    }

    // creación de entries de matriz 
    for (int i = 0; i < node_count; i++) {
        for (int j = 0; j < node_count; j++) {
            GtkWidget *entry = gtk_entry_new();
            gtk_widget_set_size_request(entry, 50, 30);
            gtk_entry_set_text(GTK_ENTRY(entry), (i == j) ? "0" : "INF");
            gtk_grid_attach(GTK_GRID(matrix_grid), entry, j + 1, i + 1, 1, 1);
            entries[i][j] = entry;
        }
    }

    gtk_widget_show_all(matrix_grid);

    if (GTK_IS_TREE_VIEW(result_view))
        clear_treeview_columns(GTK_TREE_VIEW(result_view));
}

/**
 * UI
 * Detecta los cambios en los nombres de los encabezados 
 * signal blockers: https://docs.gtk.org/gobject/func.signal_handlers_block_by_func.html
 */
static void on_header_changed(GtkEditable *editable, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);

    if (idx & 0x1000) {
        // cambia columna
        idx &= 0x0FFF;
        const char *text = gtk_entry_get_text(GTK_ENTRY(col_user[idx]));

        // block señal de fila
        g_signal_handlers_block_by_func(row_user[idx], on_header_changed, GINT_TO_POINTER(idx));
        gtk_entry_set_text(GTK_ENTRY(row_user[idx]), text);
        g_signal_handlers_unblock_by_func(row_user[idx], on_header_changed, GINT_TO_POINTER(idx));
    } else {
        // viceversa
        // cambia header fila
        const char *text = gtk_entry_get_text(GTK_ENTRY(row_user[idx]));

        // block señal columna
        g_signal_handlers_block_by_func(col_user[idx], on_header_changed, GINT_TO_POINTER(idx | 0x1000));
        gtk_entry_set_text(GTK_ENTRY(col_user[idx]), text);
        g_signal_handlers_unblock_by_func(col_user[idx], on_header_changed, GINT_TO_POINTER(idx | 0x1000));
    }
}

/**
 * UI
 * Transforma el widget de labels de encabezados a char para el latex
 */
char **entries_to_labels(GtkWidget **entries, int n) {
    if (!entries || n <= 0) return NULL;

    char **labels = malloc(n * sizeof(char*));
    if (!labels) return NULL;

    for (int i = 0; i < n; i++) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entries[i]));
        labels[i] = g_strdup(text ? text : "");  
    }
    return labels;
}

/**
 * UI
 * Libera el array de entradas
 */
void free_labels_array(char **labels, int n) {
    if (!labels) return;
    for (int i = 0; i < n; i++) g_free(labels[i]);
    free(labels);
}

/**
 * FLOYD
 * Función principal de ejecución de floyd
 */
static void run_floyd(GtkButton *button, gpointer user_data) {
    if (node_count <= 0 || !entries) return;
    int n = node_count;

    // lee matriz desde la UI
    int **dist = g_new0(int*, n);
    for (int i = 0; i < n; i++) {
        dist[i] = g_new0(int, n);
        for (int j = 0; j < n; j++) {
            const char *text = gtk_entry_get_text(GTK_ENTRY(entries[i][j]));
            dist[i][j] = token_is_inf(text) ? INF : atoi(text);
        }
    }

    // convierte encabezados a labels normales para latex 
    char **labels = entries_to_labels(row_user, n);



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
    // sección de resultados en el Glade 
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

    // se usa  Tree View para mostrar resultado
    gtk_tree_view_set_model(GTK_TREE_VIEW(result_view), GTK_TREE_MODEL(store));
    g_object_unref(store);
    clear_treeview_columns(GTK_TREE_VIEW(result_view));
    for (int j = 0; j < n; j++) {
        gchar *title = g_strdup_printf("Nodo %s", labels[j]);
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
        tex_write_preamble(f, "Proyecto 1 - Rutas Òptimas Algoritmo de Floyd", "Investigación de Operaciones", "II Semestre 2025");
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
/**
 * DOCS
 * Usa File Chooser para guardar archivos de Floyd
 * Dpcumentación: https://lazka.github.io/pgi-docs/Gtk-3.0/interfaces/FileChooser.html
 */
static void on_save_clicked(GtkButton *b, gpointer user_data) {
    (void)b; (void)user_data;
    if (node_count <= 0 || !entries) return;

    g_mkdir_with_parents("cases", 0755);

    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Save Floyd ", NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "cases");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "case.floyd");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        FILE *f = fopen(fname, "w");
        if (!f) {
            GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                  "Error de abrir archivo:\n%s", fname);
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

/**
 * DOCS
 * File chooser para abrir archivos guardados
 */
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
                    "Invalido file.");
                gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
                fclose(f); g_free(fname); gtk_widget_destroy(dlg); return;
            }
            /* salta MATRIX */
            fgets(first, sizeof first, f);
        } else {
            fseek(f, pos, SEEK_SET);
            if (fscanf(f, "%d", &n) != 1 || n <= 0) {
                GtkWidget *m = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                    "Invalido file");
                gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
                fclose(f); g_free(fname); gtk_widget_destroy(dlg); return;
            }
        }

        gtk_spin_button_set_value(spin, n);
        // crea matriz nueva a partir de archivo
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

/***
 * Main
 */
int main(int argc, char *argv[]) {
    /* si nos lanzaron desde el menú con FLOYD_PIDFILE, escribe tu PID */
    const char *pidpath = g_getenv("FLOYD_PIDFILE");
    if (pidpath && *pidpath) {
        FILE *pf = fopen(pidpath, "w");
        if (pf) { fprintf(pf, "%ld\n", (long)getpid()); fclose(pf); }
    }

    gtk_init(&argc, &argv);


    GtkCssProvider *style_provider = gtk_css_provider_new();
    GError *error_style = NULL;

    gtk_css_provider_load_from_path(style_provider, "src/style.css", &error_style);
    if (error_style)
    {
        g_printerr("Error en carga de CSS: %s\n", error_style->message);
        g_clear_error(&error_style);
    }

    gtk_style_context_add_provider_for_screen(
    gdk_screen_get_default(),
    GTK_STYLE_PROVIDER(style_provider),
    GTK_STYLE_PROVIDER_PRIORITY_USER);

    GtkBuilder *builder = gtk_builder_new_from_file(argv[1]);
    if (!builder) { g_printerr("Error: Could not load UI file %s\n", argv[1]); return 1; }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) { g_printerr("No se encontró 'main_window' en el .glade\n"); return 1; }

    /* matrix_grid: si falta, crear dentro matrix_scroll_window */
    matrix_grid = GTK_WIDGET(gtk_builder_get_object(builder, "matrix_grid"));
    if (!GTK_IS_GRID(matrix_grid)) {
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

    // vista de resultado result_view
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


    // CSS 
    gtk_style_context_add_class(gtk_widget_get_style_context(window), "bg-main");
    gtk_style_context_add_class(gtk_widget_get_style_context(create_button), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(run_button), "option");
    // gtk_style_context_add_class(gtk_widget_get_style_context(save_button), "btn_load");
    // gtk_style_context_add_class(gtk_widget_get_style_context(load_button), "btn_load");
    gtk_widget_set_name(save_button, "btn_option");
    gtk_widget_set_name(load_button, "btn_option");
    gtk_widget_set_name(nodes_spin, "spinbutton");
    

    if (create_button) g_signal_connect(create_button, "clicked", G_CALLBACK(create_matrix), nodes_spin);
    if (run_button)    g_signal_connect(run_button,    "clicked", G_CALLBACK(run_floyd),   NULL);
    if (save_button)   g_signal_connect(save_button,   "clicked", G_CALLBACK(on_save_clicked), NULL);
    if (load_button)   g_signal_connect(load_button,   "clicked", G_CALLBACK(on_load_clicked), nodes_spin);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
