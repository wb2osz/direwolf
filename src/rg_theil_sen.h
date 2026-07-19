/* rg_theil_sen.h - Theil-Sen robust linear regression for Rattlegram */

#ifndef RG_THEIL_SEN_H
#define RG_THEIL_SEN_H

typedef struct {
    float xint, yint, slope;
    float *temp;
    int size;
} rg_theil_sen_t;

/* Create for max LEN_MAX points */
rg_theil_sen_t *rg_theil_sen_create(int len_max);
void rg_theil_sen_free(rg_theil_sen_t *ts);

/* Compute: fit line y = yint + slope * x through (x[],y[]) of LEN points */
void rg_theil_sen_compute(rg_theil_sen_t *ts, const float *x, const float *y, int len);

/* Evaluate fitted line at x */
float rg_theil_sen_eval(rg_theil_sen_t *ts, float x);

#endif /* RG_THEIL_SEN_H */
