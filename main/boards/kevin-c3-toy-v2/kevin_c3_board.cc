#include "wifi_board.h"
#include "blufi_board.h"
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
class KevinBoxBoard : public BlufiBoard {
// class KevinBoxBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    CircularStrip* led_strip_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    
    int click_count_ = 0;
    int64_t last_click_time_ = 0;
    static constexpr int64_t CLICK_TIMEOUT_MS = 5000; // 双击后5秒内需要完成长按
    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_NC);
        // power_manager_->OnChargingStatusChanged([this](bool is_charging) {
        //     if (is_charging) {
        //         power_save_timer_->SetEnabled(false);
        //     } else {
        //         power_save_timer_->SetEnabled(true);
        //     }
        // });
        power_manager_->OnCriticalBatteryStatusChanged([this](bool is_critical) {
            if (is_critical) {
                auto& app = Application::GetInstance();
                app.Alert(Lang::Strings::BATTERY_LOW, Lang::Strings::BATTERY_LOW, "sad", Lang::Sounds::P3_BATRTERY_LOW);
            }
        });
        power_save_timer_->SetEnabled(true);
    }
    void InitializePowerSaveTimer() {
        // Initialize power save timer
        
        ESP_LOGW(TAG, "Initialize power save timer...");
        power_save_timer_ = new PowerSaveTimer(-1, -1, 600);
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down...");
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
        click_count_++;
        last_click_time_ = esp_timer_get_time() / 1000; // 转换为毫秒
        
        // 如果超过2秒没有新的点击，重置计数器
        if (click_count_ == 1) {
            esp_timer_handle_t reset_timer;
            esp_timer_create_args_t timer_args = {
                .callback = [](void* arg) {
                    KevinBoxBoard* board = static_cast<KevinBoxBoard*>(arg);
                    if (board->click_count_ == 1) {
                        board->click_count_ = 0;
                    }
                },
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "click_reset"
            };
            esp_timer_create(&timer_args, &reset_timer);
            esp_timer_start_once(reset_timer, 2000 * 1000); // 2秒
        }
        
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                // ResetWifiConfiguration();
            }
            else
            {
                // Application::GetInstance().ToggleChatState();
            }
        });
        // 添加错误处理到其他按键回调
        boot_button_.OnMultiClick(6, [this]() {
            ESP_LOGI(TAG, "Detected 6 clicks, entering WiFi configuration mode");
            try {
                ResetWifiConfiguration();
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception in OnMultiClick: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception in OnMultiClick");
            }
        });
        
        boot_button_.OnPressDown([this]() {
            ESP_LOGI(TAG, "Boot button pressed down");
            try {
                Application::GetInstance().StartListening();
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception in OnPressDown: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception in OnPressDown");
            }
            
        });

        boot_button_.OnPressUp([this]() {
            ESP_LOGI(TAG, "Boot button released");
            try {
                Application::GetInstance().StopListening();
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception in OnPressUp: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception in OnPressUp");
            }
        });
        boot_button_.OnLongPress([this]() {
            int64_t current_time = esp_timer_get_time() / 1000;
            if (click_count_ >= 2 && (current_time - last_click_time_ < CLICK_TIMEOUT_MS)) {
                ESP_LOGI(TAG, "Shutting down by long press after double click");
                gpio_set_level(GPIO_NUM_11, 0);
            }
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Power"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
        
        led_strip_ = new CircularStrip(BUILTIN_LED_GPIO, 4);
        auto led_strip_control = new LedStripControl(led_strip_);
        thing_manager.AddThing(led_strip_control);
    }

public:
    KevinBoxBoard() : boot_button_(BOOT_BUTTON_GPIO) {  
        // 把 ESP32C3 的 VDD SPI 引脚作为普通 GPIO 口使用
        esp_efuse_write_field_bit(ESP_EFUSE_VDD_SPI_AS_GPIO);
        
        InitializeCodecI2c();
        InitializeButtons();
        InitializePowerSaveTimer();
        InitializePowerManager();
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
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        BlufiBoard::SetPowerSaveMode(enabled);
        // WifiBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(KevinBoxBoard);
