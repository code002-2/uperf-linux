#include "state_machine.h"
#include "log.h"

#include <string.h>
#include <math.h>

/* Internal state machine implementation */
struct StateMachine {
    SceneState    current_scene;
    PowerMode     current_mode;
    uint64_t      enter_time_ms;  /* monotonic ms when we entered current scene */

    /* Per-mode, per-scene action parameters (populated from presets at init) */
    ActionParams actions[MODE_NUM][SCENE_NUM_STATES];

    /* Config reference */
    const Config *cfg;

    /* Per-mode, per-scene action parameters */
    ActionParams actions[MODE_NUM][SCENE_NUM_STATES];

    /* Hint durations (seconds) */
    float hint_duration[SCENE_NUM_STATES];

    /* Burst slack / debounce */
    float request_burst_slack_ms;
    uint64_t last_boost_exit_ms;
    bool heavy_load_active;

    /* Load tracking for boost transitions */
    float current_load;
    float load_history[16];
    int   load_hist_idx;
};

/* Map SceneState to actions array index */
static int scene_idx(SceneState s) { return (int)s; }
static int mode_idx(PowerMode m) { return (int)m; }

/* Get the ActionParams for the current (mode, scene) pair */
static ActionParams *get_action_params(StateMachine *sm, SceneState scene) {
    (void)sm;
    (void)scene;
    /* TODO: return from cfg->presets[sm->current_mode].presets.actions[scene] */
    /* For now, return a static zero-initialized params */
    return NULL;
}

StateMachine *state_machine_new(const Config *cfg) {
    StateMachine *sm = calloc(1, sizeof(*sm));
    if (!sm) return NULL;

    sm->cfg = cfg;
    sm->current_scene = SCENE_IDLE;
    sm->current_mode = MODE_BALANCE;
    sm->heavy_load_active = false;
    sm->last_boost_exit_ms = 0;
    sm->request_burst_slack_ms = 3000.0f;
    sm->load_hist_idx = 0;
    memset(sm->load_history, 0, sizeof(sm->load_history));

    /* Copy hint durations from config */
    for (int i = 0; i < SCENE_NUM_STATES; i++) {
        sm->hint_duration[i] = cfg->switcher.hint_duration[i];
    }

    /* Initialize actions from presets */
    for (int m = 0; m < MODE_NUM; m++) {
        for (int s = 0; s < SCENE_NUM_STATES; s++) {
            memset(&sm->actions[m][s], 0, sizeof(ActionParams));
            /* TODO: populate from cfg->presets[m].presets.actions[s] */
        }
    }

    log_info("StateMachine created: mode=%d scene=%d",
             sm->current_mode, sm->current_scene);
    return sm;
}

void state_machine_free(StateMachine *sm) {
    if (!sm) return;
    log_debug("StateMachine destroyed (was in scene %d)", sm->current_scene);
    free(sm);
}

void state_machine_tick(StateMachine *sm) {
    uint64_t now_ms = (uint64_t)sm->hint_duration[0];  /* Placeholder */
    (void)now_ms;

    /* Check hint duration timeouts */
    /* In a full implementation, we'd compare now_ms against enter_time_ms
     * and transition if exceeded. For now, this is a no-op stub. */
}

SceneState state_machine_feed_event(StateMachine *sm, EventType evt) {
    SceneState old = sm->current_scene;
    SceneState next = old;

    switch (old) {
        case SCENE_IDLE:
            switch (evt) {
                case EVT_TOUCH_DOWN:
                    next = SCENE_TOUCH;
                    break;
                case EVT_WINDOW_SWITCH:
                    next = SCENE_SWITCH;
                    break;
                default:
                    break;
            }
            break;

        case SCENE_TOUCH:
            switch (evt) {
                case EVT_TOUCH_UP:
                    next = SCENE_TRIGGER;
                    break;
                case EVT_GESTURE:
                    next = SCENE_GESTURE;
                    break;
                case EVT_JUNK:
                    next = SCENE_JUNK;
                    break;
                case EVT_WINDOW_SWITCH:
                    next = SCENE_SWITCH;
                    break;
                case EVT_TIMEOUT:
                    next = SCENE_IDLE;
                    break;
                default:
                    break;
            }
            break;

        case SCENE_TRIGGER:
        case SCENE_JUNK:
            switch (evt) {
                case EVT_TIMEOUT:
                case EVT_TOUCH_UP:
                    next = SCENE_TOUCH;
                    break;
                case EVT_WINDOW_SWITCH:
                    next = SCENE_SWITCH;
                    break;
                default:
                    break;
            }
            break;

        case SCENE_GESTURE:
            switch (evt) {
                case EVT_TIMEOUT:
                    next = SCENE_TOUCH;
                    break;
                case EVT_WINDOW_SWITCH:
                    next = SCENE_SWITCH;
                    break;
                default:
                    break;
            }
            break;

        case SCENE_SWITCH:
            if (evt == EVT_TIMEOUT)
                next = SCENE_TOUCH;
            break;

        case SCENE_BOOST:
            switch (evt) {
                case EVT_HEAVY_LOAD_END:
                    next = SCENE_TOUCH;
                    break;
                case EVT_TIMEOUT:
                    next = SCENE_IDLE;
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }

    if (next != old) {
        log_debug("State transition: %d -> %d (event=%d)",
                   old, next, evt);
        sm->current_scene = next;
    }

    return next;
}

SceneState state_machine_get_scene(const StateMachine *sm) {
    return sm->current_scene;
}

PowerMode state_machine_get_mode(const StateMachine *sm) {
    return sm->current_mode;
}

void state_machine_set_mode(StateMachine *sm, PowerMode mode) {
    if (mode == sm->current_mode) return;

    log_info("Power mode changed: %d -> %d", sm->current_mode, mode);
    sm->current_mode = mode;

    /* Reset to idle in the new mode */
    sm->current_scene = SCENE_IDLE;
    sm->heavy_load_active = false;
    sm->last_boost_exit_ms = 0;
}

void state_machine_get_actions(const StateMachine *sm, ActionParams *out) {
    SceneState scene = sm->current_scene;
    PowerMode  mode  = sm->current_mode;

    ActionParams *src = get_action_params((StateMachine *)sm, scene);
    if (src) {
        memcpy(out, src, sizeof(*out));
    } else {
        memset(out, 0, sizeof(*out));
    }

    /* Apply initial defaults as base */
    out->latency_time       = sm->cfg->initial_latency_time;
    out->slow_limit_power   = sm->cfg->initial_slow_limit_power;
    out->fast_limit_power   = sm->cfg->initial_fast_limit_power;
    out->fast_limit_capacity = sm->cfg->initial_fast_limit_capacity;
    out->fast_limit_recover_scale = sm->cfg->initial_fast_limit_recover_scale;
    out->margin             = sm->cfg->initial_margin;
    out->burst              = sm->cfg->initial_burst;
    out->guide_cap          = sm->cfg->initial_guide_cap;
    out->limit_efficiency   = sm->cfg->initial_limit_efficiency;
    out->base_sample_time   = sm->cfg->initial_base_sample_time;
    out->base_slack_time    = sm->cfg->initial_base_slack_time;
    out->predict_thd        = sm->cfg->initial_predict_thd;
}

float state_machine_get_hint_duration(const StateMachine *sm, SceneState scene) {
    if (scene < 0 || scene >= SCENE_NUM_STATES)
        return 0.0f;
    return sm->hint_duration[scene];
}

bool state_machine_needs_boost(const StateMachine *sm, float current_load,
                                float heavy_load_threshold) {
    if (sm->heavy_load_active)
        return true;

    /* Check burst slack cooldown */
    if (sm->last_boost_exit_ms > 0) {
        /* TODO: compare against current monotonic time */
        /* For now, skip cooldown check */
    }

    return current_load > heavy_load_threshold;
}
