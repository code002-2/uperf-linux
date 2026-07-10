#include "../src/include/power_model.h"
#include "../src/include/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  TEST: %s ... ", #name); \
    name(); \
} while(0)
#define ASSERT_NEAR(a, b, eps, msg) do { \
    double diff = fabs((double)(a) - (double)(b)); \
    if (diff > (eps)) { \
        printf("FAIL (%s: expected ~%.2f, got %.2f, diff %.4f)\n", \
               msg, (double)(b), (double)(a), diff); \
        tests_failed++; return; \
    } \
} while(0)
#define ASSERT_GT(a, b, msg) do { \
    if ((a) <= (b)) { \
        printf("FAIL (%s: expected %.2f > %.2f)\n", msg, (double)(a), (double)(b)); \
        tests_failed++; return; \
    } \
} while(0)
#define ASSERT_LT(a, b, msg) do { \
    if ((a) >= (b)) { \
        printf("FAIL (%s: expected %.2f < %.2f)\n", msg, (double)(a), (double)(b)); \
        tests_failed++; return; \
    } \
} while(0)
#define ASSERT_PASS(msg) do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

/* Create a test power model entry (simulating a Cortex-A715-like cluster) */
static PowerModelEntry make_pm(int eff, int nr, float typ_p, float typ_f,
                                float sweet_f, float plain_f, float free_f) {
    PowerModelEntry pm = {0};
    pm.efficiency = eff;
    pm.nr_cores = nr;
    pm.typical_power_w = typ_p;
    pm.typical_freq_mhz = typ_f;
    pm.sweet_freq_mhz = sweet_f;
    pm.plain_freq_mhz = plain_f;
    pm.free_freq_mhz = free_f;
    return pm;
}

TEST(test_zero_freq_zero_power) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float p = power_model_est_power(&pm, 0.0f);
    ASSERT_NEAR(p, 0.0f, 0.01, "zero freq → zero power");
    ASSERT_PASS("zero freq returns zero power");
}

TEST(test_power_increases_with_freq) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float p_low = power_model_est_power(&pm, 600.0f);
    float p_high = power_model_est_power(&pm, 2000.0f);
    ASSERT_GT(p_high, p_low, "higher freq → more power");
    ASSERT_PASS("power increases with frequency");
}

TEST(test_sweet_spot_returns_correct_freq) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float sweet = power_model_sweet_freq(&pm);
    ASSERT_NEAR(sweet, 1600.0f, 1.0f, "sweet freq");
    ASSERT_PASS("sweet spot returns calibrated frequency");
}

TEST(test_perf_at_zero_freq) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float perf = power_model_perf_at_freq(&pm, 0.0f);
    ASSERT_NEAR(perf, 0.0f, 0.01, "zero perf at zero freq");
    ASSERT_PASS("zero frequency yields zero performance");
}

TEST(test_perf_linear_with_freq) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float perf_half = power_model_perf_at_freq(&pm, 1100.0f);
    float perf_full = power_model_perf_at_freq(&pm, 2200.0f);
    ASSERT_GT(perf_full, perf_half, "full freq > half freq perf");
    /* Full should be roughly 2x half since perf ∝ freq */
    ASSERT_NEAR(perf_full / perf_half, 2.0f, 0.3f, "perf ratio ≈ 2:1");
    ASSERT_PASS("performance scales roughly linearly with frequency");
}

TEST(test_select_freq_meets_demand) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float freq = power_model_select_freq(&pm, 0.5f, 0.0f);  /* 50% demand, no margin */
    ASSERT_GT(freq, 0.0f, "non-zero freq for positive demand");
    ASSERT_LT(freq, pm.typical_freq_mhz * 2.0f, "freq within bounds");
    ASSERT_PASS("select_freq returns reasonable frequency for 50% demand");
}

TEST(test_select_freq_with_margin) {
    PowerModelEntry pm = make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500);
    float freq_low = power_model_select_freq(&pm, 0.3f, 0.0f);
    float freq_high = power_model_select_freq(&pm, 0.3f, 0.5f);  /* 50% extra margin */
    ASSERT_GT(freq_high, freq_low, "margin increases frequency");
    ASSERT_PASS("margin parameter increases selected frequency");
}

TEST(test_system_load_computation) {
    PowerModelEntry pm[1] = { make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500) };
    int loads[2] = {50, 50};  /* 50% load on both CPUs */
    float freqs[2] = {2200, 2200};  /* Running at typical freq */
    float load = power_model_compute_system_load(loads, freqs, pm, 2);
    ASSERT_GT(load, 0.0f, "positive load");
    ASSERT_PASS("system load computed from per-CPU samples");
}

TEST(total_power_at_zero_load) {
    PowerModelEntry pm[1] = { make_pm(280, 2, 1.5f, 2200, 1600, 1200, 500) };
    int loads[2] = {0, 0};
    float freqs[2] = {1000, 1000};
    float total = power_model_total_power(loads, freqs, pm, 2);
    /* With 0% load, power should be minimal (just leakage) */
    ASSERT_LT(total, 1.0f, "low power at zero load");
    ASSERT_PASS("total power near-zero at zero load");
}

int main(void) {
    printf("=== power_model tests ===\n");
    log_init(LOG_WARN, 0, NULL);

    RUN_TEST(test_zero_freq_zero_power);
    RUN_TEST(test_power_increases_with_freq);
    RUN_TEST(test_sweet_spot_returns_correct_freq);
    RUN_TEST(test_perf_at_zero_freq);
    RUN_TEST(test_perf_linear_with_freq);
    RUN_TEST(test_select_freq_meets_demand);
    RUN_TEST(test_select_freq_with_margin);
    RUN_TEST(test_system_load_computation);
    RUN_TEST(total_power_at_zero_load);

    printf("\nResults: %d/%d passed (%d failed)\n",
           tests_passed, tests_run, tests_failed);
    log_shutdown();
    return tests_failed > 0 ? 1 : 0;
}
