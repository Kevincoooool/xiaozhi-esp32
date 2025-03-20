#ifndef _CIRCULAR_STRIP_H_
#define _CIRCULAR_STRIP_H_

#include "led.h"
#include <driver/gpio.h>
#include <led_strip.h>
#include <esp_timer.h>
#include <atomic>
#include <mutex>
#include <vector>

#define DEFAULT_BRIGHTNESS 255
#define LOW_BRIGHTNESS 4

struct StripColor {
    uint8_t red = 0, green = 0, blue = 0;
};

class CircularStrip : public Led {
public:
    CircularStrip(gpio_num_t gpio, uint8_t max_leds);
    virtual ~CircularStrip();

    void OnStateChanged() override;
    void SetBrightness(uint8_t default_brightness, uint8_t low_brightness);
    void SetAllColor(StripColor color);
    void SetSingleColor(uint8_t index, StripColor color);
    void Blink(StripColor color, int interval_ms);
    void Breathe(StripColor low, StripColor high, int interval_ms);
    void Scroll(StripColor low, StripColor high, int length, int interval_ms);

private:
    std::mutex mutex_;
    TaskHandle_t blink_task_ = nullptr;
    led_strip_handle_t led_strip_ = nullptr;
    int max_leds_ = 0;
    std::vector<StripColor> colors_;
    int blink_counter_ = 0;
    int blink_interval_ms_ = 0;
    esp_timer_handle_t strip_timer_ = nullptr;
    std::function<void()> strip_callback_ = nullptr;

    uint8_t default_brightness_ = DEFAULT_BRIGHTNESS;
    uint8_t low_brightness_ = LOW_BRIGHTNESS;
// 添加彩虹颜色数组
    const std::vector<StripColor> rainbow_colors_ = {
        {255, 0, 0},     // 红
        {255, 127, 0},   // 橙
        {255, 255, 0},   // 黄
        {0, 255, 0},     // 绿
        {0, 0, 255},     // 蓝
        {75, 0, 130},    // 靛
        {148, 0, 211}    // 紫
    };
    void RainbowBreathe(int interval_ms);
    void RainbowScroll(int interval_ms);  // 新增彩虹跑马灯函数
    void StartStripTask(int interval_ms, std::function<void()> cb);
    void Rainbow(StripColor low, StripColor high, int interval_ms);
    void FadeOut(int interval_ms);
};

#endif // _CIRCULAR_STRIP_H_
