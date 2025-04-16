#ifndef ESP_JPEG_DECODE_H
#define ESP_JPEG_DECODE_H
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_log.h"

#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"

#ifdef __cplusplus
extern "C" {
#endif
size_t get_rgb_width();
size_t get_rgb_height();


jpeg_error_t esp_jpeg_decode_one_picture(uint8_t *input_buf, int len, uint8_t **output_buf, int *out_len);

#ifdef __cplusplus
}
#endif

#endif // FS_MANAGER_H