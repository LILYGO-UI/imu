#pragma once

#include "pages/imu/imu_view_model.hpp"
#include <array>

class ImuView {
public:
    explicit ImuView(ImuViewModel &view_model) : view_model_(view_model)
    {
    }
    ~ImuView()
    {
        destroy();
    }
    lv_obj_t *create(lv_obj_t *parent);
    void destroy();

private:
    static void changed(lv_observer_t *observer, lv_subject_t *subject);
    static void tick(lv_timer_t *timer);
    static void layout_event(lv_event_t *event);
    static void command_event(lv_event_t *event);
    void layout();
    void refresh();
    ImuViewModel &view_model_;
    lv_obj_t *surface_ = nullptr, *workspace_ = nullptr, *arena_ = nullptr, *boundary_ = nullptr, *ball_ = nullptr,
             *marker_ = nullptr;
    lv_obj_t *status_ = nullptr, *error_ = nullptr, *footer_ = nullptr;
    lv_obj_t *adjustment_ = nullptr, *sensitivity_label_ = nullptr, *slider_ = nullptr, *gain_ = nullptr;
    std::array<lv_obj_t *, 6> values_{};
    std::array<lv_obj_t *, 2> groups_{};
    std::array<lv_obj_t *, 2> headings_{};
    std::array<int32_t, 4> columns_{};
    std::array<int32_t, 3> rows_{};
    lv_timer_t *timer_  = nullptr;
    uint32_t last_tick_ = 0, last_telemetry_ = 0;
    int radius_ = 24;
};
