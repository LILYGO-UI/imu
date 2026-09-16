#include "pages/imu/imu_model.hpp"

#include <algorithm>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <unistd.h>

namespace {
constexpr float g = 9.80665f;
void advance(ImuModel &model, ImuSample sample, int count = 120)
{
    for (int i = 0; i < count; ++i) model.update(sample, 1.0f / 120);
}
bool near(float a, float b, float epsilon = 0.05f)
{
    return std::abs(a - b) < epsilon;
}
void write(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream(path) << text;
}
void assert_inside_circle(const BallState &ball, float width = 568, float height = 800)
{
    const float dx = ball.x - width * 0.5f, dy = ball.y - height * 0.5f;
    assert(std::sqrt(dx * dx + dy * dy) + ball.radius <= std::min(width, height) * 0.5f + 0.001f);
}
void test_acceleration_pulse()
{
    const ImuSample level{{0, 0, g}, {}};
    const ImuSample hardware_level{{-0.3783855f, -0.7063851f, -9.5718959f}, {-0.2521988f, -0.1634653f, -0.0008324f}};
    for (const auto sample : {level, hardware_level}) {
        ImuModel stationary;
        for (int i = 0; i < 7200; ++i) {
            stationary.update(sample, 1.0f / 120);
            assert(near(stationary.ball().radius, 24, 0.001f));
        }
    }
    ImuModel noisy;
    advance(noisy, level);
    for (int i = 0; i < 600; ++i) {
        noisy.update({{i % 2 ? 0.1f : -0.1f, 0.05f, g + (i % 2 ? 0.05f : -0.05f)}, {}}, 1.0f / 120);
        assert(near(noisy.ball().radius, 24, 0.001f));
    }

    // Both lateral and vertical impulses should grow quickly, then recover.
    for (const auto pulse : {ImuSample{{6, 0, g}, {}}, ImuSample{{-6, 0, g}, {}}, ImuSample{{0, 0, g + 6}, {}},
                             ImuSample{{0, 0, g - 6}, {}}}) {
        ImuModel response;
        advance(response, level);
        for (int i = 0; i < 10; ++i) {
            response.update(pulse, 1.0f / 120);
            assert(response.ball().radius >= 24 && response.ball().radius <= 24 * 3.0f + 0.001f);
            assert_inside_circle(response.ball());
        }
        const float peak_radius = response.ball().radius;
        assert(peak_radius > 24 * 2.0f);
        advance(response, level, 48);
        assert(response.ball().radius < peak_radius);
        advance(response, level, 192);
        assert(near(response.ball().radius, 24, 0.05f));
        advance(response, pulse, 10);
        const auto enlarged = response.ball();
        assert(enlarged.radius > 24 * 2.0f);
        response.toggle_pause();
        advance(response, level);
        assert(near(response.ball().radius, enlarged.radius, 0.001f));
        assert(near(response.ball().x, enlarged.x, 0.001f) && near(response.ball().y, enlarged.y, 0.001f));
        response.disconnect();
        response.update({{std::numeric_limits<float>::quiet_NaN(), 0, g}, {}}, 1.0f / 120);
        assert(near(response.ball().radius, enlarged.radius, 0.001f));
        assert(near(response.ball().x, enlarged.x, 0.001f) && near(response.ball().y, enlarged.y, 0.001f));
        response.toggle_pause();
        response.reset();
        assert(near(response.ball().radius, 24, 0.001f));
        advance(response, level, 240);
        advance(response, pulse, 10);
        assert(response.ball().radius > 24 * 2.0f);
        response.calibrate();
        assert(near(response.ball().radius, 24, 0.001f));
        advance(response, level, 180);
        assert(!response.calibrating() && near(response.ball().radius, 24, 0.001f));
    }

    ImuModel rotation;
    advance(rotation, level);
    for (int i = 0; i < 180; ++i) {
        const float angle = (i + 1) * 1.5f / 120;
        rotation.update({{-g * std::sin(angle), 0, g * std::cos(angle)}, {0, 1.5f, 0}}, 1.0f / 120);
        assert(near(rotation.ball().radius, 24, 0.001f));
    }

    // Pausing freezes the display, but must still track changes in baseline.
    ImuModel paused;
    advance(paused, level);
    const auto before_pause = paused.ball();
    paused.toggle_pause();
    advance(paused, {{0, 0, g + 6}, {}}, 240);
    assert(near(paused.ball().radius, before_pause.radius, 0.001f));
    assert(near(paused.ball().x, before_pause.x, 0.001f) && near(paused.ball().y, before_pause.y, 0.001f));
    paused.toggle_pause();
    for (int i = 0; i < 120; ++i) {
        paused.update({{0, 0, g + 6}, {}}, 1.0f / 120);
        assert(near(paused.ball().radius, 24, 0.001f));
    }

    ImuModel saturated;
    advance(saturated, level);
    advance(saturated, {{0, 0, g + 40}, {}}, 10);
    assert(saturated.ball().radius > 24 * 2.9f && saturated.ball().radius <= 24 * 3.0f + 0.001f);

    // Scaling alone must preserve interior positions selected by attitude.
    ImuModel interior;
    const float tilt_z = std::sqrt(g * g - 9);
    advance(interior, {{3, 0, tilt_z}, {}}, 600);
    const auto interior_position = interior.ball();
    const float boost            = (g + 6) / g;
    advance(interior, {{3 * boost, 0, tilt_z * boost}, {}}, 10);
    assert(interior.ball().radius > 24 * 2.0f);
    assert(near(interior.ball().x, interior_position.x, 0.001f));
    assert(near(interior.ball().y, interior_position.y, 0.001f));

    ImuModel edge;
    const float diagonal = g / std::sqrt(2.0f);
    edge.set_sensitivity(200);
    advance(edge, {{diagonal, diagonal, 0}, {}}, 600);
    for (int i = 0; i < 10; ++i) {
        edge.update({{diagonal * boost, diagonal * boost, 0}, {}}, 1.0f / 120);
        assert_inside_circle(edge.ball());
    }
    assert(edge.ball().radius > 24 * 2.0f);
    assert(near(edge.ball().x - 284, edge.ball().y - 400, 0.001f));
    edge.resize(120, 160, 16);
    assert(edge.ball().radius > 16 * 2.0f && edge.ball().radius <= 16 * 3.0f + 0.001f);
    assert_inside_circle(edge.ball(), 120, 160);
    edge.resize(52, 68, 24);
    assert(edge.ball().radius >= 24 && edge.ball().radius <= 26);
    assert_inside_circle(edge.ball(), 52, 68);
    advance(edge, {{diagonal, diagonal, 0}, {}}, 240);
    assert(near(edge.ball().radius, 24, 0.05f));
    assert_inside_circle(edge.ball(), 52, 68);
}
}  // namespace

int main()
{
    test_acceleration_pulse();
    ImuModel model;
    model.resize(568, 800, 24);
    const auto centre = model.ball();
    advance(model, {{0, 0, g}, {0, 0, 0}});
    assert(near(model.ball().x, centre.x) && near(model.ball().y, centre.y));

    // Constant offsets and tilts must select a position, without accumulating
    // displacement for as long as the board remains in that orientation.
    ImuModel stationary_level;
    const float level_bias = 0.10f;
    const float level_z    = std::sqrt(g * g - level_bias * level_bias);
    advance(stationary_level, {{level_bias, 0, level_z}, {0, 0, 0}}, 7200);
    const auto stationary_ball = stationary_level.ball();
    assert(near(stationary_ball.x - centre.x, 260 * level_bias / g));
    assert(near(stationary_ball.y, centre.y));
    for (const float tilt_x : {1.0f, 3.0f}) {
        ImuModel held_tilt;
        const ImuSample tilted{{tilt_x, 0, std::sqrt(g * g - tilt_x * tilt_x)}, {0, 0, 0}};
        advance(held_tilt, tilted, 600);
        const auto settled = held_tilt.ball();
        assert(near(settled.x - centre.x, 260 * tilt_x / g));
        assert(near(settled.y, centre.y));
        advance(held_tilt, tilted, 6600);
        assert(near(held_tilt.ball().x, settled.x) && near(held_tilt.ball().y, settled.y));
        advance(held_tilt, {{0, 0, g}, {0, 0, 0}}, 1200);
        assert(near(held_tilt.ball().x, centre.x) && near(held_tilt.ball().y, centre.y));
    }

    // A stationary sample captured on the target board includes substantial
    // gyro bias and reports less than nominal 1 g, with gravity along -Z.
    const ImuSample hardware_level{{-0.3783855f, -0.7063851f, -9.5718959f}, {-0.2521988f, -0.1634653f, -0.0008324f}};
    ImuModel hardware;
    advance(hardware, hardware_level, 1200);
    const auto hardware_settled = hardware.ball();
    advance(hardware, hardware_level, 6000);
    assert(near(hardware.ball().x, hardware_settled.x) && near(hardware.ball().y, hardware_settled.y));
    const float hardware_dx = hardware.ball().x - centre.x, hardware_dy = hardware.ball().y - centre.y;
    assert(hardware_dx * hardware_dx + hardware_dy * hardware_dy < 130 * 130);
    hardware.calibrate();
    advance(hardware, hardware_level, 180);
    assert(!hardware.calibrating());
    advance(hardware, hardware_level, 7200);
    assert(near(hardware.ball().x, centre.x) && near(hardware.ball().y, centre.y));

    ImuModel calibration_motion;
    calibration_motion.calibrate();
    for (int i = 0; i < 240; ++i) {
        calibration_motion.update({{i % 2 ? 1.0f : -1.0f, 0, std::sqrt(g * g - 1)}, {}}, 1.0f / 120);
    }
    assert(calibration_motion.calibrating() && calibration_motion.calibration_progress() == 0);
    for (int i = 0; i < 240; ++i) {
        calibration_motion.update({{0, 0, g}, {i % 2 ? 0.5f : -0.5f, 0, 0}}, 1.0f / 120);
    }
    assert(calibration_motion.calibrating() && calibration_motion.calibration_progress() == 0);

    advance(model, {{-3, 0, 9.336f}, {0, 0, 0}});
    assert(model.ball().x < centre.x - 30);
    ImuModel positive_x;
    advance(positive_x, {{3, 0, 9.336f}, {0, 0, 0}});
    assert(positive_x.ball().x > centre.x + 30 && near(positive_x.ball().y, centre.y));
    ImuModel positive_y;
    advance(positive_y, {{0, 3, 9.336f}, {0, 0, 0}});
    assert(positive_y.ball().y > centre.y + 30 && near(positive_y.ball().x, centre.x));
    ImuModel negative_y;
    advance(negative_y, {{0, -3, 9.336f}, {0, 0, 0}});
    assert(negative_y.ball().y < centre.y - 30 && near(negative_y.ball().x, centre.x));
    ImuModel sensitivity;
    const ImuSample sensitivity_tilt{{3, 0, std::sqrt(g * g - 9)}, {0, 0, 0}};
    sensitivity.set_sensitivity(25);
    advance(sensitivity, sensitivity_tilt, 600);
    assert(near(sensitivity.ball().x - centre.x, 260 * 3 / g * 0.25f));
    sensitivity.set_sensitivity(200);
    advance(sensitivity, sensitivity_tilt, 600);
    assert(near(sensitivity.ball().x - centre.x, 260 * 3 / g * 2));
    sensitivity.reset();
    assert(near(sensitivity.ball().x, centre.x) && near(sensitivity.ball().y, centre.y));
    sensitivity.update(sensitivity_tilt, 1.0f / 120);
    assert(sensitivity.ball().x > centre.x);
    advance(sensitivity, {{g / std::sqrt(2.0f), g / std::sqrt(2.0f), 0}, {}}, 1200);
    const float limit_dx = sensitivity.ball().x - centre.x, limit_dy = sensitivity.ball().y - centre.y;
    assert(near(std::sqrt(limit_dx * limit_dx + limit_dy * limit_dy), 260));
    for (int i = 0; i < 3000; ++i) {
        model.update({{-12, -12, g}, {1, 2, 3}}, 1.0f / 120);
        assert(model.ball().x >= 22 && model.ball().x <= 546);
        assert(model.ball().y >= 22 && model.ball().y <= 778);
        const float dx = model.ball().x - 284.0f, dy = model.ball().y - 400.0f;
        assert(dx * dx + dy * dy <= 260.1f * 260.1f);
    }
    model.toggle_pause();
    auto stopped = model.ball();
    advance(model, {{4, 4, 8}, {2, 1, 3}});
    assert(near(stopped.x, model.ball().x) && near(stopped.y, model.ball().y));
    model.toggle_pause();
    model.reset();
    assert(near(model.ball().x, centre.x));
    model.calibrate();
    advance(model, {{0, 0, g}, {1, 0, 0}}, 200);
    assert(model.calibrating() && model.calibration_progress() == 0);
    advance(model, {{1, -0.5f, 9.7427f}, {0.02f, 0, 0}}, 150);
    assert(!model.calibrating());
    stopped = model.ball();
    advance(model, {{1, -0.5f, 9.7427f}, {0.02f, 0, 0}});
    assert(near(model.ball().x, stopped.x, 1) && near(model.ball().y, stopped.y, 1));

    ImuModel gyro_model;
    advance(gyro_model, {{0, 0, g}, {0, 0, 0}});
    advance(gyro_model, {{0, 0, g}, {0, 1, 0}}, 40);
    assert(gyro_model.ball().x < centre.x - 1);
    const auto previous_angle = gyro_model.ball().angle;
    advance(gyro_model, {{0, 0, g}, {0, 0, 1}});
    assert(std::abs(gyro_model.ball().angle - previous_angle) > 0.1f);
    gyro_model.disconnect();
    stopped = gyro_model.ball();
    gyro_model.update({{std::numeric_limits<float>::quiet_NaN(), 0, g}, {}}, 1);
    assert(near(gyro_model.ball().x, stopped.x) && near(gyro_model.ball().y, stopped.y));
    gyro_model.resize(1232, 448, 16);
    assert(near((gyro_model.ball().x - 616) / 208, (stopped.x - centre.x) / 260, 0.0001f));
    assert(near((gyro_model.ball().y - 224) / 208, (stopped.y - centre.y) / 260, 0.0001f));

    ImuModel rotated;
    rotated.set_rotation(90);
    advance(rotated, {{-3, 0, 9.336f}, {0, 0, 0}});
    assert(rotated.ball().y < centre.y - 30 && near(rotated.ball().x, centre.x));

    char temporary[]      = "/tmp/lilygo-imu-test-XXXXXX";
    const auto *directory = mkdtemp(temporary);
    assert(directory);
    const std::filesystem::path root(directory), device = root / "iio:device7";
    std::filesystem::create_directory(device);
    write(device / "name", "qmi8658c\n");
    write(device / "in_accel_scale", "0.002394201\n");
    write(device / "in_anglvel_scale", "0.000272707\n");
    const char *channels[] = {"in_accel_x_raw",   "in_accel_y_raw",   "in_accel_z_raw",
                              "in_anglvel_x_raw", "in_anglvel_y_raw", "in_anglvel_z_raw"};
    const char *raw[]      = {"-4096\n", "0\n", "4096\n", "64\n", "-64\n", "0\n"};
    for (int i = 0; i < 6; ++i) write(device / channels[i], raw[i]);
    IioDevice reader;
    std::string error;
    ImuSample sample;
    assert(reader.open(root, {}, error));
    assert(reader.read(sample, error));
    assert(near(sample.acceleration.x, -g, 0.001f));
    assert(near(sample.angular_velocity.y, -0.017453248f, 0.00001f));
    write(device / channels[0], "2048\n");
    assert(reader.read(sample, error) && near(sample.acceleration.x, g / 2, 0.001f));
    write(device / channels[5], "nan\n");
    assert(!reader.read(sample, error));
    write(device / channels[5], "0\n");
    assert(reader.read(sample, error));
    write(device / "in_accel_scale", "0\n");
    assert(!reader.open(root, {}, error));
    assert(!reader.open(root / "missing", {}, error));
    reader.close();
    write(device / "in_accel_scale", "0.002394201\n");
    {
        IioSource live(root);
        auto wait_for_connection = [&live](bool expected) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                if (live.snapshot().connected == expected) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        };
        assert(wait_for_connection(true));
        write(device / channels[0], "bad\n");
        assert(wait_for_connection(false));
        write(device / channels[0], "0\n");
        assert(wait_for_connection(true));
    }
    std::filesystem::remove_all(root);
    return 0;
}
