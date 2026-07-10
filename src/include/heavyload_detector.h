#ifndef UPERF_HEAVYLOAD_DETECTOR_H
#define UPERF_HEAVYLOAD_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "power_model.h"

/* Number of history entries for load smoothing */
#define LOAD_HISTORY_SIZE 16

/* Per-CPU stat sample from /proc/stat */
typedef struct {
    uint64_t user;      /* jiffies in user mode */
    uint64_t nice;      /* jiffies in user mode with nice */
    uint64_t system;    /* jiffies in kernel mode */
    uint64_t idle;      /* jiffies in idle task */
    uint64_t iowait;    /* jiffies waiting for I/O */
    uint64_t irq;       /* jiffies serving IRQs */
    uint64_t softirq;   /* jiffies serving softirqs */
    uint64_t steal;     /* jiffies stolen by hypervisor */
} CpuStat;

/* Opaque heavy load detector handle */
typedef struct HeavyLoadDetector HeavyLoadDetector;

/* Create a new heavy load detector.
 * sample_time_ms: sampling interval in milliseconds.
 * heavy_load_threshold: load score above which we consider it "heavy".
 * idle_load_threshold: load score below which we consider it "idle".
 * request_burst_slack_ms: cooldown before re-entering boost after exit.
 * Returns NULL on failure. */
HeavyLoadDetector *heavyload_detector_new(float sample_time_ms,
                                          float heavy_load_threshold,
                                          float idle_load_threshold,
                                          float request_burst_slack_ms);

/* Free detector resources. */
void heavyload_detector_free(HeavyLoadDetector *d);

/* Sample CPU load from /proc/stat.
 * Returns weighted load score. */
float heavyload_detector_sample(HeavyLoadDetector *d);

/* Check if we are currently in heavy load state. */
bool heavyload_detector_is_heavy(const HeavyLoadDetector *d);

/* Get the smoothed load average. */
float heavyload_detector_get_avg_load(const HeavyLoadDetector *d);

/* Get raw per-CPU load percentages (caller must free).
 * Returns pointer to array of nr_cpus floats. */
float *heavyload_detector_get_cpu_loads(const HeavyLoadDetector *d, int *nr_cpus);

#endif /* UPERF_HEAVYLOAD_DETECTOR_H */
