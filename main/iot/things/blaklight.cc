#include "iot/thing.h"
#include "board.h"
#include "display/lcd_display.h"

#include <esp_log.h>

#define TAG "Backlight"

namespace iot {

// 这里仅定义 Backlight 的属性和方法，不包含具体的实现
class Backlight : public Thing {
public:
    Backlight() : Thing("Backlight", "当前 AI 机器人屏幕的亮度") {
        // 定义设备的属性
        properties_.AddNumberProperty("light", "当前亮度值", [this]() -> int {
            // 这里可以添加获取当前亮度的逻辑
            return current_brightness_;
        });

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("SetLight", "设置亮度", ParameterList({
            Parameter("light", "0到100之间的整数", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            auto display = Board::GetInstance().GetDisplay();
            uint8_t brightness = static_cast<uint8_t>(parameters["light"].number());
            display->SetBacklight(brightness);
            current_brightness_ = brightness;  // 保存当前亮度值
        });
    }

private:
    int current_brightness_ = 100;  // 保存当前亮度值
};

} // namespace iot

DECLARE_THING(Backlight);
