#include "game_scanner.h"
#include "perapp_config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>

/* Built-in game detection patterns */
static const char *builtin_patterns[] = {
    /* Game engines */
    "UnityMain", "Unity", "libil2cpp", "GameThread", "RenderThread",
    "GLThread", "GLESThread", "dxvk", "vkd3d", "wineserver",
    /* Launchers / stores */
    "gamelauncher", "steam_app_", "steamoverlay",
    /* Emulators */
    "dolphin", "duckstation", "ppsspp", "retroarch",
    "yuzu", "citra", "skyline", "mGBA", "RPCS3", "RPCS3",
    /* Android game wrappers */
    "cn.hoyoverse", "com.pubg", "com.supercell",
    "com.miHoYo", "com.tencent", "com.netease",
    /* Desktop games */
    "lutris", "heroic", "proton",
    NULL
};

/* Internal game scanner state */
struct GameScanner {
    GameProcess processes[MAX_GAMES];
    int nr_processes;

    /* Custom patterns (beyond builtins) */
    const char **custom_patterns;
    int nr_custom_patterns;
    int custom_patterns_cap;

    /* Per-app mode config */
    PerAppConfig perapp;
};

GameScanner *game_scanner_new(void) {
    GameScanner *gs = calloc(1, sizeof(*gs));
    if (!gs) return NULL;

    gs->nr_processes = 0;
    gs->custom_patterns = NULL;
    gs->nr_custom_patterns = 0;
    gs->custom_patterns_cap = 0;

    log_info("GameScanner created (%d builtin patterns)",
             builtin_patterns[0] ? 1 : 0);
    return gs;
}

void game_scanner_free(GameScanner *gs) {
    if (!gs) return;
    free(gs->custom_patterns);
    perapp_free(&gs->perapp);
    log_debug("GameScanner destroyed (found %d processes)", gs->nr_processes);
    free(gs);
}

bool game_scanner_match(const char *comm, const char *cmdline) {
    if (!comm && !cmdline) return false;

    /* Check built-in patterns */
    for (int i = 0; builtin_patterns[i]; i++) {
        if (comm && strstr(comm, builtin_patterns[i]))
            return true;
        if (cmdline && strstr(cmdline, builtin_patterns[i]))
            return true;
    }

    return false;
}

int game_scanner_add_pattern(GameScanner *gs, const char *pattern) {
    if (!gs || !pattern || !*pattern) return -1;

    /* Resize pattern array if needed */
    if (gs->nr_custom_patterns >= gs->custom_patterns_cap) {
        int new_cap = gs->custom_patterns_cap ? gs->custom_patterns_cap * 2 : 16;
        const char **new_arr = realloc(gs->custom_patterns,
                                        new_cap * sizeof(char *));
        if (!new_arr) return -1;
        gs->custom_patterns = new_arr;
        gs->custom_patterns_cap = new_cap;
    }

    gs->custom_patterns[gs->nr_custom_patterns++] = pattern;
    return 0;
}

int game_scanner_pattern_count(const GameScanner *gs) {
    if (!gs) return 0;
    return gs->nr_custom_patterns;
}

int game_scanner_scan(GameScanner *gs) {
    if (!gs) return 0;

    DIR *proc = opendir("/proc");
    if (!proc) {
        log_error("game_scanner: cannot open /proc: %s", strerror(errno));
        return 0;
    }

    gs->nr_processes = 0;
    struct dirent *ent;

    while ((ent = readdir(proc)) != NULL && gs->nr_processes < MAX_GAMES) {
        /* Skip non-numeric directories (PID) */
        if (!isdigit(ent->d_name[0]))
            continue;

        pid_t pid = atoi(ent->d_name);
        if (pid < 2)  /* Skip kernel threads and self */
            continue;

        /* Read /proc/[pid]/comm */
        char comm[16];
        char comm_path[MAX_PATH_LEN];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
        FILE *fp = fopen(comm_path, "r");
        if (!fp) continue;
        fgets(comm, sizeof(comm), fp);
        fclose(fp);
        /* Strip trailing newline */
        size_t clen = strlen(comm);
        if (clen > 0 && comm[clen-1] == '\n') comm[--clen] = '\0';

        /* Read /proc/[pid]/cmdline */
        char cmdline[CMDLINE_MAX] = {0};
        char cmdline_path[MAX_PATH_LEN];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
        fp = fopen(cmdline_path, "r");
        if (fp) {
            size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
            cmdline[n] = '\0';
            /* Replace null separators with spaces for matching */
            for (size_t i = 0; i < n; i++) {
                if (cmdline[i] == '\0') cmdline[i] = ' ';
            }
            fclose(fp);
        }

        /* Check if this matches any game pattern */
        if (game_scanner_match(comm, cmdline)) {
            GameProcess *gp = &gs->processes[gs->nr_processes];
            gp->pid = pid;
            strncpy(gp->comm, comm, sizeof(gp->comm) - 1);
            gp->comm[sizeof(gp->comm) - 1] = '\0';
            strncpy(gp->cmdline, cmdline, sizeof(gp->cmdline) - 1);
            gp->cmdline[sizeof(gp->cmdline) - 1] = '\0';
            gp->is_game = true;

            /* Derive package name from cmdline (first path component) */
            const char *first_word = cmdline;
            while (*first_word && *first_word != ' ') first_word++;
            *first_word = '\0';
            strncpy(gp->package, cmdline, sizeof(gp->package) - 1);
            gp->package[sizeof(gp->package) - 1] = '\0';

            gs->nr_processes++;
            log_debug("game_scanner: found game PID=%d comm=%s cmdline=%s",
                      pid, comm, cmdline);
        }
    }

    closedir(proc);
    log_info("game_scanner: scanned /proc, found %d game process(es)",
             gs->nr_processes);
    return gs->nr_processes;
}

int game_scanner_get_results(GameScanner *gs, GameProcess *out, int max_entries) {
    if (!gs || !out) return 0;

    int count = gs->nr_processes;
    if (count > max_entries) count = max_entries;

    memcpy(out, gs->processes, count * sizeof(GameProcess));
    return count;
}

int game_scanner_perapp_scan(GameScanner *gs, const char *perapp_file) {
    if (!gs || !perapp_file) return -1;
    return perapp_load(&gs->perapp, perapp_file);
}

const char *game_scanner_get_app_mode(const GameScanner *gs, const char *comm) {
    if (!gs || !comm || gs->perapp.nr_rules == 0)
        return "balance";

    PowerMode pm = perapp_lookup(&gs->perapp, comm);
    const char *mode_names[] = {"balance", "powersave", "performance", "fast"};
    if (pm >= 0 && pm < 4)
        return mode_names[pm];
    return "balance";
}
