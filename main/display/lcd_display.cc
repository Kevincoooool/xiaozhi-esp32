#include "lcd_display.h"
#include "font_awesome_symbols.h"

#include <esp_log.h>
#include <esp_err.h>
#include <driver/ledc.h>
#include <vector>
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"
#include "avi_player.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#define TAG "LcdDisplay"
#define LCD_LEDC_CH LEDC_CHANNEL_0

#define LCD_LVGL_TICK_PERIOD_MS 2
#define LCD_LVGL_TASK_MAX_DELAY_MS 20
#define LCD_LVGL_TASK_MIN_DELAY_MS 1
#define LCD_LVGL_TASK_STACK_SIZE (4 * 1024)
#define LCD_LVGL_TASK_PRIORITY 10

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_30_1);
LV_FONT_DECLARE(font_awesome_14_1);
LV_FONT_DECLARE(font_dingding);
static lv_disp_drv_t disp_drv;



// lv_img_dsc_t img_dsc = {
//     .header.always_zero = 0,
//     .header.w = 240,
//     .header.h = 240,
//     .data_size = 240 * 240 * 2,
//     .header.cf = LV_IMG_CF_TRUE_COLOR,
//     .data = NULL,
// };
uint8_t *img_rgb565 = NULL; // MALLOC_CAP_SPIRAM
uint8_t *pbuffer = NULL;    // MALLOC_CAP_SPIRAM  MALLOC_CAP_DMA
size_t rgb_width = 0;
size_t rgb_height = 0;

static jpeg_pixel_format_t j_type = JPEG_PIXEL_FORMAT_RGB565_BE;
static jpeg_rotate_t j_rotation = JPEG_ROTATE_0D;

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
    jpeg_io = (jpeg_dec_io_t*)calloc(1, sizeof(jpeg_dec_io_t));
    if (jpeg_io == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }

    // Create out_info handle
    out_info =(jpeg_dec_header_info_t*) calloc(1, sizeof(jpeg_dec_header_info_t));
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
    out_buf = (uint8_t*)jpeg_calloc_align(*out_len, 16);
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

// #define EXAMPLE_STD_BCLK_IO1 GPIO_NUM_41 // I2S bit clock io number
// #define EXAMPLE_STD_WS_IO1 GPIO_NUM_42   // I2S word select io number
// #define EXAMPLE_STD_DIN_IO1 GPIO_NUM_2   // I2S data in io number

// #define EXAMPLE_STD_BCLK_IO2 GPIO_NUM_1 // I2S bit clock io number
// #define EXAMPLE_STD_WS_IO2 GPIO_NUM_46  // I2S word select io number
// #define EXAMPLE_STD_DOUT_IO2 GPIO_NUM_3 // I2S data out io number
// #define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_3
// #define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_46
// #define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_1
// #define EXAMPLE_BUFF_SIZE 2048

// static i2s_chan_handle_t tx_handle_; // I2S tx channel handler
// static i2s_chan_handle_t rx_handle_; // I2S rx channel handler

// #define TAG "I2S_TEST"
// static void i2s_example_init_std_duplex(void)
// {
//     // Create a new channel for speaker
//     i2s_chan_config_t chan_cfg = {
//         .id = I2S_NUM_0,
//         .role = I2S_ROLE_MASTER,
//         .dma_desc_num = 6,
//         .dma_frame_num = 240,
//         .auto_clear_after_cb = true,
//         .auto_clear_before_cb = false,
//         .intr_priority = 0,
//     };
//     ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, NULL));

//     i2s_std_config_t std_cfg = {
//         .clk_cfg = {
//             .sample_rate_hz = (uint32_t)16000,
//             .clk_src = I2S_CLK_SRC_DEFAULT,
//             .ext_clk_freq_hz = 0,
//             .mclk_multiple = I2S_MCLK_MULTIPLE_256},
//         .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, .slot_mode = I2S_SLOT_MODE_MONO, .slot_mask = I2S_STD_SLOT_LEFT, .ws_width = I2S_DATA_BIT_WIDTH_32BIT, .ws_pol = false, .bit_shift = true, .left_align = true, .big_endian = false, .bit_order_lsb = false},
//         .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = AUDIO_I2S_SPK_GPIO_BCLK, .ws = AUDIO_I2S_SPK_GPIO_LRCK, .dout = AUDIO_I2S_SPK_GPIO_DOUT, .din = I2S_GPIO_UNUSED, .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};
//     ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

//     ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
// }
/*显示spiffs的所有文件名*/
static void SPIFFS_Directory(char *path)
{
    DIR *dir = opendir(path);
    assert(dir != NULL);
    while (true)
    {
        struct dirent *pe = readdir(dir);
        if (!pe)
            break;
        ESP_LOGI(__FUNCTION__, "d_name=%s d_ino=%d d_type=%x", pe->d_name, pe->d_ino, pe->d_type);
    }
    closedir(dir);
}

static bool end_play = false;
int Rgbsize = 0;
lv_obj_t* img_cam ;
lv_img_dsc_t img_dsc;
void video_write(frame_data_t *data, void *arg)
{
    // int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    // int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    // int free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    // ESP_LOGI(TAG, "Free internal: %u minimal internal: %u free_psram: %u", free_sram, min_free_sram, free_psram);
    // ESP_LOGI(TAG, "Video write: %d", data->data_bytes);
    /* Set src of image with file name */
    esp_jpeg_decode_one_picture(data->data, data->data_bytes, &img_rgb565, &Rgbsize); // 使用乐鑫adf的jpg解码 速度快三倍
    // esp_lcd_panel_draw_bitmap(panel_handle, 0, 20, rgb_width + 1, rgb_height + 21, img_rgb565);
    img_dsc.data = img_rgb565;
    img_dsc.header.w = rgb_width;
    img_dsc.header.h = rgb_height;
    img_dsc.data_size = rgb_width * rgb_height * 2;
	lv_img_set_src(img_cam, &img_dsc);
    free(img_rgb565);
}

void audio_write(frame_data_t *data, void *arg)
{
    size_t bytes_write = 0;

    // ESP_LOGI(TAG, "Audio write: %d", data->data_bytes);
    // i2s_channel_write(tx_handle_, data->data, data->data_bytes, &bytes_write, 100);
}

void audio_set_clock(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg)
{
    ESP_LOGI(TAG, "Audio set clock, rate %" PRIu32 ", bits %" PRIu32 ", ch %" PRIu32 "", rate, bits_cfg, ch);
}

void avi_play_end(void *arg)
{
    ESP_LOGI(TAG, "Play end");
    end_play = true;
    // fclose("/spiffs/output.avi");
    avi_player_play_from_file("/spiffs/output.avi");
}

static void lcd_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(&disp_drv);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void lcd_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated)
    {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

void LcdDisplay::LvglTask() {
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = LCD_LVGL_TASK_MAX_DELAY_MS;
    while (1)
    {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (Lock())
        {
            task_delay_ms = lv_timer_handler();
            Unlock();
        }
        if (task_delay_ms > LCD_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = LCD_LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < LCD_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = LCD_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}
extern "C" void emoji_font_init();

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           gpio_num_t backlight_pin, bool backlight_output_invert,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : panel_io_(panel_io), panel_(panel), backlight_pin_(backlight_pin), backlight_output_invert_(backlight_output_invert),
      mirror_x_(mirror_x), mirror_y_(mirror_y), swap_xy_(swap_xy)
{
    width_ = width;
    height_ = height;
    offset_x_ = offset_x;
    offset_y_ = offset_y;

    
    InitializeBacklight(backlight_pin);
    emoji_font_init();
    // draw white
    // std::vector<uint16_t> buffer(width_, 0xFFFF);
    // for (int y = 0; y < height_; y++)
    // {
    //     esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    // }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(width_ * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(width_ * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, width_ * 20);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = width_;
    disp_drv.ver_res = height_;
    disp_drv.offset_x = offset_x_;
    disp_drv.offset_y = offset_y_;
    disp_drv.flush_cb = lcd_lvgl_flush_cb;
    disp_drv.drv_update_cb = lcd_lvgl_port_update_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_;

    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = [](void* arg) {
            lv_tick_inc(LCD_LVGL_TICK_PERIOD_MS);
        },
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "LVGL Tick Timer",
        .skip_unhandled_events = false
    };
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer_, LCD_LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mutex_ = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mutex_ != nullptr);
    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate([](void *arg) {
        static_cast<LcdDisplay*>(arg)->LvglTask();
        vTaskDelete(NULL);
    }, "LVGL", LCD_LVGL_TASK_STACK_SIZE, this, LCD_LVGL_TASK_PRIORITY, NULL);

    SetBacklight(100);
        // lv_img_dsc_t img_dsc;
    img_dsc.header.reserved = 0;

    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 240;
    img_dsc.header.h = 240;
    img_dsc.data_size = 240 * 240 * 2;
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 2,
        .format_if_mount_failed = true};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        else if (ret == ESP_ERR_NOT_FOUND)
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        else
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    /*显示spiffs里的文件列表*/
    SPIFFS_Directory("/spiffs/");
    img_rgb565 = (uint8_t*)heap_caps_malloc(800 * 480 * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM); // MALLOC_CAP_SPIRAM

    // avi_player_config_t config = {
    //     .buffer_size = 50 * 1024,
    //     .audio_cb = audio_write,
    //     .video_cb = video_write,
    //     // .audio_set_clock_cb = audio_set_clock,
    //     .avi_play_end_cb = avi_play_end,
    //     .coreID = 1,

    // };
 
    int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    int free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Free internal: %u minimal internal: %u free_psram: %u", free_sram, min_free_sram, free_psram);
    SetupUI();   
    avi_player_config_t config;

    config.buffer_size = 50 * 1024;
    config.audio_cb = audio_write;
    config.video_cb = video_write;
    config.audio_set_clock_cb = audio_set_clock,
    config.avi_play_end_cb = avi_play_end;
    config.coreID = 1;

    avi_player_init(config);

    avi_player_play_from_file("/spiffs/output.avi");
         free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
     min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
     free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Free internal: %u minimal internal: %u free_psram: %u", free_sram, min_free_sram, free_psram);
}

LcdDisplay::~LcdDisplay() {
    ESP_ERROR_CHECK(esp_timer_stop(lvgl_tick_timer_));
    ESP_ERROR_CHECK(esp_timer_delete(lvgl_tick_timer_));

    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (status_bar_ != nullptr)
    {
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != nullptr)
    {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr)
    {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr)
    {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr)
    {
        esp_lcd_panel_io_del(panel_io_);
    }
    vSemaphoreDelete(lvgl_mutex_);
}

void LcdDisplay::InitializeBacklight(gpio_num_t backlight_pin) {
    if (backlight_pin == GPIO_NUM_NC) {
        return;
    }
    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t backlight_channel = {
        .gpio_num = backlight_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = backlight_output_invert_,
        }};
    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false};

    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel));
}

// void LcdDisplay::SetBacklight(uint8_t brightness) {
//     if (backlight_pin_ == GPIO_NUM_NC) {
//         return;
//     }

//     if (brightness > 100)
//     {
//         brightness = 100;
//     }

//     ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness);
//     // LEDC resolution set to 10bits, thus: 100% = 1023
//     uint32_t duty_cycle = (1023 * brightness) / 100;
//     ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
//     ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
// }

bool LcdDisplay::Lock(int timeout_ms) {
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to 0, the program will block until the condition is met
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mutex_, timeout_ticks) == pdTRUE;
}

void LcdDisplay::Unlock() {
    xSemaphoreGiveRecursive(lvgl_mutex_);
}

void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    auto screen = lv_disp_get_scr_act(lv_disp_get_default());
    lv_obj_set_style_text_font(screen, &font_puhui_14_1, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    img_cam = lv_img_create(lv_scr_act());
	// lv_obj_set_pos(img_cam, 0, 0);
	// lv_obj_set_size(img_cam, 240, 240);
	lv_obj_center(img_cam);
    status_bar_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar_, LV_HOR_RES - 40, 40);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    // lv_obj_set_x(status_bar_, 5);
    lv_obj_set_align(status_bar_, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(status_bar_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_0, 0);

    emotion_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_1, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    // lv_obj_center(emotion_label_);
    // lv_obj_set_style_text_color(emotion_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    // lv_obj_set_style_align(emotion_label_, LV_ALIGN_CENTER, 0);
    lv_obj_set_align(emotion_label_, LV_ALIGN_TOP_MID);
    lv_obj_set_y(emotion_label_, 50);

    chat_message_label_ = lv_label_create(lv_scr_act());
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.8); // 限制宽度为屏幕宽度的 80%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP); // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐
    lv_obj_set_style_text_font(chat_message_label_, &font_dingding, 0);
    lv_label_set_text(chat_message_label_, "XiaoZhi AI");
    lv_obj_set_style_text_color(chat_message_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_align(chat_message_label_, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(chat_message_label_, -10);

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_y(network_label_, 10);
    lv_obj_set_style_text_font(network_label_, &font_awesome_14_1, 0);
    lv_obj_set_style_text_color(network_label_, lv_palette_main(LV_PALETTE_GREEN), 0);

    // lv_obj_set_x(network_label_, 30);
    // lv_obj_set_y(network_label_, 30);
    // lv_obj_set_align(network_label_, LV_ALIGN_TOP_LEFT);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "通知");
    // lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(notification_label_, &font_dingding, 0);
    lv_obj_set_style_text_color(notification_label_, lv_palette_main(LV_PALETTE_GREEN), 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(status_label_, "正在初始化");
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label_, &font_dingding, 0);
    lv_obj_set_style_text_color(status_label_, lv_palette_main(LV_PALETTE_GREEN), 0);
    // lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, &font_awesome_14_1, 0);
    // lv_obj_set_x(battery_label_, 220);
    // lv_obj_set_y(battery_label_, 30);
    lv_obj_set_align(battery_label_, LV_ALIGN_TOP_RIGHT);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, &font_awesome_14_1, 0);


}


void LcdDisplay::SetChatMessage(const std::string &role, const std::string &content) {
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_label_set_text(chat_message_label_, content.c_str());
}
