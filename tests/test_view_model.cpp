#include "pages/imu/imu_view_model.hpp"
#include <cassert>

class TestSource : public SensorSource {
public:
    SensorSnapshot value;
    SensorSnapshot snapshot() override
    {
        return value;
    }
};

int main()
{
    lv_init();
    {
        auto source = std::make_unique<TestSource>();
        auto *input = source.get();
        ImuViewModel vm(std::move(source));
        int notifications = 0;
        auto *observer    = lv_subject_add_observer(
            vm.changed(), [](lv_observer_t *o, lv_subject_t *) { ++*static_cast<int *>(lv_observer_get_user_data(o)); },
            &notifications);
        vm.tick(0.016f);
        assert(!vm.sensor().connected);
        vm.calibrate();
        assert(!vm.model().calibrating());
        input->value.connected = true;
        input->value.timestamp = std::chrono::steady_clock::now();
        input->value.sample    = {{-3, 0, 9.33f}, {0, 0, 0}};
        vm.tick(0.016f);
        assert(vm.sensor().connected);
        vm.toggle_pause();
        assert(vm.model().paused());
        vm.set_sensitivity(500);
        assert(vm.model().sensitivity() == 200);
        vm.calibrate();
        assert(vm.model().calibrating());
        input->value.timestamp -= std::chrono::seconds(1);
        vm.tick(0.016f);
        assert(!vm.sensor().connected && !vm.model().calibrating());
        input->value.timestamp = std::chrono::steady_clock::now();
        vm.tick(0.016f);
        assert(vm.sensor().connected && vm.model().paused());
        assert(notifications >= 8);
        lv_observer_remove(observer);
    }
    lv_deinit();
    return 0;
}
