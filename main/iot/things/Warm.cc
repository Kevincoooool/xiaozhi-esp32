#include "iot/thing.h"
#include "toy_control/warm_controller.h"
#include <esp_log.h>

#define TAG "Warm"

namespace iot {

class Warm : public Thing {
private:
    static constexpr gpio_num_t GPIO_NUM = GPIO_NUM_48;
    WarmController& controller_;

public:
    Warm() : Thing("Warm", "可调温度，分为5档从低温到高温"),
             controller_(WarmController::GetInstance()) {
        
        controller_.Initialize(GPIO_NUM);

        properties_.AddNumberProperty("Warm", "温度档位 (0-4)", [this]() -> double {
            return 0; // TODO: 实现当前档位获取
        });

        methods_.AddMethod("SetWarm", "设置温度档位", ParameterList({
            Parameter("Level", "0-4之间的整数，0最低温，4最高温", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            uint8_t level = static_cast<uint8_t>(parameters["Level"].number());
            controller_.SetLevel(level);
            ESP_LOGI(TAG, "Set warm level to %d", level);
        });

        methods_.AddMethod("StopWarm", "关闭加热", ParameterList(), 
            [this](const ParameterList& parameters) {
                controller_.StopWarm();
                ESP_LOGI(TAG, "Warm stopped");
        });
    }

    ~Warm() = default;
};

} // namespace iot

DECLARE_THING(Warm);