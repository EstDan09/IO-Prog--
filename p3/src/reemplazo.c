// Compilar:
//   gcc -O2 -Wall -o reemplazo reemplazo.c `pkg-config --cflags --libs gtk+-3.0` -lm
//
// Requiere: pdflatex y xdg-open en PATH. Glade: nuevo.glade

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <glib/gstdio.h>

#define QUIET 1
#if QUIET
  #undef g_print
  #undef g_printerr
  #define g_print(...)     do{}while(0)
  #define g_printerr(...)  do{}while(0)
#endif

// ==============================
// --------- Estructuras --------
// ==============================
typedef struct {
    int periodo;            // esperado i+1
    double reventa;         // reventa a esa edad
    double mantenimiento;   // mantenimiento a esa edad
} Periodo;

typedef struct {
    double costo_inicial;   // precio de compra (nuevo)
    int plazo;              // horizonte T
    int vida_util;          // vida útil L
    Periodo *periodos;      // longitud >= vida_util (ideal)
} ReemplazoData;

// Para DP
#define INF 1e100
#define MAX_T 200
#define MAX_L 50

typedef struct {
    int count;
    int nexts[MAX_L+2];     // candidatos (t -> x)
} NextList;

typedef struct {
    double G[MAX_T+2];
    NextList nxt[MAX_T+2];
} DPRes;

typedef struct {
    int paths_alloc;
    int path_len_alloc;
    int num_paths;
    int **paths;   // cada ruta: lista de "x" (saltos) comenzando en 0
    int *lens;
    int max_listar;
} PathSet;

typedef struct {
    DPRes R;
    double C[MAX_T+2][MAX_T+2]; // C[t][x]
    PathSet paths;
} SolveOut;

// ==============================
// ------- Widgets globales -----
// ==============================
static GtkBuilder *builder;
static GtkWidget *entryCosto, *spinPlazo, *spinVida, *tabla;
static GtkWidget *btnGuardar, *btnCargar, *btnEjecutar, *btnSalir;

// Opcionales del enunciado (si no están en glade, los creo)
static GtkWidget *checkGanancia = NULL;  // activar ganancia por uso
static GtkWidget *spinGanancia  = NULL;  // ganancia fija por período
static GtkWidget *checkInflacion = NULL; // activar inflación
static GtkWidget *spinInflacion  = NULL; // i (en %)

// ==============================
// ------ Utilitarios simples ----
// ==============================
static double clampd(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }
static int mini(int a,int b){ return a<b?a:b; }

static void* xmalloc(size_t n){
    void *p = malloc(n);
    if(!p){ fprintf(stderr,"Out of memory\n"); exit(1); }
    return p;
}

static const char* safe_entry_text(GtkWidget *e){
    return (e && GTK_IS_ENTRY(e)) ? gtk_entry_get_text(GTK_ENTRY(e)) : "0";
}
static double safe_spin_value(GtkWidget *w, double def){
    if (w && GTK_IS_SPIN_BUTTON(w)) return gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
    return def;
}
static int safe_check_active(GtkWidget *w){
    return (w && GTK_IS_TOGGLE_BUTTON(w)) ? gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)) : 0;
}

// =====================================================
// --------- Tabla editable (Edad, Mant., Reventa) -----
// =====================================================
enum { COL_EDAD = 0, COL_MANT, COL_REVENTA, N_COLS };

static GtkListStore* tabla_get_store(void){
    if (!tabla || !GTK_IS_TREE_VIEW(tabla)) return NULL;
    GtkTreeModel *m = gtk_tree_view_get_model(GTK_TREE_VIEW(tabla));
    return GTK_IS_LIST_STORE(m) ? GTK_LIST_STORE(m) : NULL;
}

static void cell_edited_double(GtkCellRendererText *r, gchar *path, gchar *new_text, gpointer col_ptr){
    int col = GPOINTER_TO_INT(col_ptr);
    GtkListStore *store = tabla_get_store();
    if (!store) return;

    char *endp = NULL;
    double val = g_strtod(new_text, &endp);
    if (endp == new_text) return; // no parseó

    GtkTreeIter it;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &it, path)){
        gtk_list_store_set(store, &it, col, val, -1);
    }
}

static void tabla_init_if_needed(void){
    if (!tabla || !GTK_IS_TREE_VIEW(tabla)) return;
    if (tabla_get_store()) return; // ya inicializada

    // Modelo
    GtkListStore *store = gtk_list_store_new(N_COLS, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
    gtk_tree_view_set_model(GTK_TREE_VIEW(tabla), GTK_TREE_MODEL(store));
    g_object_unref(store);

    // Columna Edad (solo lectura)
    GtkCellRenderer *rnd = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Edad", rnd, "text", COL_EDAD, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tabla), col);

    // Columna Mant. (editable)
    GtkCellRenderer *rnd_m = gtk_cell_renderer_text_new();
    g_object_set(rnd_m, "editable", TRUE, NULL);
    g_signal_connect(rnd_m, "edited", G_CALLBACK(cell_edited_double), GINT_TO_POINTER(COL_MANT));
    GtkTreeViewColumn *col_m = gtk_tree_view_column_new_with_attributes("Mant.", rnd_m, "text", COL_MANT, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tabla), col_m);

    // Columna Reventa (editable)
    GtkCellRenderer *rnd_r = gtk_cell_renderer_text_new();
    g_object_set(rnd_r, "editable", TRUE, NULL);
    g_signal_connect(rnd_r, "edited", G_CALLBACK(cell_edited_double), GINT_TO_POINTER(COL_REVENTA));
    GtkTreeViewColumn *col_r = gtk_tree_view_column_new_with_attributes("Reventa", rnd_r, "text", COL_REVENTA, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tabla), col_r);
}

static void tabla_resize_rows(int L){
    GtkListStore *store = tabla_get_store();
    if (!store) return;

    gtk_list_store_clear(store);
    for (int k=1; k<=L; ++k){
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it,
                           COL_EDAD, k,
                           COL_MANT, 0.0,
                           COL_REVENTA, 0.0, -1);
    }
}

static int tabla_leer_a_vectores(int L, double *mant, double *rev){
    GtkListStore *store = tabla_get_store();
    if (!store) return 0;

    GtkTreeModel *m = GTK_TREE_MODEL(store);
    GtkTreeIter it;
    int idx = 1;
    if (gtk_tree_model_get_iter_first(m, &it)){
        do {
            int edad=0; double mm=0.0, rr=0.0;
            gtk_tree_model_get(m, &it, COL_EDAD, &edad, COL_MANT, &mm, COL_REVENTA, &rr, -1);
            if (edad>=1 && edad<=L){
                mant[edad] = mm;
                rev[edad]  = rr;
            }
            idx++;
        } while (gtk_tree_model_iter_next(m, &it) && idx<=L);
    }
    int any = 0;
    for (int k=1;k<=L;k++){
        if (mant[k]!=0.0 || rev[k]!=0.0){ any=1; break; }
    }
    return any;
}

// ==============================
// --------- I/O archivos -------
// ==============================

// Guardar EXACTAMENTE las filas de la vida útil (lo visible en el grid)
void guardar_problema(const char *fname, ReemplazoData *p) {
    FILE *f = fopen(fname, "w");
    if (!f) { g_printerr("No pude abrir %s para escribir\n", fname); return; }
    fprintf(f, "%.10g %d %d\n", p->costo_inicial, p->plazo, p->vida_util);

    int L = p->vida_util;
    for (int i = 0; i < L; i++) {
        int per = i + 1;
        double rev = p->periodos ? p->periodos[i].reventa : 0.0;
        double man = p->periodos ? p->periodos[i].mantenimiento : 0.0;
        fprintf(f, "%d %.10g %.10g\n", per, rev, man);
    }
    fclose(f);
}

ReemplazoData *cargar_problema(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) { g_printerr("No pude abrir %s para lectura\n", fname); return NULL; }
    ReemplazoData *p = (ReemplazoData*)xmalloc(sizeof(ReemplazoData));
    if (fscanf(f, "%lf %d %d", &p->costo_inicial, &p->plazo, &p->vida_util) != 3) {
        fclose(f); free(p); g_printerr("Formato inválido de cabecera en %s\n", fname); return NULL;
    }
    if (p->plazo <= 0) { fclose(f); free(p); g_printerr("Plazo inválido en %s\n", fname); return NULL; }
    p->periodos = (Periodo*)xmalloc(sizeof(Periodo) * p->plazo);
    for (int i = 0; i < p->plazo; i++) { p->periodos[i].periodo = i+1; p->periodos[i].reventa = 0; p->periodos[i].mantenimiento = 0; }

    // Leer sólo L filas
    for (int i = 0; i < p->vida_util; i++) {
        int per=0; double rev=0, man=0;
        if (fscanf(f, "%d %lf %lf", &per, &rev, &man) != 3) break;
        int idx = per - 1;
        if (idx >= 0 && idx < p->plazo) {
            p->periodos[idx].periodo = per;
            p->periodos[idx].reventa = rev;
            p->periodos[idx].mantenimiento = man;
        }
    }
    fclose(f);
    return p;
}

// ==============================
// ------ Núcleo de solución -----
// ==============================

static void construir_series_edad(const ReemplazoData *p, double *mant, double *rev) {
    int L = p->vida_util;

    for (int k=1;k<=L;k++){ mant[k]=0; rev[k]=0; }
    int ok = tabla_leer_a_vectores(L, mant, rev);

    // Si la tabla no tenía datos, usar lo que venga en p->periodos; si tampoco, quedan 0
    if (!ok) {
        for (int k=1; k<=L; ++k) {
            int idx = k-1;
            if (idx < p->plazo) {
                mant[k] = p->periodos[idx].mantenimiento;
                rev[k]  = p->periodos[idx].reventa;
            }
        }
    }
    for (int k=1; k<=L; ++k) {
        if (isnan(mant[k])) mant[k]=0;
        if (isnan(rev[k]))  rev[k]=0;
    }
}

// Llena C[t][x] con opciones de ganancia e inflación (opcionales)
static void construir_C(const ReemplazoData *p, double C[MAX_T+2][MAX_T+2]) {
    int T = p->plazo;
    int L = p->vida_util;

    // Opciones leídas de UI
    const int usarG = safe_check_active(checkGanancia);
    const double gan = usarG ? safe_spin_value(spinGanancia, 0.0) : 0.0;

    const int usarI = safe_check_active(checkInflacion);
    const double i_pct = safe_spin_value(spinInflacion, 0.0); // en %
    const double i = usarI ? (i_pct/100.0) : 0.0;

    double mant[MAX_L+2]={0}, rev[MAX_L+2]={0};
    construir_series_edad(p, mant, rev);

    for (int t=0;t<=T;++t)
        for (int x=0;x<=T;++x)
            C[t][x] = INF;

    for (int t=0; t<T; ++t) {
        int x_max = mini(t+L, T);
        for (int x=t+1; x<=x_max; ++x) {
            int edad = x - t;

            // Compra al inicio del intervalo (t); dejamos nominal constante.
            double compra = p->costo_inicial;

            // Suma de mantenimiento menos ganancia, con inflación si se activa
            double sum_per = 0.0;
            for (int k=1;k<=edad;k++){
                double factor = usarI ? pow(1.0+i, (double)(k-1)) : 1.0;
                double flujo = mant[k] - (usarG ? gan : 0.0);
                sum_per += flujo * factor;
            }

            // Reventa al final del intervalo (edad), inflada si aplica
            double rev_fin = rev[edad] * (usarI ? pow(1.0+i, (double)(edad-1)) : 1.0);

            double costo = compra + sum_per - rev_fin;
            C[t][x] = costo;
        }
    }
}

// DP hacia atrás
static void dp_resolver(const ReemplazoData *p, double C[MAX_T+2][MAX_T+2], DPRes *R) {
    int T = p->plazo;
    for (int t=0; t<=T; ++t) {
        R->G[t] = INF;
        R->nxt[t].count = 0;
    }
    R->G[T] = 0.0;

    for (int t=T-1; t>=0; --t) {
        double best = INF;
        int x_max = mini(t+ p->vida_util, T);
        for (int x=t+1; x<=x_max; ++x) {
            double val = C[t][x] + R->G[x];
            if (val < best) best = val;
        }
        R->G[t] = best;
        for (int x=t+1; x<=x_max; ++x) {
            double val = C[t][x] + R->G[x];
            if (fabs(val - best) < 1e-9) {
                int k = R->nxt[t].count++;
                if (k < (int)(sizeof(R->nxt[t].nexts)/sizeof(R->nxt[t].nexts[0])))
                    R->nxt[t].nexts[k] = x;
            }
        }
    }
}

// PathSet para listar rutas óptimas
static void pathset_init(PathSet *P, int max_paths, int max_len) {
    P->paths_alloc = max_paths;
    P->path_len_alloc = max_len;
    P->num_paths = 0;
    P->paths = (int**)xmalloc(sizeof(int*)*max_paths);
    P->lens  = (int*)xmalloc(sizeof(int)*max_paths);
    for (int i=0;i<max_paths;++i) {
        P->paths[i] = (int*)xmalloc(sizeof(int)*max_len);
        memset(P->paths[i], 0, sizeof(int)*max_len);
        P->lens[i] = 0;
    }
    P->max_listar = max_paths;
}

static void pathset_free(PathSet *P) {
    if (!P->paths) return;
    for (int i=0;i<P->paths_alloc;++i) free(P->paths[i]);
    free(P->paths); free(P->lens);
    memset(P,0,sizeof(*P));
}

static void dfs_paths(const DPRes *R, int T, int t, int *stk, int len, PathSet *P) {
    if (t == T) {
        if (P->num_paths < P->max_listar) {
            memcpy(P->paths[P->num_paths], stk, sizeof(int)*len);
            P->lens[P->num_paths] = len;
            P->num_paths++;
        }
        return;
    }
    for (int i=0;i<R->nxt[t].count;++i) {
        int x = R->nxt[t].nexts[i];
        if (len < P->path_len_alloc) {
            stk[len] = x;
            dfs_paths(R, T, x, stk, len+1, P);
        }
    }
}

static void format_ruta(char *out, size_t outsz, const int *saltos, int len) {
    char buf[64];
    snprintf(out, outsz, "0");
    for (int i=0;i<len;i++) {
        snprintf(buf, sizeof(buf), " -> %d", saltos[i]);
        strncat(out, buf, outsz-1);
    }
}

// Orquestador
static void solve_caso(const ReemplazoData *p, SolveOut *S) {
    int T = p->plazo;
    construir_C(p, S->C);
    dp_resolver(p, S->C, &S->R);
    pathset_init(&S->paths, /*max_paths*/ 1024, /*max_len*/ T+1);
    int *stk = (int*)xmalloc(sizeof(int)*(T+1));
    memset(stk, 0, sizeof(int)*(T+1));
    dfs_paths(&S->R, T, 0, stk, 0, &S->paths);
    free(stk);
}

// ==============================
// --------- Generar LaTeX ------
// ==============================
static void escribir_portada(FILE *f) {
    fprintf(f,
        "\\begin{titlepage}\n"
        "\\centering\n"
        "{\\Large Instituto Tecnol\\'ogico de Costa Rica\\\\Escuela de Computaci\\'on}\\vspace{1cm}\n"
        "{\\huge Proyecto: Reemplazo de Equipos}\\vspace{1cm}\n"
        "{\\large II Semestre 2025}\\vspace{2cm}\n"
        "{\\large Estudiante(s):}\\\\Esteban Secaida - Fabian Bustos\\vfill\n"
        "{\\large Fecha: \\today}\n"
        "\\end{titlepage}\n");
}

static void escribir_problema(FILE *f, const ReemplazoData *p) {
    const int usarG = safe_check_active(checkGanancia);
    const double gan = safe_spin_value(spinGanancia, 0.0);
    const int usarI = safe_check_active(checkInflacion);
    const double i_pct = safe_spin_value(spinInflacion, 0.0);

    fprintf(f, "\\section*{Datos del Problema}\n");
    fprintf(f, "Costo inicial: $%.2f$, Horizonte $T=%d$, Vida \\'util $L=%d$.\\\\\n",
            p->costo_inicial, p->plazo, p->vida_util);

    if (usarG) fprintf(f, "\\textbf{Con ganancia por uso:} $%.2f$ por per\\'iodo.\\\\\n", gan);
    else       fprintf(f, "\\textbf{Sin ganancia por uso}.\\\\\n");

    if (usarI) fprintf(f, "\\textbf{Con inflaci\\'on:} $i=%.2f\\%%$ por per\\'iodo.\\\\\n", i_pct);
    else       fprintf(f, "\\textbf{Sin inflaci\\'on}.\\\\\n");

    fprintf(f, "\\subsection*{Mantenimiento y Reventa por Edad}\n");
    fprintf(f, "\\begin{tabular}{c|c|c}\\toprule\nEdad & Mant. & Reventa\\\\\\midrule\n");
    double mant[MAX_L+2]={0}, rev[MAX_L+2]={0};
    construir_series_edad(p, mant, rev);
    for (int k=1;k<=p->vida_util;++k) {
        fprintf(f, "%d & %.2f & %.2f \\\\\n", k, mant[k], rev[k]);
    }
    fprintf(f, "\\bottomrule\\end{tabular}\n");

    fprintf(f, "Se usa $C_{t,x}=\\text{Compra}+\\sum_{k=1}^{x-t}(\\text{Mant}(k)");
    if (usarG) fprintf(f, "-\\text{Gan}");
    fprintf(f, ")\\cdot(1+i)^{k-1}-\\text{Reventa}(x-t)\\cdot(1+i)^{x-t-1}$ si hay inflaci\\'on.\\\\\n");
}

static void escribir_ctx(FILE *f, const ReemplazoData *p, const SolveOut *S) {
    int T=p->plazo;
    fprintf(f, "\\section*{Tabla de $C_{t,x}$}\n");
    fprintf(f, "Entradas v\\'alidas con $t<x\\le\\min(t+L,T)$.\n\n");
    fprintf(f, "\\begin{tabular}{c|c|c}\\toprule\n t & x & $C_{t,x}$ \\\\\\midrule\n");
    for (int t=0;t<T;++t) {
        int x_max = mini(t+p->vida_util, T);
        for (int x=t+1;x<=x_max;++x) {
            fprintf(f, "%d & %d & %.2f \\\\\n", t, x, S->C[t][x]);
        }
    }
    fprintf(f, "\\bottomrule\\end{tabular}\n");
}

static void escribir_tabla_G(FILE *f, const ReemplazoData *p, const SolveOut *S) {
    fprintf(f, "\\section*{Programaci\\'on Din\\'amica: $G(t)$ y Siguientes}\n");
    fprintf(f, "\\begin{tabular}{c|c|l}\\toprule\n t & $G(t)$ & Siguientes \\\\\\midrule\n");
    for (int t=0;t<=p->plazo;++t) {
        fprintf(f, "%d & %.2f & ", t, S->R.G[t]);
        for (int i=0;i<S->R.nxt[t].count;++i) {
            fprintf(f, "%d%s", S->R.nxt[t].nexts[i], (i+1<S->R.nxt[t].count?", ":""));
        }
        fprintf(f, " \\\\\n");
    }
    fprintf(f, "\\bottomrule\\end{tabular}\n");
}

static void escribir_rutas(FILE *f, const ReemplazoData *p, const SolveOut *S) {
    fprintf(f, "\\section*{Todos los planes \\`optimos}\n");
    fprintf(f, "Costo m\\'inimo total: $G(0)=\\mathbf{%.2f}$.\\\\\n", S->R.G[0]);
    int mostrar = S->paths.num_paths;
    if (mostrar > 200) mostrar = 200;
    for (int pth=0; pth<mostrar; ++pth) {
        char ruta[1024]={0};
        format_ruta(ruta, sizeof(ruta), S->paths.paths[pth], S->paths.lens[pth]);
        fprintf(f, "\\noindent Ruta %d: \\texttt{%s}\\\\\n", pth+1, ruta);
    }
    if (S->paths.num_paths > mostrar) {
        fprintf(f, "\\small (Se generaron %d rutas; mostrando primeras %d.)\n", S->paths.num_paths, mostrar);
    }
}

static void generar_grafos_rutas(FILE *f, const ReemplazoData *p, const SolveOut *S) {
    fprintf(f, "\\section*{Grafos de Rutas \\`Optimas}\n");
    for (int r = 0; r < S->paths.num_paths; r++) {
        fprintf(f, "\\subsection*{Ruta óptima %d}\n", r+1);
        fprintf(f, "\\begin{tikzpicture}[->, >=stealth, node distance=2cm]\n");

        // Dibujar nodos en línea horizontal (0 incluido)
        fprintf(f, "\\node (R%dY0) at (0,0) {0};\n", r);
        for (int i = 0; i < S->paths.lens[r]; i++) {
            int year = S->paths.paths[r][i];
            fprintf(f, "\\node (R%dY%d) at (%d,0) {%d};\n", r, year, (i+1)*2, year);
        }

        // Dibujar aristas
        fprintf(f, "\\draw[->] (R%dY0) -- (R%dY%d);\n", r, r, S->paths.paths[r][0]);
        for (int i = 0; i < S->paths.lens[r]-1; i++) {
            int from = S->paths.paths[r][i];
            int to   = S->paths.paths[r][i+1];
            fprintf(f, "\\draw[->] (R%dY%d) -- (R%dY%d);\n", r, from, r, to);
        }

        fprintf(f, "\\end{tikzpicture}\n\\bigskip\n");
    }
}

static int generar_reporte_tex(const ReemplazoData *p, const SolveOut *S, const char *fname_unused) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    // 1) Crear carpeta con timestamp
    char dir[256];
    g_snprintf(dir, sizeof(dir), "reports/replace-%04d%02d%02d-%02d%02d%02d",
               tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
               tm.tm_hour, tm.tm_min, tm.tm_sec);

    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_printerr("[ERROR] No se pudo crear el directorio: %s\n", dir);
        return -1;
    }

    // 2) Archivo TEX dentro de la carpeta
    char tex[512];
    g_snprintf(tex, sizeof(tex), "%s/replace.tex", dir);

    FILE *f = fopen(tex, "w");
    if (!f) {
        g_printerr("[ERROR] No se pudo abrir para escritura: %s\n", tex);
        return -1;
    }

    // 3) Escribir LaTeX
    fprintf(f,
        "\\documentclass[11pt]{article}\n"
        "\\usepackage[margin=2.5cm]{geometry}\n"
        "\\usepackage{booktabs}\n"
        "\\usepackage{hyperref}\n"
        "\\usepackage{amsmath}\n"
        "\\usepackage[T1]{fontenc}\n"
        "\\usepackage[utf8]{inputenc}\n"
        "\\usepackage{tikz}\n"
        "\\usetikzlibrary{positioning}\n"
        "\\tikzset{every node/.style={circle, draw, minimum size=8mm}}\n"
        "\\begin{document}\n"
    );
    escribir_portada(f);
    escribir_problema(f, p);
    escribir_ctx(f, p, S);
    escribir_tabla_G(f, p, S);
    escribir_rutas(f, p, S);
    generar_grafos_rutas(f, p, S);
    fprintf(f, "\\end{document}\n");
    fclose(f);

    g_printerr("[INFO] Archivo TEX generado: %s\n", tex);

    // 4) Compilar dentro del directorio
    gchar *cmd_compile = g_strdup_printf(
        "cd '%s' && pdflatex -interaction=nonstopmode -halt-on-error replace.tex",
        dir
    );
    int ret = system(cmd_compile);
    g_free(cmd_compile);

    if (ret != 0) {
        g_printerr("[ERROR] pdflatex falló con código %d\n", ret);
        return -1;
    }

    // 5) Abrir PDF en modo presentación
    gchar *pdfpath = g_build_filename(dir, "replace.pdf", NULL);
    gchar *cmd_open = g_strdup_printf(
        "evince --presentation '%s' >/dev/null 2>&1 &",
        pdfpath
    );
    system(cmd_open);
    g_free(cmd_open);
    g_free(pdfpath);

    return 0;
}

// ==============================
// --------- Lectura UI ---------
// ==============================
static ReemplazoData leer_desde_widgets(void){
    ReemplazoData p;
    memset(&p, 0, sizeof(p));
    p.costo_inicial = atof(safe_entry_text(entryCosto));
    p.plazo = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinPlazo));
    p.vida_util = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinVida));

    if (p.plazo <= 0) p.plazo = 1;
    if (p.vida_util <= 0) p.vida_util = 1;
    if (p.vida_util > p.plazo) p.vida_util = p.plazo;

    p.periodos = (Periodo*)xmalloc(sizeof(Periodo)*p.plazo);

    // Leer directamente de la tabla; si está vacía, quedan 0
    double mant[MAX_L+2]={0}, rev[MAX_L+2]={0};
    tabla_leer_a_vectores(p.vida_util, mant, rev);

    for (int i=0;i<p.plazo;i++){
        int edad = i+1;
        p.periodos[i].periodo = edad;
        if (edad<=p.vida_util){
            p.periodos[i].mantenimiento = clampd(mant[edad], 0.0, 1e12);
            p.periodos[i].reventa = clampd(rev[edad], 0.0, 1e12);
        }else{
            p.periodos[i].mantenimiento = 0.0;
            p.periodos[i].reventa = 0.0;
        }
    }
    return p;
}

// ==============================
// ---- Helpers FileChooser -----
// ==============================
static char* elegir_ruta_guardar(GtkWindow *parent) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Guardar problema", parent, GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancelar", GTK_RESPONSE_CANCEL, "_Guardar", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "problema.rep");
    char *ruta = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        ruta = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    }
    gtk_widget_destroy(dlg);
    return ruta; // g_free() luego
}

static char* elegir_ruta_abrir(GtkWindow *parent) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Cargar problema", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancelar", GTK_RESPONSE_CANCEL, "_Abrir", GTK_RESPONSE_ACCEPT, NULL);
    char *ruta = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        ruta = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    }
    gtk_widget_destroy(dlg);
    return ruta; // g_free() luego
}

// Volcar struct en UI y grid
static void volcar_en_ui_y_tabla(const ReemplazoData *p) {
    char buf[64];
    snprintf(buf,sizeof(buf),"%.2f", p->costo_inicial);
    gtk_entry_set_text(GTK_ENTRY(entryCosto), buf);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinPlazo), p->plazo);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinVida),  p->vida_util);

    tabla_init_if_needed();
    tabla_resize_rows(p->vida_util);

    GtkListStore *store = tabla_get_store();
    if (!store) return;

    for (int i=0;i<p->vida_util;i++){
        GtkTreeIter it;
        if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &it, NULL, i)){
            gtk_list_store_set(store, &it,
                COL_MANT,    p->periodos[i].mantenimiento,
                COL_REVENTA, p->periodos[i].reventa, -1);
        }
    }
}

// ==============================
// ----------- Callbacks --------
// ==============================
static void on_spinVida_changed(GtkSpinButton *s, gpointer u){
    int L = gtk_spin_button_get_value_as_int(s);
    tabla_init_if_needed();
    tabla_resize_rows(L);
}

G_MODULE_EXPORT void on_btnGuardar_clicked(GtkButton *b, gpointer u) {
    GtkWindow *win = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(b)));
    char *ruta = elegir_ruta_guardar(win);
    if (!ruta) return;

    ReemplazoData p = leer_desde_widgets();
    guardar_problema(ruta, &p);
    free(p.periodos);
    g_print("Guardado en: %s\n", ruta);
    g_free(ruta);
}

G_MODULE_EXPORT void on_btnCargar_clicked(GtkButton *b, gpointer u) {
    GtkWindow *win = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(b)));
    char *ruta = elegir_ruta_abrir(win);
    if (!ruta) return;

    ReemplazoData *p = cargar_problema(ruta);
    if (p) {
        volcar_en_ui_y_tabla(p);
        g_print("Cargado de: %s\n", ruta);
        free(p->periodos);
        free(p);
    } else {
        g_printerr("No se pudo cargar %s\n", ruta);
    }
    g_free(ruta);
}

G_MODULE_EXPORT void on_btnEjecutar_clicked(GtkButton *b, gpointer u) {
    // Ejecuta con lo que está en UI/tabla actualmente
    ReemplazoData tmp = leer_desde_widgets();
    ReemplazoData *p = (ReemplazoData*)xmalloc(sizeof(ReemplazoData));
    *p = tmp;

    SolveOut S;
    memset(&S, 0, sizeof(S));
    solve_caso(p, &S);

    if (generar_reporte_tex(p, &S, "reporte.tex") == 0) {
        // ok
    } else {
        g_printerr("No se pudo crear reporte.tex\n");
    }

    pathset_free(&S.paths);
    free(p->periodos);
    free(p);
}

G_MODULE_EXPORT void on_btnSalir_clicked(GtkButton *b, gpointer u) {
    gtk_main_quit();
}

// ==============================
/* --------------- Main -------- */
// ==============================
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // 1) Ruta al .glade: SOLO "nuevo.glade" (argv tiene prioridad)
    const char *glade_path = NULL;
    if (argc >= 2 && g_file_test(argv[1], G_FILE_TEST_IS_REGULAR)) {
        glade_path = argv[1];
    } else {
        const char *candidates[] = {
            "p3/ui/nuevo.glade",
            "../ui/nuevo.glade",
            "ui/nuevo.glade",
            "nuevo.glade"
        };
        for (size_t i=0;i<sizeof(candidates)/sizeof(candidates[0]);++i) {
            if (g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) { glade_path = candidates[i]; break; }
        }
    }

    if (!glade_path) {
        char *cwd = g_get_current_dir();
        g_printerr("ERROR: No se encontró 'nuevo.glade'. CWD: %s\n", cwd);
        g_printerr("Prueba ejecutando con ruta: ./reemplazo ../ui/nuevo.glade\n");
        g_free(cwd);
        return 1;
    }

    // 2) Cargar la UI
    builder = gtk_builder_new_from_file(glade_path);

    #define GETW(name) GTK_WIDGET(gtk_builder_get_object(builder, name))
    #define ENSURE(wid, name) \
        if (!(wid)) { g_printerr("ERROR: No existe el widget '%s' en %s\n", name, glade_path); return 1; }

    GtkWidget *win = GETW("ventanaPrincipal");   ENSURE(win, "ventanaPrincipal");

    // IDs del glade (existentes)
    entryCosto = GETW("entryCosto");             ENSURE(entryCosto, "entryCosto");
    spinPlazo  = GETW("spinPlazo");              ENSURE(spinPlazo,  "spinPlazo");
    spinVida   = GETW("spinVida");               ENSURE(spinVida,   "spinVida");
    tabla      = GETW("tabla");                  /* TreeView editable */

    btnGuardar = GETW("btnGuardar");             ENSURE(btnGuardar, "btnGuardar");
    btnCargar  = GETW("btnCargar");              ENSURE(btnCargar,  "btnCargar");
    btnEjecutar= GETW("btnEjecutar");            ENSURE(btnEjecutar,"btnEjecutar");
    btnSalir   = GETW("btnSalir");               ENSURE(btnSalir,   "btnSalir");

    // ----- Opciones opcionales -----
    GtkWidget *grid = GETW("grid_inputs");
    checkGanancia = GETW("checkGanancia");
    spinGanancia  = GETW("spinGanancia");
    checkInflacion= GETW("checkInflacion");
    spinInflacion = GETW("spinInflacion");

    if (grid && GTK_IS_GRID(grid)) {
        int next_row = 3; // después de Costo/Plazo/Vida

        if (!checkGanancia) {
            checkGanancia = gtk_check_button_new_with_label("Usar ganancia por uso");
            gtk_grid_attach(GTK_GRID(grid), checkGanancia, 0, next_row, 2, 1);
            next_row++;
        }
        if (!spinGanancia) {
            GtkAdjustment *adj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 1e12, 10.0, 100.0, 0.0));
            GtkWidget *lbl = gtk_label_new("Ganancia por período:");
            spinGanancia = gtk_spin_button_new(adj, 1.0, 2);
            gtk_grid_attach(GTK_GRID(grid), lbl,          0, next_row, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), spinGanancia, 1, next_row, 1, 1);
            next_row++;
        }
        if (!checkInflacion) {
            checkInflacion = gtk_check_button_new_with_label("Usar inflación");
            gtk_grid_attach(GTK_GRID(grid), checkInflacion, 0, next_row, 2, 1);
            next_row++;
        }
        if (!spinInflacion) {
            GtkAdjustment *adj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, -100.0, 1000.0, 0.1, 1.0, 0.0));
            GtkWidget *lbl = gtk_label_new("Inflación (%% por período):");
            spinInflacion = gtk_spin_button_new(adj, 0.1, 2);
            gtk_grid_attach(GTK_GRID(grid), lbl,           0, next_row, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), spinInflacion, 1, next_row, 1, 1);
            next_row++;
        }
    } else {
        g_printerr("ADVERTENCIA: No se encontró 'grid_inputs'; no se pudieron insertar campos extra.\n");
    }

    // Inicializar tabla y enganchar resize al cambio de L
    tabla_init_if_needed();
    tabla_resize_rows(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinVida)));
    g_signal_connect(spinVida, "value-changed", G_CALLBACK(on_spinVida_changed), NULL);

    // 3) Señales a callbacks
    g_signal_connect(btnGuardar,  "clicked", G_CALLBACK(on_btnGuardar_clicked),  NULL);
    g_signal_connect(btnCargar,   "clicked", G_CALLBACK(on_btnCargar_clicked),   NULL);
    g_signal_connect(btnEjecutar, "clicked", G_CALLBACK(on_btnEjecutar_clicked), NULL);
    g_signal_connect(btnSalir,    "clicked", G_CALLBACK(on_btnSalir_clicked),    NULL);

    g_object_unref(builder);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}

