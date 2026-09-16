#include "pages/imu/imu_view_model.hpp"

ImuViewModel::ImuViewModel(std::unique_ptr<SensorSource> source) : source_(std::move(source))
{
}

void ImuViewModel::tick(float dt)
{
    snapshot_ = source_->snapshot();
    if (snapshot_.connected &&
        std::chrono::steady_clock::now() - snapshot_.timestamp > std::chrono::milliseconds(250)) {
        snapshot_.connected = false;
        snapshot_.error     = "IMU sample timed out";
    }
    if (snapshot_.connected)
        model_.update(snapshot_.sample, dt);
    else
        model_.disconnect();
    publish();
}

void ImuViewModel::resize(int width, int height, int radius)
{
    model_.resize(width, height, radius);
    publish();
}
void ImuViewModel::reset()
{
    model_.reset();
    publish();
}
void ImuViewModel::calibrate()
{
    if (snapshot_.connected) model_.calibrate();
    publish();
}
void ImuViewModel::toggle_pause()
{
    model_.toggle_pause();
    publish();
}
void ImuViewModel::set_sensitivity(int percent)
{
    model_.set_sensitivity(percent);
    publish();
}
void ImuViewModel::set_rotation(int degrees)
{
    model_.set_rotation(degrees);
}
void ImuViewModel::publish()
{
    revision_.set(revision_.value() == INT32_MAX ? 0 : revision_.value() + 1);
}

std::string ImuViewModel::status() const
{
    if (!snapshot_.connected) return "未连接";
    if (model_.calibrating()) return "校准 " + std::to_string(model_.calibration_progress()) + "%";
    if (model_.paused()) return "已暂停";
    return snapshot_.simulated ? "模拟" : "实时";
}
