# uperf-linux

> Userspace performance scheduler for Linux ARM64 gaming devices.
> Inspired by [Uperf-Game-Turbo](https://github.com/yinwanxi/Uperf-Game-Turbo) (Android Magisk module).

## Overview

`uperf-linux` is a systemd-managed daemon that provides fine-grained CPU/GPU scheduling
for gaming on Linux ARM64 devices. It borrows the core design philosophy from Uperf-Game-Turbo:

- **JSON-config-driven** sysfs knob writing (cpufreq, devfreq, uClamp)
- **Scene-based state machine** (idle → touch → trigger → gesture → junk → switch → boost)
- **Touch-aware pacing** — reacts to swipe/gesture events from the touchscreen
- **Power model** — finds the "sweet spot" on the P-F curve for each cluster
- **HeavyLoad detection** — automatic boost mode when sustained load exceeds threshold
- **Task affinity management** — pins game threads to performance cores via cgroup v2

Unlike the Android original, this runs on **standard Linux** (no Magisk, no AOSP, no Binder IPC).

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    systemd (PID 1)                           │
│  uperf-linux.service  (After=multi-user.target)              │
└──────────────────────────┬───────────────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────────────┐
│                    uperf-linux daemon (root)                  │
│                                                              │
│  ConfigParser ←→ JSON config (hot-reload via inotify)        │
│  InputMonitor ←→ /dev/input/event* (evdev, epoll)            │
│  StateEngine  ←→ FSM with timers (timerfd)                   │
│  SysfsWriter  ←→ batched/deduped writes to sysfs             │
│  CgroupMgr    ←→ cgroup v2 slices + uClamp                   │
│  HeavyLoad    ←→ /proc/stat polling                          │
│  GameScanner  ←→ /proc/*/comm + /proc/*/cmdline matching     │
└──────────────────────────────────────────────────────────────┘
```

## Supported Platforms

| Platform | SoC | cpufreq driver | devfreq | Status |
|----------|-----|----------------|---------|--------|
| Xiaomi Pad 6S Pro | SM8550 (SD 8 Gen 2) | cpufreq-dt | msm_dvfs | ✅ Primary target |
| Other Snapdragon | Various | cpufreq-dt | varies | ⚠️ Config required |

## Quick Start

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install cmake pkg-config libjson-c-dev libsystemd-dev

# Arch Linux
sudo pacman -S cmake json-c systemd-libs

# Fedora
sudo dnf install cmake pkg-config json-c-devel systemd-devel
```

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Install

```bash
# As root
sudo cp uperf-linux /usr/local/bin/
sudo cp uperfctl /usr/local/bin/
sudo mkdir -p /etc/uperf-linux
sudo cp ../config/sm8550.json /etc/uperf-linux/config.json
sudo cp ../config/perapp_powermode /etc/uperf-linux/
sudo mkdir -p /run/uperf-linux
sudo cp ../systemd/uperf-linux.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now uperf-linux.service
```

### Usage

```bash
# Check status
sudo uperfctl status

# Switch power mode
sudo uperfctl mode performance    # Full throttle
sudo uperfctl mode balance        # Balanced (default)
sudo uperfctl mode powersave      # Battery saving

# List detected game processes
sudo uperfctl game-list

# View logs
journalctl -u uperf-linux -f
```

## Configuration

The main config file is `/etc/uperf-linux/config.json`. Key sections:

### Power Model

Defines per-cluster performance characteristics:

```json
"powerModel": [
  {
    "efficiency": 350,        /* Relative IPC score (Cortex-A53@1GHz = 100) */
    "nr": 1,                  /* Number of cores in this cluster */
    "typicalPower": 1.2,      /* Single-core power at typicalFreq (W) */
    "typicalFreq": 2400,      /* Normal operating frequency (MHz) */
    "sweetFreq": 1800,        /* Most energy-efficient frequency (MHz) */
    "plainFreq": 1400,        /* Linear region boundary (MHz) */
    "freeFreq": 600           /* Minimum efficient frequency (MHz) */
  }
]
```

### Sysfs Knobs

Maps knob names to sysfs paths. `%d` is expanded per-CPU:

```json
"knob": {
  "cpufreqMax": "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq",
  "gpuMaxFreq": "/sys/class/devfreq/soc\\:qcom\\:gpu/max_freq"
}
```

### Presets

Three power modes, each with per-scene overrides:

```json
"presets": {
  "balance": {
    "*": { "cpu.margin": 0.2 },
    "idle": { "cpu.baseSampleTime": 0.04 },
    "touch": { "cpu.margin": 0.4 }
  }
}
```

## Design Decisions vs. Android Original

| Feature | Android uperf | uperf-linux |
|---------|--------------|-------------|
| Binary | NDK r24 (Bionic libc) | GCC/Clang (glibc/musl) |
| Process model | Shell scripts + binary | Single systemd daemon |
| cgroup | `/dev/cpuset/*/tasks` | cgroup v2 hierarchy |
| GPU control | kgsl sysfs | devfreq (`msm_dvfs`) |
| Task scheduling | cpuset assignment | cgroup v2 + uClamp |
| Render hints | SurfaceFlinger injection | N/A (dropped) |
| SoC detection | `getprop` | `/sys/devices/soc0/` |
| Service mgmt | Magisk lifecycle | systemd |

## License

MIT License (same as the original uperf project's Apache 2.0 spirit — adapt as needed).

## Development Roadmap

- [x] Project scaffolding + CMake build system
- [x] Logging subsystem (journald + file + stderr)
- [x] JSON config parser with validation + hot-reload
- [x] Sysfs writer with batching + deduplication
- [x] Power model (P-F curve, sweet spot selection)
- [x] State machine (7 scenes, timer-based transitions)
- [x] Input monitor (evdev touch event parsing)
- [x] cgroup v2 manager (slices, uClamp, CPU pinning)
- [x] HeavyLoad detector (/proc/stat polling)
- [x] Game scanner (/proc scanning)
- [x] SM8550 config
- [x] CLI tool (uperfctl)
- [x] systemd service unit
- [ ] Unit tests (expand test coverage)
- [ ] Thermal awareness (read /sys/class/thermal/)
- [ ] DRM/Modesetting hook (Linux equivalent of SfAnalysis)
- [ ] DBus interface for remote control
- [ ] Per-app power mode auto-switching
- [ ] Config generation wizard for new SoCs
