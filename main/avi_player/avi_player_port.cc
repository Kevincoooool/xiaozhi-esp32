#include "avi_player_port.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_decode.h"
#include "fs_manager.h"
#include "board.h"
static const char *TAG = "avi_player_port";

static i2s_chan_handle_t i2s_tx_handle = NULL;
static uint8_t *img_rgb565 = NULL;

static bool is_playing = false;
static char file_path[128] = {0};

void video_write(frame_data_t *data, void *arg)
{
    int Rgbsize = 0;
    esp_jpeg_decode_one_picture(data->data, data->data_bytes, &img_rgb565, &Rgbsize);
    // 通过回调函数更新LCD显示
    if (img_rgb565 != NULL&& Rgbsize > 0) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetFaceImage(img_rgb565, get_rgb_width(), get_rgb_height());
        free(img_rgb565);
    }
}

void audio_write(frame_data_t *data, void *arg)
{
    size_t bytes_write = 0;
    // i2s_channel_write(i2s_tx_handle, data->data, data->data_bytes, &bytes_write, 100);
}

static void play_end_cb(void *arg)
{
    ESP_LOGI(TAG, "Play end");
    is_playing = false;
    avi_player_port_play_file(file_path);
}

esp_err_t avi_player_port_init(avi_player_port_config_t *config)
{
    // 使用SPIFFS
    fs_config_t spiffs_config = {
        .type = FS_TYPE_SPIFFS,
        .spiffs = {
            .base_path = "/spiffs",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true
        }
    };
    // 初始化文件系统
    ESP_ERROR_CHECK(fs_manager_init(&spiffs_config));  // 或 &sdcard_config

    // 列出文件
    fs_manager_list_files("/spiffs");  // 或 "/sdcard"

    img_rgb565 = (uint8_t *)heap_caps_malloc(240 * 280 * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!img_rgb565)
    {
        return ESP_ERR_NO_MEM;
    }

    avi_player_config_t player_config = {
        .buffer_size = config->buffer_size,
        .video_cb = video_write,
        .audio_cb = audio_write,
        .avi_play_end_cb = play_end_cb,
        .coreID = config->core_id,
    };

    return avi_player_init(player_config);
}

esp_err_t avi_player_port_play_file(const char *filepath)
{
    if (is_playing == true)
    {
        avi_player_port_stop();
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    
    is_playing = true;
    memset(file_path, 0, sizeof(file_path));
    strncpy(file_path, filepath, sizeof(file_path) - 1);
    return avi_player_play_from_file(filepath);
}

esp_err_t avi_player_port_stop(void)
{
    is_playing = false;
    return avi_player_play_stop();
}

void avi_player_port_deinit(void)
{
    avi_player_play_stop();
    avi_player_deinit();
    if (img_rgb565)
    {
        free(img_rgb565);
        img_rgb565 = NULL;
    }
}