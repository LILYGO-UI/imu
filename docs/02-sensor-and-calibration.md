# Sensor and Calibration

[English README](../README.md) | [中文 README](../README.zh-CN.md) | [Architecture](00-overview.md) | [User Interface](01-user-interface.md)

## Hardware and IIO Input

The T-DisplayCM0 has a QMI8658B on `i2c1` at address `0x6a`. The board
overlay uses the compatible `qst,qmi8658c` driver, so the IIO device name is
normally `qmi8658c`. The application also recognizes `qmi8658b` and `qmi8658`.
It reads the kernel's IIO sysfs interface; it does not access I2C directly or
change the sensor configuration owned by the kernel.

Without a configured device path, the application searches
`/sys/bus/iio/devices` and selects the first entry whose `name` matches one of
these names. When multiple matching devices are present, specify the desired
directory explicitly instead of depending on discovery order.

| Input | Raw attributes | Shared scale | Converted unit |
| --- | --- | --- | --- |
| Acceleration, including gravity | `in_accel_{x,y,z}_raw` | `in_accel_scale` | `m/s^2` |
| Angular velocity | `in_anglvel_{x,y,z}_raw` | `in_anglvel_scale` | `rad/s` |

Each channel uses `raw * scale`. A readable, numeric per-axis scale, such as
`in_accel_x_scale`, takes precedence over the shared scale. If the per-axis
scale cannot be read or parsed, the shared scale is used. Scales must be finite
and positive. Raw values must be finite integers in the signed 16-bit range,
`-32768` through `32767`; invalid data rejects the whole sample. All six channels
are required, and the runtime user needs read permission for their attributes.
Scale values are loaded when the device opens and reloaded on reconnection.

The current board driver supports IIO direct mode. The application reads the
six axes sequentially, so they are not a hardware-synchronized frame. A worker
thread waits 10 ms after each successful read cycle; I2C and sysfs latency add
to this interval. The LVGL timer updates the interface approximately every
16 ms using the latest available snapshot. These are scheduling intervals,
not guaranteed sampling rates.

## Source Selection

Set environment variables before starting the application:

| Variable | Behavior when set | Default when unset |
| --- | --- | --- |
| `LILYGO_IMU_DEMO` | Exactly `1` selects simulated data and overrides `LILYGO_IMU_DEVICE`. Every other value selects IIO input. | Linux selects IIO. Non-Linux platforms, including macOS, select simulation unless `LILYGO_IMU_DEVICE` is present. |
| `LILYGO_IMU_DEVICE` | An IIO directory path selects that device directly, without checking its `name`. An empty value requests automatic discovery and also disables the non-Linux default simulation. | Automatically discover the IIO device when IIO input is selected. |
| `LILYGO_IMU_ROTATION` | An additional integer planar rotation in degrees, added to the AppKit display rotation. | `0` additional degrees. |

There is no automatic fallback from unavailable hardware to simulated data.
Simulation is explicitly identified in the interface. The application currently
has no environment variables for sampling interval, sensitivity, calibration,
or pulse response.

```sh
# Linux / target board: discover the QMI8658 device.
lilygo-ui-imu

# Select an exact IIO directory and explicitly request hardware input.
LILYGO_IMU_DEMO=0 \
  LILYGO_IMU_DEVICE=/sys/bus/iio/devices/iio:device0 \
  lilygo-ui-imu

# Enable simulated data on any platform.
LILYGO_IMU_DEMO=1 ./build/host-simulator/lilygo-ui-imu

# Check the disconnected state on macOS without enabling simulation.
LILYGO_IMU_DEMO=0 ./build/host-simulator/lilygo-ui-imu
```

A directory containing the required raw and scale files can also be selected
with `LILYGO_IMU_DEVICE` for development on macOS. Such files are test input;
selecting this path does not turn them into measurements from a physical sensor.

## Connection and Failure Behavior

A failed open or channel read marks the source disconnected, closes all channel
descriptors, and schedules another open attempt after approximately one second.
If no fixed device path is configured, each attempt repeats discovery. Changed
read/open errors are printed to standard error with the `IMU:` prefix; the same
error is not printed on every retry.

The view model independently rejects a connected snapshot that is more than
250 ms old and stores `IMU sample timed out` in its snapshot error field.
This message is not printed to standard error or displayed verbatim; the UI
shows the same disconnected state as other input failures. This prevents movement
based on stale measurements even if the worker has not yet reported a failure.
The timeout itself does not restart the worker or change its retry interval.

While disconnected, the ball retains its position, angle, and size. Ongoing
calibration is cancelled, and calibration is unavailable until connected input
returns. The saved neutral orientation, gyro bias, sensitivity, and pause state
remain in memory. A fresh sample reinitializes the gravity estimate and pulse
baseline; motion resumes unless the application is paused.

## Attitude, Rotation, and Pulse Feedback

The ball shows attitude, not measured travel or physical displacement. A
complementary filter predicts the gravity direction from angular velocity and
corrects it from acceleration when its magnitude is within `1.8 m/s^2` of
standard gravity (`9.80665 m/s^2`). Short acceleration impulses therefore do not
entirely become apparent tilt.

The normalized gravity direction minus the calibrated neutral direction selects
a target position. Its X/Y components are rotated into the display plane,
multiplied by sensitivity, and limited to the unit circle. The ball approaches
that position with a 0.10-second smoothing time constant. A fixed tilt or
constant sensor offset produces a stable position rather than continually
accumulating displacement.

With no display or additional rotation, positive sensor X moves the ball right
and positive sensor Y moves it down. AppKit display rotation is applied first;
`LILYGO_IMU_ROTATION` adds a mounting adjustment. Common values are `0`, `90`,
`180`, and `270`; integer angles are normalized modulo 360. For example:

```sh
LILYGO_IMU_ROTATION=90 lilygo-ui-imu
```

The six-axis telemetry always remains in the original sensor coordinate system.
The white marker integrates bias-corrected sensor Z angular velocity and is a
rotation cue, not an absolute heading. The correct mounting adjustment and
comfortable sensitivity still need checking on the physical board.

Sudden acceleration also enlarges the ball. The model subtracts estimated
gravity and a slowly adapting acceleration baseline, ignores the first
`0.8 m/s^2` of residual magnitude, and ramps toward maximum growth over the
next `4.0 m/s^2`. This suppresses small noise and steady sensor offsets. The
baseline has a 0.35-second time constant, so sustained acceleration gradually
stops contributing to the size effect.

The maximum requested radius is three times the normal radius. Attack and
release time constants are 25 ms and 130 ms, respectively: about 80 ms toward a
sustained peak and about 400 ms toward the normal size once the pulse target
falls to zero. These are approximate response times, not fixed animation
durations. Position continues following attitude during a pulse. The visible
radius is capped to fit the arena, and the center is radially constrained using
that enlarged radius so the entire ball stays inside the circle.

## Calibration and Controls

| Control | Effect |
| --- | --- |
| Pause / resume | Freezes the displayed ball's position, angle, and size. Sampling, telemetry, attitude estimation, and the acceleration baseline continue updating. |
| Reset | Immediately centers the ball, resets the marker angle, and restores normal size. Preserves calibration, sensitivity, and pause state. With live input and motion enabled, the ball moves back toward the current attitude's position. |
| Calibrate | Centers the ball and restores normal size, then collects approximately one second of stable readings to save the neutral direction and gyro bias. Available only while connected. |
| Sensitivity | Scales position response from 25% to 200%, with a default of 100%. Does not change telemetry units or the pulse-growth parameters. |

To calibrate, place the board in the orientation that should mean centered and
keep it still until calibration completes. A horizontal board is convenient,
but any stable orientation can be the neutral position. Calibration also works
while paused and preserves the pause state afterward.

Calibration requires all of the following throughout the collection window:

- Acceleration magnitude is within `0.8 m/s^2` of standard gravity.
- Angular velocity magnitude is below `0.6 rad/s`.
- After the first accepted reading, acceleration differs from the accumulated
  mean by less than `0.25 m/s^2`, and angular velocity differs from its mean by
  less than `0.05 rad/s`.

Movement outside those limits restarts the collection window. The algorithm
allows a stable nonzero gyro bias, which is then subtracted during attitude
updates. It records a normalized neutral direction and a gyro bias, not a
factory accelerometer scale correction. Calibration and sensitivity are not
persisted across application restarts.

## Troubleshooting

| Symptom or diagnostic | Check |
| --- | --- |
| `QMI8658 IIO device not found` | Check that the board overlay and kernel driver expose an IIO device with a supported `name`. An absent driver cannot be repaired by setting application environment variables. |
| `Cannot read IIO scale` | Check that per-axis or shared scale attributes exist, are readable, and contain finite numbers. |
| `Cannot open IIO channel or invalid scale` | Check the reported raw attribute, read permissions, and that its selected scale is greater than zero. |
| `Cannot read IIO channel` or `Invalid scaled IIO sample` | Check the affected raw value and driver health. All six axes must be valid before a sample is accepted. |
| Disconnected state despite no new IIO error in the log | The latest measurement may be over 250 ms old. Check IIO read latency and the device/driver. The view model stores `IMU sample timed out` internally and freezes the ball while waiting for fresh input. |
| Hardware was expected but the status indicates simulation | Unset `LILYGO_IMU_DEMO` on Linux, or set it to `0` explicitly. A value of `1` overrides even an explicit device path. |
| Ball remains offset while the board is level | Calibrate in the intended neutral orientation. Constant sensor bias and mounting orientation can otherwise produce a fixed offset. |
| Calibration repeatedly returns to zero progress | Keep the board still; check the magnitude and stability conditions above. A disconnected or stale source cancels calibration. |
| Tilt direction is incorrect | Check AppKit display rotation, then set `LILYGO_IMU_ROTATION` for the PCB mounting orientation. Raw telemetry axes do not rotate. |
| Ball stays enlarged while paused or disconnected | This is the retained display state. Resume with fresh samples to recover, or use Reset to restore normal size immediately. |

To inspect discovery names on the target board:

```sh
for device in /sys/bus/iio/devices/iio:device*; do
  printf '%s: ' "$device"
  cat "$device/name"
done
```

The automated tests use temporary IIO-like files and synthetic samples, including
a recorded stationary target-board sample. They cover conversion, invalid
readings, reconnection, stale snapshots, calibration, rotation, stable tilt,
pulse feedback, and circular bounds. They do not establish the live I2C sampling
rate, synchronized acquisition, absolute heading accuracy, or the correct
physical mounting orientation. See the [README](../README.md) for build and
test commands.
