/* rg_quick_select.h - Quickselect for finding k-th smallest element */

#ifndef RG_QUICK_SELECT_H
#define RG_QUICK_SELECT_H

/* Find the k-th smallest element in array a of n elements.
 * Modifies the array. Returns a[k] after selection. */
float rg_quick_select(float *a, int k, int n);

#endif /* RG_QUICK_SELECT_H */
