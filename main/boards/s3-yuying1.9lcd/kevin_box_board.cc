// #include "ml307_board.h"
#include "wifi_board.h"

#include "audio_codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "axp2101.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include <esp_log.h>
#include <esp_spiffs.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_timer.h>
#include <esp_lcd_panel_vendor.h>
#define TAG "Yuying1_9LCD"
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
    
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
//Ml307Board WifiBoard
class Yuying1_9LCD : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Axp2101* axp2101_ = nullptr;
    Button boot_button_;
    Button volume_up_button_;
    Button voice_button_;
    LcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    Pmic* pmic_ = nullptr;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, -1, 600);
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void MountStorage() {
        // Mount the storage partition
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/storage",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true,
        };
        esp_vfs_spiffs_register(&conf);
    }

    void Enable4GModule() {
        // Make GPIO HIGH to enable the 4G module
        gpio_config_t ml307_enable_config = {
            .pin_bit_mask = (1ULL << 12),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&ml307_enable_config);
        gpio_set_level(GPIO_NUM_12, 0);
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_18;
        io_config.dc_gpio_num = GPIO_NUM_17;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 60 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_15;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                .text_font = &font_puhui_20_4,
                .icon_font = &font_awesome_20_4,
                .emoji_font = font_emoji_64_init(),
            });
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
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
        buscfg.mosi_io_num = GPIO_NUM_21;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_16;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
        // boot_button_.OnPressUp([this]() {
        //     Application::GetInstance().StopListening();
        // });
        voice_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        voice_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification("音量 " + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification("最大音量");
        });

    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Backlight"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
    }
    void I2cDetect() {
            uint8_t address;
            printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
            for (int i = 0; i < 128; i += 16) {
                printf("%02x: ", i);
                for (int j = 0; j < 16; j++) {
                    fflush(stdout);
                    address = i + j;
                    esp_err_t ret = i2c_master_probe(codec_i2c_bus_, address, pdMS_TO_TICKS(200));
                    if (ret == ESP_OK) {
                        printf("%02x ", address);
                    } else if (ret == ESP_ERR_TIMEOUT) {
                        printf("UU ");
                    } else {
                        printf("-- ");
                    }
                }
                printf("\r\n");
            }
        }
public:
    Yuying1_9LCD() : 
    // Ml307Board(ML307_TX_PIN, ML307_RX_PIN, 4096),
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        voice_button_(VOICE_BUTTON_GPIO)
        // volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) 
        {
        
        InitializeCodecI2c();
        I2cDetect();
        pmic_ = new Pmic(codec_i2c_bus_, AXP2101_I2C_ADDR);
        InitializeSpi();
        InitializeSt7789Display();
        // MountStorage();
        Enable4GModule();

        InitializeButtons();
        InitializePowerSaveTimer();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }
    

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
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
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(Yuying1_9LCD);