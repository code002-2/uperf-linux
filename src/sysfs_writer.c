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

    int flushed = w->batch_len;
    w->batch_len = 0;
    clock_gettime(CLOCK_MONOTONIC, &w->last_flush);

    if (ret < 0)
        log_error("sysfs_writer_flush: some writes failed (see logs)");
    else
        log_debug("sysfs_writer_flush: %d writes applied", flushed);
}

void sysfs_writer_apply(const SysfsWriter *w, const ActionParams *params,
                        PowerMode mode) {
    if (!w || !params) return;

    const Config *cfg = w->cfg;
    bool applied_any = false;

    for (int k = 0; k < cfg->sysfs.nr_knobs; k++) {
        KnobDef *knob = &cfg->sysfs.knobs[k];
        if (!knob->enabled) continue;

        char value[MAX_PATH_LEN];

        if (strcmp(knob->name, "cpufreqMax") == 0) {
            /* Expand per-cluster */
            int cpu = 0;
            for (int c = 0; c < cfg->cpu.nr_clusters; c++) {
                for (int j = 0; j < cfg->cpu.power_model[c].nr_cores; j++, cpu++) {
                    if (params->has_cpu_freq_max && params->cpu_freq_max[c] > 0) {
                        snprintf(value, sizeof(value), "%d", params->cpu_freq_max[c]);
                    } else {
                        /* Default: max frequency (~9.9 GHz for SM8550) */
                        snprintf(value, sizeof(value), "%d", 9999000);
                    }
                    char path[MAX_PATH_LEN];
                    snprintf(path, sizeof(path), knob->path, cpu);
                    sysfs_writer_queue_raw((SysfsWriter *)w, path, value);
                    applied_any = true;
                }
            }
        } else if (strcmp(knob->name, "cpufreqMin") == 0) {
            int cpu = 0;
            for (int c = 0; c < cfg->cpu.nr_clusters; c++) {
                for (int j = 0; j < cfg->cpu.power_model[c].nr_cores; j++, cpu++) {
                    if (params->has_cpu_freq_min && params->cpu_freq_min[c] > 0) {
                        snprintf(value, sizeof(value), "%d", params->cpu_freq_min[c]);
                    } else {
                        snprintf(value, sizeof(value), "%d", 300000);  /* 300 MHz min */
                    }
                    char path[MAX_PATH_LEN];
                    snprintf(path, sizeof(path), knob->path, cpu);
                    sysfs_writer_queue_raw((SysfsWriter *)w, path, value);
                    applied_any = true;
                }
            }
        } else if (strcmp(knob->name, "cpufreqGovernor") == 0 ||
                   strcmp(knob->name, "governor") == 0) {
            if (params->has_governor && params->governor[0]) {
                snprintf(value, sizeof(value), "%s", params->governor);
            } else {
                snprintf(value, sizeof(value), "schedutil");
            }
            /* Write to first CPU (governor is typically per-cluster or global) */
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), knob->path, 0);
            sysfs_writer_queue_raw((SysfsWriter *)w, path, value);
            applied_any = true;
        } else if (strcmp(knob->name, "gpuMaxFreq") == 0 ||
                   strcmp(knob->name, "gpuMax") == 0) {
            if (params->has_gpu_max_freq && params->gpu_max_freq > 0) {
                snprintf(value, sizeof(value), "%d", params->gpu_max_freq);
            } else {
                snprintf(value, sizeof(value), "999900000");  /* Max GPU freq */
            }
            sysfs_writer_queue_raw((SysfsWriter *)w, knob->path, value);
            applied_any = true;
        } else if (strcmp(knob->name, "gpuMinFreq") == 0 ||
                   strcmp(knob->name, "gpuMin") == 0) {
            if (params->has_gpu_min_freq && params->gpu_min_freq > 0) {
                snprintf(value, sizeof(value), "%d", params->gpu_min_freq);
            } else {
                snprintf(value, sizeof(value), "0");
            }
            sysfs_writer_queue_raw((SysfsWriter *)w, knob->path, value);
            applied_any = true;
        } else if (strcmp(knob->name, "memBwMax") == 0 ||
                   strcmp(knob->name, "ddrMaxFreq") == 0) {
            if (params->has_ddr_max_freq && params->ddr_max_freq > 0) {
                snprintf(value, sizeof(value), "%d", params->ddr_max_freq);
            } else {
                snprintf(value, sizeof(value), "999900000");
            }
            sysfs_writer_queue_raw((SysfsWriter *)w, knob->path, value);
            applied_any = true;
        } else {
            /* Unknown knob type — skip */
            log_debug("sysfs_writer_apply: unknown knob '%s', skipping", knob->name);
        }
    }

    if (applied_any) {
        /* Flush immediately for mode/scene changes */
        sysfs_writer_flush((SysfsWriter *)w);
    }

    log_debug("sysfs_writer_apply: mode=%d, %s knobs",
              mode, applied_any ? "applied" : "none to apply");
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
