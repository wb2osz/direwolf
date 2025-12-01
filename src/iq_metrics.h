#ifndef IQ_METRICS_H
#define IQ_METRICS_H

#include <stddef.h>

typedef struct iq_metrics_s {
    float avg_power_fast;   // fast EMA for signal power
    float avg_power_slow;   // slow EMA for general average
    float noise_floor;      // noise floor estimate from minimum power
    float noise_alpha;      // adaptation rate for noise floor
    float rssi_db;          // computed RSSI (dBFS-like)
    float snr_db;           // computed SNR in dB
    float alpha_fast;       // EMA coeff for fast average
    float alpha_slow;       // EMA coeff for slow average
    float eps;              // small epsilon to avoid division by zero
} iq_metrics_t;

// Initialize metrics estimator for a given sample rate.
// alpha_fast/slow are chosen to approximate ~10ms and ~200ms time constants.
void iq_metrics_init(iq_metrics_t *m, float sample_rate);

// Process a block of interleaved IQ float32 samples and update metrics.
// Returns latest rssi_db and snr_db via output params if not NULL.
void iq_metrics_process(iq_metrics_t *m, const float *iq, size_t count_iq_samples,
                        float *out_rssi_db, float *out_snr_db);

#endif // IQ_METRICS_H
