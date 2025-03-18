#include "button.h"

#include <esp_log.h>
#include <esp_timer.h>
static const char* TAG = "Button";
#if CONFIG_SOC_ADC_SUPPORTED
Button::Button(const button_adc_config_t& adc_cfg) {
    button_config_t button_config = {
        .type = BUTTON_TYPE_ADC,
        .long_press_time = 1000,
        .short_press_time = 50,
        .adc_button_config = adc_cfg
    };
    button_handle_ = iot_button_create(&button_config);
    if (button_handle_ == NULL) {
        ESP_LOGE(TAG, "Failed to create button handle");
        return;
    }
}
#endif

Button::Button(gpio_num_t gpio_num, bool active_high) : gpio_num_(gpio_num) {
    if (gpio_num == GPIO_NUM_NC) {
        return;
    }
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = gpio_num,
            .active_level = static_cast<uint8_t>(active_high ? 1 : 0)
        }
    };
    button_handle_ = iot_button_create(&button_config);
    if (button_handle_ == NULL) {
        ESP_LOGE(TAG, "Failed to create button handle");
        return;
    }
}

Button::~Button() {
    if (button_handle_ != NULL) {
        iot_button_delete(button_handle_);
    }
}

void Button::OnMultiClick(uint8_t clicks, std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    
    required_clicks_ = clicks;
    on_multi_click_ = callback;
    
    // 注册单击回调来统计点击次数
    iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        int64_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
        
        // 消抖处理：忽略太快的连续点击
        if (current_time - button->last_click_time_ < 50) { // 50ms消抖时间
            return;
        }
        
        // 如果距离上次点击超过timeout,重置计数
        if (current_time - button->last_click_time_ > MULTI_CLICK_TIMEOUT_MS) {
            button->click_count_ = 0;
        }
        
        button->last_click_time_ = current_time;
        button->click_count_++;
        
        ESP_LOGI(TAG, "Button clicked, count: %d", button->click_count_);
        
        // 启动一个定时器，在超时后检查点击次数
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                Button* button = static_cast<Button*>(arg);
                if (button->click_count_ == button->required_clicks_) {
                    if (button->on_multi_click_) {
                        button->on_multi_click_();
                    }
                }
                button->click_count_ = 0;
            },
            .arg = button,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "multi_click_timer",
            .skip_unhandled_events = true,
        };
        
        esp_timer_handle_t timer;
        if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
            esp_timer_start_once(timer, MULTI_CLICK_TIMEOUT_MS * 1000); // 转换为微秒
        }
        
    }, this);
}
void Button::OnPressDown(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_press_down_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_PRESS_DOWN, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_press_down_) {
            button->on_press_down_();
        }
    }, this);
}

void Button::OnPressUp(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_press_up_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_press_up_) {
            button->on_press_up_();
        }
    }, this);
}

void Button::OnLongPress(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_long_press_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_long_press_) {
            button->on_long_press_();
        }
    }, this);
}

void Button::OnClick(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_click_) {
            button->on_click_();
        }
    }, this);
}

void Button::OnDoubleClick(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_double_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, [](void* handle, void* usr_data) {
        Button* button = static_cast<Button*>(usr_data);
        if (button->on_double_click_) {
            button->on_double_click_();
        }
    }, this);
}
