# LILYGO IMU

**English** | [简体中文](README.zh-CN.md)

LILYGO IMU is a six-axis motion viewer for T-DisplayCM0, built with C++17,
[LILYGO UI AppKit](https://github.com/LILYGO-UI/cm0-appkit) and LVGL 9.5.
It reads the onboard QMI8658B accelerometer and gyroscope through Linux IIO,
maps board tilt to a ball inside a circular arena, and displays live six-axis
readings.

The project provides a CM0 ARM64 Linux build and an SDL2 desktop simulator
for development and testing. Sensor operation does not require internet access.

## Preview

<p>
  <img src="assets/previews/preview1.png" alt="IMU simulator in portrait orientation" width="240">
  <img src="assets/previews/preview-landscape.png" alt="IMU simulator in landscape orientation" width="520">
</p>

These previews use simulated input and do not represent hardware validation.
The current interface uses Chinese labels.

## Features

- **Tilt visualization:** the ball smoothly follows board orientation and stays
  at a stable position while the angle is held. Its full radius stays inside
  the circular boundary.
- **Acceleration feedback:** sudden acceleration enlarges the ball up to three
  times its normal size, subject to available space, then it smoothly recovers.
- **Six-axis telemetry:** acceleration in `m/s^2` and angular velocity in `rad/s`,
  with a white ball marker responding to rotation around the sensor Z axis.
- **Motion controls:** pause/resume, reset, one-second stationary calibration,
  and sensitivity from 25% to 200%.
- **Responsive layout:** portrait, landscape, and compact windows; short windows
  prioritize the motion area and controls over telemetry.
- **Hardware and demo sources:** background IIO sampling, automatic reconnection,
  stale-data detection, and an explicitly identified simulation mode.

The ball visualizes attitude and transient acceleration; it does not measure
physical displacement. Calibration is retained only for the current run.
See [sensor and calibration details](docs/02-sensor-and-calibration.md).

## Controls

The top buttons are pause/resume, reset, and calibrate, from left to right.
Hover or long-press to show their names.

| Control | Behavior |
| --- | --- |
| Pause/resume | Freeze the ball's position, size, and marker; sensor sampling and telemetry continue. Resume follows the current orientation. |
| Reset | Center the ball and restore its normal size and marker angle. Calibration and sensitivity are kept; ongoing updates move it back to the position selected by the current tilt. |
| Calibrate | Hold the board still in the desired neutral orientation for about one second. Movement restarts the accumulation. Saves the neutral direction and gyro bias for this run. Unavailable when disconnected. |
| Sensitivity | Adjust the position response from 25% to 200%; default is 100%. |

The status shows `实时` (live), `模拟` (demo), `已暂停` (paused),
`校准 N%` (calibrating), or `未连接` (disconnected). A disconnected sensor
freezes the ball and shows unavailable axis readings. It never silently falls
back to simulated readings.

## Development Environment

Project identity, version, and package metadata come from [`lpm.toml`](lpm.toml).
[`CMakePresets.json`](CMakePresets.json) defines `host-simulator` and `cm0-cross`.
Run all project commands from the project root.

### Source Dependencies

For a Git checkout, initialize the pinned AppKit and Box2D dependencies:

```sh
git submodule update --init --recursive
```

For a source copy without Git metadata, ensure `third_party/cm0-appkit` is
populated, including its bundled LVGL archive and font assets, and ensure
`third_party/box2d` is checked out. The simulator
consumes AppKit directly from this directory; a separate installation is not
required. Both presets use its LVGL 9.5.0 archive with network fetching disabled.
Box2D 2.4.1 is pinned as a Git submodule; no system Box2D package is needed.
See [third-party sources](third_party/README.md).

### Host Dependencies

Use CMake 3.24 or later (including CTest and CPack), Python 3.11 or later,
and a C/C++ compiler with C++17 support. Although the project declares CMake
3.21 as its minimum, AppKit's bundled LVGL extraction uses
`DOWNLOAD_EXTRACT_TIMESTAMP`, introduced in CMake 3.24.
The presets use Unix Makefiles. AppKit requires `pkg-config`, SDL2, FreeType,
and `patch` for its bundled LVGL patches.

#### Debian 12 / Ubuntu 24.04 or Later

```sh
sudo apt update
sudo apt install build-essential cmake git pkg-config patch libsdl2-dev \
  libfreetype6-dev ca-certificates python3 python3-venv python3-pip
```

On older distributions, upgrade CMake and Python to the versions above.

#### macOS

Install the Xcode Command Line Tools if needed and dependencies through
[Homebrew](https://brew.sh/). Complete Homebrew's shell setup so its tools
are available on `PATH`:

```sh
xcode-select --install
brew install git cmake pkg-config patch sdl2 freetype python
```

#### LPM

CMake invokes `lpm metadata --format cmake` during configuration. Install
[LPM](https://github.com/LILYGO-UI/lpm) in a project-local Python environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install "git+https://github.com/LILYGO-UI/lpm.git"
lpm --version
```

Reactivate it with `. .venv/bin/activate` in each new terminal. CMake searches
`.venv/bin` as well as `PATH`; an existing installation can be selected with
`-DLPM_EXECUTABLE=/absolute/path/to/lpm` during configuration.

Check the environment before building:

```sh
cmake --version
python --version
pkg-config --modversion sdl2 freetype2
lpm metadata --format cmake
```

### Device Cross Compiler

Skip this step for simulator-only development. Device builds need an AArch64
Linux cross compiler on the development machine.

**Debian / Ubuntu:**

```sh
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

**Apple Silicon macOS:**

```sh
brew install messense/macos-cross-toolchains/aarch64-unknown-linux-gnu
```

The BSP toolchain expects these executable names on `PATH`:

```sh
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-g++ --version
```

See [CM0BspBuilder](https://github.com/LILYGO-UI/CM0BspBuilder) for SDK host
requirements. The BSP supplies target headers and libraries but does not
install the host cross compiler.

## Build and Test

### Desktop Simulator

Build and launch with an explicit demo source on either supported host platform:

```sh
LILYGO_IMU_DEMO=1 lpm start --foreground
```

To reuse a build, run `LILYGO_IMU_DEMO=1 lpm start --no-build --foreground`.
The Debug build and tests are stored in `build/host-simulator`. The interactive
simulator requires a graphical desktop session.

With no source overrides, macOS uses demo data and Linux attempts to read
hardware. Without an accessible IMU, Linux shows a disconnected state.
See [runtime configuration](#runtime-configuration) to choose a source explicitly.

### Tests

Configure, build, and run all host tests:

```sh
lpm test
```

| Test | Coverage |
| --- | --- |
| `imu-core` | Stable tilt mapping, calibration, gyro input, acceleration pulses, circular limits, rotation, IIO conversion, invalid samples, and reconnection |
| `imu-view-model` | Commands, Subject notifications, connection state, and expired samples |
| `imu-render-portrait` | Rendering and interactions at `568 x 1232` |
| `imu-render-landscape` | Rendering and interactions at `1232 x 568` |
| `imu-render-compact` | Rendering and interactions at `320 x 480` |
| `imu-render-short` | Rendering and interactions at `320 x 240` |

Each render test writes `imu-<viewport>.ppm` and `imu-<viewport>-pulse.ppm`
under `build/host-simulator`. These use synthetic sensor input. Inspect both
normal and enlarged-ball images at all four sizes after UI changes; passing
assertions do not replace visual review or device testing. See the
[UI development guidelines](docs/01-user-interface.md).

### CM0 Device Build

Cross-compile and create the ARM64 Debian package:

```sh
lpm pack
```

The `cm0-cross` preset targets CM0 Raspberry Pi OS on AArch64, uses Release
mode, disables host tests, and installs under `/usr`. On first configuration,
the [toolchain wrapper](cmake/cm0-bsp-toolchain.cmake) downloads CM0 BSP `0.1.0`,
verifies its pinned SHA-256, and extracts it to `.cache/cm0-bsp/0.1.0/sysroot`.
GitHub access is needed for this initial download; later runs reuse the cache.
To change the cache root, configure with
`-DCM0_BSP_CACHE_DIR=/absolute/path/to/cache`. The version and `sysroot`
subdirectories are appended automatically. No sibling BSP checkout is required.

The SDK sysroot must provide target headers and libraries, including DRM and
FreeType. Host libraries cannot substitute for missing target dependencies.
CPack writes the `.deb` to `dist/`, containing the executable, Launcher entry
and icon, desktop entry, AppStream metadata, and Box2D/shared-font license notices.
The target must provide `libc6`, `libstdc++6`, `libdrm2`, `libfreetype6`, and
`lilygo-ui-appkit-dev >= 0.1.0`. AppKit supplies the shared runtime fonts.

Copy the generated package to the device and install it there (adjust the
filename when the version changes):

```sh
sudo apt install ./lilygo-ui-imu_0.1.0_arm64.deb
lilygo-ui-imu
```

The Launcher title is `IMU`, the package and executable are `lilygo-ui-imu`,
and the application ID is `cc.lilygo.ui.Imu`. Device display/input permissions
and the Launcher exit gesture follow AppKit's DRM/KMS runtime.

<details>
<summary>Underlying CMake commands</summary>

LPM wraps these commands. Direct CMake configuration still requires LPM to
read the project metadata.

Host build and tests:

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

Device build and packaging:

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

</details>

## Runtime Configuration

The onboard QMI8658B is on `i2c1` at `0x6a`. The board overlay uses the
`qst,qmi8658c` compatible driver, so its IIO name is normally `qmi8658c`.
The application also discovers `qmi8658b` and `qmi8658` names under
`/sys/bus/iio/devices`. The running user needs read access to the raw channels
and scales; the application does not access I2C directly or reconfigure the chip.

| Variable | Behavior |
| --- | --- |
| `LILYGO_IMU_DEMO` | Exactly `1` forces demo mode and takes precedence over the device path. Any other set value selects IIO. When unset, Linux uses IIO; macOS uses demo unless `LILYGO_IMU_DEVICE` is set. |
| `LILYGO_IMU_DEVICE` | Select a specific IIO device directory. An empty value uses automatic discovery and also disables the macOS default demo. |
| `LILYGO_IMU_ROTATION` | Extra in-plane rotation in integer degrees, added to the AppKit display rotation. Default `0`; common values are `0`, `90`, `180`, and `270`. Telemetry stays in sensor coordinates. |

Examples after a host build:

```sh
# Explicit IIO device, including a sysfs-style test directory on macOS
LILYGO_IMU_DEMO=0 LILYGO_IMU_DEVICE=/sys/bus/iio/devices/iio:device0 \
  ./build/host-simulator/lilygo-ui-imu

# Exercise the missing-device state on a host without an IMU
LILYGO_IMU_DEMO=0 lpm start --no-build --foreground

# Extra mounting rotation on the installed device application
LILYGO_IMU_ROTATION=90 lilygo-ui-imu
```

The driver exposes sequential direct-mode channel reads, not a synchronized
six-axis frame. Sampling waits about 10 ms between successful reads; the UI
ticks about every 16 ms. Failed reads trigger reconnection attempts about once
per second, and samples older than 250 ms stop motion. See
[sensor and calibration](docs/02-sensor-and-calibration.md) for units, timing,
calibration behavior, and diagnostic steps.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| `lpm is required to read project metadata` | Install LPM in `.venv`, put it on `PATH`, or set `LPM_EXECUTABLE`. |
| `LilyGoUI`, LVGL, or Box2D source is missing | Initialize both submodules in a Git checkout, or obtain a complete source copy including `third_party/cm0-appkit` and `third_party/box2d`. |
| SDL2 or FreeType is missing | Install the host development packages; for cross builds, check the target sysroot. |
| Homebrew libraries are not found | Configure with `cmake --preset host-simulator -DCMAKE_PREFIX_PATH="$(brew --prefix)"`. |
| BSP download or compiler lookup fails | Check GitHub access, disk space, and that `aarch64-linux-gnu-gcc` and `aarch64-linux-gnu-g++` are on `PATH`. |
| Status stays disconnected | Check the board overlay/driver, IIO device path and read permissions. Run in the foreground to see IIO diagnostics; see the [sensor guide](docs/02-sensor-and-calibration.md). |
| Ball is off-center or moves in the wrong direction | Calibrate while still, then check display rotation and `LILYGO_IMU_ROTATION`. Reset alone does not change the neutral orientation. |
| Calibration repeatedly restarts | Keep the board still for about one second. The animated demo source is not a stationary calibration reference. |

## Documentation

| Guide | Contents |
| --- | --- |
| [Sensor and calibration](docs/02-sensor-and-calibration.md) | IIO channels, source selection, timing, motion mapping, calibration, and diagnostics |
| [Architecture overview](docs/00-overview.md) (Chinese) | Module boundaries, worker thread, data flow, and build integration |
| [UI development guidelines](docs/01-user-interface.md) (Chinese) | Layout, state display, typography, ownership, and validation |

## Acknowledgments

The interface uses LILYGO UI AppKit and [LVGL](https://lvgl.io/).
[Box2D](https://github.com/erincatto/box2d) supplies the model's 2D vector and
rotation math. Its MIT license and AppKit's shared-font license notices are
included in the installation package.
