#include "iq_metrics.h"
#include <math.h>

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

void iq_metrics_init(iq_metrics_t *m, float sample_rate) {
    if (!m) return;
    // Fast EMA for signal tracking (~10ms)
    float tau_fast = 0.010f;
    m->alpha_fast = 1.0f - expf(-1.0f / (sample_rate * tau_fast));
    
    // Noise floor tracking: use minimum power over longer window
    // Track min over ~1 second window
    m->alpha_slow = 1.0f - expf(-1.0f / (sample_rate * 1.0f));
    
    // Initialize
    m->avg_power_fast = 1e-6f;
    m->avg_power_slow = 1e-6f;
    m->noise_floor = 1e-6f;  // Start with low noise floor estimate
    m->noise_alpha = 0.0001f; // Very slow adaptation for noise floor
    m->rssi_db = -120.0f;
    m->snr_db = 0.0f;
    m->eps = 1e-9f;
}

void iq_metrics_process(iq_metrics_t *m, const float *iq, size_t count_iq_samples,
                        float *out_rssi_db, float *out_snr_db) {
    if (!m || !iq || count_iq_samples == 0) return;

    float af = m->alpha_fast;
    float as = m->alpha_slow;
    float min_power_in_block = 1e9f;

    // Process samples and track minimum power in this block
    for (size_t i = 0; i + 1 < count_iq_samples; i += 2) {
        float I = iq[i];
        float Q = iq[i + 1];
        float p = I * I + Q * Q;
        
        // Fast EMA for signal
        m->avg_power_fast = (1.0f - af) * m->avg_power_fast + af * p;
        
        // Slow EMA for general average
        m->avg_power_slow = (1.0f - as) * m->avg_power_slow + as * p;
        
        // Track minimum in this block
        if (p < min_power_in_block) {
            min_power_in_block = p;
        }
    }

    // Update noise floor: slowly track towards minimum observed power
    // Only update if current minimum is lower than noise floor estimate
    if (min_power_in_block < m->noise_floor * 2.0f) { // within reasonable range
        m->noise_floor = (1.0f - m->noise_alpha) * m->noise_floor + m->noise_alpha * min_power_in_block;
    }
    
    // Ensure noise floor doesn't go too low
    if (m->noise_floor < 1e-8f) m->noise_floor = 1e-8f;

    // RSSI: instantaneous signal power
    float rssi = 10.0f * log10f(clampf(m->avg_power_fast, m->eps, 1e9f));

    // SNR: signal vs noise floor estimate
    float snr = 10.0f * log10f(clampf(m->avg_power_fast / m->noise_floor, 0.1f, 1e6f));

    m->rssi_db = rssi;
    m->snr_db = snr;

    if (out_rssi_db) *out_rssi_db = rssi;
    if (out_snr_db) *out_snr_db = snr;
}
