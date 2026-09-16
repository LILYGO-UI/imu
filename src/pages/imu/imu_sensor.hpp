#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct ImuSample {
    Vec3 acceleration;      // m/s^2, including gravity
    Vec3 angular_velocity;  // rad/s
};

struct SensorSnapshot {
    ImuSample sample;
    bool connected = false;
    bool simulated = false;
    std::string error;
    std::chrono::steady_clock::time_point timestamp{};
};

class SensorSource {
public:
    virtual ~SensorSource()           = default;
    virtual SensorSnapshot snapshot() = 0;
};

class IioDevice {
public:
    ~IioDevice();
    IioDevice()                             = default;
    IioDevice(const IioDevice &)            = delete;
    IioDevice &operator=(const IioDevice &) = delete;
    bool open(const std::filesystem::path &root, const std::filesystem::path &device, std::string &error);
    bool read(ImuSample &sample, std::string &error);
    void close();

private:
    std::array<int, 6> descriptors_{{-1, -1, -1, -1, -1, -1}};
    std::array<float, 6> scales_{};
};

class IioSource final : public SensorSource {
public:
    explicit IioSource(std::filesystem::path root = "/sys/bus/iio/devices", std::filesystem::path device = {});
    ~IioSource() override;
    SensorSnapshot snapshot() override;

private:
    void run();
    std::filesystem::path root_, device_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    SensorSnapshot latest_;
    std::thread worker_;
};

class DemoSource final : public SensorSource {
public:
    SensorSnapshot snapshot() override;

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::unique_ptr<SensorSource> make_sensor_source();
