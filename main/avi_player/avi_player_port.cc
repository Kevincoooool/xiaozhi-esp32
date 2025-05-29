#include "avi_player_port.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_decode.h"
#include "fs_manager.h"
#include "board.h"

static const char *TAG = "avi_player_port";

static uint8_t *img_rgb565 = NULL;
static bool is_playing = false;
static bool loop_playback = false;  // 是否循环播放
static char last_filepath[256] = {0};  // 保存最后一次播放的文件路径

// 视频帧回调
void video_write(frame_data_t *data, void *arg)
{
    int rgb_size = 0;
    if(esp_jpeg_decode_one_picture(data->data, data->data_bytes, &img_rgb565, &rgb_size) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG decode failed");
        return;
    }
    
    if (img_rgb565 != NULL) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetFaceImage(img_rgb565, get_rgb_width(), get_rgb_height());
        free(img_rgb565);
    }
}

// 音频帧回调
void audio_write(frame_data_t *data, void *arg)
{
    // 音频处理代码，根据需要实现
}

// 播放结束回调
static void play_end_cb(void *arg)
{
    ESP_LOGI(TAG, "Play end");
    is_playing = false;
    
    // 如果启用了循环播放且有上一次播放的文件路径
    if (loop_playback && last_filepath[0] != '\0') {
        ESP_LOGI(TAG, "Restarting playback: %s", last_filepath);
        // 短暂延时确保资源释放
        vTaskDelay(50 / portTICK_PERIOD_MS);
        // 重新开始播放
        is_playing = true;
        esp_err_t ret = avi_player_play_from_file(last_filepath);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart playback: %s", esp_err_to_name(ret));
            is_playing = false;
        }
    }
}

esp_err_t avi_player_port_init(avi_player_port_config_t *config)
{
    // 初始化文件系统
    fs_config_t fs_config = {
        .type = FS_TYPE_SPIFFS,
        .spiffs = {
            .base_path = "/spiffs",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true
        }
    };
    ESP_ERROR_CHECK(fs_manager_init(&fs_config));
    fs_manager_list_files("/spiffs");

    // 分配RGB缓冲区
    img_rgb565 = (uint8_t *)heap_caps_malloc(360 * 360 * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!img_rgb565) {
        return ESP_ERR_NO_MEM;
    }

    // 初始化AVI播放器
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
    // 如果当前正在播放，先停止
    if (is_playing) {
        avi_player_play_stop();
        vTaskDelay(50 / portTICK_PERIOD_MS); // 短暂等待确保停止完成
    }
    
    // 保存文件路径用于循环播放
    strncpy(last_filepath, filepath, sizeof(last_filepath) - 1);
    last_filepath[sizeof(last_filepath) - 1] = '\0';
    
    // 开始新的播放
    is_playing = true;
    esp_err_t ret = avi_player_play_from_file(filepath);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback failed: %s", esp_err_to_name(ret));
        is_playing = false;
    }
    
    return ret;
}

// 设置是否循环播放
esp_err_t avi_player_port_set_loop(bool enable)
{
    loop_playback = enable;
    ESP_LOGI(TAG, "Loop playback %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

// 重新播放最后一个文件
esp_err_t avi_player_port_replay(void)
{
    if (last_filepath[0] == '\0') {
        ESP_LOGW(TAG, "No previous file to replay");
        return ESP_ERR_INVALID_STATE;
    }
    
    return avi_player_port_play_file(last_filepath);
}

esp_err_t avi_player_port_stop(void)
{
    if (!is_playing) {
        return ESP_OK;
    }
    
    is_playing = false;
    esp_err_t ret = avi_player_play_stop();
    vTaskDelay(50 / portTICK_PERIOD_MS); // 短暂等待确保停止完成
    return ret;
}

void avi_player_port_deinit(void)
{
    // 停止播放
    avi_player_port_stop();
    
    // 释放资源
    avi_player_deinit();
    
    if (img_rgb565) {
        free(img_rgb565);
        img_rgb565 = NULL;
    }
}