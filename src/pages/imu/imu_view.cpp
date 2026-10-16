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

lv_obj_t *ImuView::create(lv_obj_t *parent)
{
    surface_ = components::create_page(parent);
    lv_obj_set_style_pad_row(surface_, 0, 0);
    workspace_ = plain(surface_);
    lv_obj_set_name_static(workspace_, "imu_workspace");
    lv_obj_set_width(workspace_, LV_PCT(100));
    lv_obj_set_flex_grow(workspace_, 1);
    lv_obj_set_style_pad_all(workspace_, 8, 0);
    lv_obj_set_style_pad_gap(workspace_, 8, 0);

    arena_ = plain(workspace_);
    lv_obj_set_name_static(arena_, "imu_arena");
    // Share the inset content area between the visible circle and the motion model.
    lv_obj_set_style_pad_all(arena_, 8, 0);
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

    for (int group = 0; group < 2; ++group) {
        auto *section  = components::create_card(workspace_);
        groups_[group] = section;
        lv_obj_set_name_static(section, group ? "imu_angular_velocity" : "imu_acceleration");
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(section, 8, 0);
        lv_obj_set_style_pad_row(section, 4, 0);
        auto *heading    = label(section, group ? "Angular rate\n(rad/s)" : "Acceleration\n(m/s²)", 14, muted);
        headings_[group] = heading;
        lv_obj_set_name_static(heading, group ? "imu_gyro_heading" : "imu_accel_heading");
        lv_obj_set_width(heading, LV_PCT(100));
        lv_obj_set_style_text_align(heading, group ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
        for (int axis = 0; axis < 3; ++axis) {
            auto *value         = label(section, "", 22, axis_colors[axis]);
            const char *names[] = {"imu_accel_x", "imu_accel_y", "imu_accel_z",
                                   "imu_gyro_x",  "imu_gyro_y",  "imu_gyro_z"};
            lv_obj_set_name_static(value, names[group * 3 + axis]);
            values_[group * 3 + axis] = value;
            lv_obj_set_width(value, LV_PCT(100));
            lv_obj_set_style_text_align(value, group ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
        }
    }

    footer_ = plain(surface_);
    lv_obj_set_width(footer_, LV_PCT(100));
    lv_obj_set_height(footer_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(footer_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(footer_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(footer_, 16, 0);
    lv_obj_set_style_pad_ver(footer_, 8, 0);
    lv_obj_set_style_pad_column(footer_, 12, 0);
    lv_obj_set_style_pad_row(footer_, 4, 0);
    status_ = label(footer_, "Offline", 14, muted);
    lv_obj_set_name_static(status_, "imu_status");
    lv_obj_set_width(status_, 64);
    sensitivity_label_ = label(footer_, "Sensitivity", 22, muted);
    lv_obj_set_name_static(sensitivity_label_, "imu_sensitivity_label");
    adjustment_        = plain(footer_);
    lv_obj_set_style_pad_hor(adjustment_, 20, 0);
    slider_ = lv_slider_create(adjustment_);
    lv_obj_set_name_static(slider_, "imu_sensitivity");
    lv_obj_set_size(slider_, LV_PCT(100), 14);
    lv_obj_center(slider_);
    lv_slider_set_range(slider_, 25, 200);
    lv_slider_set_value(slider_, view_model_.model().sensitivity(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(ball_color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(ball_color), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_, 14, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider_, 20);
    lv_obj_add_event_cb(slider_, command_event, LV_EVENT_VALUE_CHANGED, this);
    gain_ = label(footer_, "100%", 22, muted);
    lv_obj_set_name_static(gain_, "imu_sensitivity_value");
    lv_obj_set_width(gain_, 48);
    lv_obj_set_style_text_align(gain_, LV_TEXT_ALIGN_RIGHT, 0);

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
    surface_ = workspace_ = arena_ = boundary_ = ball_ = marker_ = status_ = error_ = footer_ = nullptr;
    adjustment_ = sensitivity_label_ = slider_ = gain_ = nullptr;
    values_.fill(nullptr);
    groups_.fill(nullptr);
    headings_.fill(nullptr);
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
    const bool landscape = width > height;
    const bool narrow    = width < 480;
    const bool compact   = landscape && narrow;
    lv_label_set_text(headings_[0], narrow ? "Accel.\n(m/s²)" : "Acceleration\n(m/s²)");
    lv_label_set_text(headings_[1], narrow ? "Gyro\n(rad/s)" : "Angular rate\n(rad/s)");
    if (landscape) {
        const int card_width = compact ? 88 : width < 600 ? 144 : 184;
        columns_             = {card_width, LV_GRID_FR(1), card_width, LV_GRID_TEMPLATE_LAST};
        rows_                = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST, LV_GRID_TEMPLATE_LAST};
        lv_obj_set_grid_cell(arena_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    } else {
        columns_ = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST, LV_GRID_TEMPLATE_LAST};
        rows_    = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
        lv_obj_set_grid_cell(arena_, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
    lv_obj_set_grid_dsc_array(workspace_, columns_.data(), rows_.data());
    for (int group = 0; group < 2; ++group) {
        lv_obj_set_grid_cell(groups_[group], LV_GRID_ALIGN_STRETCH, landscape ? group * 2 : group, 1,
                             landscape ? LV_GRID_ALIGN_CENTER : LV_GRID_ALIGN_START, 0, 1);
    }
    if (auto *font = lilygo_ui_font_get(compact ? 14 : narrow ? 22 : 28)) {
        for (auto *value : values_) lv_obj_set_style_text_font(value, font, 0);
    }
    if (auto *font = lilygo_ui_font_get(narrow ? 14 : 22)) {
        for (auto *heading : headings_) lv_obj_set_style_text_font(heading, font, 0);
        lv_obj_set_style_text_font(sensitivity_label_, font, 0);
        lv_obj_set_style_text_font(gain_, font, 0);
    }

    // Give the touch target a full row on narrow displays instead of squeezing
    // the slider between its labels. Both layouts leave room for the end knobs.
    static constexpr int32_t footer_columns[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT,
                                                 LV_GRID_TEMPLATE_LAST};
    static constexpr int32_t footer_rows[]    = {56, LV_GRID_TEMPLATE_LAST};
    static constexpr int32_t narrow_columns[] = {LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT,
                                                 LV_GRID_TEMPLATE_LAST};
    static constexpr int32_t narrow_rows[]    = {LV_GRID_CONTENT, 56, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(footer_, narrow ? narrow_columns : footer_columns, narrow ? narrow_rows : footer_rows);
    lv_obj_set_grid_cell(status_, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(sensitivity_label_, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(gain_, LV_GRID_ALIGN_END, narrow ? 2 : 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(adjustment_, LV_GRID_ALIGN_STRETCH, narrow ? 0 : 2, narrow ? 3 : 1, LV_GRID_ALIGN_STRETCH,
                         narrow ? 1 : 0, 1);
    lv_obj_set_width(gain_, narrow ? 48 : 64);

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
    lv_label_set_text_fmt(gain_, "%d%%", model.sensitivity());
    if (sensor.connected) {
        lv_obj_add_flag(error_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(error_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(error_, "QMI8658: waiting for connection");
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
    if (target == self->slider_) self->view_model_.set_sensitivity(lv_slider_get_value(self->slider_));
}
