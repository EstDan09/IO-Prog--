#ifndef SIMPLEX_REPORT_H
#define SIMPLEX_REPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Config general ===== */
#ifndef SIMPLEX_EPS
#define SIMPLEX_EPS 1e-9
#endif

/* ===== Definiciones del problema ===== */
typedef enum {
    SIMPLEX_MAXIMIZE = 1,
    SIMPLEX_MINIMIZE = 2
} ObjectiveSense;

typedef enum {
    SIMPLEX_OK = 0,
    SIMPLEX_UNBOUNDED = 1,
    SIMPLEX_MULTIPLE = 2,      /* hay óptimos alternos */
    SIMPLEX_DEGENERATE = 3,    /* se detectó ratio 0 en alguna iteración */
    SIMPLEX_INFEASIBLE = 4,    /* (no aplica en esta versión con ≤ y b>=0) */
    SIMPLEX_ITER_LIMIT = 5
} SimplexStatus;

typedef enum {
    CONSTR_LEQ,   // <=
    CONSTR_GEQ,   // >=
    CONSTR_EQ     // =
} ConstraintType;

typedef struct {
    int n, m;
    ObjectiveSense sense;
    double *c;
    double *A;
    double *b;
    char **var_names;       // NEW: array of variable names (length n)
    const char *problem_name;
    ConstraintType *ctype;  // tamaño m, una por restricción // NEW: user-defined problem name
} SimplexProblem;


/* ===== Traza (snapshots de tabla por iteración) ===== */
typedef struct {
    int iter;          /* 0 = inicial, luego 1..k para cada pivote (antes del pivote) */
    int rows, cols;    /* m+1, n+m+1 */
    int entering;      /* columna que entra en este paso (o -1 si ninguno) */
    int leaving_row;   /* fila que sale en este paso (1..m, o -1 si ninguno) */
    double *tableau;   /* copia del tableau (rows*cols) en este paso (ANTES del pivote) */

    /* Fracciones b_i / a_ij para la columna de entrada (solo cuando hay pivote) */
    int ratios_m;      /* = m si ratios != NULL */
    double *ratios;    /* tamaño m; NaN si no aplica en esa fila */
} SimplexStep;

typedef struct {
    int count;         /* pasos almacenados */
    int capacity;      /* capacidad del arreglo steps */
    SimplexStep *steps;
} SimplexTrace;

/* ===== Resultado ===== */
typedef struct {
    SimplexStatus status;
    double z;             /* valor objetivo */
    double *x;            /* n (solución final) */
    int iterations;       /* #iteraciones efectivas (pivotes) */
    int *basis;           /* m: índices de variables básicas (0..n+m-1) */

    int encountered_degeneracy; /* 1 si algún ratio mínimo fue 0 */
    int has_alternate;          /* 1 si detectó alternativa */

    /* Copia de la tabla final (para imprimir) */
    int rows, cols;       /* m+1, n+m+1 */
    double *final_tableau;

    /* Óptimo alterno (si existe) */
    int alt_var_index;    /* j de la no-básica con costo reducido ~0 (en variables originales) */
    double *alt_tableau;  /* tabla alterna (tras un pivote extra) */
    double *x_alt;        /* n: solución alterna básica */
    double z_alt;
} SimplexResult;

/* ===== API ===== */

/* Inicializa una traza vacía. Opcional: puedes pasar NULL al solver para no guardar nada. */
void simplex_trace_init(SimplexTrace *tr);

/* Libera toda la memoria de la traza. */
void simplex_trace_free(SimplexTrace *tr);

/* Resuelve el problema con Simplex (≤, b>=0) y guarda snapshots en 'trace':
   - Paso 0: tabla inicial.
   - Un paso por pivote (ANTES del pivote) con columna que entra, fila que sale y fracciones.
   - Paso final (tabla óptima o de parada).
   Devuelve 0 si ejecutó correctamente (mira 'result.status' para el caso). */
int simplex_solve_with_trace(const SimplexProblem *prob,
                             SimplexResult *result,
                             int max_iterations,
                             SimplexTrace *trace);

/* Libera memoria asociada al resultado. */
void simplex_free_result(SimplexResult *r);

/* Genera un .tex con:
   - Portada corta
   - Problema (FO y restricciones)
   - Tabla inicial
   - Todas las tablas intermedias (columna que entra, fila que sale y fracciones)
   - Tabla final y solución (x*, Z*)
   - Si hay múltiples: pivote extra (tabla alterna), recta de óptimos y 3 soluciones adicionales
   - Si no acotado: explicación
   - Degeneración: explicación y mención de desempate (Bland)
   Retorna 0 si pudo escribir el archivo. */
int simplex_write_latex_report(const char *tex_path,
                               const SimplexProblem *prob,
                               const SimplexResult *res,
                               const SimplexTrace *trace);

/* Utilidad: devuelve string amigable del estado (para logs). */
const char* simplex_status_str(SimplexStatus s);

#ifdef __cplusplus
}
#endif

#endif /* SIMPLEX_REPORT_H */
