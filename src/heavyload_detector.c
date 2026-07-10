#include "heavyload_detector.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* Internal heavy load detector state */
struct HeavyLoadDetector {
    float sample_time_ms;
    float heavy_load_threshold;
    float idle_load_threshold;
    float request_burst_slack_ms;

    /* Per-CPU stats */
    CpuStat *prev_stats;
    CpuStat *curr_stats;
    int nr_cpus;

    /* Load history for smoothing */
    float load_history[LOAD_HISTORY_SIZE];
    int   load_hist_idx;
    int   load_hist_count;

    /* Current state */
    bool heavy_load_active;
    float current_load;
    float smoothed_load;
    uint64_t last_sample_ms;
    uint64_t last_heavy_end_ms;
};

/* Read /proc/stat for a single CPU (or aggregate) */
static int read_cpu_stat(CpuStat *stat) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;

    char line[1024];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Parse: cpu  user nice system idle iowait irq softirq steal */
    int parsed = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                        &stat->user, &stat->nice, &stat->system,
                        &stat->idle, &stat->iowait, &stat->irq,
                        &stat->softirq, &stat->steal);
    return (parsed >= 7) ? 0 : -1;
}

/* Read per-CPU stats from /proc/stat */
static int read_per_cpu_stats(CpuStat *stats, int nr_cpus) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;

    char line[256];
    int cpu_idx = 0;
    while (fgets(line, sizeof(line), fp) && cpu_idx < nr_cpus) {
        /* Match lines like "cpu0 1234 567 ..." */
        if (line[0] == 'c' && line[1] == 'p' && line[2] >= '0' && line[2] <= '9') {
            CpuStat *stat = &stats[cpu_idx];
            int parsed = sscanf(line, "cpu%*d %lu %lu %lu %lu %lu %lu %lu %lu",
                                &stat->user, &stat->nice, &stat->system,
                                &stat->idle, &stat->iowait, &stat->irq,
                                &stat->softirq, &stat->steal);
            if (parsed < 7)
                memset(stat, 0, sizeof(*stat));
            cpu_idx++;
        }
    }
    fclose(fp);
    return cpu_idx;
}

/* Get number of online CPUs */
static int get_nr_cpus(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

HeavyLoadDetector *heavyload_detector_new(float sample_time_ms,
                                          float heavy_load_threshold,
                                          float idle_load_threshold,
                                          float request_burst_slack_ms) {
    HeavyLoadDetector *d = calloc(1, sizeof(*d));
    if (!d) return NULL;

    d->sample_time_ms = sample_time_ms;
    d->heavy_load_threshold = heavy_load_threshold;
    d->idle_load_threshold = idle_load_threshold;
    d->request_burst_slack_ms = request_burst_slack_ms;
    d->nr_cpus = get_nr_cpus();

    d->prev_stats = calloc(d->nr_cpus, sizeof(CpuStat));
    d->curr_stats = calloc(d->nr_cpus, sizeof(CpuStat));
    if (!d->prev_stats || !d->curr_stats) {
        free(d->prev_stats);
        free(d->curr_stats);
        free(d);
        return NULL;
    }

    d->heavy_load_active = false;
    d->current_load = 0.0f;
    d->smoothed_load = 0.0f;
    d->load_hist_count = 0;
    d->load_hist_idx = 0;
    memset(d->load_history, 0, sizeof(d->load_history));

    log_info("HeavyLoadDetector created: nr_cpus=%d heavy_thresh=%.1f idle_thresh=%.1f",
             d->nr_cpus, heavy_load_threshold, idle_load_threshold);
    return d;
}

void heavyload_detector_free(HeavyLoadDetector *d) {
    if (!d) return;
    free(d->prev_stats);
    free(d->curr_stats);
    log_debug("HeavyLoadDetector destroyed");
    free(d);
}

float heavyload_detector_sample(HeavyLoadDetector *d) {
    if (!d) return 0.0f;

    /* Read current CPU stats */
    int nr = read_per_cpu_stats(d->curr_stats, d->nr_cpus);
    if (nr <= 0) {
        log_warn("heavyload_detector: failed to read /proc/stat");
        return d->current_load;
    }

    if (nr != d->nr_cpus) {
        /* CPU topology changed — reinitialize */
        free(d->prev_stats);
        free(d->curr_stats);
        d->nr_cpus = nr;
        d->prev_stats = calloc(nr, sizeof(CpuStat));
        d->curr_stats = calloc(nr, sizeof(CpuStat));
        if (!d->prev_stats || !d->curr_stats) return 0.0f;
    }

    /* Calculate delta for each CPU */
    float total_load = 0.0f;
    for (int i = 0; i < nr; i++) {
        uint64_t prev_total = d->prev_stats[i].user + d->prev_stats[i].nice +
                              d->prev_stats[i].system + d->prev_stats[i].idle +
                              d->prev_stats[i].iowait + d->prev_stats[i].irq +
                              d->prev_stats[i].softirq + d->prev_stats[i].steal;
        uint64_t curr_total = d->curr_stats[i].user + d->curr_stats[i].nice +
                              d->curr_stats[i].system + d->curr_stats[i].idle +
                              d->curr_stats[i].iowait + d->curr_stats[i].irq +
                              d->curr_stats[i].softirq + d->curr_stats[i].steal;

        uint64_t delta = curr_total - prev_total;
        if (delta == 0) {
            d->prev_stats[i] = d->curr_stats[i];
            continue;
        }

        uint64_t busy = delta - (d->curr_stats[i].idle + d->curr_stats[i].iowait);
        float load_pct = (float)busy / (float)delta * 100.0f;
        total_load += load_pct;

        /* Store for next iteration */
        d->prev_stats[i] = d->curr_stats[i];
    }

    /* Average across CPUs */
    d->current_load = total_load / (float)nr;

    /* Smooth with exponential moving average */
    if (d->load_hist_count == 0) {
        d->smoothed_load = d->current_load;
    } else {
        d->smoothed_load = d->smoothed_load * 0.7f + d->current_load * 0.3f;
    }

    /* Update load history ring buffer */
    d->load_history[d->load_hist_idx] = d->smoothed_load;
    d->load_hist_idx = (d->load_hist_idx + 1) % LOAD_HISTORY_SIZE;
    if (d->load_hist_count < LOAD_HISTORY_SIZE)
        d->load_hist_count++;

    /* Check heavy load state transitions */
    uint64_t now_ms = (uint64_t)time(NULL) * 1000;  /* Placeholder — use clock_gettime */

    if (!d->heavy_load_active && d->smoothed_load > d->heavy_load_threshold) {
        d->heavy_load_active = true;
        log_info("HeavyLoad START: load=%.1f%% (threshold=%.1f%%)",
                 d->smoothed_load, d->heavy_load_threshold);
    } else if (d->heavy_load_active && d->smoothed_load < d->idle_load_threshold) {
        /* Must stay below idle threshold for a grace period */
        if (now_ms - d->last_heavy_end_ms > 1000) {  /* 1 second grace */
            d->heavy_load_active = false;
            d->last_heavy_end_ms = now_ms;
            log_info("HeavyLoad END: load=%.1f%% (threshold=%.1f%%)",
                     d->smoothed_load, d->idle_load_threshold);
        }
    } else if (d->heavy_load_active) {
        d->last_heavy_end_ms = now_ms;
    }

    return d->smoothed_load;
}

bool heavyload_detector_is_heavy(const HeavyLoadDetector *d) {
    if (!d) return false;
    return d->heavy_load_active;
}

float heavyload_detector_get_avg_load(const HeavyLoadDetector *d) {
    if (!d) return 0.0f;
    return d->smoothed_load;
}

float *heavyload_detector_get_cpu_loads(const HeavyLoadDetector *d, int *nr_cpus) {
    if (!d || !nr_cpus) return NULL;

    /* Return a snapshot of the last computed per-CPU loads.
     * In the current implementation we only track the average,
     * so this returns a single-element array with the average.
     * TODO: store per-CPU smoothed loads. */
    static float snapshot[64];  /* Static buffer — not thread-safe */
    *nr_cpus = d->nr_cpus;
    float avg = d->smoothed_load;
    for (int i = 0; i < d->nr_cpus && i < 64; i++) {
        snapshot[i] = avg;
    }
    return snapshot;
}
