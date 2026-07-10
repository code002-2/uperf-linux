#define _GNU_SOURCE
#include "input_monitor.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

/* Internal input monitor state */
struct InputMonitor {
    /* Device state */
    int device_fds[INPUT_MAX_DEVICES];
    int nr_devices;

    /* Epoll infrastructure */
    int epoll_fd;
    struct epoll_event epoll_events[INPUT_MAX_DEVICES];

    /* Touch tracking state (per-device, simplified to single tracker) */
    struct {
        int32_t last_x;
        int32_t last_y;
        int32_t first_x;
        int32_t first_y;
        bool    touch_down;
        int     sample_count;
        uint64_t first_ts_ms;
        uint64_t last_ts_ms;
        float   total_distance;
        bool    is_swipe;
        bool    is_gesture;
    } tracker;

    /* Configuration */
    float swipe_thd;
    float gesture_thd_x;
    float gesture_thd_y;
    float gesture_delay_time;

    /* Screen dimensions */
    int screen_width;
    int screen_height;
    float screen_diagonal;
};

/* Calculate Euclidean distance between two points */
static float dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return sqrtf((float)(dx * dx + dy * dy));
}

/* Check if a device file descriptor corresponds to a touchscreen */
static int is_touchscreen_device(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;

    /* Check for EV_KEY with BTN_TOUCH */
    unsigned long key_bits[(KEY_MAX + 1) / 8];
    memset(key_bits, 0, sizeof(key_bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        close(fd);
        return 0;
    }
    bool has_btn_touch = key_bits[BTN_TOUCH / 32] & (1 << (BTN_TOUCH % 32));

    /* Check for EV_ABS with ABS_MT_POSITION_X */
    unsigned long abs_bits[(ABS_MAX + 1) / 8];
    memset(abs_bits, 0, sizeof(abs_bits));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) {
        close(fd);
        return 0;
    }
    bool has_mt_x = abs_bits[ABS_MT_POSITION_X / 32] & (1 << (ABS_MT_POSITION_X % 32));
    bool has_mt_y = abs_bits[ABS_MT_POSITION_Y / 32] & (1 << (ABS_MT_POSITION_Y % 32));

    close(fd);
    return has_btn_touch || (has_mt_x && has_mt_y);
}

InputMonitor *input_monitor_new(float swipe_thd, float gesture_thd_x,
                                float gesture_thd_y, float gesture_delay_time,
                                int screen_width, int screen_height) {
    InputMonitor *im = calloc(1, sizeof(*im));
    if (!im) return NULL;

    im->swipe_thd = swipe_thd;
    im->gesture_thd_x = gesture_thd_x;
    im->gesture_thd_y = gesture_thd_y;
    im->gesture_delay_time = gesture_delay_time;
    im->screen_width = screen_width;
    im->screen_height = screen_height;
    im->screen_diagonal = sqrtf(screen_width * screen_width +
                                screen_height * screen_height);

    /* Initialize tracker */
    memset(&im->tracker, 0, sizeof(im->tracker));
    im->tracker.last_x = -1;
    im->tracker.last_y = -1;

    /* Create epoll fd */
    im->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (im->epoll_fd < 0) {
        log_error("input_monitor: epoll_create1 failed: %s", strerror(errno));
        free(im);
        return NULL;
    }

    /* Initialize device fds to -1 */
    for (int i = 0; i < INPUT_MAX_DEVICES; i++)
        im->device_fds[i] = -1;

    log_info("InputMonitor created: screen=%dx%d swipeThd=%.3f gesture=(%.3f, %.3f)",
             screen_width, screen_height, swipe_thd, gesture_thd_x, gesture_thd_y);
    return im;
}

void input_monitor_free(InputMonitor *im) {
    if (!im) return;

    input_monitor_close_devices(im);

    if (im->epoll_fd >= 0) {
        close(im->epoll_fd);
    }

    log_debug("InputMonitor destroyed");
    free(im);
}

int input_monitor_discover_devices(InputMonitor *im) {
    if (!im) return -1;

    /* Scan /dev/input/ for event devices */
    DIR *dir = opendir("/dev/input/");
    if (!dir) {
        log_error("input_monitor: cannot open /dev/input/: %s", strerror(errno));
        return -1;
    }

    struct dirent *ent;
    im->nr_devices = 0;

    while ((ent = readdir(dir)) != NULL && im->nr_devices < INPUT_MAX_DEVICES) {
        if (ent->d_name[0] != 'e') continue;  /* Skip non-event* names */

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);

        if (is_touchscreen_device(path)) {
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                log_warn("input_monitor: cannot open %s: %s", path, strerror(errno));
                continue;
            }

            im->device_fds[im->nr_devices] = fd;

            /* Add to epoll */
            struct epoll_event ev = {0};
            ev.events = EPOLLIN;
            ev.data.fd = fd;
            if (epoll_ctl(im->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
                log_error("input_monitor: epoll_ctl ADD fd=%d: %s", fd, strerror(errno));
                close(fd);
                im->device_fds[im->nr_devices] = -1;
                continue;
            }

            /* Get device name for logging */
            char name[64];
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            log_info("input_monitor: found touchscreen #%d: %s (%s)",
                     im->nr_devices, ent->d_name, name);
            im->nr_devices++;
        }
    }

    closedir(dir);
    log_info("input_monitor: discovered %d touchscreen device(s)", im->nr_devices);
    return im->nr_devices;
}

void input_monitor_close_devices(InputMonitor *im) {
    if (!im) return;

    for (int i = 0; i < im->nr_devices; i++) {
        if (im->device_fds[i] >= 0) {
            /* Remove from epoll */
            epoll_ctl(im->epoll_fd, EPOLL_CTL_DEL, im->device_fds[i], NULL);
            close(im->device_fds[i]);
            im->device_fds[i] = -1;
        }
    }
    im->nr_devices = 0;
}

int input_monitor_get_epoll_fd(const InputMonitor *im) {
    if (!im) return -1;
    return im->epoll_fd;
}

int input_monitor_device_count(const InputMonitor *im) {
    if (!im) return 0;
    return im->nr_devices;
}

void input_monitor_set_screen_size(InputMonitor *im, int w, int h) {
    if (!im) return;
    im->screen_width = w;
    im->screen_height = h;
    im->screen_diagonal = sqrtf(w * w + h * h);
}

static uint64_t timespec_to_ms(const struct timespec *ts) {
    return (uint64_t)ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

int input_monitor_poll(InputMonitor *im, InputEvent *events, int max_events) {
    if (!im || im->nr_devices == 0) return 0;
    if (!events || max_events <= 0) return 0;

    /* Prepare epoll events array */
    struct epoll_event ep_events[INPUT_MAX_DEVICES];
    int nfds = epoll_wait(im->epoll_fd, ep_events, INPUT_MAX_DEVICES, 0);
    if (nfds < 0) {
        if (errno != EINTR)
            log_error("input_monitor: epoll_wait failed: %s", strerror(errno));
        return 0;
    }

    int event_count = 0;

    for (int e = 0; e < nfds && event_count < max_events; e++) {
        int fd = ep_events[e].data.fd;

        /* Read batch of input events from this device */
        struct input_event ie[16];
        ssize_t n = read(fd, &ie, sizeof(ie));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                log_warn("input_monitor: read from fd %d: %s", fd, strerror(errno));
            continue;
        }

        size_t nevents = n / sizeof(struct input_event);
        for (size_t i = 0; i < nevents; i++) {
            struct input_event *ev = &ie[i];
            uint64_t ts_ms = timespec_to_ms(&ev->time);

            switch (ev->type) {
                case EV_ABS:
                    switch (ev->code) {
                        case ABS_MT_POSITION_X:
                            im->tracker.last_x = ev->value;
                            if (im->tracker.touch_down) {
                                float d = dist(im->tracker.first_x, im->tracker.first_y,
                                               im->tracker.last_x, im->tracker.last_y);
                                im->tracker.total_distance += d;
                                im->tracker.first_x = im->tracker.last_x;
                                im->tracker.first_y = im->tracker.last_y;
                            }
                            break;

                        case ABS_MT_POSITION_Y:
                            im->tracker.last_y = ev->value;
                            if (im->tracker.touch_down) {
                                float d = dist(im->tracker.first_x, im->tracker.first_y,
                                               im->tracker.last_x, im->tracker.last_y);
                                im->tracker.total_distance += d;
                                im->tracker.first_y = im->tracker.last_y;
                            }
                            break;

                        case ABS_MT_TRACKING_ID:
                            if (ev->value == -1 && im->tracker.touch_down) {
                                /* Touch released — classify event */
                                im->tracker.touch_down = false;

                                float distance_ratio = im->tracker.total_distance /
                                                       im->screen_diagonal;
                                float elapsed_ms = ts_ms - im->tracker.first_ts_ms;
                                float velocity = (elapsed_ms > 0) ?
                                    (distance_ratio * im->screen_diagonal) /
                                    (elapsed_ms / 1000.0f) : 0.0f;

                                InputEvent *out = &events[event_count++];
                                memset(out, 0, sizeof(*out));

                                /* Classify: swipe? gesture? */
                                if (distance_ratio > im->swipe_thd) {
                                    out->type = EVT_SWIPE;
                                    out->distance_ratio = distance_ratio;
                                    out->velocity = velocity;
                                    log_debug("SWIPE: dist_ratio=%.3f vel=%.1f",
                                              distance_ratio, velocity);
                                } else {
                                    out->type = EVT_TOUCH_UP;
                                    log_debug("TOUCH_UP: dist_ratio=%.3f", distance_ratio);
                                }

                                /* Reset tracker */
                                im->tracker.total_distance = 0;
                                im->tracker.sample_count = 0;

                                if (event_count >= max_events)
                                    goto done;
                            }
                            break;
                    }
                    break;

                case EV_KEY:
                    switch (ev->code) {
                        case BTN_TOUCH:
                            if (ev->value > 0 && !im->tracker.touch_down) {
                                /* Touch down */
                                im->tracker.touch_down = true;
                                im->tracker.first_x = im->tracker.last_x;
                                im->tracker.first_y = im->tracker.last_y;
                                im->tracker.total_distance = 0;
                                im->tracker.first_ts_ms = ts_ms;
                                im->tracker.sample_count = 0;

                                InputEvent *out = &events[event_count++];
                                memset(out, 0, sizeof(*out));
                                out->type = EVT_TOUCH_DOWN;
                                out->start_x = im->tracker.first_x;
                                out->start_y = im->tracker.first_y;
                                log_debug("TOUCH_DOWN: pos=(%d, %d)",
                                          im->tracker.first_x, im->tracker.first_y);

                                if (event_count >= max_events)
                                    goto done;
                            }
                            break;
                    }
                    break;

                case EV_SYN:
                    /* Sync event — increment sample count */
                    im->tracker.sample_count++;
                    im->tracker.last_ts_ms = ts_ms;
                    break;
            }
        }
    }

done:
    if (event_count > 0)
        log_debug("input_monitor: polled %d event(s)", event_count);
    return event_count;
}
