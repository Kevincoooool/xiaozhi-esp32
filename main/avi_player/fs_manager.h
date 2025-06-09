#ifndef FS_MANAGER_H
#define FS_MANAGER_H

#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// 文件系统类型枚举
typedef enum {
    FS_TYPE_SPIFFS,
    FS_TYPE_SD_CARD
} fs_type_t;

// 文件系统配置结构体
typedef struct {
    fs_type_t type;
    union {
        struct {
            const char *base_path;
            const char *partition_label;
            size_t max_files;
            bool format_if_mount_failed;
        } spiffs;
        struct {
            const char *mount_point;
            gpio_num_t clk;
            gpio_num_t cmd;
            gpio_num_t d0;
            bool format_if_mount_failed;
            int max_files;
        } sd_card;
    };
} fs_config_t;

/**
 * @brief 初始化文件系统
 * @param config 文件系统配置
 * @return esp_err_t
 */
esp_err_t fs_manager_init(fs_config_t *config);

/**
 * @brief 自动初始化文件系统，先尝试TF卡，失败则使用SPIFFS
 * @param sd_config TF卡配置
 * @param spiffs_config SPIFFS配置
 * @return esp_err_t
 */
esp_err_t fs_manager_auto_init(fs_config_t *sd_config, fs_config_t *spiffs_config);

/**
 * @brief 列出指定目录下的文件
 * @param path 目录路径
 */
void fs_manager_list_files(const char* path);

/**
 * @brief 反初始化文件系统
 */
void fs_manager_deinit(void);

/**
 * @brief 获取当前使用的文件系统类型
 * @return fs_type_t 
 */
fs_type_t fs_manager_get_type(void);

#ifdef __cplusplus
}
#endif

#endif // FS_MANAGER_H