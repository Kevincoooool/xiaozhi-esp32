#pragma once

#include <stdint.h>
#include "esp_lcd_types.h"
#include "esp_lcd_panel_vendor.h"
#include "hal/spi_ll.h"
#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 *
 */
typedef struct {
    int cmd;                /*<! The specific LCD command */
    const void *data;       /*<! Buffer that holds the command specific data */
    size_t data_bytes;      /*<! Size of `data` in memory, in bytes */
    unsigned int delay_ms;  /*<! Delay in milliseconds after this command */
} st7305_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 *
 * @note  This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
 *
 */
typedef struct {
    const st7305_lcd_init_cmd_t *init_cmds;     /*!< Pointer to initialization commands array. Set to NULL if using default commands.
                                                 *   The array should be declared as `static const` and positioned outside the function.
                                                 *   Please refer to `vendor_specific_init_default` in source file.
                                                 */
    uint16_t init_cmds_size;                    /*<! Number of commands in above array */
} st7305_vendor_config_t;

// ST7305显示配置参数
#define ST7305_RESOLUTION_HOR 384  // 水平分辨率
#define ST7305_RESOLUTION_VER 168  // 垂直分辨率
#define ST7305_BITS_PER_PIXEL 1    // 每像素位数
#define ST7305_WIDTH     ST7305_RESOLUTION_HOR   // 显示屏宽度
#define ST7305_HEIGHT    ST7305_RESOLUTION_VER   // 显示屏高度

// ST7305控制命令
#define ST7305_CMD_NOP      0x00  // 空操作
#define ST7305_CMD_SWRESET  0x01  // 软件复位
#define ST7305_CMD_SLPOUT   0x11  // 退出睡眠模式
#define ST7305_CMD_NORON    0x13  // 正常显示模式
#define ST7305_CMD_INVOFF   0x20  // 显示反转关闭
#define ST7305_CMD_INVON    0x21  // 显示反转开启
#define ST7305_CMD_DISPOFF  0x28  // 显示关闭
#define ST7305_CMD_DISPON   0x29  // 显示开启
#define ST7305_CMD_CASET    0x2A  // 列地址设置
#define ST7305_CMD_RASET    0x2B  // 行地址设置
#define ST7305_CMD_RAMWR    0x2C  // 内存写入
#define ST7305_CMD_MADCTL   0x36  // 内存数据访问控制

// MADCTL位定义
#define ST7305_MADCTL_MY    0x80  // 行地址顺序
#define ST7305_MADCTL_MX    0x40  // 列地址顺序
#define ST7305_MADCTL_MV    0x20  // 行列交换
#define ST7305_MADCTL_ML    0x10  // 垂直刷新顺序
#define ST7305_MADCTL_RGB   0x00  // RGB-BGR顺序
#define ST7305_ROTATION_180  (ST7305_MADCTL_MY | ST7305_MADCTL_MX)
/**
 * @brief Create LCD panel for model ST7305
 *
 * @note  Vendor specific initialization can be different between manufacturers, should consult the LCD supplier for initialization sequence code.
 *
 * @param[in] io LCD panel IO handle
 * @param[in] panel_dev_config general panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *          - ESP_ERR_INVALID_ARG   if parameter is invalid
 *          - ESP_ERR_NO_MEM        if out of memory
 *          - ESP_OK                on success
 */
esp_err_t esp_lcd_new_panel_st7305(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

#define ST7305_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz)  \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .mosi_io_num = mosi,                                    \
        .miso_io_num = -1,                                      \
        .quadhd_io_num = -1,                                    \
        .quadwp_io_num = -1,                                    \
        .max_transfer_sz = max_trans_sz,                        \
    }

/**
 * @brief LCD panel IO configuration structure
 *
 * @param[in] cs SPI chip select pin number
 * @param[in] dc SPI data/command pin number
 * @param[in] cb Callback function when SPI transfer is done
 * @param[in] cb_ctx Callback function context
 *
 */
#define ST7305_PANEL_IO_SPI_CONFIG(cs, dc, callback, callback_ctx) \
    {                                                               \
        .cs_gpio_num = cs,                                          \
        .dc_gpio_num = dc,                                          \
        .spi_mode = 0,                                              \
        .pclk_hz = 60 * 1000 * 1000,                                \
        .trans_queue_depth = 10,                                    \
        .on_color_trans_done = callback,                            \
        .user_ctx = callback_ctx,                                   \
        .lcd_cmd_bits = 8,                                          \
        .lcd_param_bits = 8,                                        \
    }

#ifdef __cplusplus
}
#endif
