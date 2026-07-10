#ifndef UPERF_DBUS_INTERFACE_H
#define UPERF_DBUS_INTERFACE_H

#include <glib.h>
#include <glib-object.h>

/* Maximum number of clusters and CPUs tracked */
#define DBUS_MAX_CLUSTERS  8
#define DBUS_MAX_CPUS      16

/* Opaque DBus manager handle */
typedef struct DbusManager DbusManager;

/* Create and register the DBus manager.
 * bus_type: G_BUS_TYPE_SYSTEM or G_BUS_TYPE_SESSION
 * Returns NULL on failure. */
DbusManager *dbus_manager_new(GBusType bus_type);

/* Free DBus manager and unregister from bus. */
void dbus_manager_free(DbusManager *mgr);

/* Update current power mode. Triggers ModeChanged signal.
 * mode: "balance", "powersave", or "performance" */
void dbus_manager_set_mode(DbusManager *mgr, const char *mode);

/* Update current scene state. Triggers SceneChanged signal.
 * scene: "idle", "touch", "trigger", "gesture", "junk", "switch", "boost" */
void dbus_manager_set_scene(DbusManager *mgr, const char *scene);

/* Update CPU frequency data (in MHz).
 * freqs: array of nr_clusters doubles
 * nr_clusters: number of clusters (max DBUS_MAX_CLUSTERS) */
void dbus_manager_update_frequencies(DbusManager *mgr,
                                      const double *freqs,
                                      int nr_clusters);

/* Update CPU load data (percent 0-100).
 * loads: array of nr_cpus doubles
 * nr_cpus: number of CPUs (max DBUS_MAX_CPUS) */
void dbus_manager_update_loads(DbusManager *mgr,
                                const double *loads,
                                int nr_cpus);

/* Update heavy load state. Triggers HeavyLoadStateChanged signal. */
void dbus_manager_set_heavy_load(DbusManager *mgr, gboolean active);

/* Update game process list.
 * processes: array of GameProcessEntry
 * nr: number of entries */
typedef struct {
    pid_t  pid;
    gchar *comm;
    gchar *cmdline;
    gchar *package;
    gchar *mode;
} GameProcessEntry;

void dbus_manager_update_games(DbusManager *mgr,
                                const GameProcessEntry *processes,
                                int nr);

/* Get the current mode string (for DBus property getter). */
const char *dbus_manager_get_mode(const DbusManager *mgr);

/* Get the current scene string. */
const char *dbus_manager_get_scene(const DbusManager *mgr);

/* Called periodically (e.g., every 500ms) to emit StatsUpdated signal.
 * freqs: per-cluster frequency in MHz
 * loads: per-CPU load percentage
 * Returns TRUE if signal was emitted. */
gboolean dbus_manager_emit_stats(DbusManager *mgr,
                                  const double *freqs, int nr_clusters,
                                  const double *loads, int nr_cpus);

/* Called from main.c during initialization to connect the mode setter
 * to the daemon's internal mode-changing logic.
 * set_mode_callback: function pointer that actually changes the mode.
 * user_data: opaque pointer passed to callback. */
typedef void (*DbusSetModeFunc)(const char *mode, void *user_data);
void dbus_manager_set_mode_handler(DbusManager *mgr,
                                    DbusSetModeFunc callback,
                                    void *user_data);

#endif /* UPERF_DBUS_INTERFACE_H */
