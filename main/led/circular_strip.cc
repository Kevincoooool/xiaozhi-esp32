#include "circular_strip.h"
#include "application.h"
#include <esp_log.h>

#define TAG "CircularStrip"

#define BLINK_INFINITE -1

CircularStrip::CircularStrip(gpio_num_t gpio, uint8_t max_leds) : max_leds_(max_leds) {
    // If the gpio is not connected, you should use NoLed class
    assert(gpio != GPIO_NUM_NC);

    colors_.resize(max_leds_);

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = max_leds_;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);

    esp_timer_create_args_t strip_timer_args = {
        .callback = [](void *arg) {
            auto strip = static_cast<CircularStrip*>(arg);
            std::lock_guard<std::mutex> lock(strip->mutex_);
            if (strip->strip_callback_ != nullptr) {
                strip->strip_callback_();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "strip_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&strip_timer_args, &strip_timer_));
}

CircularStrip::~CircularStrip() {
    esp_timer_stop(strip_timer_);
    if (led_strip_ != nullptr) {
        led_strip_del(led_strip_);
    }
}


void CircularStrip::SetAllColor(StripColor color) {
    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(strip_timer_);
    for (int i = 0; i < max_leds_; i++) {
        colors_[i] = color;
        led_strip_set_pixel(led_strip_, i, color.red, color.green, color.blue);
    }
    led_strip_refresh(led_strip_);
}

void CircularStrip::SetSingleColor(uint8_t index, StripColor color) {
    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(strip_timer_);
    colors_[index] = color;
    led_strip_set_pixel(led_strip_, index, color.red, color.green, color.blue);
    led_strip_refresh(led_strip_);
}

void CircularStrip::Blink(StripColor color, int interval_ms) {
    for (int i = 0; i < max_leds_; i++) {
        colors_[i] = color;
    }
    StartStripTask(interval_ms, [this]() {
        static bool on = true;
        if (on) {
            for (int i = 0; i < max_leds_; i++) {
                led_strip_set_pixel(led_strip_, i, colors_[i].red, colors_[i].green, colors_[i].blue);
            }
            led_strip_refresh(led_strip_);
        } else {
            led_strip_clear(led_strip_);
        }
        on = !on;
    });
}

void CircularStrip::FadeOut(int interval_ms) {
    StartStripTask(interval_ms, [this]() {
        bool all_off = true;
        for (int i = 0; i < max_leds_; i++) {
            colors_[i].red /= 2;
            colors_[i].green /= 2;
            colors_[i].blue /= 2;
            if (colors_[i].red != 0 || colors_[i].green != 0 || colors_[i].blue != 0) {
                all_off = false;
            }
            led_strip_set_pixel(led_strip_, i, colors_[i].red, colors_[i].green, colors_[i].blue);
        }
        if (all_off) {
            led_strip_clear(led_strip_);
            esp_timer_stop(strip_timer_);
        } else {
            led_strip_refresh(led_strip_);
        }
    });
}

void CircularStrip::Breathe(StripColor low, StripColor high, int interval_ms) {
    StartStripTask(interval_ms, [this, low, high]() {
        static bool increase = true;
        static StripColor color = low;
        if (increase) {
            if (color.red < high.red) {
                color.red++;
            }
            if (color.green < high.green) {
                color.green++;
            }
            if (color.blue < high.blue) {
                color.blue++;
            }
            if (color.red == high.red && color.green == high.green && color.blue == high.blue) {
                increase = false;
            }
        } else {
            if (color.red > low.red) {
                color.red--;
            }
            if (color.green > low.green) {
                color.green--;
            }
            if (color.blue > low.blue) {
                color.blue--;
            }
            if (color.red == low.red && color.green == low.green && color.blue == low.blue) {
                increase = true;
            }
        }
        for (int i = 0; i < max_leds_; i++) {
            led_strip_set_pixel(led_strip_, i, color.red, color.green, color.blue);
        }
        led_strip_refresh(led_strip_);
    });
}

void CircularStrip::Scroll(StripColor low, StripColor high, int length, int interval_ms) {
    for (int i = 0; i < max_leds_; i++) {
        colors_[i] = low;
    }
    StartStripTask(interval_ms, [this, low, high, length]() {
        static int offset = 0;
        for (int i = 0; i < max_leds_; i++) {
            colors_[i] = low;
        }
        for (int j = 0; j < length; j++) {
            int i = (offset + j) % max_leds_;
            colors_[i] = high;
        }
        for (int i = 0; i < max_leds_; i++) {
            led_strip_set_pixel(led_strip_, i, colors_[i].red, colors_[i].green, colors_[i].blue);
        }
        led_strip_refresh(led_strip_);
        offset = (offset + 1) % max_leds_;
    });
}

void CircularStrip::StartStripTask(int interval_ms, std::function<void()> cb) {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(strip_timer_);
    
    strip_callback_ = cb;
    esp_timer_start_periodic(strip_timer_, interval_ms * 1000);
}

void CircularStrip::SetBrightness(uint8_t default_brightness, uint8_t low_brightness) {
    default_brightness_ = default_brightness;
    low_brightness_ = low_brightness;
    OnStateChanged();
}

void CircularStrip::RainbowScroll(int interval_ms) {
    StartStripTask(interval_ms, [this]() {
        static int offset = 0;
        
        for (int i = 0; i < max_leds_; i++) {
            // 计算当前LED对应的彩虹颜色索引
            int color_index = (i + offset) % rainbow_colors_.size();
            const auto& color = rainbow_colors_[color_index];
            
            // 应用亮度调整
            uint8_t r = color.red * default_brightness_ / 255;
            uint8_t g = color.green * default_brightness_ / 255;
            uint8_t b = color.blue * default_brightness_ / 255;
            
            // 设置LED颜色
            led_strip_set_pixel(led_strip_, i, r, g, b);
        }
        
        // 刷新LED显示
        led_strip_refresh(led_strip_);
        
        // 更新偏移量，实现跑马灯效果
        offset = (offset + 1) % rainbow_colors_.size();
    });
}
void CircularStrip::RainbowBreathe(int interval_ms) {
    StartStripTask(interval_ms, [this]() {
        static int brightness = 0;
        static bool increasing = true;
        static int current_index = 0;
        static float transition_progress = 0.0f;  // 颜色过渡进度(0.0-1.0)
        
        // 获取当前颜色和下一个颜色
        const auto& current_color = rainbow_colors_[current_index];
        const auto& next_color = rainbow_colors_[(current_index + 1) % rainbow_colors_.size()];
        
        // 计算当前亮度
        if (increasing) {
            brightness += 2;
            if (brightness >= default_brightness_) {
                brightness = default_brightness_;
                increasing = false;
                
                // 开始颜色过渡
                transition_progress += 0.02f;  // 每次增加2%的过渡进度
                
                // 当过渡完成时,切换到下一个颜色
                if (transition_progress >= 1.0f) {
                    transition_progress = 0.0f;
                    current_index = (current_index + 1) % rainbow_colors_.size();
                }
            }
        } else {
            brightness -= 2;
            if (brightness <= 0) {
                brightness = 0;
                increasing = true;
            }
        }
        
        // 计算过渡后的颜色
        uint8_t r = (current_color.red * (1.0f - transition_progress) + 
                    next_color.red * transition_progress) * brightness / 255;
        uint8_t g = (current_color.green * (1.0f - transition_progress) + 
                    next_color.green * transition_progress) * brightness / 255;
        uint8_t b = (current_color.blue * (1.0f - transition_progress) + 
                    next_color.blue * transition_progress) * brightness / 255;
        
        // 设置所有LED为相同颜色
        for (int i = 0; i < max_leds_; i++) {
            led_strip_set_pixel(led_strip_, i, r, g, b);
        }
        
        led_strip_refresh(led_strip_);
    });
}
void CircularStrip::OnStateChanged() {
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();
    switch (device_state) {
        case kDeviceStateStarting: {
            // 启动状态：蓝色呼吸灯效果
            StripColor low = { 0, 0, 0 };
            StripColor high = { 0, 0, default_brightness_ };  // 纯蓝色
            Scroll(low, high, 3, 100);
            break;
        }
        case kDeviceStateWifiConfiguring: {
            // 配网状态：蓝色闪烁
            StripColor color = { 0, 0, default_brightness_ };  // 纯蓝色
            Blink(color, 500);
            break;
        }
        case kDeviceStateIdle: {
            // 空闲状态：渐隐
            FadeOut(50);
            // 空闲状态：淡白光常亮
            uint8_t dim_white = default_brightness_ / 3;  // 降低亮度为正常的1/8
            StripColor color = { dim_white, dim_white, dim_white };  // RGB均衡的白光
            SetAllColor(color);
            }
            break;
        case kDeviceStateConnecting: {
            // 连接状态：蓝色常亮
            // StripColor color = { 0, 0, default_brightness_ };  // 纯蓝色
            // SetAllColor(color);
            StripColor color = { 0, default_brightness_, 0 };  // 纯绿色
            Blink(color, 300);  // 300ms的闪烁间隔
            break;
        }
        case kDeviceStateListening: {
            // 录音状态：鲜红色
            StripColor color = { default_brightness_, 0, 0 };  // 纯红色
            SetAllColor(color);
            // RainbowBreathe(50);  // 每20ms更新一次亮度
            break;
        }
        case kDeviceStateSpeaking: {
            // 播放状态：鲜绿色
            // StripColor color = { 0, default_brightness_, 0 };  // 纯绿色
            // SetAllColor(color);
            RainbowScroll(100);  // 每50ms更新一次，速度更快

            break;
        }
        case kDeviceStateUpgrading: {
            // 升级状态：黄色快闪
            StripColor color = { default_brightness_, default_brightness_, 0 };  // 黄色
            Blink(color, 100);
            break;
        }
        case kDeviceStateActivating: {
            // 激活状态：绿色慢闪
            StripColor color = { 0, default_brightness_, 0 };  // 纯绿色
            Blink(color, 500);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown led strip event: %d", device_state);
            return;
    }
}
