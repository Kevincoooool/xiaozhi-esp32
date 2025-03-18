#include "iot/thing.h"
#include "board.h"
#include "application.h"

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
        // ESP_LOGI(TAG, "Shutdown timer expired, powering off");
        auto& app = Application::GetInstance();
        
        // 如果当前不是空闲状态，先等待变为空闲
        if (app.GetDeviceState() != kDeviceStateIdle) {
            // ESP_LOGI(TAG, "Waiting for device to become idle before shutdown");
            vTaskDelay(pdMS_TO_TICKS(100)); // 等待100ms
            return;
        }
        
        // 确保设备为空闲状态后再关机
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

    // 执行关机流程
    void execute_shutdown() {
        auto& app = Application::GetInstance();
        
        // 如果设备不是空闲状态，先尝试切换到空闲状态
        if (app.GetDeviceState() != kDeviceStateIdle) {
            // 启动定时器进行状态检查
            esp_timer_start_periodic(shutdown_timer_, 100000); // 每100ms检查一次
        } else {
            // 已经是空闲状态，直接关机
            gpio_set_level(GPIO_NUM_11, 0);
        }
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
                ESP_LOGI(TAG, "Starting shutdown sequence");
                execute_shutdown();
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