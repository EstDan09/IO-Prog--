// reemplazo.c - Único archivo con UI + DP + PDF
// Compilar (GTK3):
//   gcc -O2 -Wall -o reemplazo reemplazo.c `pkg-config --cflags --libs gtk+-3.0`
//
// Requiere: pdflatex y evince en PATH. Glade en: p3/ui/reemplazo.glade

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

// ==============================
// --------- I/O archivos -------
// ==============================
// Formato simple:
// costo plazo vida
// (luego 'plazo' filas: i reventa mantenimiento)
// Se usan hasta 'vida' filas para edad; si faltan, se repite la última
void guardar_problema(const char *fname, ReemplazoData *p) {
    FILE *f = fopen(fname, "w");
    if (!f) return;
    fprintf(f, "%.10g %d %d\n", p->costo_inicial, p->plazo, p->vida_util);
    for (int i = 0; i < p->plazo; i++) {
        int per = (p->periodos && p->periodos[i].periodo>0) ? p->periodos[i].periodo : (i+1);
        double rev = p->periodos ? p->periodos[i].reventa : 0.0;
        double man = p->periodos ? p->periodos[i].mantenimiento : 0.0;
        fprintf(f, "%d %.10g %.10g\n", per, rev, man);
    }
    fclose(f);
}

ReemplazoData *cargar_problema(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) return NULL;
    ReemplazoData *p = (ReemplazoData*)xmalloc(sizeof(ReemplazoData));
    if (fscanf(f, "%lf %d %d", &p->costo_inicial, &p->plazo, &p->vida_util) != 3) {
        fclose(f); free(p); return NULL;
    }
    if (p->plazo <= 0) { fclose(f); free(p); return NULL; }
    p->periodos = (Periodo*)xmalloc(sizeof(Periodo) * p->plazo);
    for (int i = 0; i < p->plazo; i++) {
        int per=0; double rev=0, man=0;
        if (fscanf(f, "%d %lf %lf", &per, &rev, &man) != 3) {
            // si faltan filas, rellenamos con última válida o ceros
            if (i>0){ per=i+1; rev=p->periodos[i-1].reventa; man=p->periodos[i-1].mantenimiento; }
            else    { per=i+1; rev=0; man=0; }
        }
        p->periodos[i].periodo = per;
        p->periodos[i].reventa = rev;
        p->periodos[i].mantenimiento = man;
    }
    fclose(f);
    return p;
}

// ==============================
// ------ Núcleo de solución -----
// ==============================

// Construye series por edad (1..L): mant[edad], rev[edad]
// Toma de p->periodos (si hay menos de L, repite el último)
static void construir_series_edad(const ReemplazoData *p, double *mant, double *rev) {
    int L = p->vida_util;
    // base: intenta leer de periodos por índice (se asume i ~ edad)
    for (int k=1; k<=L; ++k) {
        int idx = k-1;
        if (idx < p->plazo) {
            // si el archivo trae valores por "periodo", usamos los de esa fila
            mant[k] = p->periodos[idx].mantenimiento;
            rev[k]  = p->periodos[idx].reventa;
        } else {
            // extrapolar último
            mant[k] = mant[k-1];
            rev[k]  = rev[k-1];
        }
    }
    // sanity: si alguna entrada no quedó inicializada (poco probable)
    for (int k=1; k<=L; ++k) {
        if (isnan(mant[k])) mant[k]=0;
        if (isnan(rev[k]))  rev[k]=0;
    }
}

// Llena matriz C[t][x] para 0<=t<T y t+1<=x<=min(t+L,T)
// C_{t,x} = costo_inicial + sum_{k=1..edad} mant[k] - rev[edad], con edad = x-t
static void construir_C(const ReemplazoData *p, double C[MAX_T+2][MAX_T+2]) {
    int T = p->plazo;
    int L = p->vida_util;
    double mant[MAX_L+2]={0}, rev[MAX_L+2]={0};
    construir_series_edad(p, mant, rev);

    for (int t=0;t<=T;++t)
        for (int x=0;x<=T;++x)
            C[t][x] = INF;

    for (int t=0; t<T; ++t) {
        int x_max = mini(t+L, T);
        for (int x=t+1; x<=x_max; ++x) {
            int edad = x - t;
            double sumMant = 0.0;
            for (int k=1;k<=edad;k++)
                sumMant += mant[k];
            double costo = p->costo_inicial + sumMant - rev[edad];
            C[t][x] = costo;
        }
    }
}

// DP hacia atrás: G[T]=0; G[t]=min_x {C[t][x]+G[x]}; guardando TODOS los argmin
static void dp_resolver(const ReemplazoData *p, double C[MAX_T+2][MAX_T+2], DPRes *R) {
    int T = p->plazo;
    int L = p->vida_util;
    for (int t=0; t<=T; ++t) {
        R->G[t] = INF;
        R->nxt[t].count = 0;
    }
    R->G[T] = 0.0;

    for (int t=T-1; t>=0; --t) {
        double best = INF;
        int x_max = mini(t+L, T);
        // mínimo
        for (int x=t+1; x<=x_max; ++x) {
            double val = C[t][x] + R->G[x];
            if (val < best) best = val;
        }
        R->G[t] = best;
        // empates
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
        "{\\large Estudiante(s):}\\\\Esteban Secaida (y equipo)\\vfill\n"
        "{\\large Fecha: \\today}\n"
        "\\end{titlepage}\n");
}

static void escribir_problema(FILE *f, const ReemplazoData *p) {
    fprintf(f, "\\section*{Datos del Problema}\n");
    fprintf(f, "Costo inicial: $%.2f$, Horizonte $T=%d$, Vida \\'util $L=%d$.\\\\\n",
            p->costo_inicial, p->plazo, p->vida_util);
    fprintf(f, "\\subsection*{Mantenimiento y Reventa por Edad}\n");
    fprintf(f, "\\begin{tabular}{c|c|c}\\toprule\nEdad & Mant. & Reventa\\\\\\midrule\n");
    double mant[MAX_L+2]={0}, rev[MAX_L+2]={0};
    construir_series_edad(p, mant, rev);
    for (int k=1;k<=p->vida_util;++k) {
        fprintf(f, "%d & %.2f & %.2f \\\\\n", k, mant[k], rev[k]);
    }
    fprintf(f, "\\bottomrule\\end{tabular}\n");
    fprintf(f, "Se usa $C_{t,x}=\\text{Compra}+\\sum_{k=1}^{x-t}\\text{Mant}(k)-\\text{Reventa}(x-t)$.\n");
}

static void escribir_ctx(FILE *f, const ReemplazoData *p, const SolveOut *S) {
    int T=p->plazo, L=p->vida_util;
    fprintf(f, "\\section*{Tabla de $C_{t,x}$}\n");
    fprintf(f, "Entradas v\\'alidas con $t<x\\le\\min(t+L,T)$.\n\n");
    fprintf(f, "\\begin{tabular}{c|c|c}\\toprule\n t & x & $C_{t,x}$ \\\\\\midrule\n");
    for (int t=0;t<T;++t) {
        int x_max = mini(t+L, T);
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
    if (mostrar > 200) mostrar = 200; // evitar PDFs gigantes
    for (int pth=0; pth<mostrar; ++pth) {
        char ruta[1024]={0};
        format_ruta(ruta, sizeof(ruta), S->paths.paths[pth], S->paths.lens[pth]);
        fprintf(f, "\\noindent Ruta %d: \\texttt{%s}\\\\\n", pth+1, ruta);
    }
    if (S->paths.num_paths > mostrar) {
        fprintf(f, "\\small (Se generaron %d rutas; mostrando primeras %d.)\n", S->paths.num_paths, mostrar);
    }
}

static int generar_reporte_tex(const ReemplazoData *p, const SolveOut *S, const char *fname) {
    FILE *f = fopen(fname, "w");
    if (!f) return -1;
    fprintf(f, "\\documentclass[11pt]{article}\n"
               "\\usepackage[margin=2.5cm]{geometry}\n"
               "\\usepackage{booktabs}\n"
               "\\usepackage{hyperref}\n"
               "\\begin{document}\n");
    escribir_portada(f);
    escribir_problema(f, p);
    escribir_ctx(f, p, S);
    escribir_tabla_G(f, p, S);
    escribir_rutas(f, p, S);
    fprintf(f, "\\end{document}\n");
    fclose(f);
    return 0;
}

static void compilar_y_abrir_pdf(const char *tex) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pdflatex -interaction=nonstopmode -halt-on-error %s > /dev/null", tex);
    system(cmd);
    system(cmd);
    char pdf[512];
    strncpy(pdf, tex, sizeof(pdf)-1);
    char *dot = strrchr(pdf, '.'); if (dot) strcpy(dot, ".pdf");
    snprintf(cmd, sizeof(cmd), "evince \"%s\" &", pdf);
    system(cmd);
}

// ==============================
// --------- Lectura UI ---------
// ==============================

// Si en esta fase no tienes editables reales en la tabla del Glade,
// puedes arrancar con este generador "demo" para guardar y luego editar el archivo:
static void rellenar_demo(ReemplazoData *p){
    // ejemplo base: decaimiento de reventa y mantenimiento creciente
    for (int i=0;i<p->plazo;i++){
        p->periodos[i].periodo = i+1;
        // usa clamps para valores razonables si cambias L/T
        p->periodos[i].reventa = clampd( (i+1)<=p->vida_util ? (400.0 - 100.0*i) : 0.0, 0.0, 1e9 );
        p->periodos[i].mantenimiento = clampd( (i+1)<=p->vida_util ? (20.0 + 10.0*i) : 0.0, 0.0, 1e9 );
    }
}

// Lee desde widgets básicos (entry/spins). La tabla real no se usa aquí;
// se recomienda gestionar periodos vía archivo (Guardar→editar→Cargar)
static ReemplazoData leer_desde_widgets_o_demo(void){
    ReemplazoData p;
    memset(&p, 0, sizeof(p));
    p.costo_inicial = atof(gtk_entry_get_text(GTK_ENTRY(entryCosto)));
    p.plazo = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinPlazo));
    p.vida_util = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinVida));

    if (p.plazo <= 0) p.plazo = 1;
    if (p.vida_util <= 0) p.vida_util = 1;
    if (p.vida_util > p.plazo) p.vida_util = p.plazo;

    p.periodos = (Periodo*)xmalloc(sizeof(Periodo)*p.plazo);
    rellenar_demo(&p); // puedes reemplazar por lectura de tu GtkTreeView si ya lo tienes cableado
    return p;
}

// ==============================
// ----------- Callbacks --------
// ==============================
G_MODULE_EXPORT void on_btnGuardar_clicked(GtkButton *b, gpointer u) {
    ReemplazoData p = leer_desde_widgets_o_demo();
    guardar_problema("problema.rep", &p);
    free(p.periodos);
    gtk_widget_set_sensitive(GTK_WIDGET(b), TRUE);
}

G_MODULE_EXPORT void on_btnCargar_clicked(GtkButton *b, gpointer u) {
    ReemplazoData *p = cargar_problema("problema.rep");
    if (p) {
        // Podrías volcar a los widgets si deseas
        char buf[64];
        snprintf(buf,sizeof(buf),"%.2f", p->costo_inicial);
        gtk_entry_set_text(GTK_ENTRY(entryCosto), buf);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinPlazo), p->plazo);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinVida), p->vida_util);
        // (Si tienes tabla editable en UI, aquí la llenarías)
        g_print("Cargado: Costo=%.2f, Plazo=%d, Vida=%d\n", p->costo_inicial, p->plazo, p->vida_util);
        free(p->periodos);
        free(p);
    } else {
        g_printerr("No se pudo cargar problema.rep\n");
    }
}

G_MODULE_EXPORT void on_btnEjecutar_clicked(GtkButton *b, gpointer u) {
    ReemplazoData *p = cargar_problema("problema.rep");
    if (!p) {
        // Si no hay archivo, ejecuta con datos de los widgets (demo)
        ReemplazoData tmp = leer_desde_widgets_o_demo();
        p = (ReemplazoData*)xmalloc(sizeof(ReemplazoData));
        *p = tmp; // shallow copy; tmp.periodos queda en p
    }
    // Resolver
    SolveOut S;
    memset(&S, 0, sizeof(S));
    solve_caso(p, &S);

    // Generar PDF
    if (generar_reporte_tex(p, &S, "reporte.tex") == 0) {
        compilar_y_abrir_pdf("reporte.tex");
    } else {
        g_printerr("No se pudo crear reporte.tex\n");
    }

    // Limpieza
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

    // 1) Resolver ruta del .glade probando varias opciones relativas
    const char *candidates[] = {
        "p3/ui/reemplazo.glade",   // si corres desde raíz del repo
        "../ui/reemplazo.glade",   // si corres desde p3/src  <-- TU CASO
        "ui/reemplazo.glade",      // por si mueves el binario a p3/
        "reemplazo.glade"          // por si lo dejas junto al binario
    };
    const int npaths = (int)(sizeof(candidates)/sizeof(candidates[0]));
    const char *glade_path = NULL;

    for (int i = 0; i < npaths; ++i) {
        if (g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) {
            glade_path = candidates[i];
            break;
        }
    }

    if (!glade_path) {
        // Mensaje útil: imprime CWD y las rutas que intentó
        char *cwd = g_get_current_dir();
        g_printerr("ERROR: No se encontró 'reemplazo.glade'. CWD: %s\n", cwd);
        g_printerr("Se intentaron estas rutas relativas:\n");
        for (int i = 0; i < npaths; ++i) g_printerr("  - %s\n", candidates[i]);
        g_free(cwd);
        return 1;
    }

    // 2) Cargar la UI
    builder = gtk_builder_new_from_file(glade_path);
    GtkWidget *win = GTK_WIDGET(gtk_builder_get_object(builder, "ventanaPrincipal"));
    if (!win) {
        g_printerr("ERROR: No se encontró el objeto 'ventanaPrincipal' dentro de %s\n", glade_path);
        return 1;
    }

    // 3) Obtener los widgets (los mismos IDs que ya usas)
    entryCosto = GTK_WIDGET(gtk_builder_get_object(builder, "entryCostoInicial"));
    spinPlazo  = GTK_WIDGET(gtk_builder_get_object(builder, "spinPlazo"));
    spinVida   = GTK_WIDGET(gtk_builder_get_object(builder, "spinVidaUtil"));
    tabla      = GTK_WIDGET(gtk_builder_get_object(builder, "tablaDatos"));
    btnGuardar = GTK_WIDGET(gtk_builder_get_object(builder, "btnGuardar"));
    btnCargar  = GTK_WIDGET(gtk_builder_get_object(builder, "btnCargar"));
    btnEjecutar= GTK_WIDGET(gtk_builder_get_object(builder, "btnEjecutar"));
    btnSalir   = GTK_WIDGET(gtk_builder_get_object(builder, "btnSalir"));

    gtk_builder_connect_signals(builder, NULL);
    g_object_unref(builder);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}


