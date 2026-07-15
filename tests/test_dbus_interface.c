#include "dbus_interface.h"

#include <assert.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    GVariant *result;
    GError *error;
    gboolean done;
} AsyncCall;

static void async_call_done(GObject *source, GAsyncResult *result,
                            gpointer user_data) {
    AsyncCall *call = user_data;
    call->result = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), result, &call->error);
    call->done = TRUE;
}

static GVariant *call_and_dispatch(GDBusConnection *connection,
                                   const gchar *interface_name,
                                   const gchar *method_name,
                                   GVariant *parameters,
                                   const GVariantType *reply_type) {
    AsyncCall call = {0};
    g_dbus_connection_call(
        connection, "org.uperflinux.Daemon", "/org/uperflinux/Daemon",
        interface_name, method_name, parameters, reply_type,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, async_call_done, &call);
    while (!call.done)
        g_main_context_iteration(NULL, TRUE);
    assert(call.error == NULL);
    return call.result;
}

static int mode_callback_count;
static int mode_signal_count;
static int reload_callback_count;
static int active_pid_callback_count;

static void on_set_mode(const char *mode, void *user_data) {
    DbusManager *manager = user_data;
    assert(strcmp(mode, "performance") == 0);
    mode_callback_count++;
    dbus_manager_set_mode(manager, mode);
}

static void on_mode_changed(GDBusConnection *connection, const gchar *sender,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *signal_name, GVariant *parameters,
                            gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;
    const char *mode = NULL;
    assert(strcmp(signal_name, "ModeChanged") == 0);
    g_variant_get(parameters, "(&s)", &mode);
    assert(strcmp(mode, "performance") == 0);
    mode_signal_count++;
}

static gboolean on_reload(void *user_data) {
    (void)user_data;
    reload_callback_count++;
    return TRUE;
}

static gboolean on_active_pid(pid_t pid, void *user_data) {
    (void)user_data;
    assert(pid == 4242 || pid == 0);
    active_pid_callback_count++;
    return TRUE;
}

int main(void) {
    GTestDBus *test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(test_bus);

    DbusManager *manager = dbus_manager_new(G_BUS_TYPE_SESSION);
    assert(manager != NULL);
    dbus_manager_set_mode_handler(manager, on_set_mode, manager);
    dbus_manager_set_reload_handler(manager, on_reload, NULL);
    dbus_manager_set_active_pid_handler(manager, on_active_pid, NULL);

    GError *error = NULL;
    GDBusConnection *client = g_dbus_connection_new_for_address_sync(
        g_test_dbus_get_bus_address(test_bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, &error);
    assert(error == NULL && client != NULL);
    guint mode_subscription = g_dbus_connection_signal_subscribe(
        client, "org.uperflinux.Daemon", "org.uperflinux.Daemon",
        "ModeChanged", "/org/uperflinux/Daemon", NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_mode_changed, NULL, NULL);

    GameProcessEntry input[40] = {0};
    char names[40][16];
    for (int i = 0; i < 40; i++) {
        snprintf(names[i], sizeof(names[i]), "game-%d", i);
        input[i].pid = 1000 + i;
        input[i].comm = names[i];
        input[i].cmdline = names[i];
        input[i].package = "test.package";
        input[i].mode = "balance";
    }

    dbus_manager_update_games(manager, input, 40);
    int count = 0;
    const GameProcessEntry *games = dbus_manager_get_games(manager, &count);
    assert(games != NULL && count == 40);
    assert(games[39].pid == 1039);
    assert(strcmp(games[39].comm, "game-39") == 0);

    GVariant *reply = call_and_dispatch(
        client, "org.freedesktop.DBus.Properties", "GetAll",
        g_variant_new("(s)", "org.uperflinux.Daemon"),
        G_VARIANT_TYPE("(a{sv})"));
    GVariant *properties = NULL;
    g_variant_get(reply, "(@a{sv})", &properties);
    GVariant *game_property = g_variant_lookup_value(
        properties, "GameProcesses", G_VARIANT_TYPE("a(issss)"));
    assert(game_property != NULL);
    assert(g_variant_n_children(game_property) == 40);
    g_variant_unref(game_property);
    g_variant_unref(properties);
    g_variant_unref(reply);
    gboolean success = FALSE;

    reply = call_and_dispatch(
        client, "org.uperflinux.Daemon", "ReloadConfig", NULL,
        G_VARIANT_TYPE("(b)"));
    g_variant_get(reply, "(b)", &success);
    assert(success && reload_callback_count == 1);
    g_variant_unref(reply);

    reply = call_and_dispatch(
        client, "org.uperflinux.Daemon", "SetMode",
        g_variant_new("(s)", "performance"), G_VARIANT_TYPE("(b)"));
    g_variant_get(reply, "(b)", &success);
    assert(success && mode_callback_count == 1);
    assert(strcmp(dbus_manager_get_mode(manager), "performance") == 0);
    g_variant_unref(reply);
    for (int i = 0; i < 100 && mode_signal_count == 0; i++) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    }
    assert(mode_signal_count == 1);

    reply = call_and_dispatch(
        client, "org.uperflinux.Daemon", "SetActiveProcess",
        g_variant_new("(i)", 4242), G_VARIANT_TYPE("(b)"));
    g_variant_get(reply, "(b)", &success);
    assert(success && active_pid_callback_count == 1);
    assert(dbus_manager_get_active_pid(manager) == 4242);
    g_variant_unref(reply);

    reply = call_and_dispatch(
        client, "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.uperflinux.Daemon", "ActiveProcess"),
        G_VARIANT_TYPE("(v)"));
    GVariant *active_box = g_variant_get_child_value(reply, 0);
    GVariant *active_variant = g_variant_get_variant(active_box);
    assert(g_variant_get_int32(active_variant) == 4242);
    g_variant_unref(active_variant);
    g_variant_unref(active_box);
    g_variant_unref(reply);

    reply = call_and_dispatch(
        client, "org.uperflinux.Daemon", "SetMode",
        g_variant_new("(s)", "invalid"), G_VARIANT_TYPE("(b)"));
    g_variant_get(reply, "(b)", &success);
    assert(!success && mode_callback_count == 1);
    g_variant_unref(reply);

    strcpy(names[39], "changed");
    assert(strcmp(games[39].comm, "game-39") == 0);

    dbus_manager_update_games(manager, input, 2);
    games = dbus_manager_get_games(manager, &count);
    assert(games != NULL && count == 2);

    dbus_manager_update_games(manager, input, 25);
    games = dbus_manager_get_games(manager, &count);
    assert(games != NULL && count == 25);
    assert(strcmp(games[24].comm, "game-24") == 0);

    dbus_manager_update_games(manager, NULL, 0);
    games = dbus_manager_get_games(manager, &count);
    assert(games != NULL && count == 0);

    reply = call_and_dispatch(
        client, "org.uperflinux.Daemon", "SetManualFreq",
        g_variant_new("(ix)", 0, (gint64)2956800000LL),
        G_VARIANT_TYPE("(b)"));
    g_variant_get(reply, "(b)", &success);
    assert(success);
    assert(dbus_manager_get_manual_freq(manager, 0) == 2956800000LL);
    g_variant_unref(reply);

    g_dbus_connection_signal_unsubscribe(client, mode_subscription);
    g_object_unref(client);
    dbus_manager_free(manager);
    g_test_dbus_down(test_bus);
    g_object_unref(test_bus);
    puts("dbus interface tests passed");
    return 0;
}
