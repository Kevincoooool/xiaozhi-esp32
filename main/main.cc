#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>

#include "application.h"
#include "system_info.h"
#include <esp_efuse_table.h>

#define TAG "main"
#define POWER_BTN_GPIO GPIO_NUM_10
#define POWER_CTRL_GPIO GPIO_NUM_11

void power_button_task(void* arg) {
    const TickType_t LONG_PRESS_TIME = pdMS_TO_TICKS(4000);  // 4秒
    TickType_t press_start = 0;
    bool btn_pressed = false;

    while (1) {
        int level = gpio_get_level(POWER_BTN_GPIO);
        
        if (level == 0 && !btn_pressed) {  // 按钮按下
            btn_pressed = true;
            press_start = xTaskGetTickCount();
            ESP_LOGI(TAG, "Power button pressed");
        }
        else if (level == 1 && btn_pressed) {  // 按钮释放
            btn_pressed = false;
            TickType_t press_duration = xTaskGetTickCount() - press_start;
            
            if (press_duration >= LONG_PRESS_TIME) {                
                ESP_LOGI(TAG, "Long press detected, power on");
                vTaskDelay(pdMS_TO_TICKS(1000));  // 等待100ms，防止误触发

                gpio_set_level(POWER_CTRL_GPIO, 1);  // 长按4秒，拉高电源控制
                break;
            } else {                
                ESP_LOGI(TAG, "Short press detected, power off");
                vTaskDelay(pdMS_TO_TICKS(1000));  // 等待100ms，防止误触发
                gpio_set_level(POWER_CTRL_GPIO, 0);  // 短按，拉低电源控制
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));  // 20ms 采样间隔
    }
    vTaskDelete(NULL);
}
extern "C" void app_main(void)
{
    // esp_rom_gpio_pad_select_gpio(POWER_BTN_GPIO);
    // esp_rom_gpio_pad_select_gpio(POWER_CTRL_GPIO);
    // gpio_set_direction(POWER_BTN_GPIO, GPIO_MODE_INPUT);
    // gpio_set_direction(POWER_CTRL_GPIO, GPIO_MODE_OUTPUT);
    esp_rom_gpio_pad_select_gpio(POWER_BTN_GPIO);
    esp_rom_gpio_pad_select_gpio(POWER_CTRL_GPIO);
    
    esp_efuse_write_field_bit(ESP_EFUSE_VDD_SPI_AS_GPIO);
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << POWER_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,    // 启用上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&btn_config));
    
    gpio_config_t ctrl_config = {
        .pin_bit_mask = (1ULL << POWER_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&ctrl_config));
    gpio_set_level(POWER_CTRL_GPIO, 1);  // 长按4秒，拉高电源控制

    // 创建按钮检测任务
    xTaskCreate(power_button_task, "power_btn", 2048, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(4000));  // 等待100ms，防止误触发
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Launch the application
    Application::GetInstance().Start();
    // The main thread will exit and release the stack memory
}
