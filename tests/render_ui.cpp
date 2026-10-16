#include "app.hpp"
#include "pages/imu/imu_view.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kPartialRows                   = 80;
constexpr std::uint32_t kColorPageBackground = 0xf2f2f7;

std::vector<lv_color32_t> framebuffer;
int display_width = 0;

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);

    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(display_width) + static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

bool write_ppm(const char *path, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    return file.good();
}

bool colors_differ(const lv_color32_t &left, const lv_color32_t &right)
{
    return left.red != right.red || left.green != right.green || left.blue != right.blue || left.alpha != right.alpha;
}

class TestSource : public SensorSource {
public:
    SensorSnapshot value;
    SensorSnapshot snapshot() override
    {
        value.timestamp = std::chrono::steady_clock::now();
        return value;
    }
};

void assert_inside(lv_obj_t *object, lv_obj_t *parent)
{
    lv_area_t child{}, outer{};
    lv_obj_get_coords(object, &child);
    lv_obj_get_coords(parent, &outer);
    assert(child.x1 >= outer.x1 && child.y1 >= outer.y1);
    assert(child.x2 <= outer.x2 && child.y2 <= outer.y2);
}

void assert_inside_circle(lv_obj_t *ball, lv_obj_t *boundary)
{
    lv_area_t child{}, circle{};
    lv_obj_get_coords(ball, &child);
    lv_obj_get_coords(boundary, &circle);
    const float dx     = (child.x1 + child.x2 - circle.x1 - circle.x2) * 0.5f;
    const float dy     = (child.y1 + child.y2 - circle.y1 - circle.y2) * 0.5f;
    const float radius = lv_area_get_width(&child) * 0.5f;
    assert(std::hypot(dx, dy) + radius <= lv_area_get_width(&circle) * 0.5f + 1);
}

void assert_telemetry_layout(lv_obj_t *root)
{
    auto *surface      = lv_obj_find_by_name(root, "app_surface");
    auto *arena        = lv_obj_find_by_name(root, "imu_arena");
    auto *acceleration = lv_obj_find_by_name(root, "imu_acceleration");
    auto *gyro         = lv_obj_find_by_name(root, "imu_angular_velocity");
    assert(surface && arena && acceleration && gyro);
    lv_area_t left{}, right{}, motion{};
    lv_obj_get_coords(acceleration, &left);
    lv_obj_get_coords(gyro, &right);
    lv_obj_get_coords(arena, &motion);
    assert(left.x2 < right.x1);
    for (auto *panel : {acceleration, gyro}) {
        assert(!lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
        assert_inside(panel, surface);
        lv_area_t area{};
        lv_obj_get_coords(panel, &area);
        assert(area.x2 < motion.x1 || area.x1 > motion.x2 || area.y2 < motion.y1 || area.y1 > motion.y2);
    }
    const char *names[] = {"imu_accel_x", "imu_accel_y", "imu_accel_z", "imu_gyro_x", "imu_gyro_y", "imu_gyro_z"};
    for (int i = 0; i < 6; ++i) {
        auto *value = lv_obj_find_by_name(root, names[i]);
        assert(value && !lv_obj_has_flag(value, LV_OBJ_FLAG_HIDDEN));
        assert_inside(value, i < 3 ? acceleration : gyro);
        lv_point_t text_size{};
        lv_text_get_size(&text_size, lv_label_get_text(value), lv_obj_get_style_text_font(value, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(value, LV_PART_MAIN),
                         lv_obj_get_style_text_line_space(value, LV_PART_MAIN), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        assert(text_size.x <= lv_obj_get_content_width(value));
        assert(text_size.y <= lv_obj_get_content_height(value));
    }
}

void assert_slider_touch(lv_obj_t *slider, ImuViewModel &view_model, lv_display_t *display)
{
    const auto initial = lv_slider_get_value(slider);
    lv_area_t track{};
    lv_obj_get_coords(slider, &track);
    assert_inside(slider, lv_obj_get_parent(slider));
    const int minimum = lv_slider_get_min_value(slider), maximum = lv_slider_get_max_value(slider);
    const int knob_x = track.x1 + (initial - minimum) * lv_area_get_width(&track) / (maximum - minimum);
    // Touch above the narrow track, then drag through both ends of its range.
    // This exercises hit testing and pointer events rather than sending VALUE_CHANGED directly.
    const int touch_y = (track.y1 + track.y2) / 2 - 22;
    lv_indev_data_t sample{};
    sample.state  = LV_INDEV_STATE_RELEASED;
    auto *pointer = lv_indev_create();
    assert(pointer);
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_user_data(pointer, &sample);
    lv_indev_set_read_cb(pointer, [](lv_indev_t *input, lv_indev_data_t *data) {
        *data = *static_cast<lv_indev_data_t *>(lv_indev_get_user_data(input));
    });
    const auto send = [&](int x, lv_indev_state_t state) {
        sample.point = {x, touch_y};
        sample.state = state;
        lv_indev_read(pointer);
    };
    lv_refr_now(display);
    send(knob_x, LV_INDEV_STATE_PRESSED);
    send(track.x2 + 1, LV_INDEV_STATE_PRESSED);
    send(track.x2 + 1, LV_INDEV_STATE_RELEASED);
    assert(view_model.model().sensitivity() == maximum);
    lv_refr_now(display);
    send(track.x2, LV_INDEV_STATE_PRESSED);
    send(track.x1 - 1, LV_INDEV_STATE_PRESSED);
    send(track.x1 - 1, LV_INDEV_STATE_RELEASED);
    assert(view_model.model().sensitivity() == minimum);
    lv_indev_delete(pointer);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(view_model.model().sensitivity() == initial);
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4);
    const int width  = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    assert(width >= 320 && height >= 240);

    display_width = width;
    framebuffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(width) * kPartialRows);

    lv_init();
    auto *display = lv_display_create(width, height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    auto source            = std::make_unique<TestSource>();
    auto *input            = source.get();
    input->value.connected = true;
    input->value.simulated = true;
    input->value.sample    = {{0, 0, 9.80665f}, {0, 0, 0}};
    auto view_model        = std::make_unique<ImuViewModel>(std::move(source));
    ImuView view(*view_model);
    auto *root = lv_screen_active();
    view.create(root);
    lv_obj_update_layout(root);
    view_model->tick(0.016f);

    auto *surface  = lv_obj_find_by_name(root, "app_surface");
    auto *arena    = lv_obj_find_by_name(root, "imu_arena");
    auto *ball     = lv_obj_find_by_name(root, "imu_ball");
    auto *boundary = lv_obj_find_by_name(root, "imu_boundary");
    auto *marker   = lv_obj_find_by_name(root, "imu_ball_marker");
    auto *slider   = lv_obj_find_by_name(root, "imu_sensitivity");
    assert(surface && arena && ball && boundary && marker && slider);
    for (const char *name : {"app_header", "app_title", "imu_pause", "imu_reset", "imu_calibrate"})
        assert(!lv_obj_find_by_name(root, name));
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
    assert(lv_obj_get_style_bg_opa(surface, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert_inside(arena, surface);
    assert_inside(ball, arena);
    const auto initial_x = view_model->model().ball().x;
    input->value.sample  = {{-3, 1, 9.27f}, {0, 0, 0.3f}};
    for (int i = 0; i < 50; ++i) view_model->tick(0.016f);
    assert(view_model->model().ball().x < initial_x - 5);
    lv_slider_set_value(slider, 150, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(view_model->model().sensitivity() == 150);
    input->value.connected = false;
    view_model->tick(0.016f);
    assert(!lv_obj_has_flag(lv_obj_find_by_name(root, "imu_error"), LV_OBJ_FLAG_HIDDEN));
    assert(std::string(lv_label_get_text(lv_obj_find_by_name(root, "imu_status"))) == "Offline");
    assert(std::string(lv_label_get_text(lv_obj_find_by_name(root, "imu_accel_x"))) == "X --");
    input->value.connected = true;
    input->value.sample    = {{-2.1f, -0.8f, 9.54f}, {0.04f, -0.12f, 0.35f}};
    for (int i = 0; i < 40; ++i) view_model->tick(0.016f);
    assert(std::string(lv_label_get_text(lv_obj_find_by_name(root, "imu_accel_x"))) == "X -2.10");
    assert(std::string(lv_label_get_text(lv_obj_find_by_name(root, "imu_gyro_z"))) == "Z +0.35");
    assert(lv_obj_has_flag(lv_obj_find_by_name(root, "imu_error"), LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(root);
    assert_telemetry_layout(root);

    lv_tick_inc(20);
    lv_refr_now(display);
    const bool has_contrast = std::any_of(framebuffer.cbegin() + 1, framebuffer.cend(),
                                          [](const auto &pixel) { return colors_differ(pixel, framebuffer[0]); });
    assert(has_contrast);
    assert(write_ppm(argv[3], width, height));

    const int teal_pixels = static_cast<int>(std::count_if(framebuffer.begin(), framebuffer.end(), [](const auto &p) {
        return p.green > 100 && p.blue > 95 && p.red < 20;
    }));
    assert(teal_pixels > 200);

    // Exercise the rendered size and marker at the circular edge, including
    // the compact viewports where growth consumes most of the free space.
    input->value.sample = {{9.80665f, 0, 0}, {}};
    lv_slider_set_value(slider, 200, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    for (int i = 0; i < 350; ++i) view_model->tick(0.016f);
    lv_obj_update_layout(root);
    const int base_diameter = lv_obj_get_width(ball);
    input->value.sample     = {{17.80665f, 0, 0}, {}};
    for (int i = 0; i < 5; ++i) {
        view_model->tick(0.016f);
        lv_obj_update_layout(root);
        assert_inside_circle(ball, boundary);
        assert_inside(marker, ball);
    }
    assert(lv_obj_get_width(ball) > base_diameter * 2.0f);
    assert(lv_obj_get_width(ball) <= base_diameter * 3.0f);
    assert(lv_obj_get_width(ball) == 2 * static_cast<int>(view_model->model().ball().radius));
    lv_tick_inc(100);
    view_model->set_sensitivity(200);
    lv_refr_now(display);
    auto pulse_path = std::filesystem::path(argv[3]);
    pulse_path.replace_filename(pulse_path.stem().string() + "-pulse.ppm");
    assert(write_ppm(pulse_path.c_str(), width, height));
    input->value.sample = {{9.80665f, 0, 0}, {}};
    for (int i = 0; i < 150; ++i) view_model->tick(0.016f);
    lv_obj_update_layout(root);
    assert(lv_obj_get_width(ball) == base_diameter);
    assert_inside_circle(ball, boundary);
    lv_slider_set_value(slider, 150, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    assert_slider_touch(slider, *view_model, display);
    view_model->toggle_pause();
    lv_obj_set_size(surface, height, width);
    lv_obj_update_layout(root);
    assert_telemetry_layout(root);
    assert(view_model->model().paused() && view_model->model().sensitivity() == 150);
    const float arena_width  = static_cast<float>(lv_obj_get_content_width(arena));
    const float arena_height = static_cast<float>(lv_obj_get_content_height(arena));
    const float ball_radius  = lv_obj_get_width(ball) * 0.5f;
    const auto resized_ball  = view_model->model().ball();
    const float dx           = resized_ball.x - arena_width * 0.5f;
    const float dy           = resized_ball.y - arena_height * 0.5f;
    const float circle_limit = std::max(0.0f, std::min(arena_width, arena_height) * 0.5f - ball_radius);
    assert(dx * dx + dy * dy <= (circle_limit + 2.0f) * (circle_limit + 2.0f));
    assert_inside(ball, arena);

    view.destroy();
    view.destroy();
    view_model.reset();
    setenv("LILYGO_IMU_DEMO", "1", 1);
    cm0_app_context_t context{};
    context.root = root;
    cm0_app_get_descriptor()->open(&context);
    cm0_app_get_descriptor()->close();
    cm0_app_get_descriptor()->close();
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
