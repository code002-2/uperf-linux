#include "../src/include/state_machine.h"
#include "../src/include/config.h"
#include "../src/include/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  TEST: %s ... ", #name); \
    name(); \
} while(0)
#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL (%s: expected %d, got %d)\n", msg, (b), (a)); \
        tests_failed++; return; \
    } \
} while(0)
#define ASSERT_PASS(msg) do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

/* Create a minimal config for state machine tests */
static Config *make_test_config(void) {
    Config *cfg = calloc(1, sizeof(*cfg));
    cfg->cpu.nr_clusters = 3;
    cfg->switcher.hint_duration[SCENE_IDLE] = 0.0f;
    cfg->switcher.hint_duration[SCENE_TOUCH] = 4.0f;
    cfg->switcher.hint_duration[SCENE_TRIGGER] = 0.03f;
    cfg->switcher.hint_duration[SCENE_GESTURE] = 0.1f;
    cfg->switcher.hint_duration[SCENE_JUNK] = 0.06f;
    cfg->switcher.hint_duration[SCENE_SWITCH] = 0.4f;
    cfg->switcher.hint_duration[SCENE_BOOST] = 0.0f;
    cfg->initial_latency_time = 0.2f;
    cfg->initial_slow_limit_power = 3.0f;
    cfg->initial_fast_limit_power = 6.0f;
    cfg->initial_margin = 0.25f;
    return cfg;
}

TEST(test_create_destroy) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);
    if (!sm) { printf("FAIL (null state machine)\n"); tests_failed++; return; }
    state_machine_free(sm);
    ASSERT_PASS("create and destroy");
}

TEST(test_initial_state) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);
    ASSERT_EQ((int)state_machine_get_scene(sm), SCENE_IDLE, "initial scene");
    ASSERT_EQ((int)state_machine_get_mode(sm), MODE_BALANCE, "initial mode");
    ASSERT_PASS("initial state is idle/balance");
}

TEST(test_touch_transition) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    SceneState s = state_machine_feed_event(sm, EVT_TOUCH_DOWN);
    ASSERT_EQ((int)s, SCENE_TOUCH, "touch transition");
    ASSERT_PASS("idle -> touch on TOUCH_DOWN");
}

TEST(test_trigger_on_touch_up) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    state_machine_feed_event(sm, EVT_TOUCH_DOWN);
    SceneState s = state_machine_feed_event(sm, EVT_TOUCH_UP);
    ASSERT_EQ((int)s, SCENE_TRIGGER, "trigger transition");
    ASSERT_PASS("touch -> trigger on TOUCH_UP");
}

TEST(test_gesture_on_swipe) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    state_machine_feed_event(sm, EVT_TOUCH_DOWN);
    SceneState s = state_machine_feed_event(sm, EVT_GESTURE);
    ASSERT_EQ((int)s, SCENE_GESTURE, "gesture transition");
    ASSERT_PASS("touch -> gesture on GESTURE event");
}

TEST(test_switch_on_window_event) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    state_machine_feed_event(sm, EVT_TOUCH_DOWN);
    SceneState s = state_machine_feed_event(sm, EVT_WINDOW_SWITCH);
    ASSERT_EQ((int)s, SCENE_SWITCH, "switch transition");
    ASSERT_PASS("touch -> switch on WINDOW_SWITCH event");
}

TEST(test_mode_change) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    state_machine_feed_event(sm, EVT_TOUCH_DOWN);  /* Go to touch */
    state_machine_set_mode(sm, MODE_PERFORMANCE);

    ASSERT_EQ((int)state_machine_get_mode(sm), MODE_PERFORMANCE, "mode changed");
    ASSERT_EQ((int)state_machine_get_scene(sm), SCENE_IDLE, "reset to idle on mode change");
    ASSERT_PASS("mode change resets to idle");
}

TEST(test_boost_detection) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    bool needs = state_machine_needs_boost(sm, 80.0f, 60.0f);
    ASSERT_EQ(needs, true, "heavy load detected");
    ASSERT_PASS("boost needed when load > threshold");
}

TEST(test_boost_not_needed) {
    Config *cfg = make_test_config();
    StateMachine *sm = state_machine_new(cfg);
    free(cfg);

    bool needs = state_machine_needs_boost(sm, 10.0f, 60.0f);
    ASSERT_EQ(needs, false, "not heavy load");
    ASSERT_PASS("no boost when load < threshold");
}

int main(void) {
    printf("=== state_machine tests ===\n");
    log_init(LOG_WARN, 0, NULL);

    RUN_TEST(test_create_destroy);
    RUN_TEST(test_initial_state);
    RUN_TEST(test_touch_transition);
    RUN_TEST(test_trigger_on_touch_up);
    RUN_TEST(test_gesture_on_swipe);
    RUN_TEST(test_switch_on_window_event);
    RUN_TEST(test_mode_change);
    RUN_TEST(test_boost_detection);
    RUN_TEST(test_boost_not_needed);

    printf("\nResults: %d/%d passed (%d failed)\n",
           tests_passed, tests_run, tests_failed);
    log_shutdown();
    return tests_failed > 0 ? 1 : 0;
}
