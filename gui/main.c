#include <gtk/gtk.h>
#include <libadwaita-1/adwaita.h>
#include "dbus_proxy.h"

/* ----------------------------------------------------------------
 * App state
 * ---------------------------------------------------------------- */

typedef struct {
    DbusProxy *proxy;
    GtkListBox *game_list;
    GtkTextView  *log_view;
    GtkLabel     *lbl_mode;
    GtkLabel     *lbl_scene;
    GtkLabel     *lbl_heavy;
    GtkLabel     *lbl_max_temp;
    GtkLabel     *lbl_thermal;
    GtkProgressBar *load_bar;
    GtkProgressBar *temp_bar;
    GtkWidget **freq_labels;
    GtkWidget **load_labels;
    GtkWidget *stack;
} AppState;

static AppState g_app;

/* ----------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------- */

static void refresh_display(void) {
    if (!g_app.proxy) return;

    if (g_app.lbl_mode)
        gtk_label_set_text(g_app.lbl_mode, g_app.proxy->current_mode);
    if (g_app.lbl_scene)
        gtk_label_set_text(g_app.lbl_scene, g_app.proxy->current_scene);
    if (g_app.lbl_heavy)
        gtk_label_set_text(g_app.lbl_heavy,
            g_app.proxy->is_heavy_load ? "HEAVY LOAD ACTIVE" : "Normal Load");
    if (g_app.load_bar)
        gtk_progress_bar_set_fraction(g_app.load_bar,
            g_app.proxy->is_heavy_load ? 0.9 : 0.3);
    if (g_app.lbl_max_temp) {
        double temp_c = g_app.proxy->max_temp / 1000.0;
        gchar buf[64];
        g_snprintf(buf, sizeof(buf), "%.1f C", temp_c);
        gtk_label_set_text(g_app.lbl_max_temp, buf);
    }
    if (g_app.lbl_thermal)
        gtk_label_set_text(g_app.lbl_thermal, g_app.proxy->thermal_state);
    if (g_app.temp_bar) {
        double frac = g_app.proxy->max_temp > 0
            ? CLAMP((g_app.proxy->max_temp / 1000.0 - 40.0) / 60.0, 0.0, 1.0)
            : 0.0;
        gtk_progress_bar_set_fraction(g_app.temp_bar, frac);
    }
    for (int i = 0; i < g_app.proxy->nr_freqs && i < 8; i++) {
        if (g_app.freq_labels && g_app.freq_labels[i]) {
            gchar buf[32];
            g_snprintf(buf, sizeof(buf), "%.2f GHz", g_app.proxy->freqs[i] / 1000.0);
            gtk_label_set_text(GTK_LABEL(g_app.freq_labels[i]), buf);
        }
        if (g_app.load_labels && g_app.load_labels[i]) {
            gchar buf[32];
            g_snprintf(buf, sizeof(buf), "%.0f%%", g_app.proxy->loads[i]);
            gtk_label_set_text(GTK_LABEL(g_app.load_labels[i]), buf);
        }
    }
}

/* Callback helpers to bridge GCallback with our functions */
static void on_mode_changed(DbusProxy *p, gchar *m, gpointer ud) {
    (void)p; (void)m; (void)ud;
    refresh_display();
}
static void on_scene_changed(DbusProxy *p, gchar *m, gpointer ud) {
    (void)p; (void)m; (void)ud;
    refresh_display();
}
static void on_stats_updated(DbusProxy *p, gpointer ud) {
    (void)p; (void)ud;
    refresh_display();
}
static void on_heavy_changed(DbusProxy *p, gboolean h, gpointer ud) {
    (void)p; (void)h; (void)ud;
    refresh_display();
}
static void on_thermal_changed(DbusProxy *p, gint32 t, gpointer ud) {
    (void)p; (void)t; (void)ud;
    refresh_display();
}

static void on_set_mode_clicked(GtkButton *btn, const gchar *mode) {
    if (g_app.proxy) dbus_proxy_set_mode(g_app.proxy, mode);
    refresh_display();
}

static void on_set_game_mode(GtkComboBox *cb, gpointer userdata) {
    if (!g_app.proxy) return;
    const gchar *modes[] = {"balance", "powersave", "performance"};
    gint idx = gtk_combo_box_get_active(cb);
    if (idx >= 0 && idx < 3) {
        dbus_proxy_set_game_mode(g_app.proxy, 0, "unknown", modes[idx]);
    }
}

static void on_refresh_logs_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_app.log_view);
    gtk_text_buffer_set_text(buf,
        "[info] Logs refreshed\n"
        "[info] (Production: reads from journald via DBus)\n", -1);
}

static void on_clear_logs_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(g_app.log_view);
    gtk_text_buffer_set_text(buf, "", -1);
}

static void on_apply_settings_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    g_debug("Apply settings clicked");
}

static void on_apply_freq_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    g_debug("Apply freq override");
}

static void on_release_freq_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    if (g_app.proxy) dbus_proxy_release_freq_override(g_app.proxy);
}

static void on_tab_switch(GtkStack *stack, GtkStackPage *page, gpointer ud) {
    (void)stack; (void)page; (void)ud;
}

static void on_tab_clicked(GtkButton *btn, const gchar *page_name) {
    gtk_stack_set_visible_child_name(g_app.stack, page_name);
}

/* ----------------------------------------------------------------
 * Dashboard page
 * ---------------------------------------------------------------- */

static GtkWidget *create_dashboard_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(title), "uperf-linux");
    gtk_label_set_css_classes(GTK_LABEL(title), (const gchar *[]){"heading", NULL});
    gtk_box_append(GTK_BOX(box), title);

    /* SOC badge */
    GtkWidget *soc = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(soc), "SM8550 - Snapdragon 8 Gen 2");
    gtk_label_set_css_classes(GTK_LABEL(soc), (const gchar *[]){"caption", NULL});
    gtk_box_append(GTK_BOX(box), soc);

    /* Power Mode buttons */
    GtkWidget *mode_frame = adw_clamp_new();
    gtk_widget_set_valign(mode_frame, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(mode_frame, TRUE);
    GtkWidget *mode_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(mode_grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(mode_grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(mode_grid), 12);
    adw_clamp_set_child(ADW_CLAMP(mode_frame), mode_grid);

    g_app.lbl_mode = GTK_LABEL(gtk_label_new("balance"));
    gtk_label_set_css_classes(GTK_LABEL(g_app.lbl_mode), (const gchar *[]){"caption", NULL});
    gtk_label_set_justify(g_app.lbl_mode, GTK_JUSTIFY_CENTER);

    const char *modes[] = {"balance", "powersave", "performance"};
    const char *icons[] = {"B", "P", "X"};
    const char *labels[] = {"Balance", "Powersave", "Performance"};

    for (int i = 0; i < 3; i++) {
        GtkWidget *btn = gtk_button_new();
        gtk_button_set_css_classes(btn, (const gchar *[]){"flat", NULL});
        gtk_widget_set_hexpand(btn, TRUE);
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *icon = gtk_label_new(icons[i]);
        GtkFontWeight fw = GTK_FONT_WEIGHT_HEAVY;
        gtk_label_set_font_weight(GTK_LABEL(icon), fw);
        GtkWidget *lbl = gtk_label_new(labels[i]);
        gtk_label_set_font_weight(GTK_LABEL(lbl), GTK_FONT_WEIGHT_BOLD);
        gtk_box_append(GTK_BOX(vbox), icon);
        gtk_box_append(GTK_BOX(vbox), lbl);
        gtk_button_set_child(GTK_BUTTON(btn), vbox);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_set_mode_clicked), (gpointer)modes[i]);
        gtk_grid_attach(GTK_GRID(mode_grid), btn, i, 0, 1, 1);
    }
    gtk_grid_attach(GTK_GRID(mode_grid), GTK_WIDGET(g_app.lbl_mode), 0, 1, 3, 1);
    gtk_box_append(GTK_BOX(box), mode_frame);

    /* Scene badge */
    g_app.lbl_scene = GTK_LABEL(gtk_label_new("IDLE"));
    gtk_label_set_font_weight(g_app.lbl_scene, GTK_FONT_WEIGHT_BOLD);
    gtk_label_set_css_classes(GTK_LABEL(g_app.lbl_scene), (const gchar *[]){"heading", NULL});
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_app.lbl_scene));

    /* Heavy load */
    g_app.lbl_heavy = GTK_LABEL(gtk_label_new("Normal"));
    gtk_label_set_font_weight(g_app.lbl_heavy, GTK_FONT_WEIGHT_BOLD);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_app.lbl_heavy));

    /* CPU frequency grid */
    GtkWidget *freq_frame = adw_clamp_new();
    gtk_widget_set_valign(freq_frame, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(freq_frame, TRUE);
    GtkWidget *freq_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(freq_grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(freq_grid), 4);
    gtk_container_set_border_width(GTK_CONTAINER(freq_grid), 8);
    adw_clamp_set_child(ADW_CLAMP(freq_frame), freq_grid);

    g_app.freq_labels = g_malloc0(8 * sizeof(GtkWidget *));
    g_app.load_labels = g_malloc0(8 * sizeof(GtkWidget *));

    for (int i = 0; i < 8; i++) {
        int col = i % 4;
        int row = i / 4;
        GtkWidget *cpu_lbl = gtk_label_new(NULL);
        gchar cpu_name[16];
        g_snprintf(cpu_name, sizeof(cpu_name), "CPU%d", i);
        gtk_label_set_text(GTK_LABEL(cpu_lbl), cpu_name);
        gtk_label_set_css_classes(GTK_LABEL(cpu_lbl), (const gchar *[]){"caption", NULL});
        GtkWidget *freq_lbl = gtk_label_new("--");
        g_app.freq_labels[i] = freq_lbl;
        GtkWidget *load_lbl = gtk_label_new("--");
        g_app.load_labels[i] = load_lbl;
        gtk_label_set_css_classes(GTK_LABEL(load_lbl), (const gchar *[]){"caption", NULL});

        GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_append(GTK_BOX(cell), cpu_lbl);
        gtk_box_append(GTK_BOX(cell), freq_lbl);
        gtk_box_append(GTK_BOX(cell), load_lbl);
        gtk_grid_attach(GTK_GRID(freq_grid), cell, col, row, 1, 1);
    }
    gtk_box_append(GTK_BOX(box), freq_frame);

    /* Load bar */
    g_app.load_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_widget_set_hexpand(GTK_WIDGET(g_app.load_bar), TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_app.load_bar));
    gtk_progress_bar_set_fraction(g_app.load_bar, 0.3);

    /* Thermal section */
    GtkWidget *therm_frame = adw_clamp_new();
    gtk_widget_set_valign(therm_frame, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(therm_frame, TRUE);
    GtkWidget *therm_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(therm_box), 12);
    adw_clamp_set_child(ADW_CLAMP(therm_frame), therm_box);

    g_app.lbl_max_temp = GTK_LABEL(gtk_label_new("-- C"));
    gtk_label_set_font_weight(g_app.lbl_max_temp, GTK_FONT_WEIGHT_BOLD);
    gtk_label_set_css_classes(GTK_LABEL(g_app.lbl_max_temp), (const gchar *[]){"heading", NULL});
    gtk_box_append(GTK_BOX(therm_box), GTK_WIDGET(g_app.lbl_max_temp));

    g_app.lbl_thermal = GTK_LABEL(gtk_label_new("NORMAL"));
    gtk_box_append(GTK_BOX(therm_box), GTK_WIDGET(g_app.lbl_thermal));

    g_app.temp_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_widget_set_hexpand(GTK_WIDGET(g_app.temp_bar), TRUE);
    gtk_box_append(GTK_BOX(therm_box), GTK_WIDGET(g_app.temp_bar));

    gtk_box_append(GTK_BOX(box), therm_frame);

    return box;
}

/* ----------------------------------------------------------------
 * Games page
 * ---------------------------------------------------------------- */

static void refresh_games(void) {
    if (!g_app.game_list) return;

    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(g_app.game_list));
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(g_app.game_list, child);
        child = next;
    }

    for (int i = 0; i < g_app.proxy->nr_games; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);

        GtkWidget *icon = gtk_label_new("G");
        gtk_label_set_font_weight(GTK_LABEL(icon), GTK_FONT_WEIGHT_BOLD);

        GtkWidget *name_lbl = gtk_label_new(g_app.proxy->game_comm[i]);
        gtk_label_set_font_weight(GTK_LABEL(name_lbl), GTK_FONT_WEIGHT_BOLD);

        GtkWidget *detail_lbl = gtk_label_new(NULL);
        gchar detail[256];
        g_snprintf(detail, sizeof(detail), "PID: %d", g_app.proxy->game_pid[i]);
        gtk_label_set_text(GTK_LABEL(detail_lbl), detail);
        gtk_label_set_css_classes(GTK_LABEL(detail_lbl), (const gchar *[]){"caption", NULL});

        GtkWidget *combo = gtk_combo_box_string_new();
        gtk_combo_box_string_append_literal(combo, "balance", "Balance");
        gtk_combo_box_string_append_literal(combo, "powersave", "Powersave");
        gtk_combo_box_string_append_literal(combo, "performance", "Performance");

        const char *cur = g_app.proxy->game_mode[i];
        if (cur) {
            gint id = gtk_combo_box_string_find_string(combo, cur);
            if (id >= 0) gtk_combo_box_set_active(combo, id);
        }

        g_signal_connect(combo, "notify::active", G_CALLBACK(on_set_game_mode), NULL);

        gtk_box_append(GTK_BOX(hbox), icon);
        gtk_box_append(GTK_BOX(hbox), name_lbl);
        gtk_box_append(GTK_BOX(hbox), detail_lbl);
        gtk_box_pack_end(GTK_BOX(hbox), combo, FALSE, FALSE, 0);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
        gtk_list_box_insert(g_app.game_list, row, -1);
    }
}

static GtkWidget *create_games_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(title), "Detected Games");
    gtk_label_set_css_classes(GTK_LABEL(title), (const gchar *[]){"heading", NULL});
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(hint), "Running game and game-like processes");
    gtk_label_set_css_classes(GTK_LABEL(hint), (const gchar *[]){"caption", NULL});
    gtk_box_append(GTK_BOX(box), hint);

    g_app.game_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_set_vexpand(g_app.game_list, TRUE);
    gtk_widget_set_hexpand(g_app.game_list, TRUE);
    gtk_list_box_set_selection_mode(g_app.game_list, GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_app.game_list));

    return box;
}

/* ----------------------------------------------------------------
 * Settings page
 * ---------------------------------------------------------------- */

static GtkWidget *create_settings_page(void) {
    GtkWidget *prefs = adw_preferences_window_new();

    /* Helper: create a group with title and add rows */
    struct GroupDef {
        const char *title;
        struct {
            const char *name;
            const char *desc;
            const char *init;
        } rows[4];
        int nr;
    };

    struct GroupDef groups[] = {
        { "Load Detection", {
            { "HeavyLoad Threshold", "System load % above which boost mode activates", "60" },
            { "Idle Load Threshold", "Load % below which boost mode deactivates", "20" },
            { "Sample Time", "Interval between /proc/stat samples (ms)", "10" },
            { "Burst Slack", "Cooldown before re-entering boost (ms)", "3000" },
        }, 4 },
        { "Response Timing", {
            { "Latency Time", "Response delay before boosting frequency (ms)", "200" },
            { "Margin", "Headroom multiplier for frequency selection (%)", "25" },
            { "Burst", "Additional burst intensity modifier (%)", "0" },
            { "", NULL, NULL },
        }, 3 },
        { "Power Budgets", {
            { "Slow Limit", "Power budget for slow response mode (Watts)", "3.0" },
            { "Fast Limit", "Power budget for fast response mode (Watts)", "6.0" },
            { "Fast Limit Capacity", "Maximum capacity cap during burst", "10.0" },
            { "", NULL, NULL },
        }, 3 },
        { "Thermal Thresholds", {
            { "Warn Temp", "Temperature at which warnings are logged (C)", "70" },
            { "Throttle Temp", "Temperature at which CPU/GPU frequency is reduced (C)", "80" },
            { "Critical Temp", "Emergency threshold (C)", "95" },
            { "Recovery Temp", "Temperature below which normal operation resumes (C)", "75" },
        }, 4 },
    };

    for (int gi = 0; gi < 4; gi++) {
        GtkWidget *grp = adw_preferences_group_new();
        adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(grp), groups[gi].title);
        adw_preferences_window_add_page(ADW_PREFERENCES_WINDOW(prefs), grp, NULL);

        for (int ri = 0; ri < groups[gi].nr; ri++) {
            GtkWidget *row = adw_entry_row_new();
            adw_entry_row_set_title(ADW_ENTRY_ROW(row), groups[gi].rows[ri].name);
            adw_entry_row_set_subtitle(ADW_ENTRY_ROW(row), groups[gi].rows[ri].desc);
            adw_entry_row_set_numeric(ADW_ENTRY_ROW(row), TRUE);
            GtkEntry *e = GTK_ENTRY(adw_entry_row_get_entry(ADW_ENTRY_ROW(row)));
            if (groups[gi].rows[ri].init[0]) {
                gtk_entry_set_text(e, groups[gi].rows[ri].init);
                gtk_entry_set_width_chars(e, 6);
            }
            adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), row);
        }
    }

    /* Apply button in bottom sheet */
    GtkWidget *apply_btn = gtk_button_new_with_label("Apply Settings");
    gtk_style_context_add_class(gtk_widget_get_style_context(apply_btn), "suggested-action");
    gtk_widget_set_hexpand(apply_btn, TRUE);
    gtk_button_set_css_classes(apply_btn, (const gchar *[]){"suggested-action", "pill", NULL});
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_settings_clicked), NULL);

    GtkWidget *bottom_sheet = adw_bottom_sheet_new(NULL, NULL);
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(bottom_box), apply_btn);
    adw_bottom_sheet_set_child(ADW_BOTTOM_SHEET(bottom_sheet), bottom_box);
    adw_preferences_window_set_bottom_sheet(ADW_PREFERENCES_WINDOW(prefs),
        ADW_BOTTOM_SHEET(bottom_sheet));

    return prefs;
}

/* ----------------------------------------------------------------
 * Logs page
 * ---------------------------------------------------------------- */

static GtkWidget *create_logs_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(title), "Log Output");
    gtk_label_set_css_classes(GTK_LABEL(title), (const gchar *[]){"heading", NULL});
    gtk_box_append(GTK_BOX(box), title);

    GtkTextBuffer *buf = gtk_text_buffer_new(NULL);
    g_app.log_view = GTK_TEXT_VIEW(gtk_text_view_new_with_buffer(buf));
    gtk_text_view_set_editable(g_app.log_view, FALSE);
    gtk_text_view_set_wrap_mode(g_app.log_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(g_app.log_view, TRUE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(g_app.log_view));
    gtk_box_append(GTK_BOX(box), scroll);

    gtk_text_buffer_set_text(buf,
        "[info] uperf-linux daemon started\n"
        "[info] Config loaded: sm8550 by uperf-linux\n"
        "[info] DBus manager created on system bus\n"
        "[info] CgroupManager initialized\n"
        "[info] HeavyLoadDetector created: nr_cpus=8\n"
        "[info] InputMonitor: no touchscreen devices found\n"
        "[info] === uperf-linux ready ===\n", -1);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    gtk_button_set_css_classes(refresh_btn, (const gchar *[]){"flat", NULL});
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_logs_clicked), NULL);
    gtk_box_append(GTK_BOX(hbox), refresh_btn);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_button_set_css_classes(clear_btn, (const gchar *[]){"flat", NULL});
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_logs_clicked), NULL);
    gtk_box_append(GTK_BOX(hbox), clear_btn);

    gtk_box_append(GTK_BOX(box), hbox);
    return box;
}

/* ----------------------------------------------------------------
 * Frequency override page
 * ---------------------------------------------------------------- */

static GtkWidget *create_frequency_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(title), "Manual Frequency Override");
    gtk_label_set_css_classes(GTK_LABEL(title), (const gchar *[]){"heading", NULL});
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(hint), "Lock CPU/GPU frequency to a fixed value. Set to 0 to release back to auto-scaling.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_css_classes(GTK_LABEL(hint), (const gchar *[]){"caption", NULL});
    gtk_box_append(GTK_BOX(box), hint);

    /* Enable toggle */
    GtkWidget *toggle_row = adw_action_row_new();
    GtkWidget *toggle = gtk_switch_new();
    adw_action_row_set_title(ADW_ACTION_ROW(toggle_row), "Override Enabled");
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(toggle_row), toggle);
    gtk_box_append(GTK_BOX(box), toggle_row);

    struct { const char *title; gdouble min; gdouble max; gdouble def; } clusters[] = {
        { "CPU Prime (cpu0)", 600000, 3200000, 2400000 },
        { "CPU Performance (cpu1-2)", 500000, 2800000, 2200000 },
        { "CPU Efficiency (cpu3-7)", 300000, 2000000, 1600000 },
        { "GPU", 300000000, 1000000000, 600000000 },
    };

    for (int i = 0; i < 4; i++) {
        GtkWidget *frame = adw_clamp_new();
        gtk_widget_set_valign(frame, GTK_ALIGN_FILL);
        gtk_widget_set_hexpand(frame, TRUE);
        GtkWidget *slider_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(slider_box), 12);
        adw_clamp_set_child(ADW_CLAMP(frame), slider_box);

        GtkAdjustment *adj = gtk_adjustment_new(clusters[i].def,
            clusters[i].min, clusters[i].max, 50000, 100000, 0);
        GtkWidget *slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
        gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
        gtk_widget_set_hexpand(slider, TRUE);
        gtk_box_append(GTK_BOX(slider_box), slider);

        gtk_box_append(GTK_BOX(box), frame);
    }

    /* Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), hbox);

    GtkWidget *apply_btn = gtk_button_new_with_label("Apply");
    gtk_widget_set_hexpand(apply_btn, TRUE);
    gtk_button_set_css_classes(apply_btn, (const gchar *[]){"suggested-action", "pill", NULL});
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_freq_clicked), NULL);
    gtk_box_append(GTK_BOX(hbox), apply_btn);

    GtkWidget *release_btn = gtk_button_new_with_label("Release All");
    gtk_button_set_css_classes(release_btn, (const gchar *[]){"destructive-action", "pill", NULL});
    g_signal_connect(release_btn, "clicked", G_CALLBACK(on_release_freq_clicked), NULL);
    gtk_box_append(GTK_BOX(hbox), release_btn);

    return box;
}

/* ----------------------------------------------------------------
 * Activate handler (pure C, no lambdas)
 * ---------------------------------------------------------------- */

static void on_activate(GtkApplication *app, gpointer ud) {
    (void)ud;

    GtkWidget *window = adw_application_window_new(ADW_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "uperf-linux");
    gtk_window_set_default_size(GTK_WINDOW(window), 1080, 2400);

    /* Stack with pages */
    g_app.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(g_app.stack),
        GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(g_app.stack), 200);

    gtk_stack_add_named(GTK_STACK(g_app.stack),
        create_dashboard_page(), "dashboard");
    gtk_stack_add_named(GTK_STACK(g_app.stack),
        create_games_page(), "games");
    gtk_stack_add_named(GTK_STACK(g_app.stack),
        create_settings_page(), "settings");
    gtk_stack_add_named(GTK_STACK(g_app.stack),
        create_logs_page(), "logs");
    gtk_stack_add_named(GTK_STACK(g_app.stack),
        create_frequency_page(), "frequency");

    /* Header bar as bottom nav */
    GtkWidget *tab_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(tab_bar), FALSE);
    gtk_window_set_titlebar(GTK_WINDOW(window), tab_bar);

    struct { const char *label; const char *page; } tabs[] = {
        { "Dashboard",  "dashboard" },
        { "Games",      "games" },
        { "Settings",   "settings" },
        { "Logs",       "logs" },
        { "Freq",       "frequency" },
    };
    for (int i = 0; i < 5; i++) {
        GtkWidget *btn = gtk_button_new_with_label(tabs[i].label);
        gtk_button_set_css_classes(btn, (const gchar *[]){"flat", "pill", NULL});
        gtk_widget_set_hexpand(btn, TRUE);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_tab_clicked),
            (gpointer)tabs[i].page);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(tab_bar), btn);
    }

    gtk_window_set_content(GTK_WINDOW(window), g_app.stack);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    adw_init();

    /* Create DBus proxy */
    DbusProxy *proxy = g_object_new(UPERF_TYPE_DBUS_PROXY, NULL);
    g_app.proxy = proxy;
    dbus_proxy_start_polling(proxy);

    /* Live update callbacks */
    dbus_proxy_set_mode_cb(proxy, G_CALLBACK(on_mode_changed), NULL);
    dbus_proxy_set_scene_cb(proxy, G_CALLBACK(on_scene_changed), NULL);
    dbus_proxy_set_stats_cb(proxy, G_CALLBACK(on_stats_updated), NULL);
    dbus_proxy_set_heavy_cb(proxy, G_CALLBACK(on_heavy_changed), NULL);
    dbus_proxy_set_thermal_cb(proxy, G_CALLBACK(on_thermal_changed), NULL);

    /* Application */
    GtkApplication *app = gtk_application_new("org.uperflinux.gui",
        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    return g_application_run(G_APPLICATION(app), argc, argv);
}
