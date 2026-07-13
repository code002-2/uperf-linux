#ifndef UPERF_DBUS_PROXY_H
#define UPERF_DBUS_PROXY_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#define UPERF_TYPE_DBUS_PROXY (uperf_dbus_proxy_get_type())

typedef struct _DbusProxy     DbusProxy;
typedef struct _DbusProxyClass DbusProxyClass;

struct _DbusProxy {
    GObject parent;
    gchar   *current_mode;
    gchar   *current_scene;
    gdouble  freqs[8];
    int      nr_freqs;
    gdouble  loads[8];
    int      nr_loads;
    gboolean is_heavy_load;
    gint32   max_temp;
    gchar   *thermal_state;
    gchar   *manual_freq_str;
    gchar  **game_comm;
    gint    *game_pid;
    gchar  **game_mode;
    int      nr_games;
    GDBusConnection *bus;
    guint            poll_id;
    guint            stats_poll_id;
    GCallback mode_cb;
    gpointer   mode_ud;
    GCallback scene_cb;
    gpointer   scene_ud;
    GCallback stats_cb;
    gpointer   stats_ud;
    GCallback heavy_cb;
    gpointer   heavy_ud;
    GCallback thermal_cb;
    gpointer   thermal_ud;
};

struct _DbusProxyClass {
    GObjectClass parent_class;
};

GType uperf_dbus_proxy_get_type(void) G_GNUC_CONST;
void  dbus_proxy_start_polling(DbusProxy *self);
void  dbus_proxy_disconnect(DbusProxy *self);
void  dbus_proxy_set_mode_cb(DbusProxy *self, GCallback cb, gpointer ud);
void  dbus_proxy_set_scene_cb(DbusProxy *self, GCallback cb, gpointer ud);
void  dbus_proxy_set_stats_cb(DbusProxy *self, GCallback cb, gpointer ud);
void  dbus_proxy_set_heavy_cb(DbusProxy *self, GCallback cb, gpointer ud);
void  dbus_proxy_set_thermal_cb(DbusProxy *self, GCallback cb, gpointer ud);
gboolean dbus_proxy_set_mode(DbusProxy *self, const gchar *mode);
gboolean dbus_proxy_set_game_mode(DbusProxy *self, gint pid, const gchar *app, const gchar *mode);
gboolean dbus_proxy_apply_settings(DbusProxy *self,
    gdouble heavy_load_thd, gdouble idle_load_thd,
    gdouble sample_time, gdouble burst_slack,
    gdouble latency_time, gdouble margin, gdouble burst,
    gdouble slow_limit, gdouble fast_limit, gdouble fast_limit_cap,
    gdouble warn_temp, gdouble throttle_temp,
    gdouble critical_temp, gdouble recovery_temp);
gboolean dbus_proxy_apply_freq_override(DbusProxy *self,
    gint64 prime, gint64 perf, gint64 eff, gint64 gpu);
gboolean dbus_proxy_release_freq_override(DbusProxy *self);
#endif
