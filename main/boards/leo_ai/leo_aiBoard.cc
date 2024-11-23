
#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "led.h"
#include "config.h"
#include "i2c_device.h"
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include "axp2101.h"
#include <esp_spiffs.h>
#include <driver/gpio.h>
#include "display/no_display.h"

#define TAG "Leo_AIBoard"

class Leo_AIBoard : public WifiBoard
{
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Axp2101* axp2101_ = nullptr;


    void InitializeI2c()
    {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
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
        // Initialize PCA9557
    }

    void InitializeButtons()
    {
        boot_button_.OnClick([this]()
                             { Application::GetInstance().ToggleChatState(); });

        volume_up_button_.OnClick([this]()
                                  {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume); });

        volume_up_button_.OnLongPress([this]()
                                      {
            auto codec = GetAudioCodec();
            codec->SetOutputVolume(100); });

        volume_down_button_.OnClick([this]()
                                    {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume); });

        volume_down_button_.OnLongPress([this]()
                                        {
            auto codec = GetAudioCodec();
            codec->SetOutputVolume(0); });
    }
public:

    Leo_AIBoard() : 
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
    }
    
    virtual void Initialize() override {
        ESP_LOGI(TAG, "Initializing Leo_AIBoard");
        InitializeI2c();
        axp2101_ = new Axp2101(codec_i2c_bus_, AXP2101_I2C_ADDR);

        InitializeButtons();
        WifiBoard::Initialize();
    }
        virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec* audio_codec = nullptr;
        if (audio_codec == nullptr) {
            audio_codec = new BoxAudioCodec(codec_i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE);
            audio_codec->SetOutputVolume(AUDIO_DEFAULT_OUTPUT_VOLUME);
        }
        return audio_codec;
    }
    virtual Led* GetBuiltinLed() override {
        static Led led(BUILTIN_LED_GPIO);
        return &led;
    }    
        virtual Display* GetDisplay() override {
        static NoDisplay display;
        return &display;
    }

};

DECLARE_BOARD(Leo_AIBoard);
