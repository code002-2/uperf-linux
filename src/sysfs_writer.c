#include "sysfs_writer.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

/* Internal state of the writer */
struct SysfsWriter {
    const Config         *cfg;
    uint64_t              batch_window_ns;
    WriteRequest          batch[SYSFS_BATCH_MAX];
    int                   batch_len;
    struct timespec       last_flush;
};

/* Helper: monotonic time in nanoseconds */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Helper: difference between two timespecs in nanoseconds */
static uint64_t timespec_diff_ns(const struct timespec *a, const struct timespec *b) {
    return (uint64_t)(b->tv_sec - a->tv_sec) * 1000000000ULL +
           (uint64_t)(b->tv_nsec - a->tv_nsec);
}

SysfsWriter *sysfs_writer_new(const Config *cfg, uint64_t batch_window_ns) {
    SysfsWriter *w = calloc(1, sizeof(*w));
    if (!w) return NULL;

    w->cfg = cfg;
    w->batch_window_ns = batch_window_ns;
    w->batch_len = 0;
    clock_gettime(CLOCK_MONOTONIC, &w->last_flush);

    log_info("SysfsWriter created (batch_window=%lu ns)", (unsigned long)batch_window_ns);
    return w;
}

void sysfs_writer_free(SysfsWriter *w) {
    if (!w) return;
    if (w->batch_len > 0)
        sysfs_writer_flush((SysfsWriter *)w);  /* Cast away const for cfg access */
    free(w);
    log_debug("SysfsWriter destroyed");
}

static bool values_match(const char *existing, const char *new_val) {
    if (!existing || !new_val) return false;
    /* Trim whitespace */
    while (*existing == ' ' || *existing == '\t') existing++;
    while (*new_val == ' ' || *new_val == '\t') new_val++;
    return strcmp(existing, new_val) == 0;
}

static int write_one(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        switch (errno) {
            case ENOENT:
                log_warn("sysfs write skipped (path not found): %s = %s", path, value);
                return 0;  /* Non-fatal: path may appear later */
            case EACCES:
                log_error("sysfs write FAILED (permission denied): %s = %s: %s",
                          path, value, strerror(errno));
                return -1;  /* Fatal: cannot function without sysfs access */
            case EROFS:
                log_warn("sysfs write skipped (read-only filesystem): %s", path);
                return 0;
            default:
                log_warn("sysfs write failed: %s = %s: %s", path, value, strerror(errno));
                return 0;
        }
    }

    size_t len = strlen(value);
    ssize_t n = write(fd, value, len);
    close(fd);

    if (n < 0 || (size_t)n != len) {
        log_error("sysfs write partial/fail: %s = %s: %s", path, value, strerror(errno));
        return -1;
    }

    log_debug("sysfs write: %s = %s", path, value);
    return 0;
}

void sysfs_writer_flush(SysfsWriter *w) {
    if (!w || w->batch_len == 0) return;

    int ret = 0;
    for (int i = 0; i < w->batch_len; i++) {
        if (!w->batch[i].has_value) continue;

        /* Dedup: read current value and skip if unchanged */
        char *current = sysfs_reader_read(w->batch[i].path);
        if (current && values_match(current, w->batch[i].value)) {
            free(current);
            continue;
        }
        free(current);

        if (write_one(w->batch[i].path, w->batch[i].value) < 0)
            ret = -1;
    }

    w->batch_len = 0;
    clock_gettime(CLOCK_MONOTONIC, &w->last_flush);

    if (ret < 0)
        log_error("sysfs_writer_flush: some writes failed (see logs)");
    else
        log_debug("sysfs_writer_flush: %d writes applied", w->batch_len);
}

void sysfs_writer_apply(const SysfsWriter *w, const ActionParams *params,
                        PowerMode mode) {
    (void)w;
    (void)params;
    (void)mode;
    /* TODO: iterate config knobs and queue writes based on ActionParams fields.
     *
     * For each enabled knob in cfg->sysfs.knobs:
     *   - If knob is cpufreqMax: write params->cpu_freq_max[cluster] to path
     *   - If knob is cpufreqMin: write params->cpu_freq_min[cluster] to path
     *   - If knob is gpuMaxFreq: write params->gpu_max_freq to devfreq path
     *   - If knob is governor: write params->governor string
     *
     * This is a stub — the full implementation expands per-cluster knobs
     * and queues individual write requests before flushing.
     */
    log_debug("sysfs_writer_apply: mode=%d (stub — no knobs written yet)", mode);
}

int sysfs_writer_queue_raw(SysfsWriter *w, const char *path, const char *value) {
    if (!w || !path || !value) return -1;
    if (w->batch_len >= SYSFS_BATCH_MAX)
        sysfs_writer_flush(w);  /* Force flush */

    WriteRequest *req = &w->batch[w->batch_len++];
    strncpy(req->path, path, MAX_PATH_LEN - 1);
    req->path[MAX_PATH_LEN - 1] = '\0';
    strncpy(req->value, value, MAX_PATH_LEN - 1);
    req->value[MAX_PATH_LEN - 1] = '\0';
    req->has_value = true;

    return 0;
}

char *sysfs_reader_read(const char *path) {
    if (!path) return NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) return NULL;
    buf[n] = '\0';

    /* Trim trailing newline/whitespace */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        buf[--n] = '\0';

    if (n == 0) return NULL;

    char *result = malloc(n + 1);
    if (result) {
        memcpy(result, buf, n + 1);
    }
    return result;
}
