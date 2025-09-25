#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

// --- Estructuras ---
typedef struct {
    int periodo;
    double reventa;
    double mantenimiento;
} Periodo;

typedef struct {
    double costo_inicial;
    int plazo;
    int vida_util;
    Periodo *periodos;
} ReemplazoData;

// --- Widgets globales ---
static GtkBuilder *builder;
static GtkWidget *entryCosto, *spinPlazo, *spinVida, *tabla;
static GtkWidget *btnGuardar, *btnCargar, *btnEjecutar, *btnSalir;

// --- Guardar datos ---
void guardar_problema(const char *fname, ReemplazoData *p) {
    FILE *f = fopen(fname, "w");
    if (!f) return;
    fprintf(f, "%.2f %d %d\n", p->costo_inicial, p->plazo, p->vida_util);
    for (int i = 0; i < p->plazo; i++) {
        fprintf(f, "%d %.2f %.2f\n", p->periodos[i].periodo,
                p->periodos[i].reventa, p->periodos[i].mantenimiento);
    }
    fclose(f);
}

// --- Cargar datos ---
ReemplazoData *cargar_problema(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) return NULL;
    ReemplazoData *p = malloc(sizeof(ReemplazoData));
    fscanf(f, "%lf %d %d", &p->costo_inicial, &p->plazo, &p->vida_util);
    p->periodos = malloc(sizeof(Periodo) * p->plazo);
    for (int i = 0; i < p->plazo; i++) {
        fscanf(f, "%d %lf %lf", &p->periodos[i].periodo,
               &p->periodos[i].reventa, &p->periodos[i].mantenimiento);
    }
    fclose(f);
    return p;
}

// --- Generar LaTeX (simplificado) ---
void generar_latex(ReemplazoData *p) {
    FILE *f = fopen("reporte.tex", "w");
    fprintf(f, "\\documentclass{article}\n\\begin{document}\n");
    fprintf(f, "\\section*{Problema de Reemplazo de Equipos}\n");
    fprintf(f, "Costo inicial: %.2f, Plazo: %d, Vida útil: %d \\\\\n",
            p->costo_inicial, p->plazo, p->vida_util);
    fprintf(f, "\\begin{tabular}{|c|c|c|}\\hline\n");
    fprintf(f, "Periodo & Reventa & Mantenimiento \\\\\\hline\n");
    for (int i = 0; i < p->plazo; i++) {
        fprintf(f, "%d & %.2f & %.2f \\\\\\hline\n",
                p->periodos[i].periodo, p->periodos[i].reventa, p->periodos[i].mantenimiento);
    }
    fprintf(f, "\\end{tabular}\n\\end{document}\n");
    fclose(f);
    system("pdflatex reporte.tex");
    system("evince reporte.pdf &");
}

// --- Callbacks ---
void on_btnGuardar_clicked(GtkButton *b, gpointer u) {
    ReemplazoData p;
    p.costo_inicial = atof(gtk_entry_get_text(GTK_ENTRY(entryCosto)));
    p.plazo = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinPlazo));
    p.vida_util = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinVida));
    p.periodos = malloc(sizeof(Periodo) * p.plazo);
    for (int i = 0; i < p.plazo; i++) {
        p.periodos[i].periodo = i+1;
        p.periodos[i].reventa = 100 - 10*i;
        p.periodos[i].mantenimiento = 20 + 5*i;
    }
    guardar_problema("problema.rep", &p);
    free(p.periodos);
}

void on_btnCargar_clicked(GtkButton *b, gpointer u) {
    ReemplazoData *p = cargar_problema("problema.rep");
    if (p) {
        printf("Cargado: Costo=%.2f, Plazo=%d\n", p->costo_inicial, p->plazo);
        free(p->periodos);
        free(p);
    }
}

void on_btnEjecutar_clicked(GtkButton *b, gpointer u) {
    ReemplazoData *p = cargar_problema("problema.rep");
    if (p) {
        generar_latex(p);
        free(p->periodos);
        free(p);
    }
}

void on_btnSalir_clicked(GtkButton *b, gpointer u) {
    gtk_main_quit();
}

// --- Main ---
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    builder = gtk_builder_new_from_file("p3/ui/reemplazo.glade");
    GtkWidget *win = GTK_WIDGET(gtk_builder_get_object(builder, "ventanaPrincipal"));

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
