#include "wifi_board.h"
#include "ml307_board.h"
#include "dual_network_board.h"
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
#include <esp_sleep.h> // 添加休眠头文件
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
        value = value | 0x02;          // set bit 1 (ALDO2)
        WriteReg(0x90, value);         // and power channels now enabled

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
class KEVIN_LCD_18 : public DualNetworkBoard {
    // class KEVIN_LCD_18 : public Ml307Board {
    // class KEVIN_LCD_18 : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
    LcdDisplay *display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    Pmic *pmic_ = nullptr;
    PCF85063 *rtc_ = nullptr; // 添加RTC对象
    // 添加RTC唤醒标志
    bool rtc_wakeup_ = false;
    esp_timer_handle_t alarm_timer_ = nullptr;

    Button volume_up_button_;
    // Button volume_down_button_;
    PowerSaveTimer *power_save_timer_;
    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, -1, 600);
        power_save_timer_->OnShutdownRequest([this]()
                                             {
            // 检查闹钟是否启用且未触发
            bool has_alarm = false;
            if (rtc_ != nullptr) {
                has_alarm = rtc_->IsAlarmEnabled() && !rtc_->IsAlarmTriggered();
                
                if (has_alarm) {
                    // 获取闹钟时间进行检查
                    struct tm alarm_time;
                    if (rtc_->GetAlarmTime(&alarm_time)) {
                        // 获取当前时间
                        struct tm current_time;
                        rtc_->GetTimeStruct(&current_time);
                        
                        // 比较时间,检查闹钟是否已过期
                        alarm_time.tm_mon = current_time.tm_mon;
                        alarm_time.tm_year = current_time.tm_year;
                        time_t alarm_timestamp = mktime(&alarm_time);
                        time_t current_timestamp = mktime(&current_time);
                        
                        // 修改判断逻辑：如果闹钟时间大于当前时间，则认为是有效闹钟
                        has_alarm = (alarm_timestamp > current_timestamp);
                        
                        if (has_alarm) {
                            ESP_LOGI(TAG, "检测到有效闹钟，时间为: %02d:%02d:%02d",
                                     alarm_time.tm_hour, alarm_time.tm_min, alarm_time.tm_sec);
                        } else {
                            ESP_LOGI(TAG, "闹钟时间已过期: %02d:%02d:%02d",
                                     alarm_time.tm_hour, alarm_time.tm_min, alarm_time.tm_sec);
                        }
                    }
                }
            }
            
            if (has_alarm) {
                ESP_LOGI(TAG, "检测到有效闹钟设置，进入深度休眠模式而不是关机");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                EnterDeepSleep();
            } else {
                ESP_LOGI(TAG, "无有效闹钟设置，执行正常关机");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                pmic_->PowerOff();
            } });
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

    esp_timer_handle_t alarm_check_timer_ = nullptr;
    volatile bool alarm_triggered_ = false;

    // 闹钟中断处理函数，只设置标志位
    static void IRAM_ATTR OnAlarmInterruptHandler(void *arg) {
        KEVIN_LCD_18 *board = (KEVIN_LCD_18 *)arg;
        board->alarm_triggered_ = true;
    }

    // 定时器回调函数，检查闹钟状态
    static void OnAlarmCheckTimer(void *arg) {
        KEVIN_LCD_18 *board = (KEVIN_LCD_18 *)arg;
        if (board->alarm_triggered_)
        {
            board->alarm_triggered_ = false;
            if (board->rtc_ != nullptr && board->rtc_->IsAlarmEnabled() && board->rtc_->IsAlarmTriggered())
            {
                // 在定时器中执行Alert
                Application::GetInstance().Alert(Lang::Strings::ALARM, "", "", Lang::Sounds::P3_ALARM);
            }
        }
    }

    // 实现闹钟初始化方法
    void InitializeAlarm() override {
        if (rtc_ != nullptr)
        {
            // 检查唤醒源
            esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
            if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0)
            {
                // rtc_wakeup_ = true;
                ESP_LOGI(TAG, "Wakeup from EXT0");
            }
            else if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1)
            {
                // 获取触发唤醒的 GPIO 位掩码
                uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
                // 找出具体是哪个引脚触发了唤醒
                for (int i = 0; i < 64; i++)
                {
                    if (wakeup_pin_mask & (1ULL << i))
                    {
                        ESP_LOGI(TAG, "Wakeup from GPIO %d", i);
                        rtc_wakeup_ = true;
                        break;
                    }
                }
            }
        }
    }
    // 定时器回调函数
    static void alarm_timer_callback(void *arg) {
        Application::GetInstance().Alert(Lang::Strings::ALARM, "", "", Lang::Sounds::P3_ALARM);
    }
    // 实现闹钟检查方法

    void CheckAlarmAfterInit() override {
        if (rtc_ != nullptr && rtc_wakeup_)
        {
            // 创建定时器配置
            esp_timer_create_args_t timer_args = {
                .callback = alarm_timer_callback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "alarm_timer",
                .skip_unhandled_events = true};

            // 创建定时器
            ESP_ERROR_CHECK(esp_timer_create(&timer_args, &alarm_timer_));

            // 启动定时器，每3秒触发一次
            ESP_ERROR_CHECK(esp_timer_start_periodic(alarm_timer_, 5000000));

            power_save_timer_->SetEnabled(false);
            // 立即播放第一次提醒
            Application::GetInstance().Alert(Lang::Strings::ALARM, "", "", Lang::Sounds::P3_ALARM);
        }
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

        // 配置RTC中断检测
        gpio_config_t int_conf = {
            .pin_bit_mask = (1ULL << ALARM_INT_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE};
        gpio_config(&int_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(ALARM_INT_GPIO, OnAlarmInterruptHandler, this);

        // 创建定时器检查闹钟状态
        esp_timer_create_args_t timer_args = {
            .callback = OnAlarmCheckTimer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "alarm_check_timer",
            .skip_unhandled_events = true};
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &alarm_check_timer_)); // 100ms检查一次
        ESP_ERROR_CHECK(esp_timer_start_periodic(alarm_check_timer_, 100000)); // 1
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
        gpio_set_level(GPIO_NUM_45, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_45, 1);
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
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // Implemented by LCD command `36h`
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,   // Implemented by LCD command `3Ah` (16/18)
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
        boot_button_.OnClick([this]()
                             {
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            if (alarm_timer_ != nullptr) {
                
                esp_timer_stop(alarm_timer_);
                esp_timer_delete(alarm_timer_);
                alarm_timer_ = nullptr;
                ESP_LOGI(TAG, "Alarm stopped by BOOT button");
                Application::GetInstance().Alert(Lang::Strings::ALARM, "", "", Lang::Sounds::P3_SUCCESS);
                power_save_timer_->SetEnabled(true);
            } });
        boot_button_.OnPressDown([this]()
                                 { Application::GetInstance().StartListening(); });
        boot_button_.OnPressUp([this]()
                               { Application::GetInstance().StopListening(); });
        boot_button_.OnDoubleClick([this]()
                                   {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                SwitchNetworkType();
            } });
        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume)); });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME); });

        // volume_down_button_.OnClick([this]() {
        //     power_save_timer_->WakeUp();
        //     auto codec = GetAudioCodec();
        //     auto volume = codec->output_volume() - 10;
        //     if (volume < 0) {
        //         volume = 0;
        //     }
        //     codec->SetOutputVolume(volume);
        //     GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        // });

        // volume_down_button_.OnLongPress([this]() {
        //     power_save_timer_->WakeUp();
        //     GetAudioCodec()->SetOutputVolume(0);
        //     GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        // });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto &thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("AlarmClock")); // 添加闹钟组件
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
    }

public:
    KEVIN_LCD_18() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, 4096),
                     boot_button_(BOOT_BUTTON_GPIO),
                     volume_up_button_(VOLUME_UP_BUTTON_GPIO)
    // ,
    // volume_down_button_(ALARM_INT_GPIO)
    {
        ESP_LOGI(TAG, "Initializing KEVIN LCD 1.8 Board");
        // // 检查唤醒源
        // esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
        // if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
        //     // 从RTC INT引脚唤醒，检查是否是闹钟触发
        //     rtc_wakeup_ = true;
        // }
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

    virtual AudioCodec *GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                                            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
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
    virtual Backlight *GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
    // 获取RTC对象
    virtual PCF85063 *GetRTC() override {
        return rtc_;
    }

    // 设置RTC时间
    void SetRTCTime(time_t time) {
        if (rtc_)
        {
            rtc_->SetTime(time);
        }
    }
    virtual void SyncTimeToRTC(time_t time) override {
        if (rtc_ != nullptr)
        {
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

        // 关闭 ALDO3 输出 (3.3V LDO)
        // if (pmic_ != nullptr) {
        //     // 写入新状态
        //     pmic_->EnableAldo3(false);
        // }

        // 配置唤醒引脚
        const uint64_t wakeup_pin_mask =
            (1ULL << GPIO_NUM_0) |    // BOOT按键
            (1ULL << ALARM_INT_GPIO); // 闹钟中断

        // 配置上拉
        // rtc_gpio_pullup_en(GPIO_NUM_0);
        // rtc_gpio_pulldown_dis(GPIO_NUM_0);
        // rtc_gpio_pullup_en(ALARM_INT_GPIO);
        // rtc_gpio_pulldown_dis(ALARM_INT_GPIO);

        // 使用ext1唤醒源，任意一个配置的GPIO变为低电平都会触发唤醒
        esp_sleep_enable_ext1_wakeup(wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_LOW);

        // 进入深度休眠
        ESP_LOGI(TAG, "正在进入深度休眠...");
        esp_deep_sleep_start();
    }
};

DECLARE_BOARD(KEVIN_LCD_18);
