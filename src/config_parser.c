#include "config.h"
#include "log.h"

#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>

/* ----------------------------------------------------------------
 * JSON parsing helpers
 * ---------------------------------------------------------------- */

static int parse_int_field(struct json_object *obj, const char *field, int *out) {
    struct json_object *jo;
    if (!json_object_object_get_ex(obj, field, &jo))
        return -1;
    *out = (int)json_object_get_int(jo);
    return 0;
}

static double parse_double_field(struct json_object *obj, const char *field, double *out) {
    struct json_object *jo;
    if (!json_object_object_get_ex(obj, field, &jo))
        return -1;
    *out = json_object_get_double(jo);
    return 0;
}

static const char *parse_string_field(struct json_object *obj, const char *field,
                                       const char **out) {
    struct json_object *jo;
    if (!json_object_object_get_ex(obj, field, &jo))
        return "missing";
    *out = json_object_get_string(jo);
    return NULL;  /* OK */
}

static bool parse_bool_field(struct json_object *obj, const char *field, bool def) {
    struct json_object *jo;
    if (!json_object_object_get_ex(obj, field, &jo))
        return def;
    return json_object_get_boolean(jo);
}

/* ----------------------------------------------------------------
 * Power model parsing
 * ---------------------------------------------------------------- */

static int parse_power_model(struct json_object *mod_obj, Config *cfg) {
    struct json_object *arr;
    if (!json_object_object_get_ex(mod_obj, "powerModel", &arr)) {
        log_error("Missing 'powerModel' array in cpu module");
        return -1;
    }

    int nr = json_object_array_length(arr);
    if (nr <= 0 || nr > MAX_CLUSTERS) {
        log_error("Invalid powerModel array length: %d (expected 1-%d)", nr, MAX_CLUSTERS);
        return -1;
    }

    cfg->cpu.nr_clusters = nr;
    for (int i = 0; i < nr; i++) {
        struct json_object *entry = json_object_array_get_idx(arr, i);
        PowerModelEntry *pm = &cfg->cpu.power_model[i];

        memset(pm, 0, sizeof(*pm));

        if (parse_int_field(entry, "efficiency", &pm->efficiency) < 0) {
            log_error("powerModel[%d]: missing 'efficiency'", i);
            return -1;
        }
        if (parse_int_field(entry, "nr", &pm->nr_cores) < 0) {
            log_error("powerModel[%d]: missing 'nr'", i);
            return -1;
        }
        if (parse_double_field(entry, "typicalPower", &pm->typical_power_w) < 0)
            pm->typical_power_w = 1.0;
        if (parse_double_field(entry, "typicalFreq", &pm->typical_freq_mhz) < 0)
            pm->typical_freq_mhz = 1000.0;
        if (parse_double_field(entry, "sweetFreq", &pm->sweet_freq_mhz) < 0)
            pm->sweet_freq_mhz = pm->typical_freq_mhz * 0.7;
        if (parse_double_field(entry, "plainFreq", &pm->plain_freq_mhz) < 0)
            pm->plain_freq_mhz = pm->typical_freq_mhz * 0.5;
        if (parse_double_field(entry, "freeFreq", &pm->free_freq_mhz) < 0)
            pm->free_freq_mhz = pm->typical_freq_mhz * 0.25;

        log_debug("powerModel[%d]: eff=%d nr=%d typP=%.1fW typF=%.0fMHz sweet=%.0f plain=%.0f free=%.0f",
                   i, pm->efficiency, pm->nr_cores,
                   pm->typical_power_w, pm->typical_freq_mhz,
                   pm->sweet_freq_mhz, pm->plain_freq_mhz, pm->free_freq_mhz);
    }
    return 0;
}

/* ----------------------------------------------------------------
 * Preset / ActionParams parsing
 * ---------------------------------------------------------------- */

static int parse_action_params(struct json_object *obj, ActionParams *ap) {
    memset(ap, 0, sizeof(*ap));

    /* Helper lambda-style: iterate all top-level keys in the preset object */
    struct json_object *key, *val;
    json_object_object_foreach(obj, k, v) {
        if (strncmp(k, "cpu.", 4) != 0)
            continue;
        const char *sub = k + 4;
        double d = json_object_get_double(v);
        if (strcmp(sub, "latencyTime") == 0)
            ap->latency_time = (float)d;
        else if (strcmp(sub, "slowLimitPower") == 0)
            ap->slow_limit_power = (float)d;
        else if (strcmp(sub, "fastLimitPower") == 0)
            ap->fast_limit_power = (float)d;
        else if (strcmp(sub, "fastLimitCapacity") == 0)
            ap->fast_limit_capacity = (float)d;
        else if (strcmp(sub, "fastLimitRecoverScale") == 0)
            ap->fast_limit_recover_scale = (float)d;
        else if (strcmp(sub, "margin") == 0)
            ap->margin = (float)d;
        else if (strcmp(sub, "burst") == 0)
            ap->burst = (float)d;
        else if (strcmp(sub, "guideCap") == 0)
            ap->guide_cap = (bool)d;
        else if (strcmp(sub, "limitEfficiency") == 0)
            ap->limit_efficiency = (bool)d;
        else if (strcmp(sub, "baseSampleTime") == 0)
            ap->base_sample_time = (float)d;
        else if (strcmp(sub, "baseSlackTime") == 0)
            ap->base_slack_time = (float)d;
        else if (strcmp(sub, "predictThd") == 0)
            ap->predict_thd = (float)d;
    }
    return 0;
}

/* ----------------------------------------------------------------
 * Sysfs knobs parsing
 * ---------------------------------------------------------------- */

static int parse_sysfs_knobs(struct json_object *mod_obj, Config *cfg) {
    struct json_object *knobs_obj;
    if (!json_object_object_get_ex(mod_obj, "knob", &knobs_obj)) {
        log_warn("sysfs module: no 'knob' object found");
        return 0;  /* Optional */
    }

    struct json_object *key, *val;
    int idx = 0;
    json_object_object_foreach(knobs_obj, k, v) {
        if (idx >= MAX_KNOBS) {
            log_warn("sysfs: too many knobs (max %d), truncating", MAX_KNOBS);
            break;
        }
        KnobDef *knob = &cfg->sysfs.knobs[idx];
        strncpy(knob->name, k, MAX_NAME_LEN - 1);
        knob->name[MAX_NAME_LEN - 1] = '\0';
        strncpy(knob->path, json_object_get_string(v), MAX_PATH_LEN - 1);
        knob->path[MAX_PATH_LEN - 1] = '\0';
        knob->enabled = true;
        idx++;
        log_debug("sysfs knob[%d]: %s = %s", idx - 1, knob->name, knob->path);
    }
    cfg->sysfs.nr_knobs = idx;
    return 0;
}

/* ----------------------------------------------------------------
 * Sched rules parsing
 * ---------------------------------------------------------------- */

static AffinityClass parse_affinity_class(const char *s) {
    if (!s) return AC_AUTO;
    if (strcmp(s, "bg") == 0)   return AC_BG;
    if (strcmp(s, "norm") == 0) return AC_NORM;
    if (strcmp(s, "coop") == 0) return AC_COOP;
    if (strcmp(s, "ui") == 0)   return AC_UI;
    if (strcmp(s, "rtusr") == 0) return AC_RTUSR;
    if (strcmp(s, "all") == 0)  return AC_NORM;
    return AC_AUTO;
}

static int parse_sched_rules(struct json_object *mod_obj, Config *cfg) {
    struct json_object *rules_arr;
    if (!json_object_object_get_ex(mod_obj, "rules", &rules_arr))
        return 0;  /* Optional */

    int nr = json_object_array_length(rules_arr);
    if (nr > MAX_RULES) nr = MAX_RULES;

    cfg->sched.nr_rules = nr;
    for (int i = 0; i < nr; i++) {
        struct json_object *rule_obj = json_object_array_get_idx(rules_arr, i);
        SchedRule *rule = &cfg->sched.rules[i];
        memset(rule, 0, sizeof(*rule));

        const char *name_str = NULL;
        parse_string_field(rule_obj, "name", &name_str);
        strncpy(rule->name, name_str ? name_str : "unnamed", MAX_NAME_LEN - 1);

        const char *regex_str = NULL;
        parse_string_field(rule_obj, "regex", &regex_str);
        strncpy(rule->regex, regex_str ? regex_str : ".*", MAX_PATH_LEN - 1);

        rule->pinned = parse_bool_field(rule_obj, "pinned", false);

        /* Parse nested rules array for affinity/priority */
        struct json_object *nested;
        if (json_object_object_get_ex(rule_obj, "rules", &nested)) {
            int nn = json_object_array_length(nested);
            if (nn > 0) {
                struct json_object *first = json_object_array_get_idx(nested, 0);
                const char *ac_str = NULL;
                parse_string_field(first, "ac", &ac_str);
                rule->affinity_class = parse_affinity_class(ac_str);

                const char *pc_str = NULL;
                parse_string_field(first, "pc", &pc_str);
                /* pc maps to priority profile — simplified: map to fg priority */
                if (strcmp(pc_str, "bg") == 0)   rule->prio_profile.fg = -3;
                else if (strcmp(pc_str, "norm") == 0) rule->prio_profile.fg = -1;
                else if (strcmp(pc_str, "coop") == 0) rule->prio_profile.fg = 124;
                else if (strcmp(pc_str, "ui") == 0)   rule->prio_profile.fg = 120;
                else if (strcmp(pc_str, "rtusr") == 0) rule->prio_profile.fg = 98;
                else                                   rule->prio_profile.fg = 0;
            }
        }

        log_debug("sched rule[%d]: %s regex=%s pinned=%d ac=%d prio_fg=%d",
                   i, rule->name, rule->regex, rule->pinned,
                   rule->affinity_class, rule->prio_profile.fg);
    }
    return 0;
}

/* ----------------------------------------------------------------
 * Presets (balance/powersave/performance) parsing
 * ---------------------------------------------------------------- */

static PowerMode mode_from_string(const char *s) {
    if (!s) return MODE_BALANCE;
    if (strcmp(s, "balance") == 0)       return MODE_BALANCE;
    if (strcmp(s, "powersave") == 0)     return MODE_POWERSAVE;
    if (strcmp(s, "performance") == 0)   return MODE_PERFORMANCE;
    if (strcmp(s, "fast") == 0)          return MODE_FAST;
    return MODE_BALANCE;
}

static int parse_presets(struct json_object *cfg_obj, Config *out) {
    struct json_object *presets_obj;
    if (!json_object_object_get_ex(cfg_obj, "presets", &presets_obj)) {
        log_error("Missing 'presets' object in config");
        return -1;
    }

    /* Iterate each power mode preset */
    struct json_object *pname, *pval;
    json_object_object_foreach(presets_obj, mode_name, mode_obj) {
        PowerMode mode = mode_from_string(mode_name);
        if (mode >= MODE_NUM) {
            log_warn("Unknown preset mode: %s, skipping", mode_name);
            continue;
        }

        PowerModePreset *preset = &out->presets[mode];
        strncpy(preset->name, mode_name, MAX_NAME_LEN - 1);

        /* Each mode has state sub-objects: idle, touch, trigger, etc. */
        struct json_object *sname, *sval;
        json_object_object_foreach(mode_obj, state_name, state_obj) {
            /* Parse global defaults (*) */
            if (strcmp(state_name, "*") == 0) {
                parse_action_params(state_obj, &preset->presets.global);
                continue;
            }

            /* Map state name to enum */
            SceneState scene;
            if (strcmp(state_name, "idle") == 0)      scene = SCENE_IDLE;
            else if (strcmp(state_name, "touch") == 0) scene = SCENE_TOUCH;
            else if (strcmp(state_name, "trigger") == 0) scene = SCENE_TRIGGER;
            else if (strcmp(state_name, "gesture") == 0) scene = SCENE_GESTURE;
            else if (strcmp(state_name, "junk") == 0)  scene = SCENE_JUNK;
            else if (strcmp(state_name, "switch") == 0) scene = SCENE_SWITCH;
            else if (strcmp(state_name, "boost") == 0) scene = SCENE_BOOST;
            else continue;

            /* Parse state-specific params */
            parse_action_params(state_obj,
                &preset->presets.actions[scene]);
        }
    }
    return 0;
}

/* ----------------------------------------------------------------
 * Initials parsing
 * ---------------------------------------------------------------- */

static int parse_initials(struct json_object *cfg_obj, Config *out) {
    struct json_object *initials_obj;
    if (!json_object_object_get_ex(cfg_obj, "initials", &initials_obj))
        return 0;  /* Optional */

    struct json_object *cpu_obj;
    if (json_object_object_get_ex(initials_obj, "cpu", &cpu_obj)) {
        out->initial_latency_time       = 0.2f;
        out->initial_slow_limit_power   = 3.0f;
        out->initial_fast_limit_power   = 6.0f;
        out->initial_fast_limit_capacity = 10.0f;
        out->initial_fast_limit_recover_scale = 0.3f;
        out->initial_margin             = 0.25f;
        out->initial_burst              = 0.0f;
        out->initial_guide_cap          = false;
        out->initial_limit_efficiency   = false;
        out->initial_base_sample_time   = 0.01f;
        out->initial_base_slack_time    = 0.01f;
        out->initial_predict_thd        = 0.3f;

        struct json_object *key, *val;
        json_object_object_foreach(cpu_obj, k, v) {
            double d = json_object_get_double(v);
            if (strcmp(k, "latencyTime") == 0)       out->initial_latency_time = (float)d;
            else if (strcmp(k, "slowLimitPower") == 0) out->initial_slow_limit_power = (float)d;
            else if (strcmp(k, "fastLimitPower") == 0) out->initial_fast_limit_power = (float)d;
            else if (strcmp(k, "fastLimitCapacity") == 0) out->initial_fast_limit_capacity = (float)d;
            else if (strcmp(k, "fastLimitRecoverScale") == 0) out->initial_fast_limit_recover_scale = (float)d;
            else if (strcmp(k, "margin") == 0) out->initial_margin = (float)d;
            else if (strcmp(k, "burst") == 0) out->initial_burst = (float)d;
            else if (strcmp(k, "guideCap") == 0) out->initial_guide_cap = (bool)d;
            else if (strcmp(k, "limitEfficiency") == 0) out->initial_limit_efficiency = (bool)d;
            else if (strcmp(k, "baseSampleTime") == 0) out->initial_base_sample_time = (float)d;
            else if (strcmp(k, "baseSlackTime") == 0) out->initial_base_slack_time = (float)d;
            else if (strcmp(k, "predictThd") == 0) out->initial_predict_thd = (float)d;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------
 * Meta parsing
 * ---------------------------------------------------------------- */

static int parse_meta(struct json_object *cfg_obj, Config *out) {
    struct json_object *meta_obj;
    if (!json_object_object_get_ex(cfg_obj, "meta", &meta_obj))
        return 0;  /* Optional */

    const char *name = NULL;
    if (json_object_object_get_ex(meta_obj, "name", &name))
        strncpy(out->meta_name, name, MAX_NAME_LEN - 1);

    const char *author = NULL;
    if (json_object_object_get_ex(meta_obj, "author", &author))
        strncpy(out->meta_author, author, MAX_NAME_LEN - 1);

    return 0;
}

/* ----------------------------------------------------------------
 * Switcher / Input module parsing
 * ---------------------------------------------------------------- */

static int parse_switcher(struct json_object *cfg_obj, Config *out) {
    struct json_object *mod_obj;
    if (!json_object_object_get_ex(cfg_obj, "modules", &mod_obj))
        return -1;

    struct json_object *sw_obj;
    if (json_object_object_get_ex(mod_obj, "switcher", &sw_obj)) {
        const char *s = NULL;
        if (json_object_object_get_ex(sw_obj, "switchInode", &s))
            strncpy(out->switcher.switch_inode, s, MAX_PATH_LEN - 1);
        if (json_object_object_get_ex(sw_obj, "perapp", &s))
            strncpy(out->switcher.perapp_file, s, MAX_PATH_LEN - 1);

        /* hintDuration */
        struct json_object *hd;
        if (json_object_object_get_ex(sw_obj, "hintDuration", &hd)) {
            const char *sn;
            struct json_object *sv;
            json_object_object_foreach(hd, sn, sv) {
                SceneState scene;
                if (strcmp(sn, "idle") == 0)      scene = SCENE_IDLE;
                else if (strcmp(sn, "touch") == 0) scene = SCENE_TOUCH;
                else if (strcmp(sn, "trigger") == 0) scene = SCENE_TRIGGER;
                else if (strcmp(sn, "gesture") == 0) scene = SCENE_GESTURE;
                else if (strcmp(sn, "junk") == 0)  scene = SCENE_JUNK;
                else if (strcmp(sn, "switch") == 0) scene = SCENE_SWITCH;
                else if (strcmp(sn, "boost") == 0) scene = SCENE_BOOST;
                else continue;
                out->switcher.hint_duration[scene] = json_object_get_double(sv);
            }
        }
    }

    /* Input module */
    struct json_object *inp_obj;
    if (json_object_object_get_ex(mod_obj, "input", &inp_obj)) {
        out->input.enable = parse_bool_field(inp_obj, "enable", true);
        out->input.swipe_thd = 0.03f;
        out->input.gesture_thd_x = 0.03f;
        out->input.gesture_thd_y = 0.03f;
        out->input.gesture_delay_time = 2.0f;
        out->input.hold_enter_time = 1.0f;

        const char *s;
        if (json_object_object_get_ex(inp_obj, "swipeThd", &s))
            out->input.swipe_thd = json_object_get_double(s);
        if (json_object_object_get_ex(inp_obj, "gestureThdX", &s))
            out->input.gesture_thd_x = json_object_get_double(s);
        if (json_object_object_get_ex(inp_obj, "gestureThdY", &s))
            out->input.gesture_thd_y = json_object_get_double(s);
        if (json_object_object_get_ex(inp_obj, "gestureDelayTime", &s))
            out->input.gesture_delay_time = json_object_get_double(s);
        if (json_object_object_get_ex(inp_obj, "holdEnterTime", &s))
            out->input.hold_enter_time = json_object_get_double(s);
    }

    return 0;
}

/* ----------------------------------------------------------------
 * Main entry: config_load
 * ---------------------------------------------------------------- */

int config_load(Config *cfg, const char *path) {
    memset(cfg, 0, sizeof(*cfg));

    /* Defaults */
    cfg->switcher.hint_duration[SCENE_IDLE]      = 0.0f;
    cfg->switcher.hint_duration[SCENE_TOUCH]     = 4.0f;
    cfg->switcher.hint_duration[SCENE_TRIGGER]   = 0.03f;
    cfg->switcher.hint_duration[SCENE_GESTURE]   = 0.1f;
    cfg->switcher.hint_duration[SCENE_JUNK]      = 0.06f;
    cfg->switcher.hint_duration[SCENE_SWITCH]    = 0.4f;
    cfg->switcher.hint_duration[SCENE_BOOST]     = 0.0f;

    strncpy(cfg->switcher.switch_inode, "/run/uperf-linux/cur_powermode", MAX_PATH_LEN - 1);
    strncpy(cfg->switcher.perapp_file, "/etc/uperf-linux/perapp_powermode", MAX_PATH_LEN - 1);

    strncpy(cfg->meta_name, "unknown", MAX_NAME_LEN - 1);
    strncpy(cfg->meta_author, "unknown", MAX_NAME_LEN - 1);

    /* Parse JSON */
    char *err_msg = NULL;
    struct json_object *root = json_parse_file(path, &err_msg);
    if (!root) {
        log_error("Failed to parse JSON config '%s': %s", path, err_msg ?: "(unknown)");
        free(err_msg);
        return -1;
    }

    /* Walk the top-level structure */
    if (parse_meta(root, cfg) < 0)               goto err;
    if (parse_switcher(root, cfg) < 0)           goto err;
    if (parse_power_model(root, cfg) < 0)        goto err;
    if (parse_sysfs_knobs(root, cfg) < 0)        goto err;
    if (parse_sched_rules(root, cfg) < 0)        goto err;
    if (parse_presets(root, cfg) < 0)            goto err;
    if (parse_initials(root, cfg) < 0)           goto err;

    json_object_put(root);
    log_info("Config loaded: %s by %s (%d clusters, %d knobs, %d rules)",
             cfg->meta_name, cfg->meta_author,
             cfg->cpu.nr_clusters, cfg->sysfs.nr_knobs, cfg->sched.nr_rules);
    return 0;

err:
    json_object_put(root);
    log_error("Config parse failed for '%s'", path);
    return -1;
}

/* ----------------------------------------------------------------
 * config_validate
 * ---------------------------------------------------------------- */

int config_validate(const Config *cfg) {
    if (cfg->cpu.nr_clusters <= 0) {
        log_error("Validation failed: no power model clusters defined");
        return -1;
    }

    /* Verify all clusters have positive frequencies */
    for (int i = 0; i < cfg->cpu.nr_clusters; i++) {
        const PowerModelEntry *pm = &cfg->cpu.power_model[i];
        if (pm->typical_freq_mhz <= 0) {
            log_error("Validation failed: cluster %d typicalFreq must be > 0", i);
            return -1;
        }
        if (pm->efficiency <= 0) {
            log_error("Validation failed: cluster %d efficiency must be > 0", i);
            return -1;
        }
    }

    /* Verify at least one power mode preset exists */
    bool has_preset = false;
    for (int m = 0; m < MODE_NUM; m++) {
        if (cfg->presets[m].name[0] != '\0') {
            has_preset = true;
            break;
        }
    }
    if (!has_preset) {
        log_error("Validation failed: no power mode presets defined");
        return -1;
    }

    log_info("Config validation passed (%d clusters, %d modes)",
             cfg->cpu.nr_clusters, MODE_NUM);
    return 0;
}

/* ----------------------------------------------------------------
 * config_check_paths
 * ---------------------------------------------------------------- */

int config_check_paths(const Config *cfg) {
    int missing = 0;
    int total = 0;

    /* Check sysfs knobs */
    for (int i = 0; i < cfg->sysfs.nr_knobs; i++) {
        const KnobDef *knob = &cfg->sysfs.knobs[i];
        if (!knob->enabled) continue;
        total++;

        /* Try opening for write to check accessibility */
        int fd = open(knob->path, O_WRONLY);
        if (fd < 0) {
            if (errno == ENOENT) {
                log_warn("Config path not found: %s (%s)", knob->path, knob->name);
                missing++;
            } else {
                log_error("Cannot write to config path: %s (%s): %s",
                          knob->path, knob->name, strerror(errno));
                /* EACCES is fatal — we can't function without sysfs access */
                return -1;
            }
        } else {
            close(fd);
        }
    }

    /* Check switch inode directory */
    const char *dir = strrchr(cfg->switcher.switch_inode, '/');
    if (dir) {
        char dirpath[MAX_PATH_LEN];
        strncpy(dirpath, cfg->switcher.switch_inode, dir - cfg->switcher.switch_inode + 1);
        dirpath[dir - cfg->switcher.switch_inode + 1] = '\0';
        struct stat st;
        if (stat(dirpath, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_warn("Switch inode directory does not exist: %s (will be created at runtime)", dirpath);
        }
    }

    if (missing > 0) {
        log_warn("%d/%d sysfs paths missing (non-fatal: they may appear at runtime)",
                 missing, total);
    } else if (total > 0) {
        log_info("All %d sysfs paths verified OK", total);
    }

    return 0;
}

void config_free(Config *cfg) {
    (void)cfg;
    /* Currently no dynamically allocated resources in Config,
     * but this is here for future-proofing. */
}
