#ifndef BUTTON_H_
#define BUTTON_H_

#include <driver/gpio.h>
#include <iot_button.h>
#include <functional>

class Button {
public:
#if CONFIG_SOC_ADC_SUPPORTED
    Button(const button_adc_config_t& cfg);
#endif
    Button(gpio_num_t gpio_num, bool active_high = false);
    ~Button();

    void OnPressDown(std::function<void()> callback);
    void OnPressUp(std::function<void()> callback);
    void OnLongPress(std::function<void()> callback);
    void OnClick(std::function<void()> callback);
    void OnDoubleClick(std::function<void()> callback);
    void OnMultiClick(uint8_t clicks, std::function<void()> callback);
    
private:
    gpio_num_t gpio_num_;
    button_handle_t button_handle_ = nullptr;
    std::function<void()> on_multi_click_;
    uint8_t required_clicks_ = 0;
    uint8_t click_count_ = 0;
    int64_t last_click_time_ = 0;
    static constexpr int64_t MULTI_CLICK_TIMEOUT_MS = 2500; // 连续点击超时时间

    std::function<void()> on_press_down_;
    std::function<void()> on_press_up_;
    std::function<void()> on_long_press_;
    std::function<void()> on_click_;
    std::function<void()> on_double_click_;
};

#endif // BUTTON_H_
