#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/circular_strip.h"
#include "led_strip_control.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <esp_efuse_table.h>
#include <driver/i2c_master.h>
#include "power_save_timer.h"
#include "assets/lang_config.h"
#include "power_manager.h"

#define TAG "KevinBoxBoard"

class KevinBoxBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    CircularStrip* led_strip_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_NC);
        // power_manager_->OnChargingStatusChanged([this](bool is_charging) {
        //     if (is_charging) {
        //         power_save_timer_->SetEnabled(false);
        //     } else {
        //         power_save_timer_->SetEnabled(true);
        //     }
        // });
    }
    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, -1, 600);
        power_save_timer_->OnShutdownRequest([this]() {
            gpio_set_level(GPIO_NUM_11, 0);
        });
        power_save_timer_->SetEnabled(true);
    }
    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                // ResetWifiConfiguration();
            }
            else
            {
                // Application::GetInstance().ToggleChatState();
            }
        });
        // boot_button_.OnDoubleClick([this]() {
        //     auto& app = Application::GetInstance();
        //     if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
        //         ResetWifiConfiguration();
        //     }
        // });

        boot_button_.OnMultiClick(5, [this]() {
            ESP_LOGI(TAG, "Detected 5 clicks, entering WiFi configuration mode");
            auto& app = Application::GetInstance();
            ResetWifiConfiguration();
            // app.Alert(Lang::Strings::WIFI_CONFIG_MODE, "", "", Lang::Sounds::P3_WIFICONFIG);
    
        });
        boot_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        boot_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Power"));
        // thing_manager.AddThing(iot::CreateThing("Battery"));
        
        led_strip_ = new CircularStrip(BUILTIN_LED_GPIO, 4);
        auto led_strip_control = new LedStripControl(led_strip_);
        thing_manager.AddThing(led_strip_control);
    }

public:
    KevinBoxBoard() : boot_button_(BOOT_BUTTON_GPIO) {  
        // 把 ESP32C3 的 VDD SPI 引脚作为普通 GPIO 口使用
        esp_efuse_write_field_bit(ESP_EFUSE_VDD_SPI_AS_GPIO);
        // InitializePowerManager();
        InitializeCodecI2c();
        InitializeButtons();
        InitializePowerSaveTimer();
        InitializeIot();
    }

    virtual Led* GetLed() override {
        return led_strip_;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }


    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(KevinBoxBoard);
