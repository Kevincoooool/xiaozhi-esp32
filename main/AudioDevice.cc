#include "AudioDevice.h"
#include <esp_log.h>
#include <cstring>
#include <driver/gpio.h>
#include <es8311.h>
#define TAG "AudioDevice"

AudioDevice::AudioDevice() {
}

AudioDevice::~AudioDevice() {
    if (audio_input_task_ != nullptr) {
        vTaskDelete(audio_input_task_);
    }
    if (rx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    if (tx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    }
}

void AudioDevice::Start(int input_sample_rate, int output_sample_rate) {
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

#ifdef CONFIG_AUDIO_DEVICE_I2S_SIMPLEX
        CreateSimplexChannels();
#else
        CreateDuplexChannels();
#endif

    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));

    // Start PA
    gpio_config_t io_conf = {
        .pin_bit_mask = 1 << GPIO_NUM_13,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_13, 1);

    // Initialize I2C peripheral
    const i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_0,
        .scl_io_num = GPIO_NUM_1,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 100000,
        },
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &es_i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    
    // Initialize es8311 codec
    es8311_handle_t es_handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (es_handle == NULL) {
        ESP_LOGE(TAG, "es8311 create failed");
        return;
    }

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = output_sample_rate * 256,
        .sample_frequency = output_sample_rate
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es_handle, output_sample_rate * 256, output_sample_rate));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es_handle, 70, NULL));
    ESP_ERROR_CHECK(es8311_microphone_config(es_handle, false));
    ESP_ERROR_CHECK(es8311_microphone_gain_set(es_handle, ES8311_MIC_GAIN_12DB));

    xTaskCreate([](void* arg) {
        auto audio_device = (AudioDevice*)arg;
        audio_device->InputTask();
    }, "audio_input", 4096 * 2, this, 5, &audio_input_task_);
}

void AudioDevice::CreateDuplexChannels() {
    duplex_ = true;

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = false,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_10,
            .bclk = GPIO_NUM_8,
            .ws = GPIO_NUM_12,
            .dout = GPIO_NUM_11,
            .din = GPIO_NUM_7,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Duplex channels created");
}

#ifdef CONFIG_AUDIO_DEVICE_I2S_SIMPLEX
void AudioDevice::CreateSimplexChannels() {
    // Create a new channel for speaker
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = false,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_BCLK,
            .ws = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_WS,
            .dout = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    // Create a new channel for MIC
    chan_cfg.id = I2S_NUM_1;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));
    std_cfg.clk_cfg.sample_rate_hz = (uint32_t)input_sample_rate_;
    std_cfg.gpio_cfg.bclk = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_MIC_GPIO_BCLK;
    std_cfg.gpio_cfg.ws = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_MIC_GPIO_WS;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_MIC_GPIO_DIN;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Simplex channels created");
}
#endif

int AudioDevice::Write(const int16_t* data, int samples) {
    size_t bytes_written;
    ESP_ERROR_CHECK(i2s_channel_write(tx_handle_, data, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY));
    return bytes_written / sizeof(int16_t);

    // int32_t buffer[samples];
    // for (int i = 0; i < samples; i++) {
    //     buffer[i] = int32_t(data[i]) << 15;
    // }

    // size_t bytes_written;
    // ESP_ERROR_CHECK(i2s_channel_write(tx_handle_, buffer, samples * sizeof(int32_t), &bytes_written, portMAX_DELAY));
}

int AudioDevice::Read(int16_t* dest, int samples) {
    size_t bytes_read;
    ESP_ERROR_CHECK(i2s_channel_read(rx_handle_, dest, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY));
    return bytes_read / sizeof(int16_t);
    /*
    size_t bytes_read;

    int32_t bit32_buffer_[samples];
    if (i2s_channel_read(rx_handle_, bit32_buffer_, samples * sizeof(int32_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Read Failed!");
        return 0;
    }

    samples = bytes_read / sizeof(int32_t);
    for (int i = 0; i < samples; i++) {
        int32_t value = bit32_buffer_[i] >> 12;
        dest[i] = (value > INT16_MAX) ? INT16_MAX : (value < -INT16_MAX) ? -INT16_MAX : (int16_t)value;
    }
    return samples;*/
}

void AudioDevice::OnInputData(std::function<void(const int16_t*, int)> callback) {
    on_input_data_ = callback;
}

void AudioDevice::OutputData(std::vector<int16_t>& data) {
    Write(data.data(), data.size());
}

void AudioDevice::InputTask() {
    int duration = 30;
    int input_frame_size = input_sample_rate_ / 1000 * duration;
    int16_t input_buffer[input_frame_size];
    while (true) {
        int samples = Read(input_buffer, input_frame_size);
        if (samples > 0) {
            on_input_data_(input_buffer, samples);
        }
    }
}
