#ifndef BUTTON_H_
#define BUTTON_H_

#include <driver/gpio.h>
#include <iot_button.h>
#include <button_types.h>
#include <button_adc.h>
#include <button_gpio.h>
#include <functional>

class Button {
public:
    Button(button_handle_t button_handle);
    Button(gpio_num_t gpio_num, bool active_high = false, uint16_t long_press_time = 0, uint16_t short_press_time = 0);
    ~Button();

    void OnPressDown(std::function<void()> callback);
    void OnPressUp(std::function<void()> callback);
    void OnLongPress(std::function<void()> callback);
    void OnClick(std::function<void()> callback);
    void OnDoubleClick(std::function<void()> callback);
    void OnMultiClick(uint8_t clicks, std::function<void()> callback);
    void OnLongPressHold(std::function<void()> callback);  // 添加新函数声明
    void OnMultipleClick(std::function<void()> callback, uint8_t click_count); // 添加缺失的声明

protected:
    button_handle_t button_handle_ = nullptr;

private:
    gpio_num_t gpio_num_;
    std::function<void()> on_multi_click_;
    uint8_t required_clicks_ = 0;
    uint8_t click_count_ = 0;
    int64_t last_click_time_ = 0;
    static constexpr int64_t MULTI_CLICK_TIMEOUT_MS = 3000; // 连续点击超时时间

    std::function<void()> on_press_down_;
    std::function<void()> on_press_up_;
    std::function<void()> on_long_press_;
    std::function<void()> on_click_;
    std::function<void()> on_double_click_;
    std::function<void()> on_long_press_hold_;
    std::function<void()> on_multiple_click_; // 添加缺失的成员变量
};

#if CONFIG_SOC_ADC_SUPPORTED
class AdcButton : public Button {
public:
    AdcButton(const button_adc_config_t& adc_config);
};
#endif

#endif // BUTTON_H_
