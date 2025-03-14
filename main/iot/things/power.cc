#include "iot/thing.h"
#include "board.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>
#define TAG "Power"

namespace iot {

class Power : public Thing {
private:
    int level_ = 0;
    bool charging_ = false;
    bool discharging_ = false;
    esp_timer_handle_t shutdown_timer_ = nullptr;

    // 定时器回调函数
    static void shutdown_timer_callback(void* arg) {
        ESP_LOGI(TAG, "Shutdown timer expired, powering off");
        gpio_set_level(GPIO_NUM_11, 0);  // 拉低电源控制
    }

    // 初始化定时器
    void init_shutdown_timer() {
        const esp_timer_create_args_t timer_args = {
            .callback = &shutdown_timer_callback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "shutdown_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &shutdown_timer_));
    }

public:
    Power() : Thing("Power", "电池管理") {
        init_shutdown_timer();  // 初始化定时器

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("SetPower", "设置关机", ParameterList({
            Parameter("Power", "是否关机", kValueTypeBoolean, true)
        }), [this](const ParameterList& parameters) {
            auto power = static_cast<uint8_t>(parameters["Power"].boolean());
            if(power == true) {
                ESP_LOGI(TAG, "Starting 4 second shutdown timer");
                // 启动4秒定时器
                esp_timer_stop(shutdown_timer_);  // 先停止之前的定时器（如果有）
                esp_timer_start_once(shutdown_timer_, 4000000); // 4秒 = 4000000微秒
            }
        });
    }

    ~Power() {
        if (shutdown_timer_) {
            esp_timer_stop(shutdown_timer_);
            esp_timer_delete(shutdown_timer_);
        }
    }
};

} // namespace iot

DECLARE_THING(Power);