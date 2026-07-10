#include "cgroup_manager.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sched.h>

#define CGROUP_ROOT "/sys/fs/cgroup"
#define SLICE_NAME "uperf-linux.slice"
#define SLICE_PATH "/sys/fs/cgroup/uperf-linux.slice"

/* Internal cgroup manager state */
struct CgroupManager {
    bool available;
    char paths[SLICE_NUM][MAX_PATH_LEN];
};

bool cgroup_manager_is_available(void) {
    struct stat st;
    if (stat(CGROUP_ROOT, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;

    /* Check if cgroup2 is mounted */
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return false;

    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "cgroup2") && strstr(line, "/sys/fs/cgroup")) {
            found = true;
            break;
        }
    }
    fclose(fp);
    return found;
}

CgroupManager *cgroup_manager_new(void) {
    CgroupManager *cm = calloc(1, sizeof(*cm));
    if (!cm) return NULL;

    cm->available = cgroup_manager_is_available();
    if (!cm->available) {
        log_warn("cgroup v2 not mounted — cgroup_manager will fall back to prctl");
    }

    /* Initialize paths */
    snprintf(cm->paths[SLICE_GAME],       sizeof(cm->paths[SLICE_GAME]),
             "%s/game", SLICE_PATH);
    snprintf(cm->paths[SLICE_SYSTEM],     sizeof(cm->paths[SLICE_SYSTEM]),
             "%s/system", SLICE_PATH);
    snprintf(cm->paths[SLICE_BACKGROUND], sizeof(cm->paths[SLICE_BACKGROUND]),
             "%s/background", SLICE_PATH);

    log_info("CgroupManager created: available=%d", cm->available);
    return cm;
}

void cgroup_manager_free(CgroupManager *cm) {
    if (!cm) return;
    log_debug("CgroupManager destroyed");
    free(cm);
}

static int mkdir_recursive(const char *path, mode_t mode) {
    /* Simple mkdir — for cgroup hierarchy, parents should already exist */
    if (mkdir(path, mode) < 0 && errno != EEXIST) {
        log_error("mkdir %s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        log_warn("write %s: %s", path, strerror(errno));
        return -1;
    }
    size_t len = strlen(value);
    ssize_t n = write(fd, value, len);
    close(fd);
    if (n < 0 || (size_t)n != len) {
        log_warn("write %s: partial/write error: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static char *read_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return NULL;
    buf[n] = '\0';

    char *result = strdup(buf);
    return result;
}

int cgroup_manager_init(CgroupManager *cm) {
    if (!cm || !cm->available) return -1;

    /* Create slice directory */
    if (mkdir_recursive(SLICE_PATH, 0755) < 0)
        return -1;

    /* Create sub-slices */
    for (int i = 0; i < SLICE_NUM; i++) {
        if (mkdir_recursive(cm->paths[i], 0755) < 0)
            return -1;
    }

    /* Enable cpu and cpuset controllers in subtree_control */
    write_file(SLICE_PATH "/cgroup.subtree_control", "+cpu +cpuset");

    /* Set root slice CPU weight */
    write_file(SLICE_PATH "/cpu.weight", "100");

    log_info("CgroupManager initialized: slice=%s", SLICE_PATH);
    return 0;
}

int cgroup_manager_assign_pid(CgroupManager *cm, pid_t pid, CgroupSlice slice) {
    if (!cm || slice < 0 || slice >= SLICE_NUM) return -1;

    if (cm->available) {
        /* Write PID to cgroup.procs in the target slice */
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/cgroup.procs", cm->paths[slice]);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", pid);
        return write_file(path, buf);
    }

    /* Fallback: just log, no cgroup available */
    log_warn("cgroup v2 not available — cannot assign PID %d to slice %d", pid, slice);
    return -1;
}

int cgroup_manager_set_slice_cpus(CgroupManager *cm, CgroupSlice slice,
                                   uint64_t cpu_mask) {
    if (!cm || slice < 0 || slice >= SLICE_NUM) return -1;

    if (cm->available) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/cpuset.cpus", cm->paths[slice]);
        char buf[64];
        snprintf(buf, sizeof(buf), "0-%lu", (unsigned long)(cpu_mask));
        return write_file(path, buf);
    }
    return -1;
}

int cgroup_manager_set_slice_weight(CgroupManager *cm, CgroupSlice slice,
                                     int weight) {
    if (!cm || slice < 0 || slice >= SLICE_NUM) return -1;
    if (weight < 1) weight = 1;
    if (weight > 10000) weight = 10000;

    if (cm->available) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/cpu.weight", cm->paths[slice]);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", weight);
        return write_file(path, buf);
    }
    return -1;
}

int cgroup_manager_set_slice_uclamp(CgroupManager *cm, CgroupSlice slice,
                                     int uclamp_min, int uclamp_max) {
    if (!cm || slice < 0 || slice >= SLICE_NUM) return -1;
    if (uclamp_min < 0) uclamp_min = 0;
    if (uclamp_min > 1024) uclamp_min = 1024;
    if (uclamp_max < 0) uclamp_max = 0;
    if (uclamp_max > 1024) uclamp_max = 1024;

    if (cm->available) {
        char path_min[MAX_PATH_LEN], path_max[MAX_PATH_LEN];
        snprintf(path_min, sizeof(path_min), "%s/cpu.uclamp.min", cm->paths[slice]);
        snprintf(path_max, sizeof(path_max), "%s/cpu.uclamp.max", cm->paths[slice]);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d", uclamp_min);
        write_file(path_min, buf);
        snprintf(buf, sizeof(buf), "%d", uclamp_max);
        write_file(path_max, buf);
    }
    return 0;
}

const char *cgroup_manager_get_path(const CgroupManager *cm, CgroupSlice slice) {
    if (!cm || slice < 0 || slice >= SLICE_NUM) return "(null)";
    return cm->paths[slice];
}

int cgroup_manager_set_pid_uclamp(pid_t pid, int uclamp_min, int uclamp_max) {
    /* Fallback: set uClamp per-thread via prctl + sched_setattr.
     * This is less elegant than cgroup v2 but works without cgroup support. */
    (void)pid;  /* Future: implement sched_setattr syscall */
    log_debug("cgroup_manager_set_pid_uclamp: PID %d uclamp=[%d, %d] (stub)",
              pid, uclamp_min, uclamp_max);
    return -1;  /* Not yet implemented */
}
