#include "app.hpp"

#include <SDL.h>
#include <src/drivers/sdl/lv_sdl_window.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

const cm0_app_descriptor_t *application = nullptr;
cm0_app_context_t context{};
lv_timer_t *driver = nullptr;
unsigned step      = 0;
int touched_value  = 0;
bool completed     = false;

void push_key(SDL_Keycode key, unsigned count = 1)
{
    SDL_Event event{};
    event.type           = SDL_KEYDOWN;
    event.key.windowID   = SDL_GetWindowID(lv_sdl_window_get_window(lv_display_get_default()));
    event.key.keysym.sym = key;
    for (unsigned i = 0; i < count; ++i) assert(SDL_PushEvent(&event) == 1);
}

void tap_slider(lv_obj_t *slider)
{
    lv_area_t area{};
    lv_obj_get_coords(slider, &area);
    const float zoom = lv_sdl_window_get_zoom(lv_display_get_default());
    SDL_Event event{};
    event.button.windowID = SDL_GetWindowID(lv_sdl_window_get_window(lv_display_get_default()));
    event.button.button   = SDL_BUTTON_LEFT;
    event.button.x        = static_cast<int>(std::lround((area.x1 + lv_area_get_width(&area) * 0.75f) * zoom));
    event.button.y        = static_cast<int>(std::lround((area.y1 + area.y2) * 0.5f * zoom));
    event.type            = SDL_MOUSEBUTTONDOWN;
    assert(SDL_PushEvent(&event) == 1);
    event.type = SDL_MOUSEBUTTONUP;
    assert(SDL_PushEvent(&event) == 1);
}

void expect_value(lv_obj_t *slider, int expected)
{
    auto *value = lv_obj_find_by_name(context.root, "imu_sensitivity_value");
    assert(value);
    if (lv_slider_get_value(slider) != expected || lv_label_get_text(value) != std::to_string(expected) + "%") {
        std::fprintf(stderr, "keyboard step %u: expected %d%%, slider=%d, label=%s\n", step, expected,
                     lv_slider_get_value(slider), lv_label_get_text(value));
        assert(false);
    }
}

void drive_input(lv_timer_t *)
{
    auto *slider = lv_obj_find_by_name(context.root, "imu_sensitivity");
    auto *group  = lv_group_get_default();
    assert(slider && group);
    switch (step) {
        case 0:
            // The real runtime must focus the slider before any pointer input.
            assert(lv_group_get_focused(group) == slider);
            expect_value(slider, 100);
            push_key(SDLK_RIGHT);
            break;
        case 1:
            expect_value(slider, 101);
            push_key(SDLK_LEFT);
            break;
        case 2:
            expect_value(slider, 100);
            push_key(SDLK_UP);
            break;
        case 3:
            expect_value(slider, 101);
            push_key(SDLK_DOWN);
            break;
        case 4:
            expect_value(slider, 100);
            tap_slider(slider);
            break;
        case 5:
            touched_value = lv_slider_get_value(slider);
            assert(touched_value > 100 && touched_value < 200);
            expect_value(slider, touched_value);
            assert(lv_group_get_focused(group) == slider);
            push_key(SDLK_RIGHT);
            break;
        case 6:
            expect_value(slider, touched_value + 1);
            push_key(SDLK_ESCAPE);
            break;
        case 7:
            assert(!lv_group_get_focused(group));
            push_key(SDLK_TAB);
            break;
        case 8:
            assert(lv_group_get_focused(group) == slider);
            expect_value(slider, touched_value + 1);
            push_key(SDLK_LEFT);
            break;
        case 9:
            expect_value(slider, touched_value);
            push_key(SDLK_ESCAPE);
            break;
        case 10:
            assert(!lv_group_get_focused(group));
            push_key(SDLK_RIGHT);
            break;
        case 11:
            assert(lv_group_get_focused(group) == slider);
            expect_value(slider, touched_value + 1);
            push_key(SDLK_RIGHT, 200);
            break;
        case 12:
            expect_value(slider, 200);
            push_key(SDLK_DOWN, 200);
            break;
        case 13:
            expect_value(slider, 25);
            push_key(SDLK_UP);
            break;
        case 14:
            expect_value(slider, 26);
            completed = true;
            context.request_exit(context.user_data);
            break;
        default:
            assert(false);
    }
    ++step;
}

void open_application(const cm0_app_context_t *runtime_context)
{
    context = *runtime_context;
    application->open(runtime_context);
    driver = lv_timer_create(drive_input, 100, nullptr);
    assert(driver);
}

void close_application()
{
    lv_timer_delete(driver);
    driver = nullptr;
    application->close();
}

}  // namespace

int main(int argc, char **argv)
{
    application      = cm0_app_get_descriptor();
    auto wrapper     = *application;
    wrapper.open     = open_application;
    wrapper.close    = close_application;
    const int result = cm0_app_run(argc, argv, &wrapper);
    assert(result == 0 && completed);
    std::puts("Runtime keyboard: arrows, pointer handoff, focus recovery and sensitivity limits passed");
    return 0;
}
