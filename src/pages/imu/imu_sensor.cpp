#include "pages/imu/imu_sensor.hpp"

#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <unistd.h>

namespace {
constexpr const char *channels[] = {"in_accel_x",   "in_accel_y",   "in_accel_z",
                                    "in_anglvel_x", "in_anglvel_y", "in_anglvel_z"};

bool parse_number(const std::string &text, float &value)
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> value;
    if (!stream || !std::isfinite(value)) return false;
    stream >> std::ws;
    return stream.eof();
}

bool read_number(const std::filesystem::path &path, float &value)
{
    std::ifstream stream(path);
    std::string text;
    return std::getline(stream, text) && parse_number(text, value);
}
}  // namespace

IioDevice::~IioDevice()
{
    close();
}

void IioDevice::close()
{
    for (auto &fd : descriptors_) {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
}

bool IioDevice::open(const std::filesystem::path &root, const std::filesystem::path &device, std::string &error)
{
    close();
    auto selected = device;
    if (selected.empty()) {
        std::error_code ec;
        std::filesystem::directory_iterator it(root, ec), end;
        while (!ec && it != end) {
            std::ifstream name_file(it->path() / "name");
            std::string name;
            std::getline(name_file, name);
            if (name == "qmi8658c" || name == "qmi8658b" || name == "qmi8658") {
                selected = it->path();
                break;
            }
            it.increment(ec);
        }
    }
    if (selected.empty()) {
        error = "QMI8658 IIO device not found";
        return false;
    }
    for (std::size_t i = 0; i < descriptors_.size(); ++i) {
        const auto scale = selected / (i < 3 ? "in_accel_scale" : "in_anglvel_scale");
        if (!read_number(selected / (std::string(channels[i]) + "_scale"), scales_[i]) &&
            !read_number(scale, scales_[i])) {
            error = "Cannot read IIO scale: " + scale.string();
            close();
            return false;
        }
        const auto raw  = selected / (std::string(channels[i]) + "_raw");
        descriptors_[i] = ::open(raw.c_str(), O_RDONLY | O_CLOEXEC);
        if (scales_[i] <= 0 || descriptors_[i] < 0) {
            error = "Cannot open IIO channel or invalid scale: " + raw.string();
            close();
            return false;
        }
    }
    error.clear();
    return true;
}

bool IioDevice::read(ImuSample &sample, std::string &error)
{
    std::array<float, 6> values{};
    for (std::size_t i = 0; i < descriptors_.size(); ++i) {
        char buffer[80];
        const auto count = pread(descriptors_[i], buffer, sizeof(buffer), 0);
        float raw        = 0;
        if (count <= 0 || !parse_number(std::string(buffer, static_cast<std::size_t>(count)), raw) || raw < -32768 ||
            raw > 32767 || std::trunc(raw) != raw) {
            error = "Cannot read IIO channel: " + std::string(channels[i]);
            return false;
        }
        values[i] = raw * scales_[i];
        if (!std::isfinite(values[i])) {
            error = "Invalid scaled IIO sample";
            return false;
        }
    }
    sample = {{values[0], values[1], values[2]}, {values[3], values[4], values[5]}};
    error.clear();
    return true;
}

IioSource::IioSource(std::filesystem::path root, std::filesystem::path device)
    : root_(std::move(root)), device_(std::move(device)), worker_(&IioSource::run, this)
{
}

IioSource::~IioSource()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

SensorSnapshot IioSource::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

void IioSource::run()
{
    IioDevice device;
    bool opened = false;
    std::string last_error;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        lock.unlock();
        SensorSnapshot next;
        if (!opened) opened = device.open(root_, device_, next.error);
        if (opened) next.connected = device.read(next.sample, next.error);
        next.timestamp = std::chrono::steady_clock::now();
        if (!next.connected) {
            device.close();
            opened = false;
        }
        if (next.error != last_error) {
            if (!next.error.empty()) std::cerr << "IMU: " << next.error << '\n';
            last_error = next.error;
        }
        lock.lock();
        latest_ = std::move(next);
        wake_.wait_for(lock, opened ? std::chrono::milliseconds(10) : std::chrono::milliseconds(1000),
                       [this] { return stop_; });
    }
}

SensorSnapshot DemoSource::snapshot()
{
    const auto now   = std::chrono::steady_clock::now();
    const float t    = std::chrono::duration<float>(now - start_).count();
    const float roll = 0.3f * std::sin(t * 0.7f), pitch = 0.4f * std::sin(t * 0.5f);
    SensorSnapshot result;
    result.connected = result.simulated = true;
    result.timestamp                    = now;
    result.sample.acceleration          = {-9.80665f * std::sin(pitch), 9.80665f * std::sin(roll) * std::cos(pitch),
                                           9.80665f * std::cos(roll) * std::cos(pitch)};
    result.sample.angular_velocity      = {0.21f * std::cos(t * 0.7f), 0.2f * std::cos(t * 0.5f),
                                           0.35f * std::sin(t * 0.4f)};
    return result;
}

std::unique_ptr<SensorSource> make_sensor_source()
{
    const auto *demo = std::getenv("LILYGO_IMU_DEMO");
    const auto *path = std::getenv("LILYGO_IMU_DEVICE");
#ifdef __linux__
    const bool default_demo = false;
#else
    const bool default_demo = true;
#endif
    if (demo ? std::string(demo) == "1" : (default_demo && !path)) return std::make_unique<DemoSource>();
    return std::make_unique<IioSource>("/sys/bus/iio/devices", path ? path : "");
}
