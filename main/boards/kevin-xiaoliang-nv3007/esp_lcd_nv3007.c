/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_lcd_nv3007.h"

static const char *TAG = "lcd_panel.nv3007a";

static esp_err_t panel_nv3007a_del(esp_lcd_panel_t *panel);
static esp_err_t panel_nv3007a_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_nv3007a_init(esp_lcd_panel_t *panel);
static esp_err_t panel_nv3007a_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_nv3007a_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_nv3007a_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_nv3007a_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_nv3007a_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_nv3007a_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save current value of LCD_CMD_COLMOD register
    const nv3007a_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
} nv3007a_panel_t;

esp_err_t esp_lcd_new_panel_nv3007a(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    nv3007a_panel_t *nv3007a = NULL;
    gpio_config_t io_conf = { 0 };

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    nv3007a = (nv3007a_panel_t *)calloc(1, sizeof(nv3007a_panel_t));
    ESP_GOTO_ON_FALSE(nv3007a, ESP_ERR_NO_MEM, err, TAG, "no mem for nv3007a panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    switch (panel_dev_config->color_space) {
    case ESP_LCD_COLOR_SPACE_RGB:
        nv3007a->madctl_val = 0;
        break;
    case ESP_LCD_COLOR_SPACE_BGR:
        nv3007a->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }
#else
    switch (panel_dev_config->rgb_endian) {
    case LCD_RGB_ENDIAN_RGB:
        nv3007a->madctl_val = 0;
        break;
    case LCD_RGB_ENDIAN_BGR:
        nv3007a->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported rgb endian");
        break;
    }
#endif

    switch (panel_dev_config->bits_per_pixel) {
    case 12: // RGB444
        nv3007a->colmod_val = 0x33;
        nv3007a->fb_bits_per_pixel = 16;
        break;
    case 16: // RGB565
        nv3007a->colmod_val = 0x55;
        nv3007a->fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        nv3007a->colmod_val = 0x66;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        nv3007a->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    nv3007a->io = io;
    nv3007a->reset_gpio_num = panel_dev_config->reset_gpio_num;
    nv3007a->reset_level = panel_dev_config->flags.reset_active_high;
    if (panel_dev_config->vendor_config) {
        nv3007a->init_cmds = ((nv3007a_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds;
        nv3007a->init_cmds_size = ((nv3007a_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds_size;
    }
    nv3007a->base.del = panel_nv3007a_del;
    nv3007a->base.reset = panel_nv3007a_reset;
    nv3007a->base.init = panel_nv3007a_init;
    nv3007a->base.draw_bitmap = panel_nv3007a_draw_bitmap;
    nv3007a->base.invert_color = panel_nv3007a_invert_color;
    nv3007a->base.set_gap = panel_nv3007a_set_gap;
    nv3007a->base.mirror = panel_nv3007a_mirror;
    nv3007a->base.swap_xy = panel_nv3007a_swap_xy;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    nv3007a->base.disp_off = panel_nv3007a_disp_on_off;
#else
    nv3007a->base.disp_on_off = panel_nv3007a_disp_on_off;
#endif
    *ret_panel = &(nv3007a->base);
    // ESP_LOGD(TAG, "new nv3007a panel @%p", nv3007a);

    // ESP_LOGI(TAG, "LCD panel create success, version: %d.%d.%d", ESP_LCD_nv3007a_VER_MAJOR, ESP_LCD_nv3007a_VER_MINOR,
    //          ESP_LCD_nv3007a_VER_PATCH);

    return ESP_OK;

err:
    if (nv3007a) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(nv3007a);
    }
    return ret;
}

static esp_err_t panel_nv3007a_del(esp_lcd_panel_t *panel)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);

    if (nv3007a->reset_gpio_num >= 0) {
        gpio_reset_pin(nv3007a->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del nv3007a panel @%p", nv3007a);
    free(nv3007a);
    return ESP_OK;
}

static esp_err_t panel_nv3007a_reset(esp_lcd_panel_t *panel)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007a->io;

    // perform hardware reset
    if (nv3007a->reset_gpio_num >= 0) {
        gpio_set_level(nv3007a->reset_gpio_num, nv3007a->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(nv3007a->reset_gpio_num, !nv3007a->reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else { // perform software reset
        esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

// Modified by MakerM0 
// Driver: nv3007a, 0.85'TFT
static const nv3007a_lcd_init_cmd_t vendor_specific_init_default[] = {
    // 基础配置
    {0xff, (uint8_t []){0xa5}, 1, 0},
    {0x9a, (uint8_t []){0x08}, 1, 0},
    {0x9b, (uint8_t []){0x08}, 1, 0},
    {0x9c, (uint8_t []){0xb0}, 1, 0},
    {0x9d, (uint8_t []){0x16}, 1, 0},
    {0x9e, (uint8_t []){0xc4}, 1, 0},
    {0x8f, (uint8_t []){0x55, 0x04}, 2, 0},
    {0x84, (uint8_t []){0x90}, 1, 0},
    {0x83, (uint8_t []){0x7b}, 1, 0},
    {0x85, (uint8_t []){0x33}, 1, 0},
    
    // 时序控制
    {0x60, (uint8_t []){0x00}, 1, 0},
    {0x70, (uint8_t []){0x00}, 1, 0},
    {0x61, (uint8_t []){0x02}, 1, 0},
    {0x71, (uint8_t []){0x02}, 1, 0},
    {0x62, (uint8_t []){0x04}, 1, 0},
    {0x72, (uint8_t []){0x04}, 1, 0},
    {0x6c, (uint8_t []){0x29}, 1, 0},
    {0x7c, (uint8_t []){0x29}, 1, 0},
    {0x6d, (uint8_t []){0x31}, 1, 0},
    {0x7d, (uint8_t []){0x31}, 1, 0},
    {0x6e, (uint8_t []){0x0f}, 1, 0},
    {0x7e, (uint8_t []){0x0f}, 1, 0},
    
    // 驱动设置
    {0x66, (uint8_t []){0x21}, 1, 0},
    {0x76, (uint8_t []){0x21}, 1, 0},
    {0x68, (uint8_t []){0x3A}, 1, 0},
    {0x78, (uint8_t []){0x3A}, 1, 0},
    {0x63, (uint8_t []){0x07}, 1, 0},
    {0x73, (uint8_t []){0x07}, 1, 0},
    {0x64, (uint8_t []){0x05}, 1, 0},
    {0x74, (uint8_t []){0x05}, 1, 0},
    {0x65, (uint8_t []){0x02}, 1, 0},
    {0x75, (uint8_t []){0x02}, 1, 0},
    {0x67, (uint8_t []){0x23}, 1, 0},
    {0x77, (uint8_t []){0x23}, 1, 0},
    {0x69, (uint8_t []){0x08}, 1, 0},
    {0x79, (uint8_t []){0x08}, 1, 0},
    {0x6a, (uint8_t []){0x13}, 1, 0},
    {0x7a, (uint8_t []){0x13}, 1, 0},
    {0x6b, (uint8_t []){0x13}, 1, 0},
    {0x7b, (uint8_t []){0x13}, 1, 0},
    {0x6f, (uint8_t []){0x00}, 1, 0},
    {0x7f, (uint8_t []){0x00}, 1, 0},
    
    // 电源设置
    {0x50, (uint8_t []){0x00}, 1, 0},
    {0x52, (uint8_t []){0xd6}, 1, 0},
    {0x53, (uint8_t []){0x08}, 1, 0},
    {0x54, (uint8_t []){0x08}, 1, 0},
    {0x55, (uint8_t []){0x1e}, 1, 0},
    {0x56, (uint8_t []){0x1c}, 1, 0},
    
    // GOA 映射选择
    {0xa0, (uint8_t []){0x2b, 0x24, 0x00}, 3, 0},
    {0xa1, (uint8_t []){0x87}, 1, 0},
    {0xa2, (uint8_t []){0x86}, 1, 0},
    {0xa5, (uint8_t []){0x00}, 1, 0},
    {0xa6, (uint8_t []){0x00}, 1, 0},
    {0xa7, (uint8_t []){0x00}, 1, 0},
    {0xa8, (uint8_t []){0x36}, 1, 0},
    {0xa9, (uint8_t []){0x7e}, 1, 0},
    {0xaa, (uint8_t []){0x7e}, 1, 0},
    
    // 电压设置
    {0xB9, (uint8_t []){0x85}, 1, 0},
    {0xBA, (uint8_t []){0x84}, 1, 0},
    {0xBB, (uint8_t []){0x83}, 1, 0},
    {0xBC, (uint8_t []){0x82}, 1, 0},
    {0xBD, (uint8_t []){0x81}, 1, 0},
    {0xBE, (uint8_t []){0x80}, 1, 0},
    {0xBF, (uint8_t []){0x01}, 1, 0},
    {0xC0, (uint8_t []){0x02}, 1, 0},
    
    // 通道设置
    {0xc1, (uint8_t []){0x00}, 1, 0},
    {0xc2, (uint8_t []){0x00}, 1, 0},
    {0xc3, (uint8_t []){0x00}, 1, 0},
    {0xc4, (uint8_t []){0x33}, 1, 0},
    {0xc5, (uint8_t []){0x7e}, 1, 0},
    {0xc6, (uint8_t []){0x7e}, 1, 0},
    {0xC8, (uint8_t []){0x33, 0x33}, 2, 0},
    {0xC9, (uint8_t []){0x68}, 1, 0},
    {0xCA, (uint8_t []){0x69}, 1, 0},
    {0xCB, (uint8_t []){0x6a}, 1, 0},
    {0xCC, (uint8_t []){0x6b}, 1, 0},
    {0xCD, (uint8_t []){0x33, 0x33}, 2, 0},
    {0xCE, (uint8_t []){0x6c}, 1, 0},
    {0xCF, (uint8_t []){0x6d}, 1, 0},
    {0xD0, (uint8_t []){0x6e}, 1, 0},
    {0xD1, (uint8_t []){0x6f}, 1, 0},
    
    // Gamma 设置
    {0xAB, (uint8_t []){0x03, 0x67}, 2, 0},
    {0xAC, (uint8_t []){0x03, 0x6b}, 2, 0},
    {0xAD, (uint8_t []){0x03, 0x68}, 2, 0},
    {0xAE, (uint8_t []){0x03, 0x6c}, 2, 0},
    
    // 其他设置
    {0xf2, (uint8_t []){0x2c, 0x1b, 0x0b, 0x20}, 4, 0},
    {0xe9, (uint8_t []){0x29}, 1, 0},
    {0xec, (uint8_t []){0x04}, 1, 0},
    
    // 最终配置
    {0x35, (uint8_t []){0x00}, 1, 0},        // TE 使能
    {0x44, (uint8_t []){0x00, 0x10}, 2, 0},  // TE 配置
    {0x46, (uint8_t []){0x10}, 1, 0},        // TE 配置
    {0xff, (uint8_t []){0x00}, 1, 0},        // 结束配置
    {0x3a, (uint8_t []){0x05}, 1, 0},        // 色彩格式设置
    {0x11, (uint8_t []){0x00}, 0, 220},      // 退出睡眠模式
    {0x29, (uint8_t []){0x00}, 0, 200},      // 开启显示
};

static esp_err_t panel_nv3007a_init(esp_lcd_panel_t *panel)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007a->io;

    // LCD goes into sleep mode and display will be turned off after power on reset, exit sleep mode first
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        nv3007a->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]) {
        nv3007a->colmod_val,
    }, 1), TAG, "send command failed");

    const nv3007a_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    if (nv3007a->init_cmds) {
        init_cmds = nv3007a->init_cmds;
        init_cmds_size = nv3007a->init_cmds_size;
    } else {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(nv3007a_lcd_init_cmd_t);
    }

    bool is_cmd_overwritten = false;
    for (int i = 0; i < init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        switch (init_cmds[i].cmd) {
        case LCD_CMD_MADCTL:
            is_cmd_overwritten = true;
            nv3007a->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        case LCD_CMD_COLMOD:
            is_cmd_overwritten = true;
            nv3007a->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        default:
            is_cmd_overwritten = false;
            break;
        }

        if (is_cmd_overwritten) {
            ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
        }

        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_nv3007a_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = nv3007a->io;

    x_start += nv3007a->x_gap;
    x_end += nv3007a->x_gap;
    y_start += nv3007a->y_gap;
    y_end += nv3007a->y_gap;

    // define an area of frame memory where MCU can access
    esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4);
    esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4);
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * nv3007a->fb_bits_per_pixel / 8;
    esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_nv3007a_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007a->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_nv3007a_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007a->io;
    if (mirror_x) {
        nv3007a->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        nv3007a->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        nv3007a->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        nv3007a->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        nv3007a->madctl_val
    }, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_nv3007a_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007a->io;
    if (swap_axes) {
        nv3007a->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        nv3007a->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        nv3007a->madctl_val
    }, 1);
    return ESP_OK;
}

static esp_err_t panel_nv3007a_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    nv3007a->x_gap = x_gap;
    nv3007a->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_nv3007a_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    nv3007a_panel_t *nv3007a = __containerof(panel, nv3007a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007a->io;
    int command = 0;

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    on_off = !on_off;
#endif

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}
