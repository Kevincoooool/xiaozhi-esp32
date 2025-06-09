#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_io_expander_tca9554.h>
#include <esp_lcd_ili9341.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>
#include <esp_lcd_gc9d01.h>
#define TAG "kevin-eye"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class CustomLcdDisplay : public SpiLcdDisplay {
public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle, 
                    esp_lcd_panel_handle_t panel_handle,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy) 
        : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy,
                    {
                        .text_font = &font_puhui_20_4,
                        .icon_font = &font_awesome_20_4,
                        .emoji_font = font_emoji_64_init(),
                    }) {

        DisplayLockGuard lock(this);
        // 由于屏幕是圆的，所以状态栏需要增加左右内边距
        lv_obj_set_style_pad_left(status_bar_, LV_HOR_RES * 0.33, 0);
        lv_obj_set_style_pad_right(status_bar_, LV_HOR_RES * 0.33, 0);
    }
};


class KevinEyeBoard : public WifiBoard {
private:
    Button boot_button_;
    Button change_button_;
    i2c_master_bus_handle_t i2c_bus_;
    LcdDisplay* display_;

    void InitializeI2c() {
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
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void I2cDetect() {
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
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

    void EnableLcdCs() {
        
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_38;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_45;
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
        change_button_.OnClick([this]() {
            auto& board = Board::GetInstance();
                auto display = board.GetDisplay();
                display->changeEyeStyle(); 
            });
    }


    // GC9A01初始化
    void InitializeGc9a01Display() {
        ESP_LOGI(TAG, "Init GC9A01 display");

        ESP_LOGI(TAG, "Install panel IO");
       
        // 初始化第一个屏幕
        esp_lcd_panel_io_handle_t io_handle1 = NULL;
        esp_lcd_panel_io_spi_config_t io_config1 = {};
        io_config1.cs_gpio_num = GPIO_NUM_NC;  // 第一个屏幕的 CS 引脚
        io_config1.dc_gpio_num = GPIO_NUM_47;
        io_config1.spi_mode = 0;
        io_config1.pclk_hz = 80 * 1000 * 1000;
        io_config1.trans_queue_depth = 10;
        io_config1.lcd_cmd_bits = 8;
        io_config1.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config1, &io_handle1));

        // 初始化第二个屏幕
        esp_lcd_panel_io_handle_t io_handle2 = NULL;
        esp_lcd_panel_io_spi_config_t io_config2 = {};
        io_config2.cs_gpio_num = GPIO_NUM_NC;  // 第二个屏幕的 CS 引脚
        io_config2.dc_gpio_num = GPIO_NUM_47;  // 共用其他引脚
        io_config2.spi_mode = 0;
        io_config2.pclk_hz = 80 * 1000 * 1000;
        io_config2.trans_queue_depth = 10;
        io_config2.lcd_cmd_bits = 8;
        io_config2.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config2, &io_handle2));
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_41); // 打开功放使能
        gpio_set_direction(GPIO_NUM_41, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_41, 0);
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_48); // 打开功放使能
        gpio_set_direction(GPIO_NUM_48, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_48, 0);
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_21); // 打开功放使能
        gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_21, 0);
        ESP_LOGI(TAG, "Install GC9A01 panel driver");
          // 初始化第一个面板
          ESP_LOGI(TAG, "Install first GC9A01 panel driver");
          esp_lcd_panel_handle_t panel_handle1 = NULL;
          esp_lcd_panel_dev_config_t panel_config1 = {};
          panel_config1.reset_gpio_num = GPIO_NUM_21;
          panel_config1.rgb_endian = LCD_RGB_ELEMENT_ORDER_BGR;
          panel_config1.bits_per_pixel = 16;
          ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle1, &panel_config1, &panel_handle1));
  
          // 初始化第二个面板
          ESP_LOGI(TAG, "Install second GC9A01 panel driver");
          esp_lcd_panel_handle_t panel_handle2 = NULL;
          esp_lcd_panel_dev_config_t panel_config2 = {};
          panel_config2.reset_gpio_num = GPIO_NUM_NC;
          panel_config2.rgb_endian = LCD_RGB_ELEMENT_ORDER_BGR;
          panel_config2.bits_per_pixel = 16;
          ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle2, &panel_config2, &panel_handle2));
  
          // 初始化两个面板
          ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle1));
          ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle2));
        gpio_set_level(GPIO_NUM_21, 1);
        gpio_set_level(GPIO_NUM_41, 0);
        gpio_set_level(GPIO_NUM_48, 1);
        
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle1));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle1, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle1, true, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle1, true)); 
        gpio_set_level(GPIO_NUM_41, 1);
        gpio_set_level(GPIO_NUM_48, 0);
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle2));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle2, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle2, false, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle2, true)); 
        gpio_set_level(GPIO_NUM_41, 0);
        gpio_set_level(GPIO_NUM_48, 0);
        //display_ = new DoubleSpiLcdDisplay(io_handle1, io_handle2, panel_handle1, panel_handle2,
        display_ = new SpiLcdDisplay(io_handle1,  panel_handle1,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
                                         .text_font = &font_puhui_20_4,
                                         .icon_font = &font_awesome_20_4,
                                         .emoji_font = font_emoji_32_init(),
                                     });
    }
    // GC9A01初始化
    void InitializeGc9d01Display() {
        ESP_LOGI(TAG, "Init GC9d01 display");

        ESP_LOGI(TAG, "Install panel IO");
       
        // 初始化第一个屏幕
        esp_lcd_panel_io_handle_t io_handle1 = NULL;
        esp_lcd_panel_io_spi_config_t io_config1 = {};
        io_config1.cs_gpio_num = GPIO_NUM_NC;  // 第一个屏幕的 CS 引脚
        io_config1.dc_gpio_num = GPIO_NUM_47;
        io_config1.spi_mode = 0;
        io_config1.pclk_hz = 60 * 1000 * 1000;
        io_config1.trans_queue_depth = 10;
        io_config1.lcd_cmd_bits = 8;
        io_config1.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config1, &io_handle1));

        // 初始化第二个屏幕
        esp_lcd_panel_io_handle_t io_handle2 = NULL;
        esp_lcd_panel_io_spi_config_t io_config2 = {};
        io_config2.cs_gpio_num = GPIO_NUM_NC;  // 第二个屏幕的 CS 引脚
        io_config2.dc_gpio_num = GPIO_NUM_47;  // 共用其他引脚
        io_config2.spi_mode = 0;
        io_config2.pclk_hz = 60 * 1000 * 1000;
        io_config2.trans_queue_depth = 10;
        io_config2.lcd_cmd_bits = 8;
        io_config2.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config2, &io_handle2));
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_41); // 打开功放使能
        gpio_set_direction(GPIO_NUM_41, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_41, 0);
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_48); // 打开功放使能
        gpio_set_direction(GPIO_NUM_48, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_48, 0);
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_21); // 打开功放使能
        gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_21, 0);
        ESP_LOGI(TAG, "Install GC9A01 panel driver");
          // 初始化第一个面板
          ESP_LOGI(TAG, "Install first GC9A01 panel driver");
          esp_lcd_panel_handle_t panel_handle1 = NULL;
          esp_lcd_panel_dev_config_t panel_config1 = {};
          panel_config1.reset_gpio_num = GPIO_NUM_21;
          panel_config1.rgb_endian = LCD_RGB_ELEMENT_ORDER_BGR;
          panel_config1.bits_per_pixel = 16;
          ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(io_handle1, &panel_config1, &panel_handle1));
  
          // 初始化第二个面板
          ESP_LOGI(TAG, "Install second GC9A01 panel driver");
          esp_lcd_panel_handle_t panel_handle2 = NULL;
          esp_lcd_panel_dev_config_t panel_config2 = {};
          panel_config2.reset_gpio_num = GPIO_NUM_NC;
          panel_config2.rgb_endian = LCD_RGB_ELEMENT_ORDER_BGR;
          panel_config2.bits_per_pixel = 16;
          ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(io_handle2, &panel_config2, &panel_handle2));
  
          // 初始化两个面板
          ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle1));
          ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle2));
        gpio_set_level(GPIO_NUM_21, 1);
        gpio_set_level(GPIO_NUM_41, 0);
        gpio_set_level(GPIO_NUM_48, 1);

        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle1));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle1, false));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle1, false, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle1, true)); 
        gpio_set_level(GPIO_NUM_41, 1);
        gpio_set_level(GPIO_NUM_48, 0);
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle2));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle2, false));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle2, false, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle2, true)); 
        gpio_set_level(GPIO_NUM_41, 0);
        gpio_set_level(GPIO_NUM_48, 0);
        //display_ = new DoubleSpiLcdDisplay(io_handle1, io_handle2, panel_handle1, panel_handle2,
        display_ = new SpiLcdDisplay(io_handle1,  panel_handle1,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
                                         .text_font = &font_puhui_20_4,
                                         .icon_font = &font_awesome_20_4,
                                         .emoji_font = font_emoji_32_init(),
                                     });
    }
    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Eye"));

    }

public:
    KevinEyeBoard() : boot_button_(BOOT_BUTTON_GPIO),change_button_(CHANGE_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing kevin-eye Board");
        InitializeI2c();
        I2cDetect();
        InitializeSpi();
        InitializeButtons();
        // InitializeGc9a01Display();
        InitializeGc9d01Display();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
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
};

DECLARE_BOARD(KevinEyeBoard);