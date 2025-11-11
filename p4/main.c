// main.c — Caso con múltiples soluciones óptimas
#include "simplex_report.h"
#include <stdio.h>

int main(void) {
    int n = 2, m = 3;
    // Max Z = x1 + x2
    double c[] = {1.0, 1.0};

    // Restricciones:
    // 1) x1 + x2 <= 4
    // 2) x1       <= 3
    // 3)      x2  <= 3
    double A[] = {
        1.0, 1.0,
        1.0, 0.0,
        0.0, 1.0
    };
    double b[] = {4.0, 3.0, 3.0};

    SimplexProblem p = { n, m, SIMPLEX_MAXIMIZE, c, A, b };
    SimplexResult  r = {0};
    SimplexTrace   tr; simplex_trace_init(&tr);

    if (simplex_solve_with_trace(&p, &r, 1000, &tr) != 0) {
        puts("Error ejecutando Simplex.");
        return 1;
    }

    // Genera el .tex con tabla inicial, intermedias y final
    if (simplex_write_latex_report("reporte_simplex_mult.tex", &p, &r, &tr) == 0)
        puts("OK: reporte_simplex_mult.tex generado.");

    // (Opcional) Mensaje informativo
    printf("Estado: %s\n", simplex_status_str(r.status));
    printf("Z* = %.6f, x1 = %.6f, x2 = %.6f\n", r.z, r.x[0], r.x[1]);
    printf("Este problema tiene soluciones óptimas múltiples a lo largo de x1 + x2 = 4, 1 <= x1 <= 3.\n");

    simplex_free_result(&r);
    simplex_trace_free(&tr);
    return 0;
}
