// main_bigM_dos.c — Prueba de Gran M con un problema de Max y uno de Min

#include "simplex_report.h"
#include <stdio.h>

/* Pequeño helper para no repetir código */
static void run_problem(const char *tag, SimplexProblem *p) {
    SimplexResult r = {0};
    SimplexTrace  tr;
    simplex_trace_init(&tr);

    if (simplex_solve_with_trace(p, &r, 1000, &tr) != 0) {
        printf("[%s] Error ejecutando Simplex.\n", tag);
        simplex_trace_free(&tr);
        return;
    }

    if (simplex_write_latex_report("dummy_base_name", p, &r, &tr) == 0) {
        printf("[%s] OK: reporte LaTeX generado (carpeta basada en problem_name: \"%s\").\n",
               tag, p->problem_name ? p->problem_name : "Reporte_Simplex");
    } else {
        printf("[%s] Error generando el reporte LaTeX.\n", tag);
    }

    printf("[%s] Estado: %s\n", tag, simplex_status_str(r.status));
    printf("[%s] Z* = %.6f", tag, r.z);
    if (p->n >= 1) printf(", x1 = %.6f", r.x[0]);
    if (p->n >= 2) printf(", x2 = %.6f", r.x[1]);
    printf("\n\n");

    simplex_free_result(&r);
    simplex_trace_free(&tr);
}

int main(void) {
    /* ============================================================
     * PROBLEMA 1 (MAX) — Usa Gran M, mezcla <= y >=
     *
     * Max Z = 3x1 + 2x2
     * s.a.
     *    1) x1 +  x2 <= 4
     *    2) x1 + 2x2 >= 4
     *    x1, x2 >= 0
     *
     * Este es el mismo ejemplo que ya probaste, pero ahora dentro
     * del "main doble".
     * ============================================================ */

    {
        int n1 = 2;
        int m1 = 2;

        double c1[] = { 3.0, 2.0 };

        double A1[] = {
            1.0, 1.0,   // x1 + x2 <= 4
            1.0, 2.0    // x1 + 2x2 >= 4
        };

        double b1[] = { 4.0, 4.0 };

        ConstraintType ctype1[] = {
            CONSTR_LEQ,   // fila 0: <=
            CONSTR_GEQ    // fila 1: >=
        };

        char *varnames1[] = { "x_1", "x_2" };

        SimplexProblem p1 = {0};
        p1.n = n1;
        p1.m = m1;
        p1.sense = SIMPLEX_MAXIMIZE;
        p1.c = c1;
        p1.A = A1;
        p1.b = b1;
        p1.ctype = ctype1;
        p1.var_names = varnames1;
        p1.problem_name = "Ejemplo_BigM_Max";

        run_problem("MAX", &p1);
    }

    /* ============================================================
     * PROBLEMA 2 (MIN) — También usa Gran M (hay restricciones >=)
     *
     * Min Z = x1 + x2
     * s.a.
     *    1) x1 + x2 >= 4
     *    2) x1      >= 1
     *    3)      x2 <= 5
     *    x1, x2 >= 0
     *
     * La región factible es no vacía y el valor óptimo esperado es Z* = 4,
     * por ejemplo en puntos como (1,3) o (4,0). El método devolverá
     * alguno de los vértices óptimos.
     * ============================================================ */

    {
        int n2 = 2;
        int m2 = 3;

        double c2[] = { 1.0, 1.0 };

        double A2[] = {
            1.0, 1.0,   // x1 + x2 >= 4
            1.0, 0.0,   // x1      >= 1
            0.0, 1.0    //      x2 <= 5
        };

        double b2[] = { 4.0, 1.0, 5.0 };

        ConstraintType ctype2[] = {
            CONSTR_GEQ,  // fila 0: >=
            CONSTR_GEQ,  // fila 1: >=
            CONSTR_LEQ   // fila 2: <=
        };

        char *varnames2[] = { "x_1", "x_2" };

        SimplexProblem p2 = {0};
        p2.n = n2;
        p2.m = m2;
        p2.sense = SIMPLEX_MINIMIZE;
        p2.c = c2;
        p2.A = A2;
        p2.b = b2;
        p2.ctype = ctype2;
        p2.var_names = varnames2;
        p2.problem_name = "Ejemplo_BigM_Min";

        run_problem("MIN", &p2);
    }

    return 0;
}

