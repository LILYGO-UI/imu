#include "pages/imu/imu_model.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float gravity = 9.80665f, position_response = 0.10f;
constexpr float pulse_deadzone = 0.8f, pulse_full_acceleration = 4.0f;
constexpr float pulse_baseline_response = 0.35f, pulse_attack = 0.025f, pulse_release = 0.13f;
constexpr float pulse_max_growth = 2.0f;
float length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 multiply(Vec3 v, float k)
{
    return {v.x * k, v.y * k, v.z * k};
}
Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool valid(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
}  // namespace

ImuModel::ImuModel()
{
    resize(568, 800, 24);
}

void ImuModel::resize(float width, float height, float radius)
{
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(radius) || radius <= 0 ||
        width < 2 * radius + 4 || height < 2 * radius + 4)
        return;
    width_  = width;
    height_ = height;
    radius_ = radius;
}

void ImuModel::update(const ImuSample &sample, float dt)
{
    if (!valid(sample.acceleration) || !valid(sample.angular_velocity) || !std::isfinite(dt) || dt <= 0) return;
    dt                    = std::min(dt, 0.05f);
    const auto accel      = sample.acceleration;
    const float magnitude = length(accel);
    if (calibrating_) {
        // A stationary sensor can have a substantial constant gyro bias.
        // Require stable readings over the window, not an already-zero gyro.
        const float scale = 1.0f / std::max(calibration_samples_, 1);
        const bool still  = std::abs(magnitude - gravity) < 0.8f && length(sample.angular_velocity) < 0.6f &&
                           (calibration_samples_ == 0 ||
                            (length(subtract(accel, multiply(accel_sum_, scale))) < 0.25f &&
                             length(subtract(sample.angular_velocity, multiply(gyro_sum_, scale))) < 0.05f));
        if (!still) {
            calibration_time_    = 0;
            calibration_samples_ = 0;
            accel_sum_ = gyro_sum_ = {};
        } else {
            calibration_time_ += dt;
            ++calibration_samples_;
            accel_sum_ = add(accel_sum_, accel);
            gyro_sum_  = add(gyro_sum_, sample.angular_velocity);
            if (calibration_time_ >= 1.0f) {
                neutral_     = multiply(accel_sum_, 1.0f / length(accel_sum_));
                bias_        = multiply(gyro_sum_, 1.0f / calibration_samples_);
                gravity_     = neutral_;
                initialized_ = true;
                calibrating_ = false;
                reset();
            }
        }
        return;
    }
    auto omega = subtract(sample.angular_velocity, bias_);
    omega.x    = std::clamp(omega.x, -12.0f, 12.0f);
    omega.y    = std::clamp(omega.y, -12.0f, 12.0f);
    omega.z    = std::clamp(omega.z, -12.0f, 12.0f);
    if (!initialized_ && magnitude > 1) {
        gravity_     = multiply(accel, 1.0f / magnitude);
        initialized_ = true;
    }
    // Predict gravity in sensor coordinates from gyro; correct only near 1 g
    // so a brief translation is not interpreted entirely as board tilt.
    gravity_ = subtract(gravity_, multiply(cross(omega, gravity_), dt));
    if (std::abs(magnitude - gravity) < 1.8f) {
        const float correction = 1 - std::exp(-dt / 0.45f);
        gravity_               = add(multiply(gravity_, 1 - correction), multiply(accel, correction / magnitude));
    }
    gravity_ = multiply(gravity_, 1.0f / std::max(length(gravity_), 0.001f));

    // Remove gravity and the slowly varying sensor offset so only a transient
    // acceleration changes the size. Keep this baseline current while paused.
    const auto linear = subtract(accel, multiply(gravity_, gravity));
    if (!pulse_initialized_) {
        acceleration_baseline_ = linear;
        pulse_initialized_     = true;
    }
    const float dynamic_acceleration = length(subtract(linear, acceleration_baseline_));
    acceleration_baseline_           = add(acceleration_baseline_, multiply(subtract(linear, acceleration_baseline_),
                                                                            1 - std::exp(-dt / pulse_baseline_response)));
    if (paused_) return;
    const float pulse_target =
        std::clamp((dynamic_acceleration - pulse_deadzone) / pulse_full_acceleration, 0.0f, 1.0f);
    const float pulse_response = pulse_target > pulse_ ? pulse_attack : pulse_release;
    pulse_ += (1 - std::exp(-dt / pulse_response)) * (pulse_target - pulse_);
    const auto tilt = subtract(gravity_, neutral_);
    auto target     = b2Mul(b2Rot(rotation_ * b2_pi / 180), b2Vec2(tilt.x, tilt.y));
    target *= sensitivity_ / 100.0f;
    if (target.LengthSquared() > 1) target.Normalize();
    // A fixed attitude has a fixed position, so sensor bias cannot accumulate
    // into a trip to the edge. Smooth only the transition to that position.
    position_ += (1 - std::exp(-dt / position_response)) * (target - position_);
    angle_ = std::remainder(angle_ - omega.z * dt, 2 * b2_pi);
}

void ImuModel::reset()
{
    position_.SetZero();
    angle_             = 0;
    pulse_             = 0;
    pulse_initialized_ = false;
}

void ImuModel::disconnect()
{
    initialized_       = false;
    calibrating_       = false;
    pulse_initialized_ = false;
}

void ImuModel::calibrate()
{
    calibration_time_    = 0;
    calibration_samples_ = 0;
    accel_sum_ = gyro_sum_ = {};
    calibrating_           = true;
    reset();
}

void ImuModel::toggle_pause()
{
    paused_ = !paused_;
}
void ImuModel::set_sensitivity(int percent)
{
    sensitivity_ = std::clamp(percent, 25, 200);
}
void ImuModel::set_rotation(int degrees)
{
    rotation_ = ((degrees % 360) + 360) % 360;
}
int ImuModel::calibration_progress() const
{
    return std::clamp(static_cast<int>(calibration_time_ * 100), 0, 100);
}
BallState ImuModel::ball() const
{
    const float enclosure_radius = std::min(width_, height_) * 0.5f;
    const float visible_radius   = std::min(radius_ * (1 + pulse_max_growth * pulse_), enclosure_radius - 2);
    auto offset                  = (enclosure_radius - radius_) * position_;
    const float limit            = enclosure_radius - visible_radius;
    const float distance         = offset.Length();
    if (distance > limit) offset *= limit / distance;
    return {width_ * 0.5f + offset.x, height_ * 0.5f + offset.y, angle_, visible_radius};
}
