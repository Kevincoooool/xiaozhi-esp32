#include "wifi_board.h"
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
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>

#define TAG "shikai-toy"

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
    
class SHIKAI_TOY : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
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

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_47;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_21;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
        });
        boot_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        boot_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_14;
        io_config.dc_gpio_num = GPIO_NUM_45;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel,
                            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                            {
                                .text_font = &font_puhui_20_4,
                                .icon_font = &font_awesome_20_4,
                                .emoji_font = font_emoji_64_init(),
                            });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Motor"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
        // thing_manager.AddThing(iot::CreateThing("Lamp"));
        // thing_manager.AddThing(iot::CreateThing("Backlight"));
    }

public:
    SHIKAI_TOY() :  boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing KEVIN SP V4 Board");
        InitializeCodecI2c();
        pmic_ = new Pmic(codec_i2c_bus_, AXP2101_I2C_ADDR);

        InitializeSpi();
        InitializeButtons();
        InitializePowerSaveTimer();
        InitializeIot();
        // GetBacklight()->RestoreBrightness();
    }
    

    virtual Led* GetLed() override {
        static NoLed led;
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    // virtual Backlight* GetBacklight() override {
    //     static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //     return &backlight;
    // }
};

DECLARE_BOARD(SHIKAI_TOY);
