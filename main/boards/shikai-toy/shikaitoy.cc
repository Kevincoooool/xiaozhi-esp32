#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include "assets/lang_config.h"
#include "toy_control/vibration_controller.h"
#include "toy_control/pump_controller.h"
#include "toy_control/warm_controller.h"
#define TAG "shikai-toy"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);


class Pmic : public Axp2101 {
    public:
        Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
            // ** EFUSE defaults **
            WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
            WriteReg(0x27, 0x10);  // hold 4s to power off
            WriteReg(0x30, 0x11);  // hold 4s to power off

            WriteReg(0x93, 0x1C); // 配置 aldo2 输出为 3.3V
        
            uint8_t value = ReadReg(0x90); // XPOWERS_AXP2101_LDO_ONOFF_CTRL0
            value = value | 0x02; // set bit 1 (ALDO2)
            WriteReg(0x90, value);  // and power channels now enabled
        
            WriteReg(0x64, 0x03); // CV charger voltage setting to 4.2V
            
            WriteReg(0x61, 0x05); // set Main battery precharge current to 125mA
            WriteReg(0x62, 0x0A); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
            WriteReg(0x63, 0x15); // set Main battery term charge current to 125mA
        
            WriteReg(0x14, 0x00); // set minimum system voltage to 4.1V (default 4.7V), for poor USB cables
            WriteReg(0x15, 0x00); // set input voltage limit to 3.88v, for poor USB cables
            WriteReg(0x16, 0x05); // set input current limit to 2000mA
        
            WriteReg(0x24, 0x01); // set Vsys for PWROFF threshold to 3.2V (default - 2.6V and kill battery)
            WriteReg(0x50, 0x14); // set TS pin to EXTERNAL input (not temperature)
        }
    };
    
class SHIKAI_TOY : public WifiBoard {
private:
    Button vibration_button_;  // 原 boot_button_
    Button pump_button_;       // 原 siphon_button_
    Button mode_button_;       // 原 volume_button_
    PumpController& pump_controller_;
    VibrationController& vibration_controller_;
    WarmController& warm_controller_;
    bool is_singing_mode_ = false;  // 
    LcdDisplay* display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    PowerSaveTimer* power_save_timer_;
    Pmic* pmic_ = nullptr;
    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, -1, 600);
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
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
        // 震动按键功能
        vibration_button_.OnClick([this]() {
            static uint8_t vibration_level = 0;
            auto& app = Application::GetInstance();
            
            // 在未连接WiFi时的配置功能保持不变
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
                return;
            }

            // 震动模式循环切换
            vibration_level = (vibration_level + 1) % 11;  // 0-10循环
            if (vibration_level == 0) {
                vibration_controller_.StopVibration();
                ESP_LOGI(TAG, "Vibration stopped");
            } else {
                vibration_controller_.SetLevel(vibration_level - 1);
                ESP_LOGI(TAG, "Vibration level set to %d", vibration_level - 1);
            }
        });

        vibration_button_.OnLongPress([this]() {
            // 长按1.5秒启动震动
            vibration_controller_.SetLevel(0);  // 从最低档开始
            ESP_LOGI(TAG, "Vibration mode activated");
        });

        // 吸力按键功能
        pump_button_.OnClick([this]() {
            static uint8_t pump_mode = 0;  // 0:停止, 1:弱, 2:中, 3:强
            
            pump_mode = (pump_mode + 1) % 4;
            switch (pump_mode) {
                case 0:
                    pump_controller_.StopPump();
                    ESP_LOGI(TAG, "Pump stopped");
                    break;
                case 1:  // 弱循环模式
                    pump_controller_.StartCycle(2000, 200, 600);
                    ESP_LOGI(TAG, "Pump weak mode");
                    break;
                case 2:  // 中循环模式
                    pump_controller_.StartCycle(1500, 300, 800);
                    ESP_LOGI(TAG, "Pump medium mode");
                    break;
                case 3:  // 强循环模式
                    pump_controller_.StartCycle(1000, 400, 1000);
                    ESP_LOGI(TAG, "Pump strong mode");
                    break;
            }
        });

        pump_button_.OnLongPress([this]() {
            // 长按2秒进入待机状态
            pump_controller_.StopPump();
            vibration_controller_.StopVibration();
            warm_controller_.StopWarm();
            ESP_LOGI(TAG, "Entering standby mode");
        });

        // 模式按键功能
        mode_button_.OnClick([this]() {
            static int last_volume = 20;
            auto& app = Application::GetInstance();
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            last_volume += 20;
            
            if (last_volume > 100) {
                last_volume = 20;
            }
            
            codec->SetOutputVolume(last_volume);
            app.PlaySound(Lang::Sounds::P3_SUCCESS);
        });

        mode_button_.OnLongPress([this]() {
            // 长按1.5秒切换语音对话/哼唱模式
            is_singing_mode_ = !is_singing_mode_;
            auto& app = Application::GetInstance();
            
            if (is_singing_mode_) {
                app.StopListening();  // 停止语音对话
                ESP_LOGI(TAG, "Switching to singing mode");
                // TODO: 启动哼唱模式
            } else {
                ESP_LOGI(TAG, "Switching to voice dialogue mode");
                app.StartListening();
            }
        });
    }
    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        // thing_manager.AddThing(iot::CreateThing("Intensity"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
        thing_manager.AddThing(iot::CreateThing("Pump"));
        thing_manager.AddThing(iot::CreateThing("Vibration"));
        thing_manager.AddThing(iot::CreateThing("Warm"));
    }

    void InitializeGPIO() {
        // 配置 GPIO15 为输入
        gpio_config_t input_conf = {
            .pin_bit_mask = (1ULL << 15),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&input_conf);

        // 配置 GPIO4 为输出
        gpio_config_t output_conf = {
            .pin_bit_mask = (1ULL << 4),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&output_conf);

        // 创建定时器任务来检查 GPIO15 状态
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                int level = gpio_get_level(GPIO_NUM_15);
                gpio_set_level(GPIO_NUM_4, level); // 反转电平
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "gpio_check",
            .skip_unhandled_events = true
        };

        esp_timer_handle_t gpio_timer;
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &gpio_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(gpio_timer, 100000)); // 每100ms检查一次
    }

public:
    SHIKAI_TOY() : vibration_button_(VIBRATION_BUTTON_GPIO), 
                        pump_button_(PUMP_BUTTON_GPIO), 
                        mode_button_(MODE_BUTTON_GPIO),
                    pump_controller_(PumpController::GetInstance()),
                    vibration_controller_(VibrationController::GetInstance()),
                    warm_controller_(WarmController::GetInstance()) {
        ESP_LOGI(TAG, "Initializing SHIKAI TOY Board");
        InitializeCodecI2c();
        pmic_ = new Pmic(codec_i2c_bus_, AXP2101_I2C_ADDR);
      // 初始化控制器
        pump_controller_.Initialize(GPIO_NUM_47, GPIO_NUM_39);
        vibration_controller_.Initialize(GPIO_NUM_38);
        warm_controller_.Initialize(GPIO_NUM_48);
        InitializeButtons();
        InitializePowerSaveTimer();
        InitializeIot();
        InitializeGPIO();  // 添加 GPIO 初始化
    }
    

    virtual Led* GetLed() override {
        static NoLed led;
        return &led;
    }

    virtual AudioCodec *GetAudioCodec() override {
        static NoAudioCodecSimplexPdm audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_DIN);

            // static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            //     AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }


    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(SHIKAI_TOY);
