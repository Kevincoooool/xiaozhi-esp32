#include "avi_player_port.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_decode.h"
#include "fs_manager.h"
#include "board.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

static const char *TAG = "avi_player_port";

static i2s_chan_handle_t i2s_tx_handle = NULL;
static uint8_t *img_rgb565 = NULL;

// 使用队列来管理播放请求
static QueueHandle_t play_queue = NULL;
static TaskHandle_t player_task_handle = NULL;

// 播放请求结构
typedef struct {
    char filepath[256];
} play_request_t;

// 播放器状态
static SemaphoreHandle_t player_mutex = NULL;
static bool is_playing = false;
static bool is_player_ready = true;

void video_write(frame_data_t *data, void *arg)
{
    int Rgbsize = 0;
    esp_jpeg_decode_one_picture(data->data, data->data_bytes, &img_rgb565, &Rgbsize);
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
    ESP_LOGI(TAG, "Play end");
    
    // 使用信号量保护状态更新
    if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
        is_playing = false;
        is_player_ready = true;
        xSemaphoreGive(player_mutex);
    }
    
    ESP_LOGI(TAG, "Player state updated: playing=false, ready=true");
}

// 播放器任务
static void player_task(void *arg)
{
    play_request_t request;
    
    while (1) {
        // 等待播放请求
        if (xQueueReceive(play_queue, &request, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Received play request for: %s", request.filepath);
            
            // 停止当前播放
            if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
                if (is_playing) {
                    ESP_LOGI(TAG, "Stopping current playback");
                    xSemaphoreGive(player_mutex);
                    
                    avi_player_play_stop();
                    vTaskDelay(300 / portTICK_PERIOD_MS);
                    
                    // 更新状态
                    if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
                        is_playing = false;
                        xSemaphoreGive(player_mutex);
                    }
                } else {
                    xSemaphoreGive(player_mutex);
                }
            }
            
            // 开始新的播放
            if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
                is_playing = true;
                is_player_ready = false;
                xSemaphoreGive(player_mutex);
            }
            
            ESP_LOGI(TAG, "Starting playback: %s", request.filepath);
            esp_err_t ret = avi_player_play_from_file(request.filepath);
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start playback: %s", esp_err_to_name(ret));
                
                // 恢复状态
                if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
                    is_playing = false;
                    is_player_ready = true;
                    xSemaphoreGive(player_mutex);
                }
            } else {
                ESP_LOGI(TAG, "Playback started successfully");
            }
        }
    }
}

esp_err_t avi_player_port_init(avi_player_port_config_t *config)
{
    // 创建互斥信号量
    if (player_mutex == NULL) {
        player_mutex = xSemaphoreCreateMutex();
        if (player_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create player mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // 创建播放请求队列
    if (play_queue == NULL) {
        play_queue = xQueueCreate(3, sizeof(play_request_t));
        if (play_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create play queue");
            return ESP_ERR_NO_MEM;
        }
    }
    
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

    // 初始化状态
    if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
        is_playing = false;
        is_player_ready = true;
        xSemaphoreGive(player_mutex);
    }
    
    // 创建播放器任务
    if (player_task_handle == NULL) {
        xTaskCreate(player_task, "player_task", 4096, NULL, 5, &player_task_handle);
    }

    return avi_player_init(player_config);
}

esp_err_t avi_player_port_play_file(const char *filepath)
{
    if (play_queue == NULL) {
        ESP_LOGE(TAG, "Player not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 创建播放请求
    play_request_t request;
    strncpy(request.filepath, filepath, sizeof(request.filepath) - 1);
    request.filepath[sizeof(request.filepath) - 1] = '\0';
    
    // 发送到队列
    ESP_LOGI(TAG, "Sending play request for: %s", filepath);
    if (xQueueSend(play_queue, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send play request, queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t avi_player_port_stop(void)
{
    bool current_playing = false;
    
    // 获取当前状态
    if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
        current_playing = is_playing;
        // 先更新状态，防止回调再次调用stop
        is_playing = false;
        xSemaphoreGive(player_mutex);
    }
    
    if (!current_playing) {
        return ESP_OK;  // 如果没有在播放，直接返回成功
    }
    
    ESP_LOGI(TAG, "Stopping playback");
    esp_err_t ret = avi_player_play_stop();
    
    // 等待一段时间确保停止命令被处理
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    return ret;
}

void avi_player_port_deinit(void)
{
    // 停止播放器任务
    if (player_task_handle != NULL) {
        vTaskDelete(player_task_handle);
        player_task_handle = NULL;
    }
    
    // 清空队列
    if (play_queue != NULL) {
        xQueueReset(play_queue);
        vQueueDelete(play_queue);
        play_queue = NULL;
    }
    
    avi_player_play_stop();
    vTaskDelay(200 / portTICK_PERIOD_MS);  // 确保停止命令被处理
    avi_player_deinit();
    
    if (img_rgb565) {
        free(img_rgb565);
        img_rgb565 = NULL;
    }
    
    // 更新状态
    if (xSemaphoreTake(player_mutex, portMAX_DELAY) == pdTRUE) {
        is_playing = false;
        is_player_ready = true;
        xSemaphoreGive(player_mutex);
    }
    
    // 删除信号量
    if (player_mutex != NULL) {
        vSemaphoreDelete(player_mutex);
        player_mutex = NULL;
    }
}