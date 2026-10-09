#include <gtk/gtk.h>
#include <adwaita.h>
#include "dbus_proxy.h"

/* ----------------------------------------------------------------
 * Translation. English is the source language; Simplified Chinese is
 * selected from the environment. A static table keeps the GUI free of
 * gettext and the .mo files nothing else in this project uses.
 * ---------------------------------------------------------------- */
static const char *tr(const char *en) {
    static const struct { const char *en, *zh; } tab[] = {
        { "Prime",                  "超大核" },
        { "Performance",            "性能核" },
        { "Efficiency",             "能效核" },
        { "GPU",                    "图形核心" },
        { "Unknown",                "未知" },
        { "Balance",                "均衡" },
        { "Power Save",             "省电" },
        { "Fast",                   "高性能" },
        { "Normal",                 "正常" },
        { "None",                   "无" },
        { "Heavy load active",      "重负载运行中" },
        { "Dashboard",              "总览" },
        { "Power Mode",             "性能模式" },
        { "Status",                 "状态" },
        { "Active Mode",            "当前模式" },
        { "Scene",                  "场景" },
        { "Load State",             "负载状态" },
        { "Scheduler",              "调度器" },
        { "Thread affinity and cgroup management activity.",
          "线程亲和性与 cgroup 管理活动。" },
        { "Active Foreground PID",  "当前前台 PID" },
        { "Managed Processes / Threads", "纳管进程 / 线程" },
        { "Cluster Frequency",      "簇频率" },
        { "CPU Utilization",        "CPU 占用率" },
        { "Thermal",                "温度" },
        { "Max Temperature",        "最高温度" },
        { "Thermal State",          "温控状态" },
        { "Failed to set power mode", "设置性能模式失败" },
        { "Games",                  "游戏" },
        { "Detected Games",         "已识别的游戏" },
        { "Running game and game-like processes. Assign a per-app power mode.",
          "正在运行的游戏类进程，可为每个应用单独指定性能模式。" },
        { "No games detected",      "未检测到游戏" },
        { "Launch a game and it will appear here.", "启动游戏后就会显示在这里。" },
        { "Frequency",              "频率" },
        { "Manual Frequency Override", "手动频率锁定" },
        { "Lock each cluster to a fixed frequency. Disable to return to automatic scaling.",
          "把每个簇锁定在固定频率。关闭后恢复自动调频。" },
        { "Override Enabled",       "启用频率锁定" },
        { "Apply",                  "应用" },
        { "Release All",            "全部释放" },
        { "Override released",      "已释放频率锁定" },
        { "Frequency override applied", "频率锁定已应用" },
        { "Failed to apply override", "应用频率锁定失败" },
        { "Settings",               "设置" },
        { "Daemon Configuration",   "守护进程配置" },
        { "Edit the JSON with administrator privileges, then reload it here.",
          "用管理员权限编辑 JSON 配置，然后在这里重新加载。" },
        { "Reload",                 "重新加载" },
        { "Configuration reloaded", "配置已重新加载" },
        { "Reload failed",          "重新加载失败" },
        { "Logs",                   "日志" },
        { "Service Journal",        "服务日志" },
        { "Press Refresh to load the latest uperf-linux.service journal.\n",
          "点击「刷新」载入 uperf-linux.service 的最新日志。\n" },
        { "Refresh",                "刷新" },
        { "Clear",                  "清空" },
        { "Unable to start journalctl", "无法启动 journalctl" },
        { "Unable to read journal", "无法读取日志" },
        { "(journal is empty)",     "（日志为空）" },
    };
    static int zh = -1;
    if (zh < 0) {
        const char *lang = g_getenv("LANGUAGE");
        if (!lang || !*lang) lang = g_getenv("LC_ALL");
        if (!lang || !*lang) lang = g_getenv("LC_MESSAGES");
        if (!lang || !*lang) lang = g_getenv("LANG");
        zh = (lang && g_str_has_prefix(lang, "zh")) ? 1 : 0;
    }
    if (!zh)
        return en;
    for (guint i = 0; i < G_N_ELEMENTS(tab); i++)
        if (g_strcmp0(tab[i].en, en) == 0)
            return tab[i].zh;
    return en;
}

/* Power-mode names are their own vocabulary: "Performance" here means the
 * performance preset, not the performance CPU cluster, so it must not share
 * the cluster translations. */
static const char *tr_mode(const char *mode) {
    static const struct { const char *en, *zh; } tab[] = {
        { "Balance",     "均衡模式" },
        { "Power Save",  "省电模式" },
        { "Performance", "高性能模式" },
        { "Fast",        "疾速模式" },
    };
    const char *en = mode;
    if (g_strcmp0(mode, "balance") == 0)          en = "Balance";
    else if (g_strcmp0(mode, "powersave") == 0)   en = "Power Save";
    else if (g_strcmp0(mode, "performance") == 0) en = "Performance";
    else if (g_strcmp0(mode, "fast") == 0)        en = "Fast";
    else return mode;

    /* reuse the language decision from tr() by asking it about a known key */
    if (g_strcmp0(tr("Balance"), "Balance") == 0)
        return en;
    for (guint i = 0; i < G_N_ELEMENTS(tab); i++)
        if (g_strcmp0(tab[i].en, en) == 0)
            return tab[i].zh;
    return en;
}


/* ----------------------------------------------------------------
 * Frequency rows. The ranges come from the hardware, so nothing is
 * tied to a particular SoC: one row per cpufreq policy, ordered by
 * descending maximum to match how the daemon publishes clusters
 * (prime first, then performance, ...), plus the GPU devfreq.
 * ---------------------------------------------------------------- */
typedef struct {
    char    title[64];
    char    unit[8];
    gdouble min, max, def;
} FreqRow;

static gdouble read_sysfs_double(const char *path) {
    char *txt = NULL;
    if (!g_file_get_contents(path, &txt, NULL, NULL))
        return -1.0;
    gdouble v = g_ascii_strtod(txt, NULL);
    g_free(txt);
    return v;
}

static int cmp_row_desc(gconstpointer a, gconstpointer b) {
    const FreqRow *ra = a, *rb = b;
    return (ra->max < rb->max) - (ra->max > rb->max);
}

static int build_freq_rows(FreqRow *rows, int max_rows) {
    int n = 0;
    GDir *d = g_dir_open("/sys/devices/system/cpu/cpufreq", 0, NULL);
    const char *name;
    while (d && (name = g_dir_read_name(d)) && n < max_rows - 1) {
        if (!g_str_has_prefix(name, "policy"))
            continue;
        char base[256], path[320];
        g_snprintf(base, sizeof(base), "/sys/devices/system/cpu/cpufreq/%s", name);
        /* Use the real operating points rather than cpuinfo_min/max_freq: the latter are
         * the hardware limits, and the top one is not always a selectable bin (this
         * device reports 4320 MHz for policy6 while the highest available is 4089.6 MHz).
         * A slider that can reach an invalid value just gets snapped downward. */
        g_snprintf(path, sizeof(path), "%s/scaling_available_frequencies", base);
        char *list = NULL;
        gdouble lo = -1.0, hi = -1.0;
        if (g_file_get_contents(path, &list, NULL, NULL)) {
            gchar **parts = g_strsplit_set(list, " \t\r\n", -1);
            for (gint k = 0; parts[k]; k++) {
                if (!*parts[k])
                    continue;
                gdouble v = g_ascii_strtod(parts[k], NULL);
                if (v <= 0)
                    continue;
                if (lo < 0 || v < lo)
                    lo = v;
                if (hi < 0 || v > hi)
                    hi = v;
            }
            g_strfreev(parts);
            g_free(list);
        }
        if (lo <= 0 || hi <= 0) {
            g_snprintf(path, sizeof(path), "%s/cpuinfo_min_freq", base);
            lo = read_sysfs_double(path);
            g_snprintf(path, sizeof(path), "%s/cpuinfo_max_freq", base);
            hi = read_sysfs_double(path);
        }
        if (lo <= 0 || hi <= 0)
            continue;
        /* Both are in kHz, the unit the sliders use; the daemon wants Hz and
         * on_apply_freq multiplies by 1000. */
        rows[n].min = lo;
        rows[n].max = hi;
        rows[n].def = rows[n].max;
        g_strlcpy(rows[n].unit, "kHz", sizeof(rows[n].unit));
        g_strlcpy(rows[n].title, name, sizeof(rows[n].title));
        n++;
    }
    if (d)
        g_dir_close(d);

    if (n > 1)
        qsort(rows, n, sizeof(FreqRow), cmp_row_desc);
    for (int i = 0; i < n; i++) {
        if (i == 0)
            g_strlcpy(rows[i].title, "Prime", sizeof(rows[i].title));
        else if (i == 1)
            g_strlcpy(rows[i].title, "Performance", sizeof(rows[i].title));
        else
            g_snprintf(rows[i].title, sizeof(rows[i].title), "Efficiency %d", i - 1);
    }

    if (n < max_rows) {
        gdouble lo = read_sysfs_double("/sys/class/devfreq/3d00000.gpu/min_freq");
        gdouble hi = read_sysfs_double("/sys/class/devfreq/3d00000.gpu/max_freq");
        if (lo > 0 && hi > 0) {
            g_strlcpy(rows[n].title, "GPU", sizeof(rows[n].title));
            g_strlcpy(rows[n].unit, "Hz", sizeof(rows[n].unit));
            rows[n].min = lo;
            rows[n].max = hi;
            rows[n].def = hi;
            n++;
        }
    }
    return n;
}

/* ----------------------------------------------------------------
 * uperf-linux GUI — rebuilt around libadwaita idioms.
 *
 * Navigation:  AdwViewStack + AdwViewSwitcher (responsive, with a
 *              selected-state that the old hand-rolled tab bar lacked).
 * Content:     each page groups related data into AdwPreferencesGroup
 *              cards; live values live in AdwActionRow suffixes so the
 *              layout stays stable as numbers change.
 *
 * The DbusProxy backend contract is unchanged (see dbus_proxy.h). All
 * daemon frequencies are reported in MHz; the manual-override API takes
 * CPU values in Hz and GPU in Hz.
 * ---------------------------------------------------------------- */

#define UPERF_MAX_CLUSTERS 8
#define UPERF_MAX_CPUS     8

/* Cluster labels used by the dashboard + override page. The daemon
 * publishes clusters ordered prime → performance → efficiency. */
static const char *const CLUSTER_NAMES[] = { "Prime", "Performance", "Efficiency" };

typedef struct {
    DbusProxy *proxy;

    /* Dashboard live rows */
    GtkWidget  *row_mode;
    GtkWidget  *row_scene;
    GtkWidget  *row_heavy;
    GtkWidget  *row_temp;
    GtkWidget  *row_thermal;
    GtkWidget  *temp_bar;
    GtkWidget  *freq_rows[UPERF_MAX_CLUSTERS];
    GtkWidget  *load_rows[UPERF_MAX_CPUS];

    /* Scheduler card */
    GtkWidget  *row_active_pid;
    GtkWidget  *row_tracked;

    /* Mode selector buttons (linked group) */
    GtkWidget  *mode_buttons[4];

    /* Games */
    GtkWidget  *games_group;
    GtkWidget  *games_placeholder;
    GtkWidget **game_rows;
    int         game_rows_len;

    /* Logs */
    GtkTextView *log_view;

    /* Frequency override */
    GtkWidget    *freq_toggle;
    GtkAdjustment *freq_adj[4];
    GtkWidget    *freq_scale_rows[UPERF_MAX_CLUSTERS + 1];
    guint         nr_freq_rows;
    guint         nr_cpu_rows;   /* CPU clusters, excluding the GPU row */
    gboolean      has_gpu_row;

    /* Toasts */
    AdwToastOverlay *toasts;
} AppState;

static AppState g_app;

static void refresh_games(void);

/* ---------------------------------------------------------------- */

static void toast(const char *text) {
    if (g_app.toasts)
        adw_toast_overlay_add_toast(g_app.toasts, adw_toast_new(text));
}

static const char *mode_display_name(const char *mode) {
    if (!mode) return tr("Unknown");
    if (!g_strcmp0(mode, "balance"))     return tr_mode("balance");
    if (!g_strcmp0(mode, "powersave"))   return tr_mode("powersave");
    if (!g_strcmp0(mode, "performance")) return tr_mode("performance");
    if (!g_strcmp0(mode, "fast"))        return tr_mode("fast");
    return mode;
}

/* ----------------------------------------------------------------
 * Live refresh: pull cached proxy state into the widgets.
 * ---------------------------------------------------------------- */

static void refresh_display(void) {
    if (!g_app.proxy) return;
    DbusProxy *p = g_app.proxy;

    if (g_app.row_mode)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_mode),
                                    mode_display_name(p->current_mode));

    /* Keep the mode selector in sync with the daemon's actual mode. */
    static const char *modes[] = { "balance", "powersave", "performance", "fast" };
    for (int i = 0; i < 4; i++) {
        if (!g_app.mode_buttons[i]) continue;
        gboolean active = !g_strcmp0(p->current_mode, modes[i]);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_app.mode_buttons[i])) != active)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_app.mode_buttons[i]), active);
    }

    if (g_app.row_scene)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_scene),
                                    p->current_scene ? p->current_scene : "—");

    if (g_app.row_heavy) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_heavy),
            p->is_heavy_load ? tr("Heavy load active") : tr("Normal"));
        /* Recolor the row to signal state without a jumping label. */
        if (p->is_heavy_load)
            gtk_widget_add_css_class(g_app.row_heavy, "warning");
        else
            gtk_widget_remove_css_class(g_app.row_heavy, "warning");
    }

    if (g_app.row_temp) {
        char buf[64];
        g_snprintf(buf, sizeof(buf), "%.1f °C", p->max_temp / 1000.0);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_temp), buf);
    }
    if (g_app.temp_bar) {
        double frac = p->max_temp > 0
            ? CLAMP((p->max_temp / 1000.0 - 40.0) / 60.0, 0.0, 1.0) : 0.0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_app.temp_bar), frac);
    }
    if (g_app.row_thermal)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_thermal),
                                    p->thermal_state ? p->thermal_state : "—");

    if (g_app.row_active_pid) {
        char buf[32];
        if (p->active_pid > 0)
            g_snprintf(buf, sizeof(buf), "%d", p->active_pid);
        else
            g_strlcpy(buf, tr("None"), sizeof(buf));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_active_pid), buf);
    }
    if (g_app.row_tracked) {
        char buf[48];
        g_snprintf(buf, sizeof(buf), "%d processes · %d threads",
                   p->tracked_processes, p->tracked_threads);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.row_tracked), buf);
    }

    for (int i = 0; i < p->nr_freqs && i < UPERF_MAX_CLUSTERS; i++) {
        if (!g_app.freq_rows[i]) continue;
        char buf[32];
        g_snprintf(buf, sizeof(buf), "%.2f GHz", p->freqs[i] / 1000.0);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.freq_rows[i]), buf);
    }
    for (int i = 0; i < p->nr_loads && i < UPERF_MAX_CPUS; i++) {
        if (!g_app.load_rows[i]) continue;
        char buf[32];
        g_snprintf(buf, sizeof(buf), "%.0f %%", p->loads[i]);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.load_rows[i]), buf);
    }
}

/* Signal bridges */
static void on_mode_changed(DbusProxy *p, gchar *m, gpointer ud)
{ (void)p; (void)m; (void)ud; refresh_display(); }
static void on_scene_changed(DbusProxy *p, gchar *m, gpointer ud)
{ (void)p; (void)m; (void)ud; refresh_display(); }
static void on_stats_updated(DbusProxy *p, gpointer ud)
{ (void)p; (void)ud; refresh_display(); refresh_games(); }
static void on_heavy_changed(DbusProxy *p, gboolean h, gpointer ud)
{ (void)p; (void)h; (void)ud; refresh_display(); }
static void on_thermal_changed(DbusProxy *p, gint32 t, gpointer ud)
{ (void)p; (void)t; (void)ud; refresh_display(); }

/* Mode selector: a linked toggle group. Only react to user activation,
 * not to programmatic sync in refresh_display(). */
static void on_mode_toggled(GtkToggleButton *btn, gpointer ud) {
    if (!gtk_toggle_button_get_active(btn)) return;
    const char *mode = ud;
    if (g_app.proxy && !dbus_proxy_set_mode(g_app.proxy, mode))
        toast(tr("Failed to set power mode"));
}

/* Games: per-app mode dropdown */
typedef struct { gint pid; gchar *app; } GameTarget;

static void game_target_free(gpointer data, GClosure *closure) {
    (void)closure;
    GameTarget *t = data;
    if (!t) return;
    g_free(t->app);
    g_free(t);
}

static void on_game_mode_selected(GObject *obj, GParamSpec *pspec, gpointer ud) {
    (void)pspec;
    if (!g_app.proxy) return;
    GameTarget *t = ud;
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    static const char *modes[] = { "balance", "powersave", "performance", "fast" };
    if (t && idx < G_N_ELEMENTS(modes))
        dbus_proxy_set_game_mode(g_app.proxy, t->pid, t->app, modes[idx]);
}

/* Logs */
static void on_refresh_logs(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_app.log_view);
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &err,
        "journalctl", "-u", "uperf-linux.service", "-n", "200", "--no-pager", NULL);
    if (!proc) {
        gtk_text_buffer_set_text(buf, err ? err->message : tr("Unable to start journalctl"), -1);
        g_clear_error(&err);
        return;
    }
    gchar *out = NULL;
    if (!g_subprocess_communicate_utf8(proc, NULL, NULL, &out, NULL, &err)) {
        gtk_text_buffer_set_text(buf, err ? err->message : tr("Unable to read journal"), -1);
        g_clear_error(&err);
    } else {
        gtk_text_buffer_set_text(buf, out && *out ? out : tr("(journal is empty)"), -1);
    }
    g_free(out);
    g_object_unref(proc);
}

static void on_clear_logs(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(g_app.log_view), "", -1);
}

/* Settings */
static void on_reload_config(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    if (g_app.proxy && dbus_proxy_reload_config(g_app.proxy))
        toast(tr("Configuration reloaded"));
    else
        toast(tr("Reload failed"));
}

/* Frequency override */
static void on_freq_toggle(GObject *obj, GParamSpec *pspec, gpointer ud) {
    (void)pspec; (void)ud;
    gboolean on = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
    for (int i = 0; i < UPERF_MAX_CLUSTERS + 1; i++)
        if (g_app.freq_scale_rows[i])
            gtk_widget_set_sensitive(g_app.freq_scale_rows[i], on);
}

static void on_apply_freq(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    if (!g_app.proxy || !g_app.freq_toggle) return;
    if (!adw_switch_row_get_active(ADW_SWITCH_ROW(g_app.freq_toggle))) {
        dbus_proxy_release_freq_override(g_app.proxy);
        toast(tr("Override released"));
        return;
    }
    /* CPU rows are in kHz (the daemon wants Hz); the GPU row is already Hz and must not
     * be scaled. The last row is the GPU, so everything before it is a CPU cluster. */
    guint nr_cpu = g_app.nr_cpu_rows;
    gint64 c0 = nr_cpu > 0 ? (gint64)gtk_adjustment_get_value(g_app.freq_adj[0]) * 1000 : 0;
    gint64 c1 = nr_cpu > 1 ? (gint64)gtk_adjustment_get_value(g_app.freq_adj[1]) * 1000 : 0;
    gint64 c2 = nr_cpu > 2 ? (gint64)gtk_adjustment_get_value(g_app.freq_adj[2]) * 1000 : 0;
    gint64 gpu = g_app.has_gpu_row
                     ? (gint64)gtk_adjustment_get_value(g_app.freq_adj[nr_cpu])
                     : 0;
    if (dbus_proxy_apply_freq_override(g_app.proxy, (gint)nr_cpu, c0, c1, c2, gpu))
        toast(tr("Frequency override applied"));
    else {
        adw_switch_row_set_active(ADW_SWITCH_ROW(g_app.freq_toggle), FALSE);
        toast(tr("Failed to apply override"));
    }
}

static void on_release_freq(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    if (g_app.proxy) dbus_proxy_release_freq_override(g_app.proxy);
    if (g_app.freq_toggle)
        adw_switch_row_set_active(ADW_SWITCH_ROW(g_app.freq_toggle), FALSE);
    toast(tr("Override released"));
}

/* ----------------------------------------------------------------
 * Page scaffolding helper: a scrollable AdwPreferencesPage.
 * ---------------------------------------------------------------- */

static GtkWidget *new_prefs_page(const char *title, const char *icon) {
    GtkWidget *page = adw_preferences_page_new();
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), title);
    if (icon)
        adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page), icon);
    return page;
}

static GtkWidget *value_row(GtkWidget *group, const char *title) {
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "—");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return row;
}

/* ----------------------------------------------------------------
 * Dashboard
 * ---------------------------------------------------------------- */

static GtkWidget *create_dashboard_page(void) {
    GtkWidget *page = new_prefs_page(tr("Dashboard"), "speedometer-symbolic");

    /* --- Power mode selector (linked toggle group) --- */
    GtkWidget *mode_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(mode_group), tr("Power Mode"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(mode_group));

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(btn_box, "linked");
    gtk_widget_set_margin_top(btn_box, 4);
    gtk_widget_set_margin_bottom(btn_box, 4);

    static const char *modes[]   = { "balance", "powersave", "performance", "fast" };
    static const char *labels[]  = { "Balance", "Power Save", "Performance", "Fast" };
    GtkWidget *first = NULL;
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = gtk_toggle_button_new_with_label(tr_mode(labels[i]));
        gtk_widget_set_hexpand(b, TRUE);
        if (first)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(first));
        else
            first = b;
        g_signal_connect(b, "toggled", G_CALLBACK(on_mode_toggled), (gpointer)modes[i]);
        g_app.mode_buttons[i] = b;
        gtk_box_append(GTK_BOX(btn_box), b);
    }
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(mode_group), btn_box);

    /* --- Status card --- */
    GtkWidget *status = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(status), tr("Status"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(status));
    g_app.row_mode  = value_row(status, tr("Active Mode"));
    g_app.row_scene = value_row(status, tr("Scene"));
    g_app.row_heavy = value_row(status, tr("Load State"));

    /* --- Scheduler card (affinity / cgroup activity) --- */
    GtkWidget *sched = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(sched), tr("Scheduler"));
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(sched),
        tr("Thread affinity and cgroup management activity."));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(sched));
    g_app.row_active_pid = value_row(sched, tr("Active Foreground PID"));
    g_app.row_tracked    = value_row(sched, tr("Managed Processes / Threads"));

    /* --- Per-cluster frequency card --- */
    GtkWidget *freq = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(freq), tr("Cluster Frequency"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(freq));
    for (int i = 0; i < 3; i++)
        g_app.freq_rows[i] = value_row(freq, tr(CLUSTER_NAMES[i]));

    /* --- Per-CPU utilization card --- */
    GtkWidget *load = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(load), tr("CPU Utilization"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(load));
    for (int i = 0; i < UPERF_MAX_CPUS; i++) {
        char name[16];
        g_snprintf(name, sizeof(name), "CPU %d", i);
        g_app.load_rows[i] = value_row(load, name);
    }

    /* --- Thermal card (with a real progress bar) --- */
    GtkWidget *therm = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(therm), tr("Thermal"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(therm));
    g_app.row_temp    = value_row(therm, tr("Max Temperature"));
    g_app.row_thermal = value_row(therm, tr("Thermal State"));

    g_app.temp_bar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(g_app.temp_bar, TRUE);
    gtk_widget_set_margin_top(g_app.temp_bar, 6);
    gtk_widget_set_margin_bottom(g_app.temp_bar, 6);
    gtk_widget_set_margin_start(g_app.temp_bar, 6);
    gtk_widget_set_margin_end(g_app.temp_bar, 6);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(therm), g_app.temp_bar);

    return page;
}

/* ----------------------------------------------------------------
 * Games
 * ---------------------------------------------------------------- */

static void refresh_games(void) {
    if (!g_app.games_group || !g_app.proxy) return;

    /* Remove previously-added rows. */
    for (int i = 0; i < g_app.game_rows_len; i++) {
        if (g_app.game_rows[i])
            adw_preferences_group_remove(ADW_PREFERENCES_GROUP(g_app.games_group),
                                         g_app.game_rows[i]);
    }
    g_free(g_app.game_rows);
    g_app.game_rows = NULL;
    g_app.game_rows_len = 0;

    int n = g_app.proxy->nr_games;
    gtk_widget_set_visible(g_app.games_placeholder, n == 0);
    if (n == 0) return;

    g_app.game_rows = g_malloc0(n * sizeof(GtkWidget *));
    g_app.game_rows_len = n;

    static const char *choices[] = { "balance", "powersave", "performance", "fast", NULL };

    for (int i = 0; i < n; i++) {
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                      g_app.proxy->game_comm[i]);
        /* Look up the cgroup class the scheduler assigned to this PID. */
        const char *cls = NULL;
        for (int w = 0; w < g_app.proxy->nr_workloads; w++) {
            if (g_app.proxy->wl_pid[w] == g_app.proxy->game_pid[i]) {
                cls = g_app.proxy->wl_class[w];
                break;
            }
        }
        char sub[96];
        if (cls && *cls && g_strcmp0(cls, "—") != 0)
            g_snprintf(sub, sizeof(sub), "PID %d · class: %s",
                       g_app.proxy->game_pid[i], cls);
        else
            g_snprintf(sub, sizeof(sub), "PID %d · unmanaged",
                       g_app.proxy->game_pid[i]);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row),
            gtk_image_new_from_icon_name("applications-games-symbolic"));

        GtkStringList *list = gtk_string_list_new(choices);
        GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
        gtk_widget_set_valign(dd, GTK_ALIGN_CENTER);
        g_object_unref(list);

        const gchar *cur = g_app.proxy->game_mode[i];
        for (guint j = 0; choices[j]; j++)
            if (!g_strcmp0(choices[j], cur)) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), j);
                break;
            }

        GameTarget *t = g_new0(GameTarget, 1);
        t->pid = g_app.proxy->game_pid[i];
        t->app = g_strdup(g_app.proxy->game_comm[i]);
        g_signal_connect_data(dd, "notify::selected",
                              G_CALLBACK(on_game_mode_selected), t,
                              game_target_free, 0);

        adw_action_row_add_suffix(ADW_ACTION_ROW(row), dd);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(g_app.games_group), row);
        g_app.game_rows[i] = row;
    }
}

static GtkWidget *create_games_page(void) {
    GtkWidget *page = new_prefs_page(tr("Games"), "applications-games-symbolic");

    g_app.games_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g_app.games_group),
                                    tr("Detected Games"));
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(g_app.games_group),
        tr("Running game and game-like processes. Assign a per-app power mode."));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(g_app.games_group));

    /* Placeholder shown when nothing is detected. */
    g_app.games_placeholder = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_app.games_placeholder),
                                  tr("No games detected"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.games_placeholder),
                                tr("Launch a game and it will appear here."));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g_app.games_group),
                              g_app.games_placeholder);

    return page;
}

/* ----------------------------------------------------------------
 * Frequency override
 * ---------------------------------------------------------------- */

static GtkWidget *create_frequency_page(void) {
    GtkWidget *page = new_prefs_page(tr("Frequency"), "power-profile-performance-symbolic");

    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group),
                                    tr("Manual Frequency Override"));
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group),
        tr("Lock each cluster to a fixed frequency. Disable to return to automatic scaling."));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(group));

    /* Enable toggle */
    g_app.freq_toggle = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_app.freq_toggle),
                                  tr("Override Enabled"));
    g_signal_connect(g_app.freq_toggle, "notify::active",
                     G_CALLBACK(on_freq_toggle), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), g_app.freq_toggle);

    /* Ranges come from the hardware, not from a per-SoC table. */
    FreqRow cl[UPERF_MAX_CLUSTERS + 1];
    int n_rows = build_freq_rows(cl, G_N_ELEMENTS(cl));

    for (int i = 0; i < n_rows; i++) {
        /* AdwActionRow holds the label; a scale sits in the row body below. */
        GtkWidget *row = adw_action_row_new();
        char title[96];
        g_snprintf(title, sizeof(title), "%s (%s)", tr(cl[i].title), cl[i].unit);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);

        gdouble step = g_strcmp0(cl[i].unit, "Hz") == 0 ? 1000000.0 : 50000.0;
        g_app.freq_adj[i] = gtk_adjustment_new(cl[i].def, cl[i].min, cl[i].max,
                                               step, step * 2, 0);
        GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, g_app.freq_adj[i]);
        gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
        gtk_widget_set_hexpand(scale, TRUE);
        gtk_widget_set_size_request(scale, 260, -1);
        gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), scale);

        gtk_widget_set_sensitive(row, FALSE);  /* until override enabled */
        g_app.freq_scale_rows[i] = row;
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    }
    g_app.nr_freq_rows = n_rows;
    /* the GPU is the last row when present */
    g_app.has_gpu_row = n_rows > 0 && g_strcmp0(cl[n_rows - 1].unit, "Hz") == 0;
    g_app.nr_cpu_rows = g_app.has_gpu_row ? (guint)(n_rows - 1) : (guint)n_rows;

    /* Action buttons */
    GtkWidget *btns = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(btns));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);

    GtkWidget *apply = gtk_button_new_with_label(tr("Apply"));
    gtk_widget_add_css_class(apply, "suggested-action");
    gtk_widget_add_css_class(apply, "pill");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_freq), NULL);
    gtk_box_append(GTK_BOX(hbox), apply);

    GtkWidget *release = gtk_button_new_with_label(tr("Release All"));
    gtk_widget_add_css_class(release, "pill");
    g_signal_connect(release, "clicked", G_CALLBACK(on_release_freq), NULL);
    gtk_box_append(GTK_BOX(hbox), release);

    adw_preferences_group_add(ADW_PREFERENCES_GROUP(btns), hbox);
    return page;
}

/* ----------------------------------------------------------------
 * Settings
 * ---------------------------------------------------------------- */

static GtkWidget *create_settings_page(void) {
    GtkWidget *page = new_prefs_page(tr("Settings"), "emblem-system-symbolic");

    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group),
                                    tr("Daemon Configuration"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(group));

    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                  "/etc/uperf-linux/config.json");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
        tr("Edit the JSON with administrator privileges, then reload it here."));
    GtkWidget *reload = gtk_button_new_with_label(tr("Reload"));
    gtk_widget_set_valign(reload, GTK_ALIGN_CENTER);
    g_signal_connect(reload, "clicked", G_CALLBACK(on_reload_config), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), reload);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);

    return page;
}

/* ----------------------------------------------------------------
 * Logs
 * ---------------------------------------------------------------- */

static GtkWidget *create_logs_page(void) {
    GtkWidget *page = new_prefs_page(tr("Logs"), "text-x-generic-symbolic");

    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), tr("Service Journal"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(group));

    GtkWidget *buf_holder = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(buf_holder),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(buf_holder, -1, 320);
    gtk_widget_add_css_class(buf_holder, "card");

    GtkTextBuffer *buf = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_text(buf,
        tr("Press Refresh to load the latest uperf-linux.service journal.\n"), -1);
    g_app.log_view = GTK_TEXT_VIEW(gtk_text_view_new_with_buffer(buf));
    gtk_text_view_set_editable(g_app.log_view, FALSE);
    gtk_text_view_set_monospace(g_app.log_view, TRUE);
    gtk_text_view_set_wrap_mode(g_app.log_view, GTK_WRAP_WORD_CHAR);
    gtk_widget_set_margin_top(GTK_WIDGET(g_app.log_view), 6);
    gtk_widget_set_margin_bottom(GTK_WIDGET(g_app.log_view), 6);
    gtk_widget_set_margin_start(GTK_WIDGET(g_app.log_view), 6);
    gtk_widget_set_margin_end(GTK_WIDGET(g_app.log_view), 6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(buf_holder),
                                  GTK_WIDGET(g_app.log_view));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), buf_holder);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(hbox, 8);
    GtkWidget *refresh = gtk_button_new_with_label(tr("Refresh"));
    gtk_widget_add_css_class(refresh, "pill");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_logs), NULL);
    gtk_box_append(GTK_BOX(hbox), refresh);
    GtkWidget *clear = gtk_button_new_with_label(tr("Clear"));
    gtk_widget_add_css_class(clear, "pill");
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_logs), NULL);
    gtk_box_append(GTK_BOX(hbox), clear);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), hbox);

    return page;
}

/* ----------------------------------------------------------------
 * Window assembly
 * ---------------------------------------------------------------- */

static void on_activate(GtkApplication *app, gpointer ud) {
    (void)ud;

    GtkWidget *window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "uperf-linux");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 720);

    /* Responsive view stack + switcher (title-bar on desktop, bottom bar
     * on narrow displays via AdwViewSwitcherBar). */
    GtkWidget *view_stack = adw_view_stack_new();

    struct { GtkWidget *(*build)(void); const char *name, *title, *icon; } pages[] = {
        { create_dashboard_page, "dashboard", tr("Dashboard"), "speedometer-symbolic" },
        { create_games_page,     "games",     tr("Games"),     "applications-games-symbolic" },
        { create_frequency_page, "frequency", tr("Frequency"), "power-profile-performance-symbolic" },
        { create_settings_page,  "settings",  tr("Settings"),  "emblem-system-symbolic" },
        { create_logs_page,      "logs",      tr("Logs"),      "text-x-generic-symbolic" },
    };
    for (int i = 0; i < 5; i++) {
        AdwViewStackPage *sp = adw_view_stack_add_titled(
            ADW_VIEW_STACK(view_stack), pages[i].build(),
            pages[i].name, pages[i].title);
        adw_view_stack_page_set_icon_name(sp, pages[i].icon);
    }

    /* Header carries a wide view switcher; on narrow widths a breakpoint
     * hides it and reveals the bottom switcher bar instead. */
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher),
                                ADW_VIEW_STACK(view_stack));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher),
                                 ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);

    GtkWidget *switcher_bar = adw_view_switcher_bar_new();
    adw_view_switcher_bar_set_stack(ADW_VIEW_SWITCHER_BAR(switcher_bar),
                                    ADW_VIEW_STACK(view_stack));

    GtkWidget *toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar), switcher_bar);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), view_stack);

    g_app.toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toast_overlay_set_child(g_app.toasts, toolbar);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window),
                                       GTK_WIDGET(g_app.toasts));

    /* Narrow layout: hide the header switcher, reveal the bottom bar. */
    AdwBreakpoint *bp = adw_breakpoint_new(
        adw_breakpoint_condition_parse("max-width: 500px"));
    GValue v_false = G_VALUE_INIT;
    g_value_init(&v_false, G_TYPE_BOOLEAN);
    g_value_set_boolean(&v_false, FALSE);
    GValue v_true = G_VALUE_INIT;
    g_value_init(&v_true, G_TYPE_BOOLEAN);
    g_value_set_boolean(&v_true, TRUE);
    adw_breakpoint_add_setter(bp, G_OBJECT(switcher), "visible", &v_false);
    adw_breakpoint_add_setter(bp, G_OBJECT(switcher_bar), "reveal", &v_true);
    g_value_unset(&v_false);
    g_value_unset(&v_true);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window), bp);

    refresh_display();
    refresh_games();
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    adw_init();

    DbusProxy *proxy = g_object_new(UPERF_TYPE_DBUS_PROXY, NULL);
    g_app.proxy = proxy;
    dbus_proxy_start_polling(proxy);

    dbus_proxy_set_mode_cb(proxy,    G_CALLBACK(on_mode_changed),    NULL);
    dbus_proxy_set_scene_cb(proxy,   G_CALLBACK(on_scene_changed),   NULL);
    dbus_proxy_set_stats_cb(proxy,   G_CALLBACK(on_stats_updated),   NULL);
    dbus_proxy_set_heavy_cb(proxy,   G_CALLBACK(on_heavy_changed),   NULL);
    dbus_proxy_set_thermal_cb(proxy, G_CALLBACK(on_thermal_changed), NULL);

    AdwApplication *app = adw_application_new("org.uperflinux.gui",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_clear_object(&app);
    g_clear_object(&proxy);
    g_clear_pointer(&g_app.game_rows, g_free);
    return status;
}
