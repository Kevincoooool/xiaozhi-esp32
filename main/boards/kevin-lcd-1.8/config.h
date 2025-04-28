
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_4
#define AUDIO_I2S_GPIO_WS GPIO_NUM_7
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_15

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_18
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_17
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_16
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_1
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_2

#define RESET_NVS_BUTTON_GPIO       GPIO_NUM_NC
#define RESET_FACTORY_BUTTON_GPIO   GPIO_NUM_NC

#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH   360
#define DISPLAY_HEIGHT  360
#define DISPLAY_SWAP_XY  false
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define BACKLIGHT_INVERT false
#define QSPI_LCD_H_RES           (360)
#define QSPI_LCD_V_RES           (360)
#define QSPI_LCD_BIT_PER_PIXEL   (16)

#define QSPI_LCD_HOST           SPI2_HOST
#define QSPI_PIN_NUM_LCD_PCLK   GPIO_NUM_48
#define QSPI_PIN_NUM_LCD_CS     GPIO_NUM_39
#define QSPI_PIN_NUM_LCD_DATA0  GPIO_NUM_41
#define QSPI_PIN_NUM_LCD_DATA1  GPIO_NUM_38
#define QSPI_PIN_NUM_LCD_DATA2  GPIO_NUM_47
#define QSPI_PIN_NUM_LCD_DATA3  GPIO_NUM_21
#define QSPI_PIN_NUM_LCD_RST    GPIO_NUM_8
#define QSPI_PIN_NUM_LCD_BL     GPIO_NUM_40
#define KEVIN_ST77916_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                                                             \
        .data0_io_num = d0,                                                       \
        .data1_io_num = d1,                                                       \
        .sclk_io_num = sclk,                                                      \
        .data2_io_num = d2,                                                       \
        .data3_io_num = d3,                                                       \
        .max_transfer_sz = max_trans_sz,                                          \
    }
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_40
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define ML307_RX_PIN GPIO_NUM_12
#define ML307_TX_PIN GPIO_NUM_13

#define AXP2101_I2C_ADDR 0x34
#endif // _BOARD_CONFIG_H_
