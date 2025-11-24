// p2/src/knapsack.c
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#define MAX_ITEMS  50   // max de items, está hard setteado a 20 pero teóricamente pueden ser más 
#define MAX_CAP    200  // seguridad; GUI limita a 20
#define INF_QTY    (-1) // infinito

typedef enum { KNAP_01 = 0, KNAP_BOUNDED = 1, KNAP_UNBOUNDED = 2 } KnapType;

typedef struct {
    char  name[64];
    int   weight;
    int   value;
    int   qty;   // -1 => infinito
} Item;

typedef struct {
    int n, W;
    KnapType type;
    Item items[MAX_ITEMS];
} CaseData;

/* ---- Widgets ---- */
static GtkBuilder *builder = NULL;
static GtkWidget  *win, *btn_run, *btn_save, *btn_load, *btn_export, *grid_items, *grid_dp;
static GtkWidget  *spin_W, *spin_N, *combo_type, *sw_items, *sw_dp;

/* --- Callbacks auxiliares --- */
static void on_chk_inf_toggled(GtkToggleButton *btn, gpointer user_data) {
    GtkSpinButton *qty = GTK_SPIN_BUTTON(user_data);
    gboolean inf = gtk_toggle_button_get_active(btn);
    gtk_widget_set_sensitive(GTK_WIDGET(qty), !inf);
}

/* ---- Helpers GUI ---- */
static void clear_grid(GtkWidget *grid) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(grid));
    for (GList *l = children; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
}

/* Construye filas de edición para N items */
static void rebuild_items_rows(int N) {
    clear_grid(grid_items);
    // encabezados
    const char *hdrs[] = {"#", "Nombre", "Peso", "Valor", "Cantidad", "∞"};
    for (int c=0;c<6;c++){
        GtkWidget *lbl = gtk_label_new(hdrs[c]);
        gtk_grid_attach(GTK_GRID(grid_items), lbl, c, 0, 1, 1);
    }
    for (int i=0;i<N;i++){
        char buf[8]; g_snprintf(buf,sizeof(buf),"%d", i+1);
        gtk_grid_attach(GTK_GRID(grid_items), gtk_label_new(buf), 0, i+1, 1, 1);

        GtkWidget *e_name = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(e_name), "Item");
        g_object_set_data(G_OBJECT(e_name), "role", "name");
        gtk_grid_attach(GTK_GRID(grid_items), e_name, 1, i+1, 1, 1);

        GtkWidget *s_w = gtk_spin_button_new_with_range(0, 1000, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_w), 1);
        g_object_set_data(G_OBJECT(s_w), "role", "weight");
        gtk_grid_attach(GTK_GRID(grid_items), s_w, 2, i+1, 1, 1);

        GtkWidget *s_v = gtk_spin_button_new_with_range(0, 1000000, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_v), 1);
        g_object_set_data(G_OBJECT(s_v), "role", "value");
        gtk_grid_attach(GTK_GRID(grid_items), s_v, 3, i+1, 1, 1);

        GtkWidget *s_q = gtk_spin_button_new_with_range(0, 100, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_q), 1);
        g_object_set_data(G_OBJECT(s_q), "role", "qty");
        gtk_grid_attach(GTK_GRID(grid_items), s_q, 4, i+1, 1, 1);

        GtkWidget *chk_inf = gtk_check_button_new();
        gtk_grid_attach(GTK_GRID(grid_items), chk_inf, 5, i+1, 1, 1);

        GtkStyleContext *ctx = gtk_widget_get_style_context(chk_inf);
        gtk_style_context_add_class(ctx, "infinite-check"); 

        // toggle infinito deshabilita qty (C puro, sin lambdas)
        g_signal_connect(chk_inf, "toggled",
                         G_CALLBACK(on_chk_inf_toggled), s_q);
    }
    gtk_widget_show_all(grid_items);
}

/* Lee la tabla de items desde el grid */
static gboolean read_case_from_gui(CaseData *cs, char **errmsg) {
    cs->W = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_W));
    cs->n = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_N));
    cs->type = (KnapType) gtk_combo_box_get_active(GTK_COMBO_BOX(combo_type));
    if (cs->W < 0 || cs->W > 20) { *errmsg = g_strdup("Capacidad debe estar entre 0 y 20."); return FALSE; }
    if (cs->n < 1 || cs->n > MAX_ITEMS) { *errmsg = g_strdup("Cantidad de objetos inválida."); return FALSE; }

    for (int i=0;i<cs->n;i++){
        GtkWidget *w_name = gtk_grid_get_child_at(GTK_GRID(grid_items), 1, i+1);
        GtkWidget *w_w    = gtk_grid_get_child_at(GTK_GRID(grid_items), 2, i+1);
        GtkWidget *w_v    = gtk_grid_get_child_at(GTK_GRID(grid_items), 3, i+1);
        GtkWidget *w_q    = gtk_grid_get_child_at(GTK_GRID(grid_items), 4, i+1);
        GtkWidget *w_inf  = gtk_grid_get_child_at(GTK_GRID(grid_items), 5, i+1);

        const char *name = gtk_entry_get_text(GTK_ENTRY(w_name));
        if (!name || !*name) name = "item";
        g_strlcpy(cs->items[i].name, name, sizeof(cs->items[i].name));
        cs->items[i].weight = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(w_w));
        cs->items[i].value  = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(w_v));
        gboolean inf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_inf));
        cs->items[i].qty    = inf ? INF_QTY : (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(w_q));

        if (cs->items[i].weight < 0 || cs->items[i].value < 0) {
            *errmsg = g_strdup_printf("Valores negativos en item %d.", i+1);
            return FALSE;
        }
    }
    return TRUE;
}

/* ---- DP con tabla 2D para mostrar "como en clase" ---- */
/* Guardamos valores y decisión: 0 = skip, 1 = take, 2 = empate */
typedef struct { int val; unsigned char dec; } Cell;

static Cell **alloc_table(int n, int W){
    Cell **T = g_malloc0((n+1)*sizeof(Cell*));
    for(int i=0;i<=n;i++) T[i] = g_malloc0((W+1)*sizeof(Cell));
    return T;
}
static void free_table(Cell **T, int n){ for(int i=0;i<=n;i++) g_free(T[i]); g_free(T); }

/* Holder para poder liberar la tabla con su 'n' vía GDestroyNotify */
typedef struct { Cell **T; int n; } TableHolder;
static void destroy_table_holder(gpointer data){
    TableHolder *H = (TableHolder*)data;
    if (!H) return;
    if (H->T) free_table(H->T, H->n);
    g_free(H);
}

/* Bounded: max k de 0..min(qi, w/wi) */
static void solve_knap(const CaseData *cs, Cell ***outT){
    int n = cs->n, W = cs->W;
    Cell **T = alloc_table(n, W);

    for (int i=1;i<=n;i++){
        int wi = cs->items[i-1].weight;
        int vi = cs->items[i-1].value;
        int qi = cs->items[i-1].qty; // -1 => inf
        for (int w=0; w<=W; w++){
            // opción skip
            int best = T[i-1][w].val;
            unsigned char bestDec = 0;

            if (cs->type == KNAP_01){
                if (wi <= w) {
                    int cand = T[i-1][w - wi].val + vi;
                    if (cand > best) { best = cand; bestDec = 1; }
                    else if (cand == best && cand != T[i-1][w].val) { bestDec = 2; }
                }
            } else if (cs->type == KNAP_UNBOUNDED){
                if (wi <= w) {
                    int cand = T[i][w - wi].val + vi; // nota: i (no i-1)
                    if (cand > best) { best = cand; bestDec = 1; }
                    else if (cand == best && cand != T[i-1][w].val) { bestDec = 2; }
                }
            } else { // KNAP_BOUNDED
                int maxk = (wi==0)? 0 : w / wi;
                if (qi != INF_QTY && qi < maxk) maxk = qi;
                for (int k=1; k<=maxk; k++){
                    int cand = T[i-1][w - k*wi].val + k*vi;
                    if (cand > best) { best = cand; bestDec = 1; }
                    else if (cand == best && cand != T[i-1][w].val) { bestDec = 2; }
                }
            }
            T[i][w].val = best;
            T[i][w].dec = (best == T[i-1][w].val) ? (bestDec==1?2:0) : (bestDec?bestDec:1);
            if (best == T[i-1][w].val) T[i][w].dec = (best == T[i-1][w].val && bestDec==1) ? 2 : 0;
        }
    }
    *outT = T;
}

/* Backtracking para enumerar soluciones óptimas (puede explotar combinatoriamente; acotamos) */
typedef struct { int count; int solMax; int **sols; } Sols; // sols[k][i] = cantidad del item i
static Sols *sols_new(int n, int limit){ Sols *S = g_malloc0(sizeof(Sols)); S->solMax=limit; S->sols=g_malloc0(limit*sizeof(int*)); for(int i=0;i<limit;i++){S->sols[i]=g_malloc0(n*sizeof(int));} return S; }
static void sols_free(Sols *S){ if(!S) return; for(int i=0;i<S->solMax;i++) g_free(S->sols[i]); g_free(S->sols); g_free(S); }

/* Enumerador (simple, no exhaustivo para bounded/unbounded; suficiente para mostrar múltiples óptimos pequeños) */
static void backtrack(const CaseData *cs, Cell **T, int i, int w, int *curr, Sols *S){
    if (i==0 || w==0){ if (S->count < S->solMax){ memcpy(S->sols[S->count++], curr, sizeof(int)*cs->n);} return; }
    int wi = cs->items[i-1].weight, vi = cs->items[i-1].value, qi = cs->items[i-1].qty;
    int best = T[i][w].val;

    // Opción "arriba" (no tomar i-ésimo) si mantiene valor
    if (T[i-1][w].val == best) {
        backtrack(cs, T, i-1, w, curr, S);
    }

    // Opción "tomar" según variante
    if (wi <= w){
        if (cs->type == KNAP_01){
            if (T[i-1][w-wi].val + vi == best){
                curr[i-1] += 1;
                backtrack(cs, T, i-1, w-wi, curr, S);
                curr[i-1] -= 1;
            }
        } else if (cs->type == KNAP_UNBOUNDED){
            if (T[i][w-wi].val + vi == best){
                curr[i-1] += 1;
                backtrack(cs, T, i, w-wi, curr, S);
                curr[i-1] -= 1;
            }
        } else { // bounded: probamos k
            int maxk = (wi==0)? 0 : w/wi;
            if (qi != INF_QTY && qi < maxk) maxk = qi;
            for (int k=1;k<=maxk;k++){
                if (T[i-1][w - k*wi].val + k*vi == best){
                    curr[i-1] += k;
                    backtrack(cs, T, i-1, w - k*wi, curr, S);
                    curr[i-1] -= k;
                }
            }
        }
    }
}

/* Pinta la tabla DP como grid con color semáforo (verde=arriba, rojo=tomar, ambos=empate) */
static void render_dp_table(const CaseData *cs, Cell **T){
    clear_grid(grid_dp);
    // encabezados
    for (int w=0; w<=cs->W; w++){
        char b[16]; g_snprintf(b,sizeof(b),"%d",w);
        gtk_grid_attach(GTK_GRID(grid_dp), gtk_label_new(b), w+1, 0, 1, 1);
    }
    gtk_grid_attach(GTK_GRID(grid_dp), gtk_label_new("i\\W"), 0, 0, 1, 1);

    for (int i=0;i<=cs->n;i++){
        char b[16]; g_snprintf(b,sizeof(b),"%d",i);
        gtk_grid_attach(GTK_GRID(grid_dp), gtk_label_new(b), 0, i+1, 1, 1);
        for (int w=0; w<=cs->W; w++){
            char v[32]; g_snprintf(v,sizeof(v),"%d", T[i][w].val);
            GtkWidget *lbl = gtk_label_new(v);
            GtkStyleContext *ctx = gtk_widget_get_style_context(lbl);
            if (i>0){
                if (T[i][w].dec == 1) gtk_style_context_add_class(ctx, "dp-take");
                else if (T[i][w].dec == 0) gtk_style_context_add_class(ctx, "dp-skip");
                else if (T[i][w].dec == 2) gtk_style_context_add_class(ctx, "dp-tie");
            }
            gtk_grid_attach(GTK_GRID(grid_dp), lbl, w+1, i+1, 1, 1);
        }
    }
    gtk_widget_show_all(grid_dp);
}

/* Guardar / Cargar caso en formato simple */
static gboolean save_case(const CaseData *cs, const char *path, char **err) {
    FILE *f = fopen(path, "w");
    if (!f){ *err = g_strdup_printf("No se puede escribir %s", path); return FALSE; }
    fprintf(f, "type=%d\nW=%d\nn=%d\n", cs->type, cs->W, cs->n);
    for (int i=0;i<cs->n;i++){
        fprintf(f, "%s;%d;%d;%d\n", cs->items[i].name, cs->items[i].weight, cs->items[i].value, cs->items[i].qty);
    }
    fclose(f); return TRUE;
}
static gboolean load_case(CaseData *cs, const char *path, char **err) {
    FILE *f = fopen(path, "r");
    if (!f){ *err = g_strdup_printf("No se puede leer %s", path); return FALSE; }
    if (fscanf(f, "type=%d\nW=%d\nn=%d\n", (int*)&cs->type, &cs->W, &cs->n)!=3){ *err=g_strdup("Formato inválido"); fclose(f); return FALSE; }
    if (cs->n<1 || cs->n>MAX_ITEMS){ *err=g_strdup("n fuera de rango"); fclose(f); return FALSE; }
    for (int i=0;i<cs->n;i++){
        char line[256]; if (!fgets(line, sizeof(line), f)){ *err=g_strdup("Faltan filas"); fclose(f); return FALSE; }
        char name[64]; int w,v,q;
        if (sscanf(line, "%63[^;];%d;%d;%d", name, &w, &v, &q)!=4){ *err=g_strdup("Fila inválida"); fclose(f); return FALSE; }
        g_strlcpy(cs->items[i].name, name, sizeof(cs->items[i].name));
        cs->items[i].weight=w; cs->items[i].value=v; cs->items[i].qty=q;
    }
    fclose(f); return TRUE;
}

/* Generar LaTeX + compilar y abrir (evince -s) */
static gboolean write_latex_and_compile(const CaseData *cs, Cell **T, Sols *S, char **out_pdf_path, char **err) {
    // carpeta reports/knap-YYYYMMDD-HHMMSS
    time_t t=time(NULL); struct tm tm=*localtime(&t);
    char dir[256]; g_snprintf(dir,sizeof(dir),"reports/knap-%04d%02d%02d-%02d%02d%02d",
                              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    g_mkdir_with_parents(dir, 0755);
    char tex[512]; g_snprintf(tex,sizeof(tex), "%s/knap.tex", dir);
    char pdf[512]; g_snprintf(pdf,sizeof(pdf), "%s/knap.pdf", dir);

    FILE *f = fopen(tex, "w");
    if(!f){ *err=g_strdup("No se pudo crear .tex"); return FALSE; }

    fprintf(f,
        "\\documentclass[11pt]{article}\n"
        "\\usepackage[margin=1in]{geometry}\n"
        "\\usepackage{amsmath, amssymb}\n"
        "\\usepackage[table]{xcolor}\n"
        "\\usepackage{longtable}\n"
        "\\title{Proyecto 2: Problema de la Mochila}\\date{\\today}\n"
        "\\begin{document}\n"
        "\\begin{titlepage}\n"
        "  \\centering\n"
        "  \\vfill\n"
        "  {\\Huge Proyecto 2 : Problema de la Mochila}\\par\n"                     
        "  \\vspace{1cm}\n"
        "  {\\Large Curso: Investigación de Operaciones}\\par\n"            
        "  {\\Large Semestre: II - 2025}\\par\n"         
        "  \\vfill\n"
        "  {\\Large Autores: Fabián Bustos - Esteban Secaida}\\par\n"
        "  \\vspace{1cm}\n"
        "  {\\large Fecha: \\today}\\par\n"
        "  \\vfill\n"
        "\\end{titlepage}\n\n"
        "\\section*{Descripción}\n"
        "Se resuelve el problema de la mochila en su variante \\textit{%s}, con una capacidad total de $W=%d$ unidades. \\\\ \n"
        "El conjunto de datos incluye %d objetos disponibles, cada uno caracterizado por un peso y un valor asociado. \\\\ \n"
        "El objetivo consiste en seleccionar una combinación de estos objetos de modo que la suma de los pesos no exceda la capacidad $W$, \n"
        "maximizando al mismo tiempo el valor total obtenido en la mochila. \\\\ \n"
        "En la variante \\textit{%s}, las restricciones sobre la cantidad de copias de cada objeto difieren: en el caso 0/1 ($x_i \\in \\{0,1\\}$) \n"
        "solo puede elegirse cada objeto una vez; en la variante bounded ($0 \\leq x_i \\leq b_i$) existe un límite superior $b_i$ de copias permitidas; \n"
        "y en la variante unbounded ($x_i \\geq 0$) puede elegirse cualquier número de copias sin restricción. \\\\ \n",
        (cs->type==KNAP_01?"0/1": (cs->type==KNAP_UNBOUNDED?"unbounded":"bounded")),
        cs->W,
        cs->n,
        (cs->type==KNAP_01?"0/1": (cs->type==KNAP_UNBOUNDED?"unbounded":"bounded"))
    );


    // problema formal
    fprintf(f,"\\subsection*{Problema ingresado}\n"
              "Maximizar $Z = \\sum_{i=1}^{%d} v_i x_i$ \\quad sujeto a $\\sum_{i=1}^{%d} w_i x_i \\le %d$, $x_i \\ge 0$ enteras", cs->n, cs->n, cs->W);
    if (cs->type==KNAP_01) fprintf(f,", $x_i\\in \\{0,1\\}$.\n");
    else if (cs->type==KNAP_BOUNDED) fprintf(f,", $0\\le x_i \\le q_i$.\n");
    else fprintf(f,".\n");

    fprintf(f,"\\\\Datos:\\\\\\\n\\begin{longtable}{r|lrrr}\\# & Nombre & $w_i$ & $v_i$ & $q_i$\\\\\\hline\n");
    for (int i=0;i<cs->n;i++){
        char qtybuf[32];
        if (cs->items[i].qty == INF_QTY) g_strlcpy(qtybuf, "$\\infty$", sizeof(qtybuf));
        else g_snprintf(qtybuf, sizeof(qtybuf), "%d", cs->items[i].qty);
        fprintf(f,"%d & %s & %d & %d & %s \\\\\n",
                i+1, cs->items[i].name, cs->items[i].weight, cs->items[i].value, qtybuf);
    }
    fprintf(f,"\\end{longtable}\n");

    // tabla DP
    fprintf(f,
    "\\subsection*{Tabla de trabajo (DP)}\n"
    "\\setlength{\\tabcolsep}{4pt}"
    "\\renewcommand{\\arraystretch}{1.1}\n"
    "\\begin{center}\n"
    );
    fprintf(f,"\\noindent\\begin{tabular}{r|");
    for (int w=0; w<=cs->W; w++) fprintf(f,"r");
    fprintf(f,"}\\hline\n$i\\backslash W$ ");
    for (int w=0; w<=cs->W; w++) fprintf(f,"& %d ", w);
    fprintf(f,"\\\\\\hline\n");
    for (int i=0;i<=cs->n;i++){
        fprintf(f,"%d ", i);
        for (int w=0; w<=cs->W; w++){
            const char *cell = "";
            if (i==0) cell = "\\textcolor{black}";
            else if (T[i][w].dec==0) cell="\\textcolor{green!70!black}";
            else if (T[i][w].dec==1) cell="\\textcolor{red!70!black}";
            else cell="\\textcolor{blue!70!black}";
            fprintf(f,"& %s{%d} ", cell, T[i][w].val);
        }
        fprintf(f,"\\\\\n");
    }
    fprintf(f,"\\hline\\end{tabular}\n");
    fprintf(f, "\\end{center}\n");

    // soluciones
    fprintf(f,"\\subsection*{Solución óptima}\n"
              "Valor óptimo $Z^* = %d$.\\\\\n", T[cs->n][cs->W].val);
    for (int k=0;k<S->count;k++){
        fprintf(f,"Solución %d: ", k+1);
        for (int i=0;i<cs->n;i++) if (S->sols[k][i]>0) fprintf(f,"$x_{%d}=%d$ ", i+1, S->sols[k][i]);
        fprintf(f,"\\\\\n");
    }
    if (S->count==0) fprintf(f,"No se listaron soluciones (capacidad 0 o datos vacíos).\\\\\n");

    fprintf(f,"\\end{document}\n");
    fclose(f);

    // compilar
    char cmd[1024];
    g_snprintf(cmd,sizeof(cmd),
               "cd '%s' && pdflatex -interaction=nonstopmode knap.tex >/dev/null 2>&1 && (setsid evince -s '%s' >/dev/null 2>&1 &)",
               dir, "knap.pdf");
    int r = system(cmd);
    if (r==-1){ *err = g_strdup("Fallo al invocar pdflatex/evince"); return FALSE; }
    *out_pdf_path = g_strdup(pdf);
    return TRUE;
}

/* ---- Callbacks ---- */

static void on_change_N(GtkSpinButton *s, gpointer) {
    rebuild_items_rows((int)gtk_spin_button_get_value(s));
}
static void on_click_save(GtkButton*, gpointer){
    CaseData cs; char *msg=NULL;
    if(!read_case_from_gui(&cs,&msg)){ GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"%s",msg); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); g_free(msg); return; }
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Guardar caso (.knap)", GTK_WINDOW(win),
                                GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancelar", GTK_RESPONSE_CANCEL, "_Guardar", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), "cases");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "mochila.knap");
    if (gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT){
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        char *err=NULL; if(!save_case(&cs, path, &err)){ GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"%s",err); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); g_free(err); }
        g_free(path);
    }
    gtk_widget_destroy(chooser);
}
static void on_click_load(GtkButton*, gpointer){
    CaseData cs; char *err=NULL;
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Cargar caso (.knap)", GTK_WINDOW(win),
                                GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancelar", GTK_RESPONSE_CANCEL, "_Abrir", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), "cases");
    if (gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT){
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if(load_case(&cs, path, &err)){
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_W), cs.W);
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo_type), cs.type);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_N), cs.n);
            rebuild_items_rows(cs.n);
            for (int i=0;i<cs.n;i++){
                gtk_entry_set_text(GTK_ENTRY(gtk_grid_get_child_at(GTK_GRID(grid_items), 1, i+1)), cs.items[i].name);
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(gtk_grid_get_child_at(GTK_GRID(grid_items), 2, i+1)), cs.items[i].weight);
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(gtk_grid_get_child_at(GTK_GRID(grid_items), 3, i+1)), cs.items[i].value);
                gboolean inf = (cs.items[i].qty==INF_QTY);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_grid_get_child_at(GTK_GRID(grid_items),5,i+1)), inf);
                if (!inf) gtk_spin_button_set_value(GTK_SPIN_BUTTON(gtk_grid_get_child_at(GTK_GRID(grid_items), 4, i+1)), cs.items[i].qty);
                gtk_widget_set_sensitive(gtk_grid_get_child_at(GTK_GRID(grid_items), 4, i+1), !inf);
            }
        } else {
            GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"%s",err); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); g_free(err);
        }
        g_free(path);
    }
    gtk_widget_destroy(chooser);
}
static void on_click_run(GtkButton*, gpointer){
    CaseData cs; char *msg=NULL;
    if(!read_case_from_gui(&cs,&msg)){ GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"%s",msg); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); g_free(msg); return; }
    Cell **T=NULL; solve_knap(&cs, &T);
    render_dp_table(&cs, T);

    int *curr = g_malloc0(sizeof(int)*cs.n);
    Sols *S = sols_new(cs.n, 64); // límite razonable
    backtrack(&cs, T, cs.n, cs.W, curr, S);
    g_free(curr);

    // Guardamos último resultado en datos del botón export
    g_object_set_data_full(G_OBJECT(btn_export), "case",
        g_memdup2(&cs, sizeof(CaseData)), g_free);

    TableHolder *H = g_new0(TableHolder, 1);
    H->T = T;
    H->n = cs.n;
    g_object_set_data_full(G_OBJECT(btn_export), "table", H, destroy_table_holder);

    g_object_set_data_full(G_OBJECT(btn_export), "sols", S, (GDestroyNotify)sols_free);
}
static void on_click_export(GtkButton *b, gpointer){
    CaseData *cs = (CaseData*) g_object_get_data(G_OBJECT(b), "case");
    TableHolder *H = (TableHolder*) g_object_get_data(G_OBJECT(b), "table");
    Cell    **T  = H ? H->T : NULL;
    Sols    *S   = (Sols   *) g_object_get_data(G_OBJECT(b), "sols");
    if (!cs || !T || !S){
        GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),0,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"Primero ejecuta el algoritmo.");
        gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); return;
    }
    char *pdf=NULL, *err=NULL;
    if(!write_latex_and_compile(cs, T, S, &pdf, &err)){
        GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(win),0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"%s",err); gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); g_free(err);
    } else {
        g_message("Reporte: %s", pdf);
        g_free(pdf);
    }
}

/* ---- main ---- */
int main(int argc, char **argv){
    gtk_init(&argc, &argv);
    const char *glade = (argc>1)? argv[1] : "p2/ui/knapsack.glade";
    GError *err=NULL;
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, glade, &err)){
        g_printerr("No se pudo cargar %s: %s\n", glade, err->message); 
        g_error_free(err); 
        return 1;
    }
    GtkCssProvider *css = gtk_css_provider_new();
    GError *css_error = NULL;

    gtk_css_provider_load_from_path(css, "style.css", &css_error);

    if (css_error) {
        g_printerr("Error cargando CSS: %s\n", css_error->message);
        g_clear_error(&css_error);
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );

    win       = GTK_WIDGET(gtk_builder_get_object(builder, "knap_window"));
    btn_run   = GTK_WIDGET(gtk_builder_get_object(builder, "btn_run"));
    btn_save  = GTK_WIDGET(gtk_builder_get_object(builder, "btn_save"));
    btn_load  = GTK_WIDGET(gtk_builder_get_object(builder, "btn_load"));
    btn_export= GTK_WIDGET(gtk_builder_get_object(builder, "btn_export"));
    grid_items= GTK_WIDGET(gtk_builder_get_object(builder, "grid_items"));
    grid_dp   = GTK_WIDGET(gtk_builder_get_object(builder, "grid_dp"));
    sw_items  = GTK_WIDGET(gtk_builder_get_object(builder, "sw_items"));
    sw_dp     = GTK_WIDGET(gtk_builder_get_object(builder, "sw_dp"));
    spin_W    = GTK_WIDGET(gtk_builder_get_object(builder, "spin_W"));
    spin_N    = GTK_WIDGET(gtk_builder_get_object(builder, "spin_N"));
    combo_type= GTK_WIDGET(gtk_builder_get_object(builder, "combo_type"));

    // límites GUI pedidos en la especificación
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(spin_W), 0, 20);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(spin_N), 1, 20);

    // CSS opcional para colores
    GtkCssProvider *css = gtk_css_provider_new();
    GError *error_style = NULL;

    gtk_css_provider_load_from_path(css, "src/style.css", &error_style);
    if (error_style){
        g_printerr("Error en carga de CSS: %s\n", error_style->message);
        g_clear_error(&error_style);
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );

    // ✅ Agregar clases a widgets
    gtk_style_context_add_class(gtk_widget_get_style_context(win), "bg-pending");

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_run), "option");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_save), "pending-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_load), "load");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_export), "option");

    gtk_style_context_add_class(gtk_widget_get_style_context(spin_W), "spinbutton");
    gtk_style_context_add_class(gtk_widget_get_style_context(spin_N), "spinbutton");

    gtk_style_context_add_class(gtk_widget_get_style_context(combo_type), "pending-button");

    // señales
    g_signal_connect(spin_N, "value-changed", G_CALLBACK(on_change_N), NULL);
    g_signal_connect(btn_run, "clicked",  G_CALLBACK(on_click_run), NULL);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_click_save), NULL);
    g_signal_connect(btn_load, "clicked", G_CALLBACK(on_click_load), NULL);
    g_signal_connect(btn_export,"clicked",G_CALLBACK(on_click_export), NULL);

    rebuild_items_rows((int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_N)));
    gtk_widget_show_all(win);
    gtk_main();
    g_object_unref(builder);
    return 0;
}


