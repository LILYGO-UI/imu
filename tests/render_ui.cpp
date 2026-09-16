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
    view.create(root, "IMU");
    lv_obj_update_layout(root);
    view_model->tick(0.016f);

    auto *surface   = lv_obj_find_by_name(root, "app_surface");
    auto *header    = lv_obj_find_by_name(root, "app_header");
    auto *arena     = lv_obj_find_by_name(root, "imu_arena");
    auto *ball      = lv_obj_find_by_name(root, "imu_ball");
    auto *boundary  = lv_obj_find_by_name(root, "imu_boundary");
    auto *marker    = lv_obj_find_by_name(root, "imu_ball_marker");
    auto *pause     = lv_obj_find_by_name(root, "imu_pause");
    auto *reset     = lv_obj_find_by_name(root, "imu_reset");
    auto *calibrate = lv_obj_find_by_name(root, "imu_calibrate");
    auto *slider    = lv_obj_find_by_name(root, "imu_sensitivity");
    assert(surface && header && arena && ball && boundary && marker && pause && reset && calibrate && slider);
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
    assert(lv_obj_get_style_bg_opa(surface, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert_inside(pause, header);
    assert_inside(reset, header);
    assert_inside(calibrate, header);
    assert_inside(arena, surface);
    assert_inside(ball, arena);
    const auto initial_x = view_model->model().ball().x;
    input->value.sample  = {{-3, 1, 9.27f}, {0, 0, 0.3f}};
    for (int i = 0; i < 50; ++i) view_model->tick(0.016f);
    assert(view_model->model().ball().x < initial_x - 5);
    lv_obj_send_event(pause, LV_EVENT_CLICKED, nullptr);
    assert(view_model->model().paused());
    const auto paused = view_model->model().ball();
    view_model->tick(0.016f);
    assert(view_model->model().ball().x == paused.x);
    lv_obj_send_event(reset, LV_EVENT_CLICKED, nullptr);
    assert(std::abs(view_model->model().ball().x - initial_x) < 1);
    lv_slider_set_value(slider, 150, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(view_model->model().sensitivity() == 150);
    lv_obj_send_event(pause, LV_EVENT_CLICKED, nullptr);
    input->value.sample = {{0, 0, 9.80665f}, {0, 0, 0}};
    lv_obj_send_event(calibrate, LV_EVENT_CLICKED, nullptr);
    for (int i = 0; i < 70; ++i) view_model->tick(0.016f);
    assert(!view_model->model().calibrating());
    input->value.connected = false;
    view_model->tick(0.016f);
    assert(lv_obj_has_state(calibrate, LV_STATE_DISABLED));
    assert(!lv_obj_has_flag(lv_obj_find_by_name(root, "imu_error"), LV_OBJ_FLAG_HIDDEN));
    input->value.connected = true;
    input->value.sample    = {{-2.1f, -0.8f, 9.54f}, {0.04f, -0.12f, 0.35f}};
    for (int i = 0; i < 40; ++i) view_model->tick(0.016f);
    assert(std::string(lv_label_get_text(lv_obj_find_by_name(root, "imu_accel_x"))) == "X -2.10");
    assert(std::string(lv_label_get_text(lv_obj_find_by_name(root, "imu_gyro_z"))) == "Z +0.35");

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
    lv_obj_send_event(pause, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_size(surface, height, width);
    lv_obj_update_layout(root);
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
