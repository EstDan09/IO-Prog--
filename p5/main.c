// main_bigM.c — Ejemplo que usa Gran M (mezcla <= y >=)

#include "simplex_report.h"
#include <stdio.h>

int main(void) {
    /* Problema:
       Max Z = 3x1 + 2x2
       s.a.
          1) x1 +  x2 <= 4
          2) x1 + 2x2 >= 4
          x1, x2 >= 0
    */

    int n = 2;
    int m = 2;

    double c[] = { 3.0, 2.0 };

    /* A en formato por filas (row-major):
       [ 1 1 ]
       [ 1 2 ]
    */
    double A[] = {
        1.0, 1.0,
        1.0, 2.0
    };

    double b[] = { 4.0, 4.0 };

    /* Tipos de restricción:
       fila 0: <=
       fila 1: >=
    */
    ConstraintType ctype[] = {
        CONSTR_LEQ,
        CONSTR_GEQ
    };

    /* Nombres de variables (solo para que el reporte LaTeX se vea bonito) */
    char *varnames[] = { "x_1", "x_2" };

    SimplexProblem p = {0};
    p.n = n;
    p.m = m;
    p.sense = SIMPLEX_MAXIMIZE;
    p.c = c;
    p.A = A;
    p.b = b;
    p.ctype = ctype;
    p.var_names = varnames;
    p.problem_name = "Ejemplo_BigM";

    SimplexResult  r = (SimplexResult){0};
    SimplexTrace   tr;
    simplex_trace_init(&tr);

    if (simplex_solve_with_trace(&p, &r, 1000, &tr) != 0) {
        puts("Error ejecutando Simplex (Gran M).");
        return 1;
    }

    /* Genera el reporte LaTeX en la carpeta basada en problem_name:
       carpeta:  Ejemplo_BigM/
       archivo:  reporte_simplex.tex  (y si pdflatex funciona, reporte_simplex.pdf)
    */
    if (simplex_write_latex_report("dummy_base_name", &p, &r, &tr) == 0) {
        puts("OK: reporte LaTeX generado (carpeta basada en problem_name).");
    } else {
        puts("Error generando el reporte LaTeX.");
    }

    /* Mensaje informativo en consola */
    printf("Estado: %s\n", simplex_status_str(r.status));
    printf("Z* = %.6f, x1 = %.6f, x2 = %.6f\n", r.z, r.x[0], r.x[1]);

    simplex_free_result(&r);
    simplex_trace_free(&tr);
    return 0;
}

