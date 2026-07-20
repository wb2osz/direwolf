/* rg_theil_sen.c - Theil-Sen estimator */

#include <stdlib.h>
#include <string.h>
#include "rg_theil_sen.h"
#include "rg_quick_select.h"

rg_theil_sen_t *rg_theil_sen_create(int len_max)
{
    int size = ((len_max - 1) * len_max) / 2;
    rg_theil_sen_t *ts = calloc(1, sizeof(*ts));
    if (!ts) return NULL;
    ts->temp = calloc(size, sizeof(float));
    if (!ts->temp) { free(ts); return NULL; }
    ts->size = size;
    ts->slope = ts->yint = ts->xint = 0;
    return ts;
}

void rg_theil_sen_free(rg_theil_sen_t *ts)
{
    if (ts) { free(ts->temp); free(ts); }
}

void rg_theil_sen_compute(rg_theil_sen_t *ts, const float *x, const float *y, int len)
{
    int count = 0, i, j;

    /* Compute all pairwise slopes */
    for (i = 0; count < ts->size && i < len; i++)
        for (j = i + 1; count < ts->size && j < len; j++)
            if (x[j] != x[i])
                ts->temp[count++] = (y[j] - y[i]) / (x[j] - x[i]);

    ts->slope = rg_quick_select(ts->temp, count / 2, count);

    /* Compute all y-intercepts */
    count = 0;
    for (i = 0; count < ts->size && i < len; i++)
        ts->temp[count++] = y[i] - ts->slope * x[i];

    ts->yint = rg_quick_select(ts->temp, count / 2, count);
    ts->xint = -ts->yint / ts->slope;
}

float rg_theil_sen_eval(rg_theil_sen_t *ts, float x)
{
    return ts->yint + ts->slope * x;
}
