#include "wifi_board.h"
#include "ml307_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include <esp_lcd_st77916.h>
#include "pcf85063.h"  // 添加PCF85063头文件
#include <esp_sleep.h>  // 添加休眠头文件
#include <time.h>
#include "driver/rtc_io.h"
#include "soc/rtc.h"
#define TAG "kevin-lcd1.8"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class Pmic : public Axp2101 {
    public:
        Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
            // ** EFUSE defaults **
            WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
            WriteReg(0x27, 0x10);  // hold 4s to power off
        
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
    
class KEVIN_LCD_18 : public Ml307Board {
// class KEVIN_LCD_18 : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    Pmic* pmic_ = nullptr;
    PCF85063* rtc_ = nullptr;  // 添加RTC对象
    // 添加RTC唤醒标志
    bool rtc_wakeup_ = false;
        
    Button volume_up_button_;
    Button volume_down_button_;
    PowerSaveTimer* power_save_timer_;
    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, -1, 600);
        power_save_timer_->OnShutdownRequest([this]() {
            if (HasAlarm()) {
                ESP_LOGI(TAG, "检测到闹钟设置，进入深度休眠模式而不是关机");
                EnterDeepSleep();
            } else {
                ESP_LOGI(TAG, "无闹钟设置，执行正常关机");
                pmic_->PowerOff();
            }
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

    // 初始化RTC时钟芯片
    void InitializeRTC() {
        rtc_ = new PCF85063(codec_i2c_bus_);
        rtc_->Initialize();
        
        // 获取并打印当前时间
        struct tm time_info;
        rtc_->GetTimeStruct(&time_info);
        ESP_LOGI(TAG, "当前RTC时间: %04d-%02d-%02d %02d:%02d:%02d",
                time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
                time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
    }
    void Enable4GModule() {
        // Make GPIO HIGH to enable the 4G module
        gpio_config_t ml307_enable_config = {
            .pin_bit_mask = (1ULL << 45),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&ml307_enable_config);
        gpio_set_level(GPIO_NUM_45, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_45, 0);
    }

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize QSPI bus");

        const spi_bus_config_t bus_config = KEVIN_ST77916_PANEL_BUS_QSPI_CONFIG(QSPI_PIN_NUM_LCD_PCLK,
                                                                        QSPI_PIN_NUM_LCD_DATA0,
                                                                        QSPI_PIN_NUM_LCD_DATA1,
                                                                        QSPI_PIN_NUM_LCD_DATA2,
                                                                        QSPI_PIN_NUM_LCD_DATA3,
                                                                        QSPI_LCD_H_RES * 80 * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void Initializest77916Display() {

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Install panel IO");
        
        const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install ST77916 panel driver");
        
        st77916_vendor_config_t vendor_config = {
            .flags = {
                .use_qspi_interface = 1,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // Implemented by LCD command `36h`
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,    // Implemented by LCD command `3Ah` (16/18)
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                // ResetWifiConfiguration();
            }
        });
        boot_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        boot_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });
        
        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }


    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("AlarmClock")); // 添加闹钟组件
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
    }

public:
    KEVIN_LCD_18() :  
    Ml307Board(ML307_TX_PIN, ML307_RX_PIN, 4096),
    boot_button_(BOOT_BUTTON_GPIO) ,
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing KEVIN LCD 1.8 Board");
        InitializeCodecI2c();
        pmic_ = new Pmic(codec_i2c_bus_, AXP2101_I2C_ADDR);
        Enable4GModule();
        InitializeSpi();
        InitializeButtons();
        Initializest77916Display();
        InitializeIot();
        InitializePowerSaveTimer();
        GetBacklight()->RestoreBrightness();
        InitializeRTC();
    }
    

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
     // 获取RTC对象
     virtual PCF85063* GetRTC() override {
        return rtc_;
    }
    
    // 设置RTC时间
    void SetRTCTime(time_t time) {
        if (rtc_) {
            rtc_->SetTime(time);
        }
    }
    virtual void SyncTimeToRTC(time_t time) override {
        if (rtc_ != nullptr) {
            ESP_LOGI(TAG, "同步系统时间到RTC芯片");
            rtc_->SetTime(time);
            
            // 获取并打印同步后的RTC时间
            struct tm time_info;
            rtc_->GetTimeStruct(&time_info);
            ESP_LOGI(TAG, "RTC时间已更新: %04d-%02d-%02d %02d:%02d:%02d",
                    time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
                    time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
        }
    }
    // 实现深度休眠方法
    virtual void EnterDeepSleep(int64_t sleep_time_us = 0) override {
        ESP_LOGI(TAG, "准备进入深度休眠模式");
        
        // 保存必要的状态
        GetBacklight()->SetBrightness(0);
        
        // 关闭外设
        auto codec = GetAudioCodec();
        codec->EnableInput(false);
        codec->EnableOutput(false);
        
        
        rtc_gpio_pullup_en(VOLUME_DOWN_BUTTON_GPIO);
        rtc_gpio_pulldown_dis(VOLUME_DOWN_BUTTON_GPIO);
        
        // 可以添加按键唤醒
        esp_sleep_enable_ext0_wakeup(VOLUME_DOWN_BUTTON_GPIO, 0); // 低电平唤醒
        
        // 进入深度休眠
        ESP_LOGI(TAG, "正在进入深度休眠...");
        esp_deep_sleep_start();
    }
    
   
};

DECLARE_BOARD(KEVIN_LCD_18);
