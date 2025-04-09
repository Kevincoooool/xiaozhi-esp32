#include "iot/thing.h"
#include "board.h"
#include "toy_control/pump_controller.h"

#include <esp_log.h>

#define TAG "Pump"

namespace iot {

class Pump : public Thing {
private:
    static constexpr gpio_num_t PUMP_GPIO = GPIO_NUM_47;
    static constexpr gpio_num_t STATUS_GPIO = GPIO_NUM_39;
    PumpController& pump_controller_;

public:
    Pump() : Thing("Pump", "可调松紧度的气泵，支持自动循环收紧松开"),
             pump_controller_(PumpController::GetInstance()) {
        
        pump_controller_.Initialize(PUMP_GPIO, STATUS_GPIO);

        properties_.AddNumberProperty("Pump", "松紧度 (0-100)", [this]() -> double {
            return 0; // TODO: 实现当前速度获取
        });

        methods_.AddMethod("SetPump", "设置松紧度", ParameterList({
            Parameter("Pump", "0到100之间的整数", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            double speed = static_cast<uint8_t>(parameters["Pump"].number());
            uint32_t duty = (1023 * speed) / 100;
            pump_controller_.SetPumpSpeed(duty);
            ESP_LOGI(TAG, "Set Pump speed to %.1f%%, duty: %lu", speed, duty);
        });

        methods_.AddMethod("StartCycle", "开始循环收紧松开", ParameterList({
            Parameter("Interval", "循环间隔(ms)", kValueTypeNumber, true),
            Parameter("TightnessMin", "最小收紧度(0-100)", kValueTypeNumber, true),
            Parameter("TightnessMax", "最大收紧度(0-100)", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            uint32_t interval = static_cast<uint32_t>(parameters["Interval"].number());
            uint32_t min_tight = (1023 * static_cast<uint32_t>(parameters["TightnessMin"].number())) / 100;
            uint32_t max_tight = (1023 * static_cast<uint32_t>(parameters["TightnessMax"].number())) / 100;
            
            pump_controller_.StartCycle(interval, min_tight, max_tight);
            ESP_LOGI(TAG, "Started pump cycle: interval=%lums, min=%lu, max=%lu", 
                    interval, min_tight, max_tight);
        });

        methods_.AddMethod("StopCycle", "停止循环", ParameterList(), 
            [this](const ParameterList& parameters) {
                pump_controller_.StopCycle();
                ESP_LOGI(TAG, "Stopped pump cycle");
        });
        
        methods_.AddMethod("StopPump", "关闭气泵", ParameterList(), 
            [this](const ParameterList& parameters) {
                pump_controller_.StopPump();
                ESP_LOGI(TAG, "Pump stopped");
        });
    }

    ~Pump() = default;
};

} // namespace iot

DECLARE_THING(Pump);