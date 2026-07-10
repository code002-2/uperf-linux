#ifndef UPERF_CONFIG_H
#define UPERF_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "power_model.h"

#define MAX_CLUSTERS     4
#define MAX_KNOBS        64
#define MAX_RULES        32
#define MAX_POWER_MODES  8
#define MAX_GAMES        128
#define MAX_PATH_LEN     256
#define MAX_NAME_LEN     64

/* ---- Knob types for sysfs_writer ---- */
typedef enum {
    KNOB_PERCPU,        /* /sys/.../cpu%d/...  (one write per CPU) */
    KNOB_PERCLUSTER,    /* One value for all CPUs in a cluster */
    KNOB_DEVFREQ,       /* /sys/class/devfreq/... */
    KNOB_UCLAMP,        /* /sys/fs/cgroup/.../cpu.uclamp.* */
    KNOB_STRING,        /* Arbitrary string write (e.g. governor name) */
    KNOB_FILE,          /* Generic file write */
    KNOB_NUM_TYPES
} KnobType;

/* A single configurable sysfs knob */
typedef struct {
    char        name[MAX_NAME_LEN];
    char        path[MAX_PATH_LEN];       /* May contain %d for CPU index */
    KnobType    type;
    bool        enabled;
    char        note[MAX_NAME_LEN];       /* Human-readable description */
} KnobDef;

/* Power mode preset name */
typedef enum {
    MODE_BALANCE = 0,
    MODE_POWERSAVE,
    MODE_PERFORMANCE,
    MODE_FAST,
    MODE_NUM
} PowerMode;

/* CPU frequency / uClamp limits for an action */
typedef struct {
    bool         has_cpu_freq_max;
    int          cpu_freq_max[MAX_CLUSTERS];  /* Hz */
    bool         has_cpu_freq_min;
    int          cpu_freq_min[MAX_CLUSTERS];
    bool         has_gpu_max_freq;
    int          gpu_max_freq;                /* Hz */
    bool         has_gpu_min_freq;
    int          gpu_min_freq;
    bool         has_ddr_max_freq;
    int          ddr_max_freq;
    bool         has_uclamp_min;
    int          uclamp_min[MAX_CLUSTERS];    /* 0-1024 */
    bool         has_uclamp_max;
    int          uclamp_max[MAX_CLUSTERS];
    bool         has_governor;
    char         governor[MAX_NAME_LEN];
    bool         has_sched_boost;
    int          sched_boost_value;
    /* Global tuning params (from initials/presets) */
    float        latency_time;
    float        slow_limit_power;
    float        fast_limit_power;
    float        fast_limit_capacity;
    float        fast_limit_recover_scale;
    float        margin;
    float        burst;
    bool         guide_cap;
    bool         limit_efficiency;
    float        base_sample_time;
    float        base_slack_time;
    float        predict_thd;
} ActionParams;

/* State-specific action overrides */
typedef struct {
    ActionParams global;      /* Applied to all states */
    ActionParams idle;
    ActionParams touch;
    ActionParams trigger;
    ActionParams gesture;
    ActionParams junk;
    ActionParams switch_;
    ActionParams boost;
} StatePresets;

/* A full power-mode preset (balance/powersave/performance) */
typedef struct {
    char                        name[MAX_NAME_LEN];
    StatePresets                presets;
    /* Global CPU tuning params */
    float                       latency_time;      /* seconds */
    float                       slow_limit_power;  /* Watts */
    float                       fast_limit_power;  /* Watts */
    float                       fast_limit_capacity;
    float                       fast_limit_recover_scale;
    float                       margin;
    float                       burst;
    bool                        guide_cap;
    bool                        limit_efficiency;
    /* Sampling params */
    float                       base_sample_time;  /* seconds */
    float                       base_slack_time;
    float                       predict_thd;
} PowerModePreset;

/* Switcher / hint configuration */
typedef struct {
    char switch_inode[MAX_PATH_LEN];
    char perapp_file[MAX_PATH_LEN];
    float hint_duration[8];  /* idle, touch, trigger, gesture, junk, switch, boost, _ */
} SwitcherConfig;

/* Input module configuration */
typedef struct {
    bool    enable;
    float   swipe_thd;
    float   gesture_thd_x;
    float   gesture_thd_y;
    float   gesture_delay_time;
    float   hold_enter_time;
} InputConfig;

/* Scheduling affinity class name */
typedef enum {
    AC_AUTO = 0,
    AC_BG,
    AC_NORM,
    AC_COOP,
    AC_UI,
    AC_RTUSR,
    AC_NUM_CLASSES
} AffinityClass;

/* Per-rule scheduling priority profile */
typedef struct {
    int bg;
    int fg;
    int idle;
    int touch;
    int boost;
} SchedPrioProfile;

/* CPU mask group */
typedef struct {
    char    name[MAX_NAME_LEN];
    int     cpus[MAX_CLUSTERS * 4];  /* flat list of CPU indices */
    int     nr_cpus;
} CpuMaskGroup;

/* A single sched rule: regex + affinity class + priority profile */
typedef struct {
    char   name[MAX_NAME_LEN];
    char   regex[MAX_PATH_LEN];
    bool   pinned;
    AffinityClass affinity_class;
    SchedPrioProfile prio_profile;
} SchedRule;

/* Sched module configuration */
typedef struct {
    bool enable;
    CpuMaskGroup cpumasks[MAX_CLUSTERS];
    int nr_cpumasks;
    SchedRule rules[MAX_RULES];
    int nr_rules;
} SchedConfig;

/* CPU module configuration */
typedef struct {
    bool enable;
    PowerModelEntry power_model[MAX_CLUSTERS];
    int nr_clusters;
} CpuConfig;

/* Sysfs module configuration */
typedef struct {
    bool enable;
    KnobDef knobs[MAX_KNOBS];
    int nr_knobs;
} SysfsConfig;

/* Full config — the union of all module configs + presets */
typedef struct {
    char meta_name[MAX_NAME_LEN];
    char meta_author[MAX_NAME_LEN];

    SwitcherConfig  switcher;
    InputConfig     input;
    CpuConfig       cpu;
    SysfsConfig     sysfs;
    SchedConfig     sched;

    PowerModePreset presets[MODE_NUM];

    /* Initial defaults (applied at startup before any state) */
    float initial_latency_time;
    float initial_slow_limit_power;
    float initial_fast_limit_power;
    float initial_fast_limit_capacity;
    float initial_fast_limit_recover_scale;
    float initial_margin;
    float initial_burst;
    bool  initial_guide_cap;
    bool  initial_limit_efficiency;
    float initial_base_sample_time;
    float initial_base_slack_time;
    float initial_predict_thd;
} Config;

/* Load config from JSON file. Returns 0 on success, -1 on error. */
int config_load(Config *cfg, const char *path);

/* Validate config after loading. Returns 0 if valid, -1 otherwise. */
int config_validate(const Config *cfg);

/* Check that all referenced sysfs paths exist and are writable.
 * Returns 0 if all paths OK, -1 otherwise (logs warnings for missing ones). */
int config_check_paths(const Config *cfg);

/* Free any resources held by config (none currently, but for future-proofing). */
void config_free(Config *cfg);

#endif /* UPERF_CONFIG_H */
