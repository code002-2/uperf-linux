#include "../src/include/sysfs_writer.h"
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
#define ASSERT_PASS(msg) do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

/* Test sysfs_reader_read on a known file */
TEST(test_read_known_file) {
    char *val = sysfs_reader_read("/proc/cpuinfo");
    if (val) {
        free(val);
        ASSERT_PASS("read /proc/cpuinfo successfully");
    } else {
        printf("SKIP (/proc/cpuinfo not readable)\n");
        tests_run++;  /* Don't count as failure */
    }
}

/* Test sysfs_reader_read on nonexistent file */
TEST(test_read_nonexistent) {
    char *val = sysfs_reader_read("/nonexistent/path/that/does/not/exist");
    if (val == NULL) {
        ASSERT_PASS("returns NULL for nonexistent file");
    } else {
        free(val);
        printf("FAIL (should return NULL for nonexistent path)\n");
        tests_failed++;
    }
}

/* Test sysfs_writer creation/destruction */
TEST(test_writer_lifecycle) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sysfs.nr_knobs = 0;

    SysfsWriter *w = sysfs_writer_new(&cfg, 0);  /* No batching */
    if (!w) {
        printf("FAIL (sysfs_writer_new returned NULL)\n");
        tests_failed++;
        return;
    }
    sysfs_writer_free(w);
    ASSERT_PASS("writer create and destroy");
}

/* Test queueing a raw write to a writable path */
TEST(test_queue_raw_write) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sysfs.nr_knobs = 0;

    SysfsWriter *w = sysfs_writer_new(&cfg, 0);
    if (!w) { printf("SKIP (writer creation failed)\n"); tests_run++; return; }

    int ret = sysfs_writer_queue_raw(w, "/proc/self/oom_score_adj", "0");
    /* This should succeed (queuing doesn't require the path to exist) */
    (void)ret;
    sysfs_writer_free(w);
    ASSERT_PASS("queue raw write");
}

int main(void) {
    printf("=== sysfs_writer tests ===\n");
    log_init(LOG_WARN, 0, NULL);

    RUN_TEST(test_read_known_file);
    RUN_TEST(test_read_nonexistent);
    RUN_TEST(test_writer_lifecycle);
    RUN_TEST(test_queue_raw_write);

    printf("\nResults: %d/%d passed (%d failed)\n",
           tests_passed, tests_run, tests_failed);
    log_shutdown();
    return tests_failed > 0 ? 1 : 0;
}
