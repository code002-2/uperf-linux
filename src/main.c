#define _GNU_SOURCE
#include "log.h"
#include "config.h"
#include "state_machine.h"
#include "input_monitor.h"
#include "sysfs_writer.h"
#include "cgroup_manager.h"
#include "heavyload_detector.h"
#include "power_model.h"
#include "game_scanner.h"
#include "dbus_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Global state */
static Config g_config;
static StateMachine *g_sm = NULL;
static InputMonitor *g_im = NULL;
static SysfsWriter *g_writer = NULL;
static CgroupManager *g_cm = NULL;
static HeavyLoadDetector *g_detector = NULL;
static GameScanner *g_scanner = NULL;
static DbusManager *g_dbus = NULL;
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_reload_config = 0;

/* Signal handlers */
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) {
        g_running = 0;
    } else if (sig == SIGHUP) {
        g_reload_config = 1;
    }
}

/* Usage / help */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Userspace performance scheduler for Linux ARM64 gaming.\n"
        "Inspired by Uperf-Game-Turbo (Android).\n"
        "\n"
        "Options:\n"
        "  -c, --config FILE    Config file path (default: /etc/uperf-linux/config.json)\n"
        "  -l, --log-level N    Log level 0=debug 1=info 2=warn 3=error 4=fatal\n"
        "  -L, --log-file FILE  Log file path (default: stderr)\n"
        "  -j, --journald       Also log to systemd-journald\n"
        "  -h, --help           Show this help\n"
        "  -v, --version        Show version\n"
        "\n",
        prog);
}

static void print_version(void) {
    printf("uperf-linux v0.1.0\n");
    printf("SM8550 (Snapdragon 8 Gen 2) performance scheduler\n");
    printf("Built: %s %s\n", __DATE__, __TIME__);
}

/* Apply initial sysfs knobs from config */
static void apply_initial_state(void) {
    if (!g_writer) return;

    log_info("Applying initial sysfs state...");

    /* Lock CPU frequency to maximum */
    for (int c = 0; c < g_config.cpu.nr_clusters; c++) {
        char buf[32];
        /* Find the cpufreqMax knob */
        for (int k = 0; k < g_config.sysfs.nr_knobs; k++) {
            KnobDef *knob = &g_config.sysfs.knobs[k];
            if (strcmp(knob->name, "cpufreqMax") == 0) {
                snprintf(buf, sizeof(buf), "%d", 9999000);  /* ~9.9 GHz = max */
                char path[MAX_PATH_LEN];
                snprintf(path, sizeof(path), knob->path, 0);
                sysfs_writer_queue_raw(g_writer, path, buf);
            }
        }
    }

    /* Set governor to schedutil */
    for (int k = 0; k < g_config.sysfs.nr_knobs; k++) {
        KnobDef *knob = &g_config.sysfs.knobs[k];
        if (strcmp(knob->name, "governor") == 0) {
            sysfs_writer_queue_raw(g_writer, knob->path, "schedutil");
        }
    }

    sysfs_writer_flush(g_writer);
}

/* Main event loop */
static int event_loop(void) {
    log_info("Starting main event loop...");

    int poll_interval_ms = 50;  /* Base poll interval */

    while (g_running) {
        /* Handle config reload (from SIGHUP) */
        if (g_reload_config) {
            g_reload_config = 0;
            log_info("SIGHUP received — reloading config...");
            /* TODO: re-parse config and apply */
        }

        /* Sample heavy load */
        if (g_detector) {
            float load = heavyload_detector_sample(g_detector);

            /* Update DBus with current stats */
            if (g_dbus) {
                /* Collect per-CPU frequencies */
                double freqs[8] = {0};
                double loads[8] = {0};
                int nr = 0;
                for (int cpu = 0; cpu < 8; cpu++) {
                    char path[MAX_PATH_LEN];
                    snprintf(path, sizeof(path),
                             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
                    char *val = sysfs_reader_read(path);
                    if (val) {
                        freqs[nr] = atof(val) / 1000.0;  /* kHz → MHz */
                        free(val);
                    }
                    loads[nr] = load;
                    nr++;
                }
                dbus_manager_update_frequencies(g_dbus, freqs, nr);
                dbus_manager_update_loads(g_dbus, loads, nr);
                dbus_manager_set_heavy_load(g_dbus,
                    heavyload_detector_is_heavy(g_detector) ? TRUE : FALSE);
            }

            /* Check if we need to boost */
            if (g_sm && heavyload_detector_is_heavy(g_detector)) {
                state_machine_feed_event(g_sm, EVT_HEAVY_LOAD_START);
            }
        }

        /* Poll input events */
        if (g_im) {
            InputEvent events[16];
            int n = input_monitor_poll(g_im, events, 16);
            for (int i = 0; i < n; i++) {
                if (g_sm) {
                    SceneState new_scene = state_machine_feed_event(g_sm, events[i].type);
                    log_debug("Input event %d → scene %d", events[i].type, new_scene);

                    /* Apply new state's sysfs knobs */
                    ActionParams params;
                    state_machine_get_actions(g_sm, &params);
                    if (g_writer) {
                        sysfs_writer_apply(g_writer, &params,
                                           state_machine_get_mode(g_sm));
                    }
                }
            }
        }

        /* Tick state machine (handles timeouts) */
        if (g_sm) {
            state_machine_tick(g_sm);
        }

        /* Periodic game scan (every 5 seconds) */
        static uint64_t last_scan = 0;
        uint64_t now = (uint64_t)time(NULL);
        if (now - last_scan > 5) {
            last_scan = now;
            if (g_scanner) {
                game_scanner_scan(g_scanner);
            }
        }

        /* Sleep for poll interval */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = (long)poll_interval_ms * 1000000L };
        nanosleep(&ts, NULL);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *config_path = "/etc/uperf-linux/config.json";
    LogLevel log_level = LOG_INFO;
    const char *log_file = NULL;
    int use_journald = 0;

    static struct option long_opts[] = {
        {"config",    required_argument, 0, 'c'},
        {"log-level", required_argument, 0, 'l'},
        {"log-file",  required_argument, 0, 'L'},
        {"journald",  no_argument,       0, 'j'},
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:l:L:jhv", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'l': log_level = (LogLevel)atoi(optarg); break;
            case 'L': log_file = optarg; break;
            case 'j': use_journald = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            case 'v': print_version(); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    /* Initialize logging */
    if (log_init(log_level, use_journald, log_file) < 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }

    log_info("=== uperf-linux starting ===");
    print_version();

    /* Set up signal handlers */
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    /* Load configuration */
    log_info("Loading config: %s", config_path);
    if (config_load(&g_config, config_path) < 0) {
        log_fatal("Failed to load config from '%s'", config_path);
        return 1;
    }

    /* Validate config */
    if (config_validate(&g_config) < 0) {
        log_fatal("Config validation failed");
        return 1;
    }

    /* Check sysfs paths */
    config_check_paths(&g_config);

    /* Initialize components */
    g_sm = state_machine_new(&g_config);
    if (!g_sm) {
        log_fatal("Failed to create state machine");
        return 1;
    }

    g_writer = sysfs_writer_new(&g_config, 1000000);  /* 1ms batch window */
    if (!g_writer) {
        log_warn("Failed to create sysfs writer (continuing without batching)");
    }

    g_cm = cgroup_manager_new();
    if (g_cm) {
        cgroup_manager_init(g_cm);
        /* Set up slice configurations */
        /* Game slice: pin to performance cores, high uClamp */
        cgroup_manager_set_slice_weight(g_cm, SLICE_GAME, 200);
        cgroup_manager_set_slice_uclamp(g_cm, SLICE_GAME, 768, 1024);  /* 75%-100% */
        /* System slice: normal */
        cgroup_manager_set_slice_weight(g_cm, SLICE_SYSTEM, 100);
        cgroup_manager_set_slice_uclamp(g_cm, SLICE_SYSTEM, 0, 512);
        /* Background slice: low priority */
        cgroup_manager_set_slice_weight(g_cm, SLICE_BACKGROUND, 20);
        cgroup_manager_set_slice_uclamp(g_cm, SLICE_BACKGROUND, 0, 256);
    }

    g_detector = heavyload_detector_new(
        10.0f,    /* 10ms sample time */
        60.0f,    /* heavy load threshold (%) */
        20.0f,    /* idle load threshold (%) */
        3000.0f   /* 3 second burst slack */
    );
    if (!g_detector) {
        log_warn("Failed to create heavy load detector");
    }

    g_im = input_monitor_new(
        g_config.input.swipe_thd,
        g_config.input.gesture_thd_x,
        g_config.input.gesture_thd_y,
        g_config.input.gesture_delay_time,
        1080, 2400  /* TODO: read from sysfs or config */
    );
    if (g_im) {
        input_monitor_discover_devices(g_im);
        if (input_monitor_device_count(g_im) == 0) {
            log_warn("No touchscreen devices found — input monitoring disabled");
            input_monitor_free(g_im);
            g_im = NULL;
        }
    }

    g_scanner = game_scanner_new();
    if (g_scanner) {
        game_scanner_scan(g_scanner);
    }

    /* Initialize DBus manager */
    g_dbus = dbus_manager_new(G_BUS_TYPE_SYSTEM);
    if (g_dbus) {
        /* Wire up the mode handler */
        dbus_manager_set_mode_handler(g_dbus,
            [](const char *mode, void *ud) {
                (void)ud;
                (void)mode;
                /* TODO: call state_machine_set_mode(g_sm, mode_from_string(mode)) */
                log_info("DBus requested mode change to: %s", mode);
            },
            NULL);
        log_info("DBus manager initialized on system bus");
    } else {
        log_warn("Failed to initialize DBus manager (GUI will not be able to control daemon)");
    }

    /* Apply initial state */
    apply_initial_state();

    /* Run the main event loop */
    int ret = event_loop();

    /* Cleanup */
    log_info("Shutting down...");
    dbus_manager_free(g_dbus);
    game_scanner_free(g_scanner);
    input_monitor_free(g_im);
    heavyload_detector_free(g_detector);
    cgroup_manager_free(g_cm);
    sysfs_writer_free(g_writer);
    state_machine_free(g_sm);
    config_free(&g_config);
    log_shutdown();

    return ret;
}
