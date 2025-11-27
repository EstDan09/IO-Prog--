#include "simplex_report.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum {
    COL_X,
    COL_SLACK,
    COL_SURPLUS,
    COL_ARTIFICIAL
} ColumnKind;

/* Versión original: solo restricciones <=, b >= 0 */
static int simplex_solve_leq_only(const SimplexProblem *prob,
                                  SimplexResult *result,
                                  int max_iterations,
                                  SimplexTrace *trace);

/* Versión con Gran M (>=, =, etc.) */
static int simplex_solve_bigM(const SimplexProblem *prob,
                              SimplexResult *result,
                              int max_iterations,
                              SimplexTrace *trace);

/* ===== Utilidades internas ===== */

static inline double *ROW(double *M, int cols, int i) { return M + (size_t)i * (size_t)cols; }
static inline const double *ROWc(const double *M, int cols, int i) { return M + (size_t)i * (size_t)cols; }

static double dot(const double *a, const double *b, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += a[i] * b[i];
    return s;
}

static int almost_zero(double x) { return fabs(x) <= SIMPLEX_EPS; }
static double clip_negzero(double x) { return (fabs(x) < 1e-12) ? 0.0 : x; }

/* Pequeño replacement de strdup para evitar warnings de POSIX */
static char *simplex_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p)
        memcpy(p, s, len);
    return p;
}

/* ====== Manejo seguro de nombres LaTeX ====== */
static char *latex_safe_varname(const char *raw, int index)
{
    if (!raw || !*raw)
    {
        char *buf = (char *)malloc(16);
        snprintf(buf, 16, "$x_{%d}$", index + 1);
        return buf;
    }
    if (strchr(raw, '$'))
        return simplex_strdup(raw);

    const char *special = "_%&#\\";
    size_t len = strlen(raw);
    char *buf = (char *)malloc(len * 2 + 10);
    char *dst = buf;
    *dst++ = '$';
    for (const char *p = raw; *p; ++p)
    {
        if (strchr(special, *p))
            *dst++ = '\\';
        *dst++ = *p;
    }
    *dst++ = '$';
    *dst = '\0';
    return buf;
}

// Versión sin $...$ para entornos matemáticos \[ \]
static char *latex_safe_varname_nomath(const char *raw, int index)
{
    if (!raw || !*raw)
    {
        char *buf = (char *)malloc(16);
        snprintf(buf, 16, "x_%d", index + 1);
        return buf;
    }

    const char *special = "_%&#\\";
    size_t len = strlen(raw);
    char *buf = (char *)malloc(len * 2 + 2);
    char *dst = buf;
    for (const char *p = raw; *p; ++p)
    {
        if (strchr(special, *p))
            *dst++ = '\\';
        *dst++ = *p;
    }
    *dst = '\0';
    return buf;
}

/* ===== Traza ===== */

void simplex_trace_init(SimplexTrace *tr)
{
    if (!tr)
        return;
    tr->count = 0;
    tr->capacity = 0;
    tr->steps = NULL;
}

static int trace_append_ext(SimplexTrace *tr, int iter, int rows, int cols,
                            int entering, int leaving_row, const double *T,
                            const double *ratios, int ratios_m)
{
    if (!tr)
        return 0; /* traza opcional */
    if (tr->count == tr->capacity)
    {
        int newcap = (tr->capacity == 0) ? 8 : tr->capacity * 2;
        SimplexStep *ns = (SimplexStep *)realloc(tr->steps, (size_t)newcap * sizeof(SimplexStep));
        if (!ns)
            return -1;
        tr->steps = ns;
        tr->capacity = newcap;
    }
    SimplexStep *st = &tr->steps[tr->count++];
    st->iter = iter;
    st->rows = rows;
    st->cols = cols;
    st->entering = entering;
    st->leaving_row = leaving_row;
    st->tableau = (double *)malloc((size_t)rows * (size_t)cols * sizeof(double));
    if (!st->tableau)
        return -1;
    memcpy(st->tableau, T, (size_t)rows * (size_t)cols * sizeof(double));
    if (ratios && ratios_m > 0)
    {
        st->ratios = (double *)malloc((size_t)ratios_m * sizeof(double));
        if (!st->ratios)
            return -1;
        memcpy(st->ratios, ratios, (size_t)ratios_m * sizeof(double));
        st->ratios_m = ratios_m;
    }
    else
    {
        st->ratios = NULL;
        st->ratios_m = 0;
    }
    return 0;
}

static int trace_append(SimplexTrace *tr, int iter, int rows, int cols,
                        int entering, int leaving_row, const double *T)
{
    return trace_append_ext(tr, iter, rows, cols, entering, leaving_row, T, NULL, 0);
}

void simplex_trace_free(SimplexTrace *tr)
{
    if (!tr)
        return;
    for (int i = 0; i < tr->count; ++i)
    {
        free(tr->steps[i].tableau);
        free(tr->steps[i].ratios);
    }
    free(tr->steps);
    tr->steps = NULL;
    tr->count = tr->capacity = 0;
}

/* ===== Pivot y selección ===== */

static int argmin_ratio_with_bland(const double *col, const double *b,
                                   int m, const int *basis,
                                   int entering_col, int *degenerate_flag)
{
    double best = INFINITY;
    int best_i = -1;
    for (int i = 1; i <= m; ++i)
    {
        double aij = col[i];
        if (aij > SIMPLEX_EPS)
        {
            double ratio = b[i] / aij;
            if (ratio + SIMPLEX_EPS < best)
            {
                best = ratio;
                best_i = i;
            }
            else if (fabs(ratio - best) <= SIMPLEX_EPS)
            {
                if (basis && basis[i - 1] > -1)
                {
                    if (best_i == -1 || basis[i - 1] < basis[best_i - 1])
                        best_i = i;
                }
            }
        }
    }
    if (degenerate_flag && best_i != -1 && fabs(b[best_i]) <= SIMPLEX_EPS)
    {
        *degenerate_flag = 1;
    }
    return best_i;
}

static void pivot(double *T, int rows, int cols, int pr, int pc)
{
    double *r = ROW(T, cols, pr);
    double piv = r[pc];
    for (int j = 0; j < cols; ++j)
        r[j] /= piv;
    for (int i = 0; i < rows; ++i)
    {
        if (i == pr)
            continue;
        double *ri = ROW(T, cols, i);
        double factor = ri[pc];
        if (fabs(factor) > SIMPLEX_EPS)
        {
            for (int j = 0; j < cols; ++j)
            {
                ri[j] -= factor * r[j];
            }
        }
    }
}

static void extract_solution(const double *T, int m, int n, int cols,
                             const int *basis, double *x)
{
    for (int j = 0; j < n; ++j)
        x[j] = 0.0;
    for (int i = 1; i <= m; ++i)
    {
        int var = basis[i - 1];
        if (var >= 0 && var < n)
        {
            const double *ri = ROWc(T, cols, i);
            x[var] = ri[cols - 1];
        }
    }
}

/* ===== Solver con traza (solo <=) ===== */

static int simplex_solve_leq_only(const SimplexProblem *prob,
                                  SimplexResult *result,
                                  int max_iterations,
                                  SimplexTrace *trace)
{
    if (!prob || !result || prob->n <= 0 || prob->m <= 0)
        return -1;

    const int n = prob->n;
    const int m = prob->m;
    const int total_vars = n + m;    /* x + slacks */
    const int rows = m + 1;          /* 0..m (0=objetivo) */
    const int cols = total_vars + 1; /* vars + RHS */

    double *T = (double *)calloc((size_t)rows * (size_t)cols, sizeof(double));
    if (!T)
        return -1;

    /* Fila 0: costos reducidos de partida */
    for (int j = 0; j < n; ++j)
    {
        ROW(T, cols, 0)[j] = (prob->sense == SIMPLEX_MAXIMIZE) ? -prob->c[j] : prob->c[j];
    }
    for (int j = n; j < total_vars; ++j)
        ROW(T, cols, 0)[j] = 0.0;
    ROW(T, cols, 0)[cols - 1] = 0.0;

    /* Filas 1..m: A | I | b */
    for (int i = 0; i < m; ++i)
    {
        double *ri = ROW(T, cols, i + 1);
        for (int j = 0; j < n; ++j)
            ri[j] = prob->A[i * (size_t)n + j];
        for (int j = 0; j < m; ++j)
            ri[n + j] = (i == j) ? 1.0 : 0.0;
        ri[cols - 1] = prob->b[i];
        if (ri[cols - 1] < -SIMPLEX_EPS)
        {
            free(T);
            return -1;
        } /* fuera de alcance en esta versión */
    }

    /* Inicializa estructuras de resultado */
    result->basis = (int *)malloc((size_t)m * sizeof(int));
    if (!result->basis)
    {
        free(T);
        return -1;
    }
    for (int i = 0; i < m; ++i)
        result->basis[i] = n + i; /* slacks en la base */
    result->x = (double *)malloc((size_t)n * sizeof(double));
    if (!result->x)
    {
        free(T);
        free(result->basis);
        return -1;
    }
    result->final_tableau = NULL;
    result->rows = rows;
    result->cols = cols;
    result->iterations = 0;
    result->encountered_degeneracy = 0;
    result->has_alternate = 0;
    result->alt_var_index = -1;
    result->alt_tableau = NULL;
    result->x_alt = (double *)calloc((size_t)n, sizeof(double));
    result->z_alt = 0.0;

    /* Paso 0: tabla inicial */
    if (trace && trace_append(trace, 0, rows, cols, -1, -1, T) != 0)
    {
        free(T);
        return -1;
    }

    int iter = 0;
    for (; iter < max_iterations; ++iter)
    {
        result->iterations = iter;

        /* Selección de variable de entrada */
        int entering = -1;
        const double *row0 = ROWc(T, cols, 0);

        if (prob->sense == SIMPLEX_MAXIMIZE)
        {
            double minv = -SIMPLEX_EPS;
            for (int j = 0; j < total_vars; ++j)
                if (row0[j] < minv)
                {
                    minv = row0[j];
                    entering = j;
                }
        }
        else
        {
            double maxv = SIMPLEX_EPS;
            for (int j = 0; j < total_vars; ++j)
                if (row0[j] > maxv)
                {
                    maxv = row0[j];
                    entering = j;
                }
        }

        if (entering == -1)
            break; /* óptimo alcanzado */

        /* Test de no acotado */
        int has_pos = 0;
        for (int i = 1; i <= m; ++i)
            if (ROW(T, cols, i)[entering] > SIMPLEX_EPS)
            {
                has_pos = 1;
                break;
            }
        if (!has_pos)
        {
            result->status = SIMPLEX_UNBOUNDED;
            extract_solution(T, m, n, cols, result->basis, result->x);
            result->z = dot(prob->c, result->x, n);
            /* Snapshot final (sin pivotear) */
            if (trace)
                trace_append(trace, iter + 1, rows, cols, entering, -1, T);
            free(T);
            return 0;
        }

        /* Selección de salida y fracciones */
        double *col = (double *)malloc((size_t)(m + 1) * sizeof(double));
        double *bvec = (double *)malloc((size_t)(m + 1) * sizeof(double));
        double *ratios = (double *)malloc((size_t)m * sizeof(double));
        if (!col || !bvec || !ratios)
        {
            free(col);
            free(bvec);
            free(ratios);
            free(T);
            return -1;
        }
        for (int i = 0; i <= m; ++i)
        {
            col[i] = ROW(T, cols, i)[entering];
            bvec[i] = ROW(T, cols, i)[cols - 1];
        }
        for (int i = 1; i <= m; ++i)
        {
            if (col[i] > SIMPLEX_EPS)
                ratios[i - 1] = bvec[i] / col[i];
            else
                ratios[i - 1] = NAN;
        }
        int deg_flag = 0;
        int leaving_row = argmin_ratio_with_bland(col, bvec, m, result->basis, entering, &deg_flag);
        free(col);
        free(bvec);
        if (deg_flag)
            result->encountered_degeneracy = 1;

        /* Snapshot ANTES del pivote con fracciones */
        if (trace && trace_append_ext(trace, iter + 1, rows, cols, entering, leaving_row, T, ratios, m) != 0)
        {
            free(ratios);
            free(T);
            return -1;
        }
        free(ratios);

        /* Pivotear */
        pivot(T, rows, cols, leaving_row, entering);
        result->basis[leaving_row - 1] = entering;
    }

    if (iter >= max_iterations)
    {
        result->status = SIMPLEX_ITER_LIMIT;
    }
    else
    {
        result->status = SIMPLEX_OK;
    }

    /* Extraer solución y valor Z */
    extract_solution(T, m, n, cols, result->basis, result->x);
    result->z = dot(prob->c, result->x, n);

    /* Copiar tableau final y agregarlo a la traza como paso final */
    result->final_tableau = (double *)malloc((size_t)rows * (size_t)cols * sizeof(double));
    if (result->final_tableau)
        memcpy(result->final_tableau, T, (size_t)rows * (size_t)cols * sizeof(double));
    if (trace)
        trace_append(trace, iter + (result->status == SIMPLEX_OK ? 1 : 0), rows, cols, -1, -1, T);

    /* === Detección de múltiples soluciones === */
    if (result->status == SIMPLEX_OK)
    {
        int *is_basic = (int *)calloc((size_t)(n + m), sizeof(int));
        if (is_basic)
        {
            for (int i = 0; i < m; ++i)
            {
                int v = result->basis[i];
                if (v >= 0 && v < (n + m))
                    is_basic[v] = 1;
            }
            const double *row0f = ROWc(T, cols, 0);

            int alt_col = -1;
            for (int j = 0; j < n + m; ++j)
            {
                if (!is_basic[j] && fabs(row0f[j]) <= SIMPLEX_EPS)
                {
                    alt_col = j;
                    if (j < n)
                        break; /* si es original, paramos aquí */
                }
            }

            if (alt_col != -1)
            {
                double *Tc = (double *)malloc((size_t)rows * (size_t)cols * sizeof(double));
                int *basis_c = (int *)malloc((size_t)m * sizeof(int));
                if (Tc && basis_c)
                {
                    memcpy(Tc, T, (size_t)rows * (size_t)cols * sizeof(double));
                    memcpy(basis_c, result->basis, (size_t)m * sizeof(int));

                    int leaving2 = -1;
                    double best = INFINITY;
                    for (int i = 1; i <= m; ++i)
                    {
                        double aij = ROW(Tc, cols, i)[alt_col];
                        double bi = ROW(Tc, cols, i)[cols - 1];
                        if (aij > SIMPLEX_EPS)
                        {
                            double ratio = bi / aij;
                            if (ratio + SIMPLEX_EPS < best)
                            {
                                best = ratio;
                                leaving2 = i;
                            }
                        }
                    }
                    if (leaving2 != -1)
                    {
                        pivot(Tc, rows, cols, leaving2, alt_col);
                        basis_c[leaving2 - 1] = alt_col;

                        result->alt_tableau = (double *)malloc((size_t)rows * (size_t)cols * sizeof(double));
                        if (result->alt_tableau)
                            memcpy(result->alt_tableau, Tc, (size_t)rows * (size_t)cols * sizeof(double));
                        extract_solution(Tc, m, n, cols, basis_c, result->x_alt);
                        result->z_alt = dot(prob->c, result->x_alt, n);

                        result->has_alternate = 1;
                        result->alt_var_index = alt_col;
                        result->status = SIMPLEX_MULTIPLE;
                    }
                }
                free(Tc);
                free(basis_c);
            }
            free(is_basic);
        }
    }

    free(T);
    return 0;
}

/* ===== Wrapper principal (elige <= o Big M y maneja MIN→MAX en BigM) ===== */

int simplex_solve_with_trace(const SimplexProblem *prob,
                             SimplexResult *result,
                             int max_iterations,
                             SimplexTrace *trace)
{
    if (!prob || !result || prob->n <= 0 || prob->m <= 0)
        return -1;

    /* ¿Hay alguna restricción que NO sea <= ? */
    int need_bigM = 0;

    if (prob->ctype != NULL) {
        for (int i = 0; i < prob->m; ++i) {
            if (prob->ctype[i] == CONSTR_GEQ ||
                prob->ctype[i] == CONSTR_EQ) {
                need_bigM = 1;
                break;
            }
        }
    }

    if (!need_bigM) {
        /* Caso Proyecto 4: todas son <= (o no se definió ctype) */
        return simplex_solve_leq_only(prob, result, max_iterations, trace);
    } else {
        /* Caso Proyecto 5: hay >= o = → usar Gran M */

        if (prob->sense == SIMPLEX_MINIMIZE) {
            /* ==== Truco estándar: MIN con BigM → MAX de -Z ==== */
            SimplexProblem tmp = *prob;
            double *c_neg = (double *)malloc((size_t)prob->n * sizeof(double));
            if (!c_neg) return -1;
            for (int j = 0; j < prob->n; ++j)
                c_neg[j] = -prob->c[j];

            tmp.c = c_neg;
            tmp.sense = SIMPLEX_MAXIMIZE;

            int rc = simplex_solve_bigM(&tmp, result, max_iterations, trace);

            free(c_neg);

            if (rc == 0) {
                /* Revertimos el signo del óptimo para volver a MIN */
                result->z = -result->z;
                if (result->has_alternate) {
                    result->z_alt = -result->z_alt;
                }
            }
            return rc;
        } else {
            /* MAX con BigM: se resuelve directamente */
            return simplex_solve_bigM(prob, result, max_iterations, trace);
        }
    }
}

/* ===== Solver con Gran M (asume MAX internamente tras el wrapper para MIN) ===== */

static int simplex_solve_bigM(const SimplexProblem *prob,
                              SimplexResult *result,
                              int max_iterations,
                              SimplexTrace *trace)
{
    if (!prob || !result || prob->n <= 0 || prob->m <= 0)
        return -1;

    const int n = prob->n;    /* variables originales x */
    const int m = prob->m;    /* restricciones */

    /* ==== 1) Contar holguras, excedentes y artificiales ==== */
    int n_slack = 0, n_surplus = 0, n_artificial = 0;
    for (int i = 0; i < m; ++i) {
        ConstraintType t = prob->ctype ? prob->ctype[i] : CONSTR_LEQ;
        switch (t) {
        case CONSTR_LEQ:
            n_slack++;
            break;
        case CONSTR_GEQ:
            n_surplus++;
            n_artificial++;
            break;
        case CONSTR_EQ:
            n_artificial++;
            break;
        }
    }

    const int total_vars = n + n_slack + n_surplus + n_artificial;
    const int rows = m + 1;
    const int cols = total_vars + 1; /* variables + RHS */

    /* ==== 2) Offsets por tipo de variable ==== */
    const int slack_offset      = n;
    const int surplus_offset    = slack_offset   + n_slack;
    const int artificial_offset = surplus_offset + n_surplus;

    /* ==== 3) Tabla, tipos de columna y arrays auxiliares ==== */
    double *T = (double *)calloc((size_t)rows * (size_t)cols, sizeof(double));
    if (!T) return -1;

    ColumnKind *col_kind      = (ColumnKind *)malloc((size_t)total_vars * sizeof(ColumnKind));
    int        *col_can_enter = (int        *)malloc((size_t)total_vars * sizeof(int));
    if (!col_kind || !col_can_enter) {
        free(T);
        free(col_kind);
        free(col_can_enter);
        return -1;
    }

    for (int j = 0; j < total_vars; ++j) {
        col_can_enter[j] = 1; /* todas pueden entrar al inicio */
        col_kind[j]      = COL_X;
    }

    /* ==== 4) Mapear por fila qué columna es slack/surplus/artificial ==== */
    int *idx_slack      = (int *)malloc((size_t)m * sizeof(int));
    int *idx_surplus    = (int *)malloc((size_t)m * sizeof(int));
    int *idx_artificial = (int *)malloc((size_t)m * sizeof(int));
    if (!idx_slack || !idx_surplus || !idx_artificial) {
        free(T); free(col_kind); free(col_can_enter);
        free(idx_slack); free(idx_surplus); free(idx_artificial);
        return -1;
    }
    for (int i = 0; i < m; ++i) {
        idx_slack[i] = idx_surplus[i] = idx_artificial[i] = -1;
    }

    int next_slack = 0, next_surplus = 0, next_art = 0;

    for (int i = 0; i < m; ++i) {
        ConstraintType t = prob->ctype ? prob->ctype[i] : CONSTR_LEQ;
        switch (t) {
        case CONSTR_LEQ: {
            int col = slack_offset + next_slack++;
            idx_slack[i] = col;
            col_kind[col] = COL_SLACK;
            break;
        }
        case CONSTR_GEQ: {
            int col_e = surplus_offset   + next_surplus++;
            int col_a = artificial_offset + next_art++;
            idx_surplus[i]    = col_e;
            idx_artificial[i] = col_a;
            col_kind[col_e] = COL_SURPLUS;
            col_kind[col_a] = COL_ARTIFICIAL;
            break;
        }
        case CONSTR_EQ: {
            int col_a = artificial_offset + next_art++;
            idx_artificial[i] = col_a;
            col_kind[col_a] = COL_ARTIFICIAL;
            break;
        }
        }
    }

    /* ==== 5) Inicializar estructuras de resultado ==== */
    result->basis = (int *)malloc((size_t)m * sizeof(int));
    if (!result->basis) {
        free(T); free(col_kind); free(col_can_enter);
        free(idx_slack); free(idx_surplus); free(idx_artificial);
        return -1;
    }
    result->x = (double *)malloc((size_t)n * sizeof(double));
    if (!result->x) {
        free(T); free(col_kind); free(col_can_enter);
        free(idx_slack); free(idx_surplus); free(idx_artificial);
        free(result->basis);
        return -1;
    }
    result->final_tableau = NULL;
    result->rows = rows;
    result->cols = cols;
    result->iterations = 0;
    result->encountered_degeneracy = 0;
    result->has_alternate = 0;
    result->alt_var_index = -1;
    result->alt_tableau = NULL;
    result->x_alt = (double *)calloc((size_t)n, sizeof(double));
    result->z_alt = 0.0;

    /* Base inicial:
       - LEQ  -> slack en la base
       - GEQ  -> artificial en la base
       - EQ   -> artificial en la base
    */
    for (int i = 0; i < m; ++i) {
        ConstraintType t = prob->ctype ? prob->ctype[i] : CONSTR_LEQ;
        if (t == CONSTR_LEQ) {
            result->basis[i] = idx_slack[i];
        } else {
            result->basis[i] = idx_artificial[i];
        }
    }

    /* ==== 6) Rellenar filas de restricciones ==== */
    for (int i = 0; i < m; ++i) {
        double *ri = ROW(T, cols, i + 1);

        /* Coeficientes originales */
        for (int j = 0; j < n; ++j)
            ri[j] = prob->A[i * (size_t)n + j];

        /* Slack si hay */
        if (idx_slack[i] >= 0)
            ri[idx_slack[i]] = 1.0;

        /* Surplus si hay */
        if (idx_surplus[i] >= 0)
            ri[idx_surplus[i]] = -1.0;

        /* Artificial si hay */
        if (idx_artificial[i] >= 0)
            ri[idx_artificial[i]] = 1.0;

        /* RHS */
        ri[cols - 1] = prob->b[i];
        if (ri[cols - 1] < -SIMPLEX_EPS) {
            /* asumimos b_i >= 0 según el enunciado; si no, error */
            free(T); free(col_kind); free(col_can_enter);
            free(idx_slack); free(idx_surplus); free(idx_artificial);
            free(result->basis); free(result->x); free(result->x_alt);
            return -1;
        }
    }

    /* ==== 7) Fila 0: función objetivo con Gran M ==== */
    const double BIG_M = 1e6;
    double *row0 = ROW(T, cols, 0);

    /* X originales: misma convención que en <= */
    for (int j = 0; j < n; ++j) {
        row0[j] = (prob->sense == SIMPLEX_MAXIMIZE) ? -prob->c[j] : prob->c[j];
    }

    /* Slacks y surplus con costo 0 */
    for (int j = n; j < total_vars; ++j)
        row0[j] = 0.0;

    /* Artificiales: ±M (ojo: para MAX, va +M) */
    for (int j = 0; j < total_vars; ++j) {
        if (col_kind[j] == COL_ARTIFICIAL) {
            row0[j] = (prob->sense == SIMPLEX_MAXIMIZE)
                        ?  BIG_M    /* penalización fuerte para MAX */
                        : -BIG_M;   /* este branch ya casi no se usa (wrapper convierte MIN→MAX) */
        }
    }

    /* ==== 8) Paso 0: canonización de columnas artificiales ==== */
    for (int i = 1; i <= m; ++i) {
        int bv = result->basis[i - 1];
        if (bv >= 0 && col_kind[bv] == COL_ARTIFICIAL) {
            double coef = ROWc(T, cols, 0)[bv];
            if (fabs(coef) > SIMPLEX_EPS) {
                double *ri = ROW(T, cols, i);
                for (int j = 0; j < cols; ++j) {
                    row0[j] -= coef * ri[j];
                }
            }
        }
    }

    /* Guardar tabla inicial ya canonizada */
    if (trace && trace_append(trace, 0, rows, cols, -1, -1, T) != 0) {
        free(T); free(col_kind); free(col_can_enter);
        free(idx_slack); free(idx_surplus); free(idx_artificial);
        free(result->basis); free(result->x); free(result->x_alt);
        return -1;
    }

    /* ==== 9) Bucle principal del método Símplex con Gran M ==== */

    int iter = 0;
    for (; iter < max_iterations; ++iter) {
        result->iterations = iter;

        /* --- Selección de variable que entra --- */
        int entering = -1;
        const double *row0f = ROWc(T, cols, 0);

        if (prob->sense == SIMPLEX_MAXIMIZE) {
            double minv = -SIMPLEX_EPS;
            for (int j = 0; j < total_vars; ++j) {
                if (!col_can_enter[j]) continue;
                if (row0f[j] < minv) {
                    minv = row0f[j];
                    entering = j;
                }
            }
        } else {
            double maxv = SIMPLEX_EPS;
            for (int j = 0; j < total_vars; ++j) {
                if (!col_can_enter[j]) continue;
                if (row0f[j] > maxv) {
                    maxv = row0f[j];
                    entering = j;
                }
            }
        }

        /* Si no hay columna que mejore -> óptimo respecto a Big-M */
        if (entering == -1)
            break;

        /* --- Test de no acotado --- */
        int has_pos = 0;
        for (int i = 1; i <= m; ++i) {
            if (ROW(T, cols, i)[entering] > SIMPLEX_EPS) {
                has_pos = 1;
                break;
            }
        }
        if (!has_pos) {
            result->status = SIMPLEX_UNBOUNDED;
            extract_solution(T, m, n, cols, result->basis, result->x);
            result->z = dot(prob->c, result->x, n);
            if (trace)
                trace_append(trace, iter + 1, rows, cols, entering, -1, T);

            free(T);
            free(col_kind);
            free(col_can_enter);
            free(idx_slack);
            free(idx_surplus);
            free(idx_artificial);
            return 0;
        }

        /* --- Selección de fila que sale (Bland + ratios) --- */
        double *col = (double *)malloc((size_t)(m + 1) * sizeof(double));
        double *bvec = (double *)malloc((size_t)(m + 1) * sizeof(double));
        double *ratios = (double *)malloc((size_t)m * sizeof(double));
        if (!col || !bvec || !ratios) {
            free(col);
            free(bvec);
            free(ratios);
            free(T);
            free(col_kind);
            free(col_can_enter);
            free(idx_slack);
            free(idx_surplus);
            free(idx_artificial);
            return -1;
        }

        for (int i = 0; i <= m; ++i) {
            col[i]  = ROW(T, cols, i)[entering];
            bvec[i] = ROW(T, cols, i)[cols - 1];
        }
        for (int i = 1; i <= m; ++i) {
            if (col[i] > SIMPLEX_EPS)
                ratios[i - 1] = bvec[i] / col[i];
            else
                ratios[i - 1] = NAN;
        }

        int deg_flag = 0;
        int leaving_row = argmin_ratio_with_bland(col, bvec, m,
                                                  result->basis,
                                                  entering,
                                                  &deg_flag);
        free(col);
        free(bvec);
        if (deg_flag)
            result->encountered_degeneracy = 1;

        /* Snapshot ANTES del pivote con fracciones */
        if (trace && trace_append_ext(trace, iter + 1, rows, cols,
                                      entering, leaving_row, T,
                                      ratios, m) != 0) {
            free(ratios);
            free(T);
            free(col_kind);
            free(col_can_enter);
            free(idx_slack);
            free(idx_surplus);
            free(idx_artificial);
            return -1;
        }
        free(ratios);

        /* Si sale una artificial, la marcamos como "no volver a entrar" */
        int leaving_var = result->basis[leaving_row - 1];
        if (leaving_var >= 0 && col_kind[leaving_var] == COL_ARTIFICIAL) {
            col_can_enter[leaving_var] = 0;
        }

        /* --- Pivotear --- */
        pivot(T, rows, cols, leaving_row, entering);
        result->basis[leaving_row - 1] = entering;
    }

    /* Estado según iteraciones */
    if (iter >= max_iterations) {
        result->status = SIMPLEX_ITER_LIMIT;
    } else {
        result->status = SIMPLEX_OK;
    }

    /* ==== 10) Verificar factibilidad (artificiales en la base) ==== */
    int infeasible = 0;
    for (int i = 0; i < m; ++i) {
        int bv = result->basis[i];
        if (bv >= 0 && col_kind[bv] == COL_ARTIFICIAL) {
            double bi = ROWc(T, cols, i + 1)[cols - 1];
            if (bi > SIMPLEX_EPS) {
                infeasible = 1;
                break;
            }
        }
    }

    if (infeasible) {
        result->status = SIMPLEX_INFEASIBLE;
        /* No hay solución factible: dejamos x = 0, z = 0 */
        for (int j = 0; j < n; ++j)
            result->x[j] = 0.0;
        result->z = 0.0;
    } else {
        /* Extraer solución y valor Z "real" (sin M) */
        extract_solution(T, m, n, cols, result->basis, result->x);
        result->z = dot(prob->c, result->x, n);
    }

    /* Copiar tableau final y agregarlo a la traza como paso final */
    result->final_tableau = (double *)malloc((size_t)rows * (size_t)cols * sizeof(double));
    if (result->final_tableau)
        memcpy(result->final_tableau, T, (size_t)rows * (size_t)cols * sizeof(double));

    if (trace)
        trace_append(trace,
                     iter + ((result->status == SIMPLEX_OK ||
                              result->status == SIMPLEX_INFEASIBLE) ? 1 : 0),
                     rows, cols, -1, -1, T);

    /* Limpieza de memoria interna del solver Big-M (no del resultado) */
    free(T);
    free(col_kind);
    free(col_can_enter);
    free(idx_slack);
    free(idx_surplus);
    free(idx_artificial);

    return 0;
}

/* ===== Liberar resultado ===== */

void simplex_free_result(SimplexResult *r)
{
    if (!r)
        return;
    free(r->basis);
    r->basis = NULL;
    free(r->x);
    r->x = NULL;
    free(r->final_tableau);
    r->final_tableau = NULL;
    free(r->alt_tableau);
    r->alt_tableau = NULL;
    free(r->x_alt);
    r->x_alt = NULL;
}

/* ===== Texto estado ===== */

const char *simplex_status_str(SimplexStatus s)
{
    switch (s)
    {
    case SIMPLEX_OK:
        return "Óptimo";
    case SIMPLEX_UNBOUNDED:
        return "No acotado";
    case SIMPLEX_MULTIPLE:
        return "Óptimo (múltiples soluciones)";
    case SIMPLEX_DEGENERATE:
        return "Degenerado";
    case SIMPLEX_INFEASIBLE:
        return "Infactible";
    case SIMPLEX_ITER_LIMIT:
        return "Límite de iteraciones";
    default:
        return "Desconocido";
    }
}

/* ===== Generador LaTeX ===== */

static void tex_print_number(FILE *f, double v)
{
    v = clip_negzero(v);
    fprintf(f, "%.6f", v);
}

static void tex_varname(FILE *f, int j, int n)
{
    /* j: índice de columna en el tableau (0..cols-2).
       n: número de variables originales del problema.

       - Para j < n: mostramos x_j (variables originales).
       - Para j >= n: mostramos y_k (variables "extras": holguras, excedentes,
         artificiales, etc.). No distinguimos el tipo, pero sí quedan etiquetadas.
    */
    if (j < n)
        fprintf(f, "$x_{%d}$", j + 1);
    else
        fprintf(f, "$y_{%d}$", j - n + 1);
}


static void tex_write_table_step(FILE *f, const SimplexStep *st, int n, int m)
{
    (void)m; /* m ya no se usa directamente para columnas; lo dejamos por firma */
    const int rows = st->rows;
    const int cols = st->cols;   /* columnas reales del tableau (incluye b) */
    const double *T = st->tableau;

    fprintf(f, "\\begin{table}[h]\n\\centering\n");
    if (st->entering >= 0 && st->leaving_row >= 0) {
        fprintf(f, "\\caption{Iteraci\\'on %d: entra la columna ", st->iter);
        tex_varname(f, st->entering, n);
        fprintf(f, " y sale la fila $R_{%d}$.}\n", st->leaving_row);
    } else if (st->iter == 0) {
        fprintf(f, "\\caption{Tabla inicial.}\n");
    } else {
        fprintf(f, "\\caption{Tabla final.}\n");
    }

    fprintf(f, "\\setlength{\\tabcolsep}{6pt}\n");
    fprintf(f, "\\renewcommand{\\arraystretch}{1.15}\n");

    /* Estructura de columnas:
       - 1 columna para la etiqueta ("Base"/"Z"/"R_i")
       - cols columnas para los datos del tableau (incluida b)
     */
    fprintf(f, "\\begin{tabular}{l");
    for (int j = 0; j < cols; ++j)
        fprintf(f, "r");
    fprintf(f, "}\n\\toprule\n");

    /* Encabezados: Base | var_1 ... var_{cols-1} | b */
    fprintf(f, "Base");
    for (int j = 0; j < cols; ++j) {
        fprintf(f, " & ");
        if (j < cols - 1)
            tex_varname(f, j, n);
        else
            fprintf(f, "$b$");
    }
    fprintf(f, " \\\\\n\\midrule\n");

    /* Fila 0: Z */
    fprintf(f, "$Z$ & ");
    for (int j = 0; j < cols; ++j) {
        if (j == cols - 1) { /* b */
            tex_print_number(f, ROWc(T, cols, 0)[j]);
            fprintf(f, " \\\\\n");
        } else {
            int color_col = (st->entering == j) ? 1 : 0;
            if (color_col)
                fprintf(f, "\\cellcolor{blue!12}");
            tex_print_number(f, ROWc(T, cols, 0)[j]);
            fprintf(f, " & ");
        }
    }

    /* Filas 1..(rows-1): restricciones */
    for (int i = 1; i < rows; ++i) {
        fprintf(f, "$R_{%d}$ & ", i);
        for (int j = 0; j < cols; ++j) {
            int color_col = (st->entering == j);
            int color_row = (st->leaving_row == i);
            int is_pivot  = color_col && color_row;

            if (j == cols - 1) { /* b */
                if (color_row)
                    fprintf(f, "\\cellcolor{green!12}");
                tex_print_number(f, ROWc(T, cols, i)[j]);
                fprintf(f, " \\\\\n");
            } else {
                if (is_pivot)
                    fprintf(f, "\\cellcolor{orange!35}");
                else if (color_col)
                    fprintf(f, "\\cellcolor{blue!12}");
                else if (color_row)
                    fprintf(f, "\\cellcolor{green!12}");
                tex_print_number(f, ROWc(T, cols, i)[j]);
                fprintf(f, " & ");
            }
        }
    }

    fprintf(f, "\\bottomrule\n\\end{tabular}\n");

    /* Fracciones (si hay) */
    if (st->ratios && st->ratios_m > 0 && st->entering >= 0 && st->leaving_row > 0) {
        fprintf(f, "\\\\[4pt]\\textbf{Fracciones } $b_i/a_{i,j}$ para la columna ");
        tex_varname(f, st->entering, n);
        fprintf(f, ":\\\\\n");
        for (int i = 1; i <= st->ratios_m; ++i) {
            double r = st->ratios[i - 1];
            if (!isnan(r)) {
                fprintf(f, "$R_{%d} = %.6f$", i, clip_negzero(r));
                if (i == st->leaving_row)
                    fprintf(f, " \\;\\;\\textbf{(m\\'inima)}");
                if (i < st->ratios_m)
                    fprintf(f, ",\\ ");
            }
        }
        fprintf(f, ".\n");
    }
    fprintf(f, "\\end{table}\n\n");
}



static void write_multiple_solutions_section(FILE *f,
                                             const SimplexProblem *prob,
                                             const SimplexResult *res)
{
    int n = prob->n, m = prob->m;
    int rows = res->rows, cols = res->cols;

    if (res->alt_var_index < 0) return;
    if (!res->final_tableau) return;

    int j = res->alt_var_index;

    /* ---- Compute theta ---- */
    double theta = INFINITY;
    for (int i = 1; i <= m; ++i) {
        double aij = ROWc(res->final_tableau, cols, i)[j];
        double bi  = ROWc(res->final_tableau, cols, i)[cols - 1];
        if (aij > SIMPLEX_EPS) {
            double r = bi / aij;
            if (r + SIMPLEX_EPS < theta)
                theta = r;
        }
    }
    if (!isfinite(theta)) theta = 0.0;

    /* ---- Header ---- */
    fprintf(f, "\\subsection*{M\\'ultiples soluciones}\n");
    fprintf(f, "Se detect\\'o una variable no b\\'asica con costo reducido cero: ");
    tex_varname(f, j, n);
    fprintf(f, ". Esto implica la existencia de un conjunto infinito de \\'optimos.\\\\[4pt]\n");

    /* ---- Parametrization (NO nested $ inside \[ ! ) ---- */
    fprintf(f, "Una parametrizaci\\'on de la arista \\'optima es:\n");

    fprintf(f, "\\[\n");
    fprintf(f, "%s = t,\\quad x_{B_i} = b_i - a_{i,%d}\\,t,\\quad 0 \\le t \\le %.6f\n",
            latex_safe_varname_nomath(NULL, j), j + 1, clip_negzero(theta));
    fprintf(f, "\\]\n");

    fprintf(f, "donde $0 \\le t \\le \\theta$ proviene del an\\'alisis de fracciones v\\'alidas.\\\\[6pt]\n");

    /* ---- Sample points ---- */
    double tvals[3] = { theta/4.0, theta/2.0, (3.0*theta)/4.0 };
    double *xpt = calloc(n, sizeof(double));

    fprintf(f, "\\paragraph{Puntos adicionales sobre la arista \\\'optima.}\n");

    for (int k = 0; k < 3; ++k) {
        double t = tvals[k];

        for (int jj = 0; jj < n; ++jj) xpt[jj] = 0.0;

        if (j < n)
            xpt[j] = t;

        for (int i = 1; i <= m; ++i) {
            int var = res->basis[i - 1];
            if (var >= 0 && var < n) {
                double bi  = ROWc(res->final_tableau, cols, i)[cols - 1];
                double aij = ROWc(res->final_tableau, cols, i)[j];
                xpt[var] = bi - aij * t;
            }
        }

        fprintf(f, "Punto %d:\\; $t = %.6f$\\; $\\Rightarrow$ ", k+1, clip_negzero(t));

        for (int jj = 0; jj < n; ++jj) {
            fprintf(f, "$x_{%d} = %.6f$", jj+1, clip_negzero(xpt[jj]));
            if (jj + 1 < n) fprintf(f, ",\\;");
        }
        fprintf(f, ".\\\\\n");
    }

    free(xpt);

    /* ---- Alternate tableau ---- */
    if (res->alt_tableau) {
        fprintf(f, "\\paragraph{Tabla final alterna.}\n");

        SimplexStep fake = {0};
        fake.iter = res->iterations + 2;
        fake.rows = rows;
        fake.cols = cols;
        fake.tableau = res->alt_tableau;
        fake.entering = -1;
        fake.leaving_row = -1;

        tex_write_table_step(f, &fake, n, m);

        fprintf(f, "Soluci\\'on alterna b\\'asica: ");
        for (int jj = 0; jj < n; ++jj) {
            fprintf(f, "$x_{%d}=%.6f$", jj+1,
                    res->x_alt ? clip_negzero(res->x_alt[jj]) : 0.0);
            if (jj + 1 < n) fprintf(f, ",\\;");
        }
        fprintf(f, ";\\quad Z = %.6f.\\\\[6pt]\n", clip_negzero(res->z_alt));
        
    }
}



int simplex_write_latex_report(const char *base_name,
                               const SimplexProblem *prob,
                               const SimplexResult *res,
                               const SimplexTrace *trace)
{
    if (!base_name || !prob || !res || !trace)
        return -1;

    /* ======== Sanitize folder name ======== */
    char folder[256];
    if (prob->problem_name && strlen(prob->problem_name) > 0) {
        size_t k = 0;
        for (const char *p = prob->problem_name; *p && k < sizeof(folder) - 1; ++p) {
            char c = *p;
            folder[k++] = (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*'
                           || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                              ? '_'
                              : c;
        }
        folder[k] = '\0';
    } else {
        strcpy(folder, "Reporte_Simplex");
    }

    /* ======== Create folder if not exists ======== */
    mkdir(folder, 0755);

    /* ======== Build file paths ======== */
    char tex_path[512], pdf_path[512];
    snprintf(tex_path, sizeof(tex_path), "%s/reporte_simplex.tex", folder);
    snprintf(pdf_path, sizeof(pdf_path), "%s/reporte_simplex.pdf", folder);
    

    FILE *f = fopen(tex_path, "w");
    if (!f) return -1;

    /* ======== LaTeX preamble & cover page ======== */
    fprintf(f,
        "\\documentclass[11pt]{article}\n"
        "\\usepackage[spanish]{babel}\n"
        "\\usepackage[utf8]{inputenc}\n"
        "\\usepackage[T1]{fontenc}\n"
        "\\usepackage[a4paper,margin=2.2cm]{geometry}\n"
        "\\usepackage{amsmath,amssymb,booktabs,array,colortbl}\n"
        "\\usepackage[table,xcdraw]{xcolor}\n"
        "\\usepackage{hyperref,graphicx}\n"
        "\\begin{document}\n"
        "\\begin{titlepage}\n"
        "\\centering\n"
        "{\\Huge Proyecto 4 - Otro S\\'implex M\\'as}\\\\[1em]\n"
        "{\\Large %s}\\\\[2em]\n"
        "{\\large Curso: Investigaci\\'on de Operaciones\\\\Semestre: 2025-I}\\\\[4em]\n"
        "\\vfill\n"
        "\\textbf{Esteban Secaida - Fabian Bustos}\\\\[2em]\n"
        "Fecha: \\today\n"
        "\\end{titlepage}\n"
        "\\newpage\n",
        prob->problem_name ? prob->problem_name : "Problema Simplex"
    );

    /* ======== Problem formulation ======== */
    fputs("\\section*{Planteamiento del Problema}\n", f);
    fprintf(f, "%s \\[ Z = ",
            prob->sense == SIMPLEX_MAXIMIZE ? "Maximizar" : "Minimizar");

    for (int j = 0; j < prob->n; ++j) {
        if (j > 0 && prob->c[j] >= 0)
            fputs("+", f);
        fprintf(f, "%.3f%s ", prob->c[j],
                (prob->var_names && prob->var_names[j])
                    ? prob->var_names[j]
                    : "x");
    }
    fputs("\\]\nSujeto a:\\[ \n", f);

    for (int i = 0; i < prob->m; ++i) {
        for (int j = 0; j < prob->n; ++j) {
            if (j > 0 && prob->A[i * prob->n + j] >= 0)
                fputs("+", f);
            fprintf(f, "%.3f%s ", prob->A[i * prob->n + j],
                    (prob->var_names && prob->var_names[j])
                        ? prob->var_names[j]
                        : "x");
        }

        const char *op = "\\le";
        if (prob->ctype) {
            switch (prob->ctype[i]) {
            case CONSTR_GEQ: op = "\\ge"; break;
            case CONSTR_EQ:  op = "=";   break;
            case CONSTR_LEQ:
            default:         op = "\\le"; break;
            }
        }

        fprintf(f, "%s %.3f\\\\\n", op, prob->b[i]);
    }
    fputs("x_i \\ge 0 \\text{ para todo } i.\\]\n", f);

    /* ======== Description ======== */
    fputs("\\section*{Descripci\\'on del M\\'etodo S\\'implex}\n", f);
fputs("El m\\'etodo S\\'implex, desarrollado por George Dantzig en 1947, "
      "es un algoritmo iterativo para resolver problemas de programaci\\'on lineal "
      "en forma est\\'andar. Su fundamento te\\'orico radica en que, si el conjunto "
      "factible es un poliedro convexo y la funci\\'on objetivo es lineal, entonces la "
      "soluci\\'on \\textit{\\'optima} se alcanza necesariamente en uno de los v\\'ertices "
      "del poliedro.\\\\[1em]\n", f);

fputs("El algoritmo parte de una soluci\\'on b\\'asica factible inicial, representada "
      "por una base de variables. En cada iteraci\\'on, el S\\'implex calcula los "
      "costos reducidos o indicadores de mejora para determinar qu\\'e variable "
      "no b\\'asica debe entrar a la base (variable entrante). "
      "Simult\\'aneamente, se aplica la prueba de raz\\'on m\\'inima para identificar "
      "la variable que debe abandonar la base (variable saliente), garantizando "
      "la factibilidad de la soluci\\'on.\\\\[1em]\n", f);

fputs("Tras actualizar la base y la tabla correspondiente, el proceso se repite "
      "hasta que todos los costos reducidos indican que no existen mejoras posibles "
      "en la funci\\'on objetivo; en ese punto, la soluci\\'on b\\'asica actual es "
      "\\textbf{\\'optima}. En caso de que no exista variable saliente, "
      "el problema es no acotado. Si ninguna soluci\\'on factible puede construirse "
      "desde el inicio, se declara el problema como infactible.\\\\[1em]\n", f);

fputs("El m\\'etodo S\\'implex es eficiente en la pr\\'actica debido a que explora "
      "s\\'olo una peque\\~na fracci\\'on de los v\\'ertices del poliedro factible, "
      "y constituye uno de los algoritmos m\\'as influyentes en optimizaci\\'on "
      "matem\\'atica y operaciones.\\\\[1em]\n", f);


    /* ======== Tables ======== */
    fputs("\\section*{Tablas del M\\'etodo S\\'implex}\n", f);
    for (int k = 0; k < trace->count; ++k)
        tex_write_table_step(f, &trace->steps[k], prob->n, prob->m);

    /* ======== Results ======== */
    fputs("\\section*{Resultados y Casos Especiales}\n", f);
    fprintf(f, "Estado del problema: \\textbf{%s}.\\\\\n",
            simplex_status_str(res->status));

    if (res->status == SIMPLEX_OK || res->status == SIMPLEX_MULTIPLE) {
        fprintf(f, "Valor \\textit{\\'optimo}: $Z^* = %.6f$.\\\\[4pt]\n", res->z);
        fputs("Soluci\\'on \\textit{\\'optima}:\\\\[4pt]\n", f);
        fputs("\\[ ", f);
        for (int j = 0; j < prob->n; ++j) {
            fprintf(f, "%s = %.6f",
                    (prob->var_names && prob->var_names[j])
                        ? prob->var_names[j]
                        : "x",
                    res->x[j]);
            if (j + 1 < prob->n)
                fputs(",\\;", f);
        }
        fputs(". \\]\n", f);

        if (res->encountered_degeneracy)
            fputs("\\emph{Nota:} Se detect\\'o degeneraci\\'on (al menos un ratio m\\'inimo fue 0). "
                  "Se aplic\\'o la regla de Bland para evitar ciclos.\\\\[6pt]\n", f);

        if (res->has_alternate && res->alt_var_index >= 0)
            fputs("El problema presenta \\textbf{m\\'ultiples soluciones \\textit{\\'optimas}}. "
                  "Se puede obtener una familia de soluciones a lo largo de la recta de \\textit{\\'optimos}.\\\\[6pt]\n", f);

        if (res->has_alternate && res->alt_var_index >= 0) {
            write_multiple_solutions_section(f, prob, res);
        }
    }
    else if (res->status == SIMPLEX_UNBOUNDED)
        fputs("\\textbf{Problema no acotado:} la funci\\'on objetivo puede crecer indefinidamente.\\\\\n", f);
    else if (res->status == SIMPLEX_INFEASIBLE)
        fputs("\\textbf{Problema infactible:} no existe ning\\'un punto que satisfaga simult\\'aneamente todas las restricciones.\\\\\n", f);
    else if (res->status == SIMPLEX_ITER_LIMIT)
        fputs("\\textbf{L\\'{\\i}mite de iteraciones alcanzado.} Se recomienda aumentar el l\\'{\\i}mite o revisar la degeneraci\\'on.\\\\\n", f);

    /* ======== Footer ======== */
    fputs("\\vfill\\smallskip\\noindent Documento generado por Otro Simplex mas, de Esteban Secaida y Fabi\\'an Bustos ", f);
    fputs("\\end{document}\n", f);

    fclose(f);

    /* ======== Compile LaTeX into PDF ======== */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && pdflatex -interaction=nonstopmode -halt-on-error reporte_simplex.tex > /dev/null 2>&1",
             folder);
    system(cmd);

    /* ======== Open PDF in Evince ======== */
    if (access(pdf_path, F_OK) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "evince --presentation \"%s\" &", pdf_path);
        system(cmd);
    } else {
        fprintf(stderr, "Error: PDF file not generated in folder %s\n", folder);
    }

    return 0;
}

