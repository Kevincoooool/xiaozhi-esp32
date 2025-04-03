#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
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

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#define TAG "kevin-mcu8080"

LV_FONT_DECLARE(font_puhui_30_4);
LV_FONT_DECLARE(font_awesome_30_4);
static const ili9341_lcd_init_cmd_t nt35510_init_cmds[] = {
    // ENABLE PAGE 1
    {0xF000, (uint8_t[]){0x55, 0xAA, 0x52, 0x08, 0x01}, 5, 0},
    
    // 调整 AVDD 和 AVEE 电压，降低一点以减少冷色调
    {0xB000, (uint8_t[]){0x03, 0x03, 0x03}, 3, 0},  // AVDD: 原来是0x05
    {0xB100, (uint8_t[]){0x03, 0x03, 0x03}, 3, 0},  // AVEE: 原来是0x05
    
    // 调整 Gamma 电压，让颜色更暖一些
    {0xBC00, (uint8_t[]){0xd0, 0xd0, 0x00}, 3, 0},  // 原来是0xf0
    {0xBD00, (uint8_t[]){0xd0, 0xd0, 0x00}, 3, 0},  // 原来是0xf0
    
    // VCOM 电压调整，影响色温
    {0xBE01, (uint8_t[]){0x35}, 1, 0},  // 原来是0x3d
    // ENABLE PAGE 0
    {0xF000, (uint8_t[]){0x55, 0xAA, 0x52, 0x08, 0x00}, 5, 0},
    // Display ON & Color Format
    {0x2900, (uint8_t[]){0}, 0, 100},  // DISPLAY ON with 100ms delay
    {0x3A00, (uint8_t[]){0x55}, 1, 0}, // 16-bit color
    // {0x3600, (uint8_t[]){0xA3}, 1, 0}, // Memory Access Control
    // Memory Access Control
    // Bit7: MY  - Row Address Order
    // Bit6: MX  - Column Address Order
    // Bit5: MV  - Row/Column Exchange
    // Bit4: ML  - Vertical Refresh Order
    // Bit3: RGB - RGB-BGR Order (0=RGB, 1=BGR)
    // Bit2: MH  - Horizontal Refresh Order
    // Bit1: X   - Reserved
    // Bit0: X   - Reserved
    {0x3600, (uint8_t[]){0xE3}, 1, 0}, // BGR + MX + MY + MV

    {0, (uint8_t[]){0}, 0xff, 0}, // End mark
    {0, (uint8_t[]){0}, 0xff, 0}, // End mark
};

class Kevin_Mcu8080Board : public WifiBoard
{
private:
    Button boot_button_;
    i2c_master_bus_handle_t i2c_bus_;
    LcdDisplay *display_;
    esp_io_expander_handle_t io_expander_ = NULL;

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
        for (int i = 0; i < 128; i += 16)
        {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++)
            {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK)
                {
                    printf("%02x ", address);
                }
                else if (ret == ESP_ERR_TIMEOUT)
                {
                    printf("UU ");
                }
                else
                {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    void InitializeTca9554() {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander_);
        if (ret != ESP_OK)
        {
            ret = esp_io_expander_new_i2c_tca9554(i2c_bus_, ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_000, &io_expander_);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "TCA9554 create returned error");
                return;
            }
        }
        // 配置IO0-IO3为输出模式
        ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander_,
                                                IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 |
                                                    IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3,
                                                IO_EXPANDER_OUTPUT));

        // 复位LCD和TouchPad
        ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander_,
                                                  IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 1));
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander_,
                                                  IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 0));
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander_,
                                                  IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, 1));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]()
                             {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            } });
        boot_button_.OnPressDown([this]()
                                 { Application::GetInstance().StartListening(); });
        boot_button_.OnPressUp([this]()
                               { Application::GetInstance().StopListening(); });
    }

    void InitializeNt35510Display() {
        ESP_LOGI(TAG, "init lvgl lcd display port");
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_i80_bus_handle_t i80_bus = NULL;
        esp_lcd_i80_bus_config_t bus_config = {
            .dc_gpio_num = GPIO_LCD_RS,
            .wr_gpio_num = GPIO_LCD_WR,
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .data_gpio_nums = {
                GPIO_LCD_D00,
                GPIO_LCD_D01,
                GPIO_LCD_D02,
                GPIO_LCD_D03,
                GPIO_LCD_D04,
                GPIO_LCD_D05,
                GPIO_LCD_D06,
                GPIO_LCD_D07,
#if LCD_BIT_WIDTH == 16
                GPIO_LCD_D08,
                GPIO_LCD_D09,
                GPIO_LCD_D10,
                GPIO_LCD_D11,
                GPIO_LCD_D12,
                GPIO_LCD_D13,
                GPIO_LCD_D14,
                GPIO_LCD_D15,
#endif
            },
            .bus_width = LCD_BIT_WIDTH,
            .max_transfer_bytes = LCD_WIDTH * 480 * sizeof(uint16_t),
            .psram_trans_align = 64,
            .sram_trans_align = 4,
        };
        ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

        esp_lcd_panel_io_i80_config_t io_config = {
            .cs_gpio_num = GPIO_LCD_CS,
            .pclk_hz = 20000000, // 测试20M为稳定显示全屏USB摄像头图像速度
            .trans_queue_depth = 20,
            .lcd_cmd_bits = 16,
            .lcd_param_bits = 16,
            .dc_levels = {
                .dc_idle_level = 0,
                .dc_cmd_level = 0,
                .dc_dummy_level = 0,
                .dc_data_level = 1,
            },
            .flags = {
                .swap_color_bytes = 0, // Swap can be done in LvGL (default) or DMA
            },
        };
        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = &nt35510_init_cmds[0],
            .init_cmds_size = sizeof(nt35510_init_cmds) / sizeof(ili9341_lcd_init_cmd_t),
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &panel_io));
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GPIO_LCD_RST,
            .color_space = ESP_LCD_COLOR_SPACE_RGB,
            .bits_per_pixel = 16,
        };

        panel_config.vendor_config = (void *)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_nt35510(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel); // LCD Reset
        esp_lcd_panel_init(panel); // LCD init
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, true, false);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                     {
                                         .text_font = &font_puhui_30_4,
                                         .icon_font = &font_awesome_30_4,
                                         .emoji_font = font_emoji_64_init(),
                                     });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto &thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
    }

public:
    Kevin_Mcu8080Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing kevin-mcu8080 Board");
        InitializeI2c();
        I2cDetect();
        InitializeTca9554();
        InitializeButtons();
        InitializeNt35510Display();
        InitializeIot();
    }

    virtual AudioCodec *GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(Kevin_Mcu8080Board);
