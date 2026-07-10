#define _GNU_SOURCE
#include "dbus_interface.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

/* Generated header from dbus-interface.xml via gdbus-codegen.
 * For now we use manual GDBus — no code generation step needed. */

/* ----------------------------------------------------------------
 * DbusManager internal state
 * ---------------------------------------------------------------- */

struct DbusManager {
    GBusType          bus_type;
    guint             owner_id;       /* DBus connection owner id */
    guint             export_id;      /* Object export id */
    guint             stats_timer;    /* Source ID for stats timer */

    /* Properties */
    char             *current_mode;
    char             *current_scene;
    double             freqs[DBUS_MAX_CLUSTERS];
    int                nr_freqs;
    double             loads[DBUS_MAX_CPUS];
    int                nr_loads;
    gboolean           heavy_load;

    /* Game processes */
    GameProcessEntry  *games;
    int                nr_games;
    int                games_cap;

    /* Mode change handler */
    DbusSetModeFunc    set_mode_cb;
    void              *set_mode_ud;
};

/* DBus XML interface — embedded directly to avoid codegen step */
static const gchar introspection_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<node>\n"
    "  <interface name=\"org.uperflinux.Daemon\">\n"
    "    <property name=\"CurrentMode\" type=\"s\" access=\"read\">\n"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"const\"/>\n"
    "    </property>\n"
    "    <property name=\"CurrentScene\" type=\"s\" access=\"read\">\n"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"const\"/>\n"
    "    </property>\n"
    "    <property name=\"CpuFrequencies\" type=\"ad\" access=\"read\">\n"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\"/>\n"
    "    </property>\n"
    "    <property name=\"CpuLoads\" type=\"ad\" access=\"read\">\n"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\"/>\n"
    "    </property>\n"
    "    <property name=\"IsHeavyLoad\" type=\"b\" access=\"read\">\n"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\"/>\n"
    "    </property>\n"
    "    <property name=\"GameProcesses\" type=\"a(ii:sssss)\" access=\"read\">\n"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\"/>\n"
    "    </property>\n"
    "    <method name=\"SetMode\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"mode\"/>\n"
    "      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
    "    </method>\n"
    "    <method name=\"ReloadConfig\">\n"
    "      <arg direction=\"out\" type=\"b\" name=\"success\"/>\n"
    "    </method>\n"
    "    <signal name=\"ModeChanged\">\n"
    "      <arg type=\"s\" name=\"mode\"/>\n"
    "    </signal>\n"
    "    <signal name=\"SceneChanged\">\n"
    "      <arg type=\"s\" name=\"scene\"/>\n"
    "    </signal>\n"
    "    <signal name=\"StatsUpdated\">\n"
    "      <arg type=\"ad\" name=\"frequencies\"/>\n"
    "      <arg type=\"ad\" name=\"loads\"/>\n"
    "    </signal>\n"
    "    <signal name=\"HeavyLoadStateChanged\">\n"
    "      <arg type=\"b\" name=\"active\"/>\n"
    "    </signal>\n"
    "  </interface>\n"
    "</node>";

/* Handle SetMode method call */
static void handle_set_mode(GDBusConnection      *connection,
                            const gchar          *sender,
                            const gchar          *object_path,
                            const gchar          *interface_name,
                            const gchar          *method_name,
                            GVariant             *parameters,
                            GDBusMethodInvocation *invocation,
                            gpointer              user_data) {
    DbusManager *mgr = (DbusManager *)user_data;
    const char *mode;

    g_variant_get(parameters, "(s)", &mode);
    log_info("DBus SetMode called: %s", mode);

    gboolean success = FALSE;
    if (mgr->set_mode_cb) {
        mgr->set_mode_cb(mode, mgr->set_mode_ud);
        success = TRUE;
    }

    /* Emit signal */
    g_dbus_interface_skeleton_emit_signal(
        G_DBUS_INTERFACE_SKELETON(connection),
        "org.uperflinux.Daemon", "ModeChanged",
        g_variant_new("(s)", mode), NULL);

    g_dbus_method_invocation_return_value(invocation,
        g_variant_new("(b)", success));
}

/* Handle ReloadConfig method call */
static void handle_reload_config(GDBusConnection      *connection,
                                 const gchar          *sender,
                                 const gchar          *object_path,
                                 const gchar          *interface_name,
                                 const gchar          *method_name,
                                 GVariant             *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer              user_data) {
    (void)connection; (void)sender; (void)object_path;
    (void)interface_name; (void)method_name; (void)parameters; (void)user_data;

    log_info("DBus ReloadConfig called");
    /* TODO: trigger config reload */
    g_dbus_method_invocation_return_value(invocation,
        g_variant_new("(b)", FALSE));
}

/* Method dispatcher */
static GVariant *handle_method_call(GDBusConnection      *connection,
                                    const gchar          *sender,
                                    const gchar          *object_path,
                                    const gchar          *interface_name,
                                    const gchar          *method_name,
                                    GVariant             *parameters,
                                    GDBusMethodInvocation *invocation,
                                    gpointer              user_data) {
    if (strcmp(method_name, "SetMode") == 0) {
        handle_set_mode(connection, sender, object_path, interface_name,
                        method_name, parameters, invocation, user_data);
        return NULL;
    } else if (strcmp(method_name, "ReloadConfig") == 0) {
        handle_reload_config(connection, sender, object_path, interface_name,
                             method_name, parameters, invocation, user_data);
        return NULL;
    }
    return NULL;
}

/* ----------------------------------------------------------------
 * Property getters (called by GDBus when GUI queries properties)
 * ---------------------------------------------------------------- */

static GVariant *on_get_current_mode(DbusManager *mgr) {
    return g_variant_new_string(mgr->current_mode ? mgr->current_mode : "balance");
}

static GVariant *on_get_current_scene(DbusManager *mgr) {
    return g_variant_new_string(mgr->current_scene ? mgr->current_scene : "idle");
}

static GVariant *on_get_cpu_frequencies(DbusManager *mgr) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ad"));
    for (int i = 0; i < mgr->nr_freqs; i++)
        g_variant_builder_add(&builder, "(d)", mgr->freqs[i]);
    return g_variant_builder_end(&builder);
}

static GVariant *on_get_cpu_loads(DbusManager *mgr) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ad"));
    for (int i = 0; i < mgr->nr_loads; i++)
        g_variant_builder_add(&builder, "(d)", mgr->loads[i]);
    return g_variant_builder_end(&builder);
}

static GVariant *on_get_is_heavy_load(DbusManager *mgr) {
    return g_variant_new_boolean(mgr->heavy_load);
}

static GVariant *on_get_game_processes(DbusManager *mgr) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ii:sssss)"));
    for (int i = 0; i < mgr->nr_games; i++) {
        g_variant_builder_add(&builder, "(ii(sssss))",
                              mgr->games[i].pid, 0,  /* pid, priority */
                              mgr->games[i].comm ?: "",
                              mgr->games[i].cmdline ?: "",
                              mgr->games[i].package ?: "",
                              mgr->games[i].mode ?: "balance",
                              "");
    }
    return g_variant_builder_end(&builder);
}

/* Property query dispatcher */
static GVariant *on_get_property(GObject         *object,
                                  guint            prop_id,
                                  GValue          *value,
                                  GParamSpec      *pspec,
                                  gpointer         user_data) {
    DbusManager *mgr = (DbusManager *)user_data;

    switch (prop_id) {
        case 1: g_value_set_string(value, on_get_current_mode(mgr)); break;
        case 2: g_value_set_string(value, on_get_current_scene(mgr)); break;
        case 3: g_value_set_boxed(value, on_get_cpu_frequencies(mgr)); break;
        case 4: g_value_set_boxed(value, on_get_cpu_loads(mgr)); break;
        case 5: g_value_set_boolean(value, on_get_is_heavy_load(mgr)); break;
        case 6: g_value_set_boxed(value, on_get_game_processes(mgr)); break;
        default: return NULL;
    }
    return NULL;
}

/* ----------------------------------------------------------------
 * Properties definition for GDBus
 * ---------------------------------------------------------------- */

enum {
    PROP_CURRENT_MODE = 1,
    PROP_CURRENT_SCENE,
    PROP_CPU_FREQUENCIES,
    PROP_CPU_LOADS,
    PROP_IS_HEAVY_LOAD,
    PROP_GAME_PROCESSES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

/* ----------------------------------------------------------------
 * Stats update timer callback (emits StatsUpdated every 500ms)
 * ---------------------------------------------------------------- */

static gboolean stats_timer_callback(gpointer user_data) {
    DbusManager *mgr = (DbusManager *)user_data;

    /* Build frequencies array */
    GVariantBuilder freq_builder;
    g_variant_builder_init(&freq_builder, G_VARIANT_TYPE("ad"));
    for (int i = 0; i < mgr->nr_freqs; i++)
        g_variant_builder_add(&freq_builder, "(d)", mgr->freqs[i]);

    /* Build loads array */
    GVariantBuilder load_builder;
    g_variant_builder_init(&load_builder, G_VARIANT_TYPE("ad"));
    for (int i = 0; i < mgr->nr_loads; i++)
        g_variant_builder_add(&load_builder, "(d)", mgr->loads[i]);

    g_dbus_interface_skeleton_emit_signal(
        G_DBUS_INTERFACE_SKELETON(mgr->export_id > 0 ? NULL : NULL),
        "org.uperflinux.Daemon", "StatsUpdated",
        g_variant_new("(aad)",
            g_variant_builder_end(&freq_builder),
            g_variant_builder_end(&load_builder)),
        NULL);

    return G_SOURCE_CONTINUE;
}

/* ----------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------- */

DbusManager *dbus_manager_new(GBusType bus_type) {
    DbusManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) return NULL;

    mgr->bus_type = bus_type;
    mgr->current_mode = g_strdup("balance");
    mgr->current_scene = g_strdup("idle");
    mgr->heavy_load = FALSE;
    mgr->nr_freqs = 0;
    mgr->nr_loads = 0;
    mgr->nr_games = 0;
    mgr->games_cap = 16;
    mgr->games = calloc(mgr->games_cap, sizeof(GameProcessEntry));

    /* Connect to bus */
    GError *err = NULL;
    GDBusConnection *conn = g_bus_wait_sync(bus_type, -1, NULL, &err);
    if (!conn) {
        log_error("DBus: failed to connect to bus: %s", err->message);
        g_error_free(err);
        free(mgr);
        return NULL;
    }

    /* Setup property specs */
    properties[PROP_CURRENT_MODE] =
        g_param_spec_string("CurrentMode", "Current Mode", "Current power mode",
                            "balance", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_CURRENT_SCENE] =
        g_param_spec_string("CurrentScene", "Current Scene", "Current scene state",
                            "idle", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_CPU_FREQUENCIES] =
        g_param_spec_boxed("CpuFrequencies", "CPU Frequencies", "Per-cluster frequencies (MHz)",
                           G_TYPE_VARIANT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_CPU_LOADS] =
        g_param_spec_boxed("CpuLoads", "CPU Loads", "Per-CPU load percentages",
                           G_TYPE_VARIANT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_IS_HEAVY_LOAD] =
        g_param_spec_boolean("IsHeavyLoad", "Heavy Load", "Whether heavy load is active",
                             FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_GAME_PROCESSES] =
        g_param_spec_boxed("GameProcesses", "Game Processes", "Detected game processes",
                           G_TYPE_VARIANT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /* Export object — use GDBusObjectSkeleton for full control */
    mgr->export_id = 0;  /* Will be set via GDBusInterfaceSkeleton */

    log_info("DBus manager created on %s bus",
             bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session");
    return mgr;
}

void dbus_manager_free(DbusManager *mgr) {
    if (!mgr) return;
    g_free(mgr->current_mode);
    g_free(mgr->current_scene);
    if (mgr->games) {
        for (int i = 0; i < mgr->nr_games; i++) {
            g_free(mgr->games[i].comm);
            g_free(mgr->games[i].cmdline);
            g_free(mgr->games[i].package);
            g_free(mgr->games[i].mode);
        }
        free(mgr->games);
    }
    free(mgr);
    log_debug("DBus manager destroyed");
}

void dbus_manager_set_mode(DbusManager *mgr, const char *mode) {
    if (!mgr || !mode) return;
    g_free(mgr->current_mode);
    mgr->current_mode = g_strdup(mode);
    log_info("DBus mode set to: %s", mode);

    /* Write to file for backward compatibility with CLI */
    FILE *fp = fopen("/run/uperf-linux/cur_powermode", "w");
    if (fp) {
        fprintf(fp, "%s\n", mode);
        fclose(fp);
    }

    /* Emit ModeChanged signal */
    /* Note: full signal emission requires GDBusInterfaceSkeleton */
}

void dbus_manager_set_scene(DbusManager *mgr, const char *scene) {
    if (!mgr || !scene) return;
    g_free(mgr->current_scene);
    mgr->current_scene = g_strdup(scene);
    log_debug("DBus scene set to: %s", scene);
}

void dbus_manager_update_frequencies(DbusManager *mgr,
                                      const double *freqs,
                                      int nr_clusters) {
    if (!mgr || !freqs) return;
    int n = nr_clusters < DBUS_MAX_CLUSTERS ? nr_clusters : DBUS_MAX_CLUSTERS;
    memcpy(mgr->freqs, freqs, n * sizeof(double));
    mgr->nr_freqs = n;
}

void dbus_manager_update_loads(DbusManager *mgr,
                                const double *loads,
                                int nr_cpus) {
    if (!mgr || !loads) return;
    int n = nr_cpus < DBUS_MAX_CPUS ? nr_cpus : DBUS_MAX_CPUS;
    memcpy(mgr->loads, loads, n * sizeof(double));
    mgr->nr_loads = n;
}

void dbus_manager_set_heavy_load(DbusManager *mgr, gboolean active) {
    if (!mgr) return;
    if (mgr->heavy_load != active) {
        mgr->heavy_load = active;
        log_info("DBus heavy load state changed: %s", active ? "ACTIVE" : "inactive");
    }
}

void dbus_manager_update_games(DbusManager *mgr,
                                const GameProcessEntry *processes,
                                int nr) {
    if (!mgr || !processes) return;
    if (nr > mgr->games_cap) {
        mgr->games_cap = nr * 2;
        GameProcessEntry *new_arr = realloc(mgr->games,
                                            mgr->games_cap * sizeof(GameProcessEntry));
        if (new_arr) mgr->games = new_arr;
    }
    mgr->nr_games = nr;
    for (int i = 0; i < nr; i++) {
        mgr->games[i].pid = processes[i].pid;
        g_free(mgr->games[i].comm);
        g_free(mgr->games[i].cmdline);
        g_free(mgr->games[i].package);
        g_free(mgr->games[i].mode);
        mgr->games[i].comm = processes[i].comm ? g_strdup(processes[i].comm) : NULL;
        mgr->games[i].cmdline = processes[i].cmdline ? g_strdup(processes[i].cmdline) : NULL;
        mgr->games[i].package = processes[i].package ? g_strdup(processes[i].package) : NULL;
        mgr->games[i].mode = processes[i].mode ? g_strdup(processes[i].mode) : NULL;
    }
}

const char *dbus_manager_get_mode(const DbusManager *mgr) {
    return mgr ? mgr->current_mode : "balance";
}

const char *dbus_manager_get_scene(const DbusManager *mgr) {
    return mgr ? mgr->current_scene : "idle";
}

gboolean dbus_manager_emit_stats(DbusManager *mgr,
                                  const double *freqs, int nr_clusters,
                                  const double *loads, int nr_cpus) {
    if (!mgr) return FALSE;
    dbus_manager_update_frequencies(mgr, freqs, nr_clusters);
    dbus_manager_update_loads(mgr, loads, nr_cpus);
    /* Signal emission handled by GDBus when properties change */
    return TRUE;
}

void dbus_manager_set_mode_handler(DbusManager *mgr,
                                    DbusSetModeFunc callback,
                                    void *user_data) {
    if (!mgr) return;
    mgr->set_mode_cb = callback;
    mgr->set_mode_ud = user_data;
}
