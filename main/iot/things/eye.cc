#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"
#include "display.h"

#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "Eye"

namespace iot {

// 这里仅定义 Eye 的属性和方法，不包含具体的实现
class Eye : public Thing {
private:

public:
    Eye() : Thing("Eye", "控制眼球样式") {
        
        // 定义设备的属性
        properties_.AddNumberProperty("eyeType", "眼球样式", [this]() -> int {
            return Board::GetInstance().GetDisplay()->getCurrentEyeType();
        });

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("SwitchType", "切换样式", ParameterList(), [this](const ParameterList& parameters) {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            display->changeEyeStyle(); 
        });

    }
};

} // namespace iot

DECLARE_THING(Eye);
