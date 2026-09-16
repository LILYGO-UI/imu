#pragma once

#include "pages/imu/imu_sensor.hpp"
#include <box2d/box2d.h>

struct BallState {
    float x = 0, y = 0, angle = 0;
    float radius = 24;
};

class ImuModel {
public:
    ImuModel();
    void resize(float width, float height, float radius);
    void update(const ImuSample &sample, float dt);
    void reset();
    void disconnect();
    void calibrate();
    void toggle_pause();
    void set_sensitivity(int percent);
    void set_rotation(int degrees);
    [[nodiscard]] BallState ball() const;
    [[nodiscard]] bool paused() const
    {
        return paused_;
    }
    [[nodiscard]] bool calibrating() const
    {
        return calibrating_;
    }
    [[nodiscard]] int calibration_progress() const;
    [[nodiscard]] int sensitivity() const
    {
        return sensitivity_;
    }

private:
    b2Vec2 position_{0, 0};  // Fraction of the circular ball-centre travel radius.
    float width_ = 568, height_ = 800, radius_ = 24, angle_ = 0;
    Vec3 gravity_{0, 0, 1}, neutral_{0, 0, 1}, bias_{}, accel_sum_{}, gyro_sum_{};
    Vec3 acceleration_baseline_{};
    float pulse_             = 0;
    float calibration_time_  = 0;
    int calibration_samples_ = 0, sensitivity_ = 100, rotation_ = 0;
    bool initialized_ = false, paused_ = false, calibrating_ = false;
    bool pulse_initialized_ = false;
};
