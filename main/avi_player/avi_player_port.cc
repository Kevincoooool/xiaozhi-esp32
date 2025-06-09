#include "avi_player_port.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_decode.h"
#include "fs_manager.h"
#include "board.h"
#include <sys/stat.h>
static const char *TAG = "avi_player_port";

static i2s_chan_handle_t i2s_tx_handle = NULL;
static uint8_t *img_rgb565 = NULL;

static bool is_playing = false;

static char current_filepath[256] = {0}; // 添加文件路径存储
void video_write(frame_data_t *data, void *arg)
{
    int Rgbsize = 0;
    jpeg_error_t ret = esp_jpeg_decode_one_picture(data->data, data->data_bytes, &img_rgb565, &Rgbsize);
    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg decode error: %d", ret);
    }
    // 通过回调函数更新LCD显示
    if (img_rgb565 != NULL) {
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
    ESP_LOGI(TAG, "Play end, restart playing: %s", current_filepath);
    is_playing = false;
    // 重新开始播放
    if (strlen(current_filepath) > 0) {
        avi_player_play_from_file(current_filepath);
        is_playing = true;
    }
}


esp_err_t avi_player_port_init(avi_player_port_config_t *config)
{
    // 配置SD卡
    fs_config_t sdcard_config = {
        .type = FS_TYPE_SD_CARD,
        .sd_card = {
            .mount_point = "/sdcard",
            .clk = GPIO_NUM_2,  // 根据您的硬件配置调整这些引脚
            .cmd = GPIO_NUM_42,
            .d0 = GPIO_NUM_1,
            .format_if_mount_failed = false,
            .max_files = 5
        }
    };
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
     
    // 自动初始化文件系统，先尝试TF卡，失败则使用SPIFFS
    ESP_ERROR_CHECK(fs_manager_auto_init(&sdcard_config, &spiffs_config));
    
    // 获取当前使用的文件系统类型
    fs_type_t fs_type = fs_manager_get_type();
    const char* mount_path = (fs_type == FS_TYPE_SD_CARD) ? "/sdcard" : "/spiffs";
    
    // 列出文件
    fs_manager_list_files(mount_path);

    // img_rgb565 = (uint8_t *)heap_caps_malloc(240 * 280 * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    // if (!img_rgb565)
    // {
    //     return ESP_ERR_NO_MEM;
    // }

    avi_player_config_t player_config = {
        .buffer_size = config->buffer_size,
        .video_cb = video_write,
        .audio_cb = audio_write,
        .avi_play_end_cb = play_end_cb,
        .coreID = config->core_id,
    };

    return avi_player_init(player_config);
}

// esp_err_t avi_player_port_play_file(const char *filepath)
// {
//     if (is_playing == true)
//     {
//         avi_player_port_stop();
//         vTaskDelay(300 / portTICK_PERIOD_MS);
//     }
//     // 保存文件路径
//     strncpy(current_filepath, filepath, sizeof(current_filepath) - 1);
//     current_filepath[sizeof(current_filepath) - 1] = '\0';
    
//     is_playing = true;
//     return avi_player_play_from_file(filepath);
// }
esp_err_t avi_player_port_play_file(const char *filepath)
{
    if (is_playing == true)
    {
        avi_player_port_stop();
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    
    // 获取当前使用的文件系统类型
    fs_type_t fs_type = fs_manager_get_type();
    const char* mount_path = (fs_type == FS_TYPE_SD_CARD) ? "/sdcard" : "/spiffs";
    
    // 构建完整路径
    char full_path[256] = {0};
    
    // 检查文件路径是否已经包含挂载点
    if (strncmp(filepath, "/sdcard/", 8) == 0 || strncmp(filepath, "/spiffs/", 8) == 0) {
        // 已经包含挂载点，直接使用
        strncpy(full_path, filepath, sizeof(full_path) - 1);
    } else if (filepath[0] == '/') {
        // 路径以/开头但不包含挂载点，添加挂载点
        snprintf(full_path, sizeof(full_path) - 1, "%s%s", mount_path, filepath);
    } else {
        // 路径不以/开头，添加挂载点和/
        snprintf(full_path, sizeof(full_path) - 1, "%s/%s", mount_path, filepath);
    }
    
    full_path[sizeof(full_path) - 1] = '\0';
    ESP_LOGI(TAG, "Playing file with full path: %s", full_path);
    
    // 检查文件是否存在
    struct stat st;
    if (stat(full_path, &st) != 0) {
        ESP_LOGE(TAG, "File does not exist: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 检查是否为普通文件（非目录）
    if (!S_ISREG(st.st_mode)) {
        ESP_LOGE(TAG, "Path is not a regular file: %s", full_path);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "File exists and is valid, size: %ld bytes", (long)st.st_size);
    
    // 保存文件路径
    strncpy(current_filepath, full_path, sizeof(current_filepath) - 1);
    current_filepath[sizeof(current_filepath) - 1] = '\0';
    
    is_playing = true;
    return avi_player_play_from_file(full_path);
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
    // 清空保存的文件路径
    memset(current_filepath, 0, sizeof(current_filepath));
}