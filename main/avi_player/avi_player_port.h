#ifndef AVI_PLAYER_PORT_H
#define AVI_PLAYER_PORT_H

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "avi_player.h"
#include "esp_lcd_panel_ops.h"
#include "esp_jpeg_dec.h"
#include "driver/i2s_std.h"
#include "lcd_display.h" // 添加LCD显示头文件

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AVI播放器配置
 */
typedef struct {
    size_t buffer_size;                   // 缓冲区大小
    int core_id;                       // 运行核心ID
    Display* display;              // 添加LCD显示对象指针

} avi_player_port_config_t;

/**
 * @brief 初始化AVI播放器
 * @param config 播放器配置
 * @return esp_err_t
 */
esp_err_t avi_player_port_init(avi_player_port_config_t *config);

/**
 * @brief 开始播放指定文件
 * @param filepath 文件路径
 * @return esp_err_t
 */
esp_err_t avi_player_port_play_file(const char* filepath);

/**
 * @brief 停止播放
 * @return esp_err_t
 */
esp_err_t avi_player_port_stop(void);

/**
 * @brief 反初始化播放器
 */
void avi_player_port_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // AVI_PLAYER_PORT_H