#include "pages/imu/imu_view.hpp"
#include "components/components.hpp"
#include <cm0/typography.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace {
// Accent roles: the teal ball/live state, red faults/X, green Y, and blue Z.
constexpr uint32_t ball_color = 0x008b83, muted = 0x657078, fault = 0xc74450;
constexpr uint32_t axis_colors[] = {0xc74450, 0x24825a, 0x326ed2};

lv_obj_t *plain(lv_obj_t *parent)
{
    auto *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int size, uint32_t color)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    if (auto *font = lilygo_ui_font_get(size)) lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *circle(lv_obj_t *parent, int diameter, uint32_t color)
{
    auto *obj = plain(parent);
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
}  // namespace

lv_obj_t *ImuView::create(lv_obj_t *parent, const char *application_name)
{
    surface_ = components::create_page(parent);
    lv_obj_set_style_pad_row(surface_, 0, 0);
    header_ = plain(surface_);
    lv_obj_set_name_static(header_, "app_header");
    lv_obj_set_size(header_, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(header_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(header_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header_, 16, 0);
    lv_obj_set_style_pad_column(header_, 8, 0);
    lv_obj_set_flex_flow(header_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *identity = plain(header_);
    lv_obj_set_size(identity, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(identity, 1);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_COLUMN);
    auto *title = label(identity, application_name, 28, components::color_container_title);
    lv_obj_set_name_static(title, "app_title");
    status_ = label(identity, "未连接", 14, muted);
    lv_obj_set_name_static(status_, "imu_status");

    const char *icons[]  = {LV_SYMBOL_PAUSE, LV_SYMBOL_REFRESH, "\xef\x81\x9b"};
    const char *names[]  = {"imu_pause", "imu_reset", "imu_calibrate"};
    lv_obj_t **buttons[] = {&pause_, &reset_, &calibrate_};
    for (int i = 0; i < 3; ++i) {
        auto *button = lv_button_create(header_);
        *buttons[i]  = button;
        lv_obj_set_name_static(button, names[i]);
        lv_obj_set_size(button, 44, 44);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(components::color_page_background), 0);
        auto *glyph = label(button, icons[i], 22, components::color_action);
        lv_obj_center(glyph);
        if (i == 0) pause_label_ = glyph;
        lv_obj_add_event_cb(button, command_event, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(button, tooltip_event, LV_EVENT_ALL, this);
    }

    arena_ = plain(surface_);
    lv_obj_set_name_static(arena_, "imu_arena");
    lv_obj_set_width(arena_, LV_PCT(100));
    lv_obj_set_flex_grow(arena_, 1);
    lv_obj_set_style_border_color(arena_, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_border_width(arena_, 1, 0);
    lv_obj_set_style_border_side(arena_, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM), 0);
    boundary_ = circle(arena_, 112, components::color_page_background);
    lv_obj_set_name_static(boundary_, "imu_boundary");
    lv_obj_set_style_bg_opa(boundary_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(boundary_, 2, 0);
    lv_obj_set_style_border_color(boundary_, lv_color_hex(components::color_border), 0);
    lv_obj_clear_flag(boundary_, LV_OBJ_FLAG_CLICKABLE);
    for (int size : {48}) {
        auto *ring = circle(arena_, size, components::color_page_background);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(components::color_border), 0);
        lv_obj_center(ring);
    }
    for (bool vertical : {false, true}) {
        auto *line = plain(arena_);
        lv_obj_set_size(line, vertical ? 1 : LV_PCT(100), vertical ? LV_PCT(100) : 1);
        lv_obj_set_style_bg_color(line, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_50, 0);
        lv_obj_center(line);
    }
    ball_ = circle(arena_, radius_ * 2, ball_color);
    lv_obj_set_name_static(ball_, "imu_ball");
    lv_obj_set_style_border_width(ball_, 2, 0);
    lv_obj_set_style_border_color(ball_, lv_color_hex(0x00645f), 0);
    marker_ = circle(ball_, 9, 0xffffff);
    lv_obj_set_name_static(marker_, "imu_ball_marker");
    error_ = label(arena_, "QMI8658", 14, muted);
    lv_obj_set_name_static(error_, "imu_error");
    lv_obj_set_width(error_, LV_PCT(90));
    lv_obj_set_style_text_align(error_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(error_, LV_ALIGN_TOP_MID, 0, 12);

    footer_ = plain(surface_);
    lv_obj_set_width(footer_, LV_PCT(100));
    lv_obj_set_height(footer_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(footer_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(footer_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(footer_, 16, 0);
    lv_obj_set_style_pad_row(footer_, 12, 0);
    lv_obj_set_flex_flow(footer_, LV_FLEX_FLOW_COLUMN);
    telemetry_ = plain(footer_);
    lv_obj_set_name_static(telemetry_, "imu_telemetry");
    lv_obj_set_size(telemetry_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_gap(telemetry_, 12, 0);
    for (int group = 0; group < 2; ++group) {
        auto *section  = plain(telemetry_);
        groups_[group] = section;
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(section, 4, 0);
        label(section, group ? "角速度 (rad/s)" : "加速度 (m/s²)", 14, muted);
        auto *row = plain(section);
        lv_obj_set_size(row, LV_PCT(100), 28);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        for (int axis = 0; axis < 3; ++axis) {
            auto *value         = label(row, "", 22, axis_colors[axis]);
            const char *names[] = {"imu_accel_x", "imu_accel_y", "imu_accel_z",
                                   "imu_gyro_x",  "imu_gyro_y",  "imu_gyro_z"};
            lv_obj_set_name_static(value, names[group * 3 + axis]);
            values_[group * 3 + axis] = value;
            lv_obj_set_width(value, 0);
            lv_obj_set_flex_grow(value, 1);
            lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    auto *adjustment = plain(footer_);
    lv_obj_set_size(adjustment, LV_PCT(100), 32);
    lv_obj_set_flex_flow(adjustment, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(adjustment, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(adjustment, 16, 0);
    label(adjustment, "灵敏度", 14, muted);
    slider_ = lv_slider_create(adjustment);
    lv_obj_set_name_static(slider_, "imu_sensitivity");
    lv_obj_set_size(slider_, 0, 6);
    lv_obj_set_flex_grow(slider_, 1);
    lv_slider_set_range(slider_, 25, 200);
    lv_slider_set_value(slider_, view_model_.model().sensitivity(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(ball_color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(ball_color), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider_, 14);
    lv_obj_add_event_cb(slider_, command_event, LV_EVENT_VALUE_CHANGED, this);
    gain_ = label(adjustment, "100%", 14, muted);
    lv_obj_set_width(gain_, 48);
    lv_obj_set_style_text_align(gain_, LV_TEXT_ALIGN_RIGHT, 0);

    tooltip_ = label(surface_, "", 14, 0xffffff);
    lv_obj_add_flag(tooltip_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN));
    lv_obj_set_style_bg_color(tooltip_, lv_color_hex(components::color_action), 0);
    lv_obj_set_style_bg_opa(tooltip_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tooltip_, 4, 0);
    lv_obj_set_style_pad_all(tooltip_, 8, 0);
    lv_obj_align(tooltip_, LV_ALIGN_TOP_RIGHT, -16, 66);

    lv_obj_add_event_cb(surface_, layout_event, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(arena_, layout_event, LV_EVENT_SIZE_CHANGED, this);
    lv_subject_add_observer_obj(view_model_.changed(), changed, surface_, this);
    last_tick_ = lv_tick_get();
    timer_     = lv_timer_create(tick, 16, this);
    layout();
    return surface_;
}

void ImuView::destroy()
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    if (surface_) lv_obj_delete(surface_);
    surface_ = header_ = arena_ = boundary_ = ball_ = marker_ = status_ = error_ = telemetry_ = footer_ = nullptr;
    pause_ = pause_label_ = reset_ = calibrate_ = slider_ = gain_ = tooltip_ = nullptr;
    values_.fill(nullptr);
    groups_.fill(nullptr);
}

void ImuView::changed(lv_observer_t *observer, lv_subject_t *)
{
    static_cast<ImuView *>(lv_observer_get_user_data(observer))->refresh();
}

void ImuView::tick(lv_timer_t *timer)
{
    auto *self          = static_cast<ImuView *>(lv_timer_get_user_data(timer));
    const auto now      = lv_tick_get();
    const auto rotation = lv_display_get_rotation(lv_obj_get_display(self->surface_));
    int degrees         = static_cast<int>(rotation) * 90;
    if (const auto *override_rotation = std::getenv("LILYGO_IMU_ROTATION")) degrees += std::atoi(override_rotation);
    self->view_model_.set_rotation(degrees);
    self->view_model_.tick((now - self->last_tick_) / 1000.0f);
    self->last_tick_ = now;
}

void ImuView::layout_event(lv_event_t *event)
{
    static_cast<ImuView *>(lv_event_get_user_data(event))->layout();
}

void ImuView::layout()
{
    if (!footer_) return;
    const int width = lv_obj_get_content_width(surface_), height = lv_obj_get_content_height(surface_);
    const bool wide = width >= 700;
    if (height < 360)
        lv_obj_add_flag(telemetry_, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(telemetry_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(telemetry_, wide ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    for (auto *group : groups_) {
        lv_obj_set_width(group, wide ? 0 : LV_PCT(100));
        lv_obj_set_flex_grow(group, wide ? 1 : 0);
    }
    radius_            = height < 360 ? 16 : 24;
    const int diameter = std::min(lv_obj_get_content_width(arena_), lv_obj_get_content_height(arena_));
    lv_obj_set_size(boundary_, diameter, diameter);
    lv_obj_center(boundary_);
    const int arena_width = lv_obj_get_content_width(arena_), arena_height = lv_obj_get_content_height(arena_);
    view_model_.resize(arena_width, arena_height, radius_);
}

void ImuView::refresh()
{
    if (!ball_) return;
    const auto &model        = view_model_.model();
    const auto &sensor       = view_model_.sensor();
    const auto ball          = model.ball();
    const int visible_radius = static_cast<int>(ball.radius);
    lv_obj_set_size(ball_, visible_radius * 2, visible_radius * 2);
    lv_obj_set_pos(ball_, static_cast<int>(std::lround(ball.x)) - visible_radius,
                   static_cast<int>(std::lround(ball.y)) - visible_radius);
    const int marker_size = static_cast<int>(std::lround(9.0f * visible_radius / radius_));
    lv_obj_set_size(marker_, marker_size, marker_size);
    lv_obj_align(marker_, LV_ALIGN_CENTER, static_cast<int>(std::lround(std::cos(ball.angle) * visible_radius * 0.42f)),
                 static_cast<int>(std::lround(std::sin(ball.angle) * visible_radius * 0.42f)));
    lv_label_set_text(status_, view_model_.status().c_str());
    lv_obj_set_style_text_color(status_, lv_color_hex(sensor.connected ? ball_color : fault), 0);
    lv_label_set_text(pause_label_, model.paused() ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    lv_label_set_text_fmt(gain_, "%d%%", model.sensitivity());
    if (sensor.connected) {
        lv_obj_add_flag(error_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(calibrate_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_flag(error_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(error_, "QMI8658 · 等待连接");
        lv_obj_add_state(calibrate_, LV_STATE_DISABLED);
    }
    if (lv_tick_get() - last_telemetry_ < 100 && last_telemetry_ != 0) return;
    last_telemetry_       = lv_tick_get();
    const auto &a         = sensor.sample.acceleration;
    const auto &w         = sensor.sample.angular_velocity;
    const float numbers[] = {a.x, a.y, a.z, w.x, w.y, w.z};
    for (int i = 0; i < 6; ++i) {
        if (sensor.connected) {
            char text[48];
            std::snprintf(text, sizeof(text), "%c %+.2f", "XYZ"[i % 3], static_cast<double>(numbers[i]));
            lv_label_set_text(values_[i], text);
        } else
            lv_label_set_text_fmt(values_[i], "%c --", "XYZ"[i % 3]);
    }
}

void ImuView::command_event(lv_event_t *event)
{
    auto *self   = static_cast<ImuView *>(lv_event_get_user_data(event));
    auto *target = lv_event_get_target_obj(event);
    if (target == self->pause_) self->view_model_.toggle_pause();
    if (target == self->reset_) self->view_model_.reset();
    if (target == self->calibrate_) self->view_model_.calibrate();
    if (target == self->slider_) self->view_model_.set_sensitivity(lv_slider_get_value(self->slider_));
}

void ImuView::tooltip_event(lv_event_t *event)
{
    auto *self = static_cast<ImuView *>(lv_event_get_user_data(event));
    if (!self->tooltip_) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_HOVER_OVER || code == LV_EVENT_LONG_PRESSED) {
        auto *target     = lv_event_get_target_obj(event);
        const char *text = target == self->reset_ ? "复位" : target == self->calibrate_ ? "校准" : "暂停 / 继续";
        lv_label_set_text(self->tooltip_, text);
        lv_obj_remove_flag(self->tooltip_, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_HOVER_LEAVE || code == LV_EVENT_RELEASED) {
        lv_obj_add_flag(self->tooltip_, LV_OBJ_FLAG_HIDDEN);
    }
}
