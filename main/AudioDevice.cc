#include "AudioDevice.h"
#include <esp_log.h>
#include <cstring>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_system.h"
#include "esp_check.h"

#include "bsp_board_extra.h"

#define TAG "AudioDevice"

AudioDevice::AudioDevice()
{
}

AudioDevice::~AudioDevice()
{
    if (audio_input_task_ != nullptr)
    {
        vTaskDelete(audio_input_task_);
    }
    if (rx_handle_ != nullptr)
    {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    if (tx_handle_ != nullptr)
    {
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    }
}

void AudioDevice::Start(int input_sample_rate, int output_sample_rate)
{
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // #ifdef CONFIG_AUDIO_DEVICE_I2S_SIMPLEX
    //         CreateSimplexChannels();
    // #else
    //         CreateDuplexChannels();
    // #endif

    //     ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    //     ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));

    // es7210_init(true);
    esp_rom_gpio_pad_select_gpio(GPIO_NUM_53);
    gpio_set_direction(GPIO_NUM_53, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_53, 1); // 输出高电平

    xTaskCreate([](void *arg)
                {
        auto audio_device = (AudioDevice*)arg;
        audio_device->InputTask(); }, "audio_input", 1024 * 10, this, 20, &audio_input_task_);
}

void AudioDevice::CreateDuplexChannels()
{
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
            .mclk_multiple = I2S_MCLK_MULTIPLE_256},
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, .slot_mode = I2S_SLOT_MODE_MONO, .slot_mask = I2S_STD_SLOT_LEFT, .ws_width = I2S_DATA_BIT_WIDTH_32BIT, .ws_pol = false, .bit_shift = true, .left_align = true, .big_endian = false, .bit_order_lsb = false},
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_MIC_GPIO_BCLK, .ws = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_MIC_GPIO_WS, .dout = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_DOUT, .din = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_MIC_GPIO_DIN, .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Duplex channels created");
}

#ifdef CONFIG_AUDIO_DEVICE_I2S_SIMPLEX
void AudioDevice::CreateSimplexChannels()
{
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
            .mclk_multiple = I2S_MCLK_MULTIPLE_256},
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, .slot_mode = I2S_SLOT_MODE_MONO, .slot_mask = I2S_STD_SLOT_LEFT, .ws_width = I2S_DATA_BIT_WIDTH_32BIT, .ws_pol = false, .bit_shift = true, .left_align = true, .big_endian = false, .bit_order_lsb = false},
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_BCLK, .ws = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_WS, .dout = (gpio_num_t)CONFIG_AUDIO_DEVICE_I2S_SPK_GPIO_DOUT, .din = I2S_GPIO_UNUSED, .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};
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

void AudioDevice::Write(const int16_t *data, int samples)
{
    int32_t buffer[samples];
    for (int i = 0; i < samples; i++)
    {
        buffer[i] = int32_t(data[i]) << 15;
    }

    size_t bytes_written;

    ESP_ERROR_CHECK(bsp_extra_i2s_write(buffer, samples * sizeof(int32_t), &bytes_written, portMAX_DELAY));

}

int AudioDevice::Read(int16_t *dest, int samples)
{
    size_t bytes_read;

    int32_t bit32_buffer_[samples];

    if (bsp_extra_i2s_read(bit32_buffer_, samples * sizeof(int32_t), &bytes_read, portMAX_DELAY) != ESP_OK)
    {
        ESP_LOGE(TAG, "Read Failed!");
        return 0;
    }

    samples = bytes_read / sizeof(int32_t);
    for (int i = 0; i < samples; i++)
    {
        int32_t value = bit32_buffer_[i] >> 12;
        dest[i] = (value > INT16_MAX) ? INT16_MAX : (value < -INT16_MAX) ? -INT16_MAX
                                                                         : (int16_t)value;
    }
    return samples;
}

void AudioDevice::OnInputData(std::function<void(const int16_t *, int)> callback)
{
    on_input_data_ = callback;
}

void AudioDevice::OutputData(std::vector<int16_t> &data)
{
    Write(data.data(), data.size());
}

void AudioDevice::InputTask()
{
    int duration = 30;
    int input_frame_size = input_sample_rate_ / 1000 * duration;
    int16_t input_buffer[input_frame_size];


    while (true)
    {
        int samples = Read(input_buffer, input_frame_size);
        if (samples > 0)
        {
            on_input_data_(input_buffer, samples);
        }
    }
}
