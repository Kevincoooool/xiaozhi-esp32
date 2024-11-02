/*
 * @Author: Kevincoooool 33611679+Kevincoooool@users.noreply.github.com
 * @Date: 2024-11-01 20:52:44
 * @LastEditors: Kevincoooool 33611679+Kevincoooool@users.noreply.github.com
 * @LastEditTime: 2024-11-01 20:53:09
 * @FilePath: \xiaozhi-esp32\main\boards\leo_ai\leo_aiBoard.cc
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "WifiBoard.h"
#include "BoxAudioDevice.h"

#include <esp_log.h>

#define TAG "leo_aiBoard"

class leo_aiBoard : public WifiBoard {
public:
    virtual void Initialize() override {
        ESP_LOGI(TAG, "Initializing leo_aiBoard");
        WifiBoard::Initialize();
    }

    virtual AudioDevice* CreateAudioDevice() override {
        return new BoxAudioDevice();
    }
};

DECLARE_BOARD(leo_aiBoard);
