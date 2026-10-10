#define _GNU_SOURCE
#include "thermal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 256
#endif

#ifndef MAX_NAME_LEN
#define MAX_NAME_LEN 64
#endif

/* Internal thermal manager state */
struct ThermalManager {
    ThermalPolicy policy;
    ThermalZone zones[THERMAL_MAX_ZONES];
    int nr_zones;
    ThermalState current_state;
    ThermalState pending_state;
    int64_t pending_since_ms;
    int max_temp;
};

/* Read a single line from a file, stripping newline */
static int read_line(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    if (!fgets(buf, (int)len, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Strip trailing newline */
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
        buf[--n] = '\0';
    return 0;
}

/* Determine the type of a thermal zone from its type string */
static ThermalZoneType classify_zone(const char *type) {
    if (!type) return THERMAL_ZONE_UNKNOWN;

    if (strstr(type, "gpu"))
        return THERMAL_ZONE_GPU;
    if (strstr(type, "cpu"))
        return THERMAL_ZONE_CPU;
    if (strstr(type, "soc") || strstr(type, "chip"))
        return THERMAL_ZONE_SOC;
    if (strstr(type, "battery") || strstr(type, "batt"))
        return THERMAL_ZONE_BATTERY;
    if (strstr(type, "board") || strstr(type, "pmbus"))
        return THERMAL_ZONE_BOARD;

    return THERMAL_ZONE_UNKNOWN;
}

ThermalManager *thermal_manager_new(const ThermalPolicy *policy) {
    ThermalManager *tm = calloc(1, sizeof(*tm));
    if (!tm) return NULL;

    if (policy) {
        tm->policy = *policy;
    } else {
        tm->policy = thermal_default_policy();
    }

    tm->nr_zones = 0;
    tm->current_state = THERMAL_NORMAL;
    tm->pending_state = THERMAL_NORMAL;
    tm->pending_since_ms = -1;
    tm->max_temp = 0;

    log_info("ThermalManager created: warn=%d throttle=%d critical=%d recovery=%d",
             tm->policy.warn_temp, tm->policy.throttle_temp,
             tm->policy.critical_temp, tm->policy.recovery_temp);
    return tm;
}

void thermal_manager_free(ThermalManager *tm) {
    if (!tm) return;
    log_debug("ThermalManager destroyed (%d zones)", tm->nr_zones);
    free(tm);
}

int thermal_manager_discover_zones(ThermalManager *tm) {
    if (!tm) return -1;

    DIR *dir = opendir("/sys/class/thermal/thermal_zone0");
    if (!dir) {
        log_warn("thermal: no thermal zones found (not running on kernel with thermal framework)");
        return 0;
    }
    closedir(dir);

    tm->nr_zones = 0;

    /* Scan /sys/class/thermal/thermal_zoneN/ */
    DIR *thermal_dir = opendir("/sys/class/thermal");
    if (!thermal_dir) {
        log_error("thermal: cannot open /sys/class/thermal: %s", strerror(errno));
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(thermal_dir)) != NULL && tm->nr_zones < THERMAL_MAX_ZONES) {
        /* Only process directories named thermal_zoneN */
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0)
            continue;

        char *endptr;
        long zone_num = strtol(ent->d_name + 12, &endptr, 10);
        if (*endptr != '\0') continue;  /* Not a valid number */

        /* Build paths */
        char type_path[MAX_PATH_LEN];
        char temp_path[MAX_PATH_LEN];
        char name_path[MAX_PATH_LEN];
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/thermal_zone%d/type", (int)zone_num);
        snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/thermal_zone%d/temp", (int)zone_num);
        snprintf(name_path, sizeof(name_path), "/sys/class/thermal/thermal_zone%d/name", (int)zone_num);

        /* Read zone type */
        char type_str[THERMAL_NAME_LEN] = {0};
        if (read_line(type_path, type_str, sizeof(type_str)) < 0)
            continue;  /* Skip unreadable zones */

        /* Read zone name */
        char name_str[THERMAL_NAME_LEN] = {0};
        read_line(name_path, name_str, sizeof(name_str));

        /* Read temperature */
        char temp_str[32] = {0};
        int temp_milli = 0;
        if (read_line(temp_path, temp_str, sizeof(temp_str)) == 0) {
            temp_milli = atoi(temp_str);
        }

        /* Store zone info */
        ThermalZone *zone = &tm->zones[tm->nr_zones];
        zone->id = (int)zone_num;
        snprintf(zone->name, sizeof(zone->name), "%s", name_str);
        snprintf(zone->type, sizeof(zone->type), "%s", type_str);
        zone->temp_millidegC = temp_milli;
        zone->zone_type = classify_zone(type_str);
        zone->valid = true;

        log_debug("thermal: zone[%d] %s (%s) = %d.%03d C",
                   zone->id, zone->name, zone->type,
                   temp_milli / 1000, temp_milli % 1000);

        tm->nr_zones++;
    }

    closedir(thermal_dir);
    log_info("thermal: discovered %d thermal zone(s)", tm->nr_zones);
    return tm->nr_zones;
}

/* A zone is only trusted if it is one of the CPU, CPU-subsystem, GPU or
 * SoC-package sensors. The device exposes 49 zones and several of them are
 * either not temperature sensors in any useful sense or are still warming up,
 * so folding them all into one maximum lets a single bad sample drive the
 * whole policy. */
static bool thermal_zone_is_trusted(const char *type) {
    static const char *prefixes[] = {
        "cpu", "cpuss", "gpuss", "soc", "gpu", "tsens", "cluster", "aoss",
    };
    if (!type || !type[0]) return false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(type, prefixes[i], n) == 0) return true;
    }
    return false;
}

ThermalState thermal_manager_update(ThermalManager *tm) {
    if (!tm || tm->nr_zones == 0) return THERMAL_NORMAL;

    int prev_max = tm->max_temp;
    int max_trusted = 0;
    int nr_used = 0;
    int nr_rejected = 0;

    for (int i = 0; i < tm->nr_zones; i++) {
        ThermalZone *zone = &tm->zones[i];

        /* Re-read temperature */
        char temp_path[MAX_PATH_LEN];
        snprintf(temp_path, sizeof(temp_path),
                 "/sys/class/thermal/thermal_zone%d/temp", zone->id);
        char temp_str[32];
        if (read_line(temp_path, temp_str, sizeof(temp_str)) == 0) {
            zone->temp_millidegC = atoi(temp_str);
        }

        int t = zone->temp_millidegC;

        /* Plausibility. A millidegree reading below zero or above 150 C is a
         * sensor glitch, not a temperature this part can reach. */
        if (t <= 0 || t >= 150000) {
            nr_rejected++;
            continue;
        }

        if (!thermal_zone_is_trusted(zone->type)) {
            nr_rejected++;
            continue;
        }

        /* A single sample that leaps more than 25 C away from the previous
         * aggregate is discarded: real silicon does not move that fast between
         * two polls, so it is a transient read rather than a real excursion.
         * The first poll after start-up has no previous value and is accepted. */
        if (prev_max > 0 && t > prev_max + 25000) {
            nr_rejected++;
            continue;
        }

        if (t > max_trusted) max_trusted = t;
        nr_used++;
    }

    if (nr_used > 0) {
        tm->max_temp = max_trusted;
    } else if (prev_max > 0) {
        /* Everything was filtered out: keep the last good value rather than
         * falling back to zero, which would look like a cool device. */
        tm->max_temp = prev_max;
    }
    (void)nr_rejected;


    ThermalState proposed = thermal_policy_next_state(
        &tm->policy, tm->current_state, tm->max_temp);
    struct timespec now;
    int64_t now_ms = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    ThermalState state = thermal_debounce_transition(
        tm->current_state, proposed, &tm->pending_state,
        &tm->pending_since_ms, now_ms);

    /* Log state changes */
    if (state != tm->current_state) {
        log_info("Thermal state changed: %s -> %s (%d C max)",
                 thermal_state_to_string(tm->current_state),
                 thermal_state_to_string(state),
                 tm->max_temp / 1000);
    }

    tm->current_state = state;
    return state;
}

ThermalState thermal_policy_next_state(const ThermalPolicy *policy,
                                       ThermalState current,
                                       int max_temp) {
    if (!policy) return THERMAL_NORMAL;
    ThermalState measured = THERMAL_NORMAL;
    if (max_temp >= policy->critical_temp)
        measured = THERMAL_CRITICAL;
    else if (max_temp >= policy->throttle_temp)
        measured = THERMAL_THROTTLED;
    else if (max_temp >= policy->warn_temp)
        measured = THERMAL_WARNING;

    if (measured >= current) return measured;

    /* Retain each state until temperature has fallen far enough below its
     * entry threshold. recovery_temp is the configured throttle exit point;
     * warning uses a small fixed deadband. */
    if (current == THERMAL_CRITICAL &&
        max_temp >= policy->critical_temp - 3000)
        return THERMAL_CRITICAL;
    if (current >= THERMAL_THROTTLED && measured < THERMAL_THROTTLED &&
        max_temp >= policy->recovery_temp)
        return THERMAL_THROTTLED;
    if (current >= THERMAL_WARNING && measured == THERMAL_NORMAL &&
        max_temp >= policy->warn_temp - 3000)
        return THERMAL_WARNING;
    return measured;
}

ThermalState thermal_debounce_transition(ThermalState current,
                                         ThermalState proposed,
                                         ThermalState *pending,
                                         int64_t *pending_since_ms,
                                         int64_t now_ms) {
    if (!pending || !pending_since_ms || proposed < THERMAL_NORMAL ||
        proposed >= THERMAL_NUM_STATES)
        return current;

    if (proposed == current) {
        *pending = current;
        *pending_since_ms = -1;
        return current;
    }

    /* Never delay the emergency state. */
    if (proposed == THERMAL_CRITICAL) {
        *pending = proposed;
        *pending_since_ms = -1;
        return proposed;
    }

    if (*pending != proposed || *pending_since_ms < 0 ||
        now_ms < *pending_since_ms) {
        *pending = proposed;
        *pending_since_ms = now_ms;
        return current;
    }

    int64_t dwell_ms;
    if (proposed < current)
        dwell_ms = 2000; /* Recover only after a stable cool period. */
    else if (proposed >= THERMAL_THROTTLED)
        dwell_ms = 500;
    else
        dwell_ms = 1000;

    if (now_ms - *pending_since_ms < dwell_ms)
        return current;

    *pending = proposed;
    *pending_since_ms = -1;
    return proposed;
}

int thermal_manager_zone_count(const ThermalManager *tm) {
    return tm ? tm->nr_zones : 0;
}

const ThermalZone *thermal_manager_get_zone(const ThermalManager *tm, int index) {
    if (!tm || index < 0 || index >= tm->nr_zones) return NULL;
    return &tm->zones[index];
}

int thermal_manager_get_max_temp(const ThermalManager *tm) {
    return tm ? tm->max_temp : 0;
}

ThermalState thermal_manager_get_state(const ThermalManager *tm) {
    return tm ? tm->current_state : THERMAL_NORMAL;
}

float thermal_manager_get_reduction_factor(const ThermalManager *tm) {
    if (!tm) return 0.0f;

    switch (tm->current_state) {
        case THERMAL_NORMAL:
            return 0.0f;  /* No reduction needed */

        case THERMAL_WARNING:
            /* Mild reduction: reduce target frequency by 10-20% */
            return 0.15f;

        case THERMAL_THROTTLED:
            /* Significant reduction: reduce by 30-50% */
            /* Scale based on how far above throttle_temp we are */
            if (tm->max_temp >= tm->policy.critical_temp - 5000) {
                return 0.5f;  /* Near critical -- aggressive reduction */
            }
            return 0.3f;

        case THERMAL_CRITICAL:
            /* Emergency: reduce to minimum, may need to kill processes */
            return 0.8f;

        default:
            return 0.0f;
    }
}

bool thermal_manager_has_zone_type(const ThermalManager *tm, ThermalZoneType type) {
    if (!tm) return false;
    for (int i = 0; i < tm->nr_zones; i++) {
        if (tm->zones[i].zone_type == type)
            return true;
    }
    return false;
}

int thermal_manager_find_zone_by_type(const ThermalManager *tm, ThermalZoneType type) {
    if (!tm) return -1;
    for (int i = 0; i < tm->nr_zones; i++) {
        if (tm->zones[i].zone_type == type)
            return i;
    }
    return -1;
}

const char *thermal_state_to_string(ThermalState state) {
    switch (state) {
        case THERMAL_NORMAL:     return "NORMAL";
        case THERMAL_WARNING:    return "WARNING";
        case THERMAL_THROTTLED:  return "THROTTLED";
        case THERMAL_CRITICAL:   return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

ThermalPolicy thermal_default_policy(void) {
    /* Defaults for SM8750 (Snapdragon 8 Elite), taken from the passive trip
     * points the kernel registers for this SoC.
     *
     * The CPU package zones expose trip2 = 95 C (passive, 8 C hysteresis) and
     * trip3 = 120 C (passive); hot and critical sit at 125 C. Every other zone
     * (47 of them, GPU included) only publishes a 135 C placeholder.
     *
     * So 95 C is where the kernel itself starts pulling the frequency down, and
     * the policy has to act before that rather than at 80 C: throttling at 80 C
     * leaves the top ~40% of the frequency range unused on a part that is
     * comfortable well above it. The recovery point keeps a 7 C gap so the
     * state cannot oscillate across the boundary. */
    ThermalPolicy policy = {0};
    policy.warn_temp       = 85000;   /* 85 C: just under the 95 C kernel trip */
    policy.throttle_temp   = 95000;   /* 95 C: matches trip2, the real passive point */
    policy.critical_temp   = 120000;  /* 120 C: matches trip3 */
    policy.recovery_temp   = 88000;   /* 88 C: 7 C of hysteresis, no oscillation */
    return policy;
}

ThermalPolicy thermal_conservative_policy(void) {
    /* More conservative thresholds for battery-powered devices */
    ThermalPolicy policy = {0};
    policy.warn_temp       = 60000;   /* 60 C */
    policy.throttle_temp   = 70000;   /* 70 C */
    policy.critical_temp   = 85000;   /* 85 C */
    policy.recovery_temp   = 65000;   /* 65 C */
    return policy;
}
