#include "iot/thing.h"
#include "toy_control/vibration_controller.h"
#include <esp_log.h>

#define TAG "Vibration"

namespace iot {

class Vibration : public Thing {
private:
    static constexpr gpio_num_t GPIO_NUM = GPIO_NUM_38;
    VibrationController& controller_;

public:
    Vibration() : Thing("Vibration", "可调力度，分为10档从最弱到最强"),
                  controller_(VibrationController::GetInstance()) {
        
        controller_.Initialize(GPIO_NUM);

        properties_.AddNumberProperty("Vibration", "力度档位 (0-9)", [this]() -> double {
            return 0; // TODO: 实现当前档位获取
        });

        methods_.AddMethod("SetVibration", "设置力度档位", ParameterList({
            Parameter("Level", "0-9之间的整数，0最弱，9最强", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            uint8_t level = static_cast<uint8_t>(parameters["Level"].number());
            controller_.SetLevel(level);
            ESP_LOGI(TAG, "Set vibration level to %d", level);
        });

        methods_.AddMethod("StopVibration", "停止震动", ParameterList(), 
            [this](const ParameterList& parameters) {
                controller_.StopVibration();
                ESP_LOGI(TAG, "Vibration stopped");
        });
    }

    ~Vibration() = default;
};

} // namespace iot

DECLARE_THING(Vibration);