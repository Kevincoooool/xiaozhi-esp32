#include "iot/thing.h"
#include "board.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "display/lcd_display.h"

#define TAG "Camera"

// 摄像头配置参数
#define CAMERA_PIN_PWDN -1
#define CAMERA_PIN_RESET -1
#define CAMERA_PIN_XCLK 40
#define CAMERA_PIN_SIOD -1
#define CAMERA_PIN_SIOC -1

#define CAMERA_PIN_D7 39
#define CAMERA_PIN_D6 41
#define CAMERA_PIN_D5 42
#define CAMERA_PIN_D4 12
#define CAMERA_PIN_D3 3
#define CAMERA_PIN_D2 14
#define CAMERA_PIN_D1 47
#define CAMERA_PIN_D0 13
#define CAMERA_PIN_VSYNC 21
#define CAMERA_PIN_HREF 38
#define CAMERA_PIN_PCLK 11

namespace iot {

class Camera : public Thing {
private:
    bool is_initialized_ = false;
    bool is_displaying_ = false;
    esp_timer_handle_t display_timer_ = nullptr;

    bool InitializeCamera() {
        camera_config_t config;
        config.ledc_channel = LEDC_CHANNEL_0;
        config.ledc_timer = LEDC_TIMER_0;
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = I2C_NUM_0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = 20000000;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_240X240;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        // 初始化摄像头
        esp_err_t err = esp_camera_init(&config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
            return false;
        }

        is_initialized_ = true;
        ESP_LOGI(TAG, "Camera initialized successfully");
        return true;
    }

public:
    Camera() : Thing("Camera", "摄像头控制") {
        // 初始化摄像头
        InitializeCamera();

        // 添加摄像头状态属性
        properties_.AddBooleanProperty("initialized", "摄像头是否初始化成功", [this]() -> bool {
            return is_initialized_;
        });

        properties_.AddBooleanProperty("displaying", "摄像头是否正在显示", [this]() -> bool {
            return is_displaying_;
        });

        
        methods_.AddMethod("StartDisplay", "开启摄像头显示", ParameterList(), 
            [this](const ParameterList& parameters) {
                if (!is_initialized_) {
                    ESP_LOGE(TAG, "Camera not initialized");
                    return;
                }
                is_displaying_ = true;
                
                // 创建定时器定期更新图像
                const esp_timer_create_args_t timer_args = {
                    .callback = [](void* arg) {
                        auto* camera = static_cast<Camera*>(arg);
                        if (!camera->is_displaying_) return;
                        camera_fb_t* fb = esp_camera_fb_get();
                        if (!fb) {
                            ESP_LOGE(TAG, "Camera capture failed");
                            return;
                        }
                        auto display = Board::GetInstance().GetDisplay();
                        // 更新显示
                        display->UpdateCameraImage(fb);
                        esp_camera_fb_return(fb);
                    },
                    .arg = this,
                    .dispatch_method = ESP_TIMER_TASK,
                    .name = "camera_display",
                    .skip_unhandled_events = true
                };
                
                ESP_ERROR_CHECK(esp_timer_create(&timer_args, &display_timer_));
                ESP_ERROR_CHECK(esp_timer_start_periodic(display_timer_, 100000));  // 10fps
                
                ESP_LOGI(TAG, "Camera display started");
        });

        // 添加关闭显示方法
        methods_.AddMethod("StopDisplay", "关闭摄像头显示", ParameterList(), 
            [this](const ParameterList& parameters) {
                if (!is_initialized_) {
                    ESP_LOGE(TAG, "Camera not initialized");
                    return;
                }
                
                // 先停止定时器
                if (display_timer_) {
                    ESP_ERROR_CHECK(esp_timer_stop(display_timer_));
                    ESP_ERROR_CHECK(esp_timer_delete(display_timer_));
                    display_timer_ = nullptr;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                // 再设置状态
                is_displaying_ = false;
                
                auto display = Board::GetInstance().GetDisplay();
                // 发送空的帧缓冲区来隐藏显示
                display->UpdateCameraImage(nullptr);
                
                ESP_LOGI(TAG, "Camera display stopped");
        });
    }

    ~Camera() {
        if (is_initialized_) {
            esp_camera_deinit();
            is_initialized_ = false;
        }
    }
};

} // namespace iot

DECLARE_THING(Camera);