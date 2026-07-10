#ifndef UPERF_CGROUP_MANAGER_H
#define UPERF_CGROUP_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* Cgroup slice type */
typedef enum {
    SLICE_GAME = 0,
    SLICE_SYSTEM,
    SLICE_BACKGROUND,
    SLICE_NUM
} CgroupSlice;

/* Opaque cgroup manager handle */
typedef struct CgroupManager CgroupManager;

/* Create a new cgroup manager.
 * Returns NULL on failure (e.g., cgroup v2 not mounted). */
CgroupManager *cgroup_manager_new(void);

/* Free cgroup manager resources. */
void cgroup_manager_free(CgroupManager *cm);

/* Initialize cgroup hierarchy: create slices, enable controllers.
 * Returns 0 on success, -1 on failure. */
int cgroup_manager_init(CgroupManager *cm);

/* Assign a process to a slice.
 * pid: process ID to assign.
 * slice: target slice.
 * Returns 0 on success, -1 on failure. */
int cgroup_manager_assign_pid(CgroupManager *cm, pid_t pid, CgroupSlice slice);

/* Set CPU affinity mask for a slice.
 * slice: target slice.
 * cpu_mask: bitmask of allowed CPUs (e.g., 0xFF = CPUs 0-7).
 * Returns 0 on success. */
int cgroup_manager_set_slice_cpus(CgroupManager *cm, CgroupSlice slice,
                                  uint64_t cpu_mask);

/* Set CPU weight (fair-share) for a slice.
 * slice: target slice.
 * weight: 1-10000 (default 100). Higher = more CPU share.
 * Returns 0 on success. */
int cgroup_manager_set_slice_weight(CgroupManager *cm, CgroupSlice slice,
                                    int weight);

/* Set uClamp min/max for a slice.
 * slice: target slice.
 * uclamp_min: 0-1024 (percentage of CPU capacity).
 * uclamp_max: 0-1024.
 * Returns 0 on success. */
int cgroup_manager_set_slice_uclamp(CgroupManager *cm, CgroupSlice slice,
                                    int uclamp_min, int uclamp_max);

/* Get the cgroup path for a slice (for debugging/logging). */
const char *cgroup_manager_get_path(const CgroupManager *cm, CgroupSlice slice);

/* Check if cgroup v2 is available. */
bool cgroup_manager_is_available(void);

/* Fallback: set uClamp for a specific PID directly via prctl.
 * This is used when cgroup v2 is not available.
 * Returns 0 on success. */
int cgroup_manager_set_pid_uclamp(pid_t pid, int uclamp_min, int uclamp_max);

#endif /* UPERF_CGROUP_MANAGER_H */
