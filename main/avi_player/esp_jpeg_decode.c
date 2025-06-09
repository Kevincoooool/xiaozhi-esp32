
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_jpeg_decode.h"
static const char *TAG = "esp_jpeg_decode";

uint8_t *img_rgb565 = NULL; // MALLOC_CAP_SPIRAM
uint8_t *pbuffer = NULL;    // MALLOC_CAP_SPIRAM  MALLOC_CAP_DMA
size_t rgb_width = 0;
size_t rgb_height = 0;

static jpeg_pixel_format_t j_type = JPEG_PIXEL_FORMAT_RGB565_LE;
static jpeg_rotate_t j_rotation = JPEG_ROTATE_0D;
size_t get_rgb_width()
{
    return rgb_width;
}
size_t get_rgb_height()
{
    return rgb_height;
}
jpeg_error_t esp_jpeg_decode_one_picture(uint8_t *input_buf, int len, uint8_t **output_buf, int *out_len)
{
    uint8_t *out_buf = NULL;
    jpeg_error_t ret = JPEG_ERR_OK;
    jpeg_dec_io_t *jpeg_io = NULL;
    jpeg_dec_header_info_t *out_info = NULL;
    rgb_width = 0;
    rgb_height = 0;
    // Generate default configuration
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = j_type;
    config.rotate = j_rotation;
    // config.scale.width       = 0;
    // config.scale.height      = 0;
    // config.clipper.width     = 0;
    // config.clipper.height    = 0;

    // Create jpeg_dec handle
    jpeg_dec_handle_t jpeg_dec = NULL;
    ret = jpeg_dec_open(&config, &jpeg_dec);
    if (ret != JPEG_ERR_OK)
    {
        return ret;
    }

    // Create io_callback handle
    jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
    if (jpeg_io == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }

    // Create out_info handle
    out_info = calloc(1, sizeof(jpeg_dec_header_info_t));
    if (out_info == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }

    // Set input buffer and buffer len to io_callback
    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;

    // Parse jpeg picture header and get picture for user and decoder
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
    if (ret != JPEG_ERR_OK)
    {
        goto jpeg_dec_failed;
    }
    rgb_width = out_info->width;
    rgb_height = out_info->height;
    // ESP_LOGI(TAG, "img width:%d height:%d ", rgb_width, rgb_height);

    *out_len = out_info->width * out_info->height * 3;
    // Calloc out_put data buffer and update inbuf ptr and inbuf_len
    if (config.output_type == JPEG_PIXEL_FORMAT_RGB565_LE || config.output_type == JPEG_PIXEL_FORMAT_RGB565_BE || config.output_type == JPEG_PIXEL_FORMAT_CbYCrY)
    {
        *out_len = out_info->width * out_info->height * 2;
    }
    else if (config.output_type == JPEG_PIXEL_FORMAT_RGB888)
    {
        *out_len = out_info->width * out_info->height * 3;
    }
    else
    {
        ret = JPEG_ERR_INVALID_PARAM;
        goto jpeg_dec_failed;
    }
    out_buf = jpeg_calloc_align(*out_len, 16);
    if (out_buf == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }
    jpeg_io->outbuf = out_buf;
    *output_buf = out_buf;

    // Start decode jpeg
    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    if (ret != JPEG_ERR_OK)
    {
        goto jpeg_dec_failed;
    }

    // Decoder deinitialize
jpeg_dec_failed:
    jpeg_dec_close(jpeg_dec);
    if (jpeg_io)
    {
        free(jpeg_io);
    }
    if (out_info)
    {
        free(out_info);
    }
    return ret;
}
