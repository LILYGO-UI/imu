#pragma once

#include "components/lv_subject.hpp"
#include "pages/imu/imu_model.hpp"

class ImuViewModel {
public:
    explicit ImuViewModel(std::unique_ptr<SensorSource> source = make_sensor_source());
    void tick(float dt);
    void resize(int width, int height, int radius);
    void reset();
    void calibrate();
    void toggle_pause();
    void set_sensitivity(int percent);
    void set_rotation(int degrees);
    [[nodiscard]] lv_subject_t *changed()
    {
        return revision_.get();
    }
    [[nodiscard]] const ImuModel &model() const
    {
        return model_;
    }
    [[nodiscard]] const SensorSnapshot &sensor() const
    {
        return snapshot_;
    }
    [[nodiscard]] std::string status() const;

private:
    void publish();
    std::unique_ptr<SensorSource> source_;
    ImuModel model_;
    SensorSnapshot snapshot_;
    IntSubject revision_{0};
};
