#include "../src/include/config.h"
#include "../src/include/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  TEST: %s ... ", #name); \
    name(); \
} while(0)
#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL (%s: expected %d, got %d)\n", msg, (b), (a)); \
        tests_failed++; \
        return; \
    } \
} while(0)
#define ASSERT_OK(ret, msg) do { \
    if ((ret) != 0) { \
        printf("FAIL (%s: expected 0, got %d)\n", msg, (ret)); \
        tests_failed++; \
        return; \
    } \
} while(0)
#define ASSERT_FAIL(ret, msg) do { \
    if ((ret) == 0) { \
        printf("FAIL (%s: expected non-zero, got 0)\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)
#define ASSERT_PASS(msg) do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

/* Test: config_load with a valid JSON file */
TEST(test_load_valid_config) {
    Config cfg;
    int ret = config_load(&cfg, "config/sm8550.json");
    ASSERT_OK(ret, "config_load valid file");
    ASSERT_EQ(cfg.cpu.nr_clusters, 3, "cluster count");
    ASSERT_PASS("valid config loaded");
}

/* Test: config_load with nonexistent file */
TEST(test_load_missing_file) {
    Config cfg;
    int ret = config_load(&cfg, "/nonexistent/path.json");
    ASSERT_FAIL(ret, "config_load missing file");
    ASSERT_PASS("correctly rejected missing file");
}

/* Test: config_validate with valid config */
TEST(test_validate_valid) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cpu.nr_clusters = 3;
    cfg.cpu.power_model[0].efficiency = 350;
    cfg.cpu.power_model[0].typical_freq_mhz = 2400;
    cfg.presets[MODE_BALANCE].name[0] = 'b';

    int ret = config_validate(&cfg);
    ASSERT_OK(ret, "config_validate valid");
    ASSERT_PASS("valid config passes validation");
}

/* Test: config_validate with no clusters */
TEST(test_validate_no_clusters) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cpu.nr_clusters = 0;

    int ret = config_validate(&cfg);
    ASSERT_FAIL(ret, "config_validate no clusters");
    ASSERT_PASS("correctly rejects no clusters");
}

/* Test: config_validate with negative efficiency */
TEST(test_validate_neg_efficiency) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cpu.nr_clusters = 1;
    cfg.cpu.power_model[0].efficiency = -100;

    int ret = config_validate(&cfg);
    ASSERT_FAIL(ret, "config_validate neg efficiency");
    ASSERT_PASS("correctly rejects negative efficiency");
}

int main(void) {
    printf("=== config_parser tests ===\n");
    log_init(LOG_WARN, 0, NULL);

    RUN_TEST(test_load_valid_config);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_validate_valid);
    RUN_TEST(test_validate_no_clusters);
    RUN_TEST(test_validate_neg_efficiency);

    printf("\nResults: %d/%d passed (%d failed)\n",
           tests_passed, tests_run, tests_failed);
    log_shutdown();
    return tests_failed > 0 ? 1 : 0;
}
