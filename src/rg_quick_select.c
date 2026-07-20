/* rg_quick_select.c - Quickselect algorithm (from C++ reference quick.hh) */

#include <assert.h>
#include "rg_quick_select.h"

static void qs_swap(float *a, int i, int j)
{
    float t = a[i]; a[i] = a[j]; a[j] = t;
}

static void qs_median(float *A, int a, int b, int c, int d, int e)
{
    if (A[c] < A[a]) qs_swap(A, a, c);
    if (A[d] < A[b]) qs_swap(A, b, d);
    if (A[d] < A[c]) { qs_swap(A, c, d); qs_swap(A, a, b); }
    if (A[e] < A[b]) qs_swap(A, b, e);
    if (A[e] < A[c]) {
        qs_swap(A, c, e);
        if (A[c] < A[a]) qs_swap(A, a, c);
    } else if (A[c] < A[b]) {
        qs_swap(A, b, c);
    }
}

static void qs_insertion(float *a, int l, int h)
{
    int i, j;
    for (i = l + 1; i <= h; i++) {
        float t = a[i];
        for (j = i; j > l && t < a[j - 1]; j--)
            a[j] = a[j - 1];
        a[j] = t;
    }
}

static void qs_select(float *a, int l, int h, int k)
{
    while (l < h) {
        if (h - l < 32) {
            qs_insertion(a, l, h);
            break;
        }
        {
            int half = (h - l) / 2;
            int quarter = (h - l) / 4;
            int middle = l + half;
            qs_median(a, l, l + quarter, middle, middle + quarter, h);
            float pivot = a[middle];
            int lt = l, gt = h, i = l;
            while (i <= gt) {
                if (a[i] < pivot) qs_swap(a, i++, lt++);
                else if (a[i] > pivot) qs_swap(a, i, gt--);
                else i++;
            }
            if (k < lt) h = lt - 1;
            else if (k > gt) l = gt + 1;
            else break;
        }
    }
}

float rg_quick_select(float *a, int k, int n)
{
    assert(n > 0 && k < n);
    qs_select(a, 0, n - 1, k);
    return a[k];
}
