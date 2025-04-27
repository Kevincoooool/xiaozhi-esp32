#include "blufi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http.h>
#include <esp_mqtt.h>
#include <esp_udp.h>
#include <tcp_transport.h>
#include <tls_transport.h>
#include <web_socket.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_blufi_api.h>

#include "blufi_manager.hpp"

static const char *TAG = "BlufiBoard";

BlufiBoard::BlufiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
    
    // 不在构造函数中注册事件处理，而是在 StartNetwork 中注册
    // 这样可以避免过早使用网络栈
}

std::string BlufiBoard::GetBoardType() {
    return "blufi";
}

void BlufiBoard::LoadWifiCredentials() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for wifi_config: %s", esp_err_to_name(err));
        return;
    }

    size_t ssid_len = 0;
    size_t passwd_len = 0;
    
    // 获取SSID长度
    err = nvs_get_str(nvs_handle, "ssid", nullptr, &ssid_len);
    if (err != ESP_OK || ssid_len == 0) {
        ESP_LOGE(TAG, "Failed to get SSID length: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }
    
    // 获取密码长度
    err = nvs_get_str(nvs_handle, "passwd", nullptr, &passwd_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get password length: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }
    
    // 读取SSID和密码
    char* ssid = new char[ssid_len];
    char* password = new char[passwd_len];
    
    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, "passwd", password, &passwd_len);
        if (err == ESP_OK) {
            // 记录SSID
            wifi_ssid_ = std::string(ssid);
            
            // 注册WiFi事件处理
            esp_err_t handler_err;
            handler_err = esp_event_handler_instance_register(
                WIFI_EVENT, ESP_EVENT_ANY_ID,
                &WiFiEventHandler, this, NULL);
            if (handler_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(handler_err));
            }
            
            handler_err = esp_event_handler_instance_register(
                IP_EVENT, IP_EVENT_STA_GOT_IP,
                &WiFiEventHandler, this, NULL);
            if (handler_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(handler_err));
            }
            
            // 配置WiFi连接
            ESP_LOGI(TAG, "Connecting to saved SSID: %s", ssid);
            wifi_config_t wifi_config = {};
            memcpy(wifi_config.sta.ssid, ssid, std::min(sizeof(wifi_config.sta.ssid), ssid_len-1));
            memcpy(wifi_config.sta.password, password, std::min(sizeof(wifi_config.sta.password), passwd_len-1));
            
            esp_err_t wifi_err;
            wifi_err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (wifi_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(wifi_err));
            }
            
            wifi_err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (wifi_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(wifi_err));
            }
            
            wifi_err = esp_wifi_start();
            if (wifi_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(wifi_err));
            }
            
            wifi_err = esp_wifi_connect();
            if (wifi_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(wifi_err));
            }
        } else {
            ESP_LOGE(TAG, "Failed to get password: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to get SSID: %s", esp_err_to_name(err));
    }
    
    delete[] ssid;
    delete[] password;
    nvs_close(nvs_handle);
}

void BlufiBoard::SaveWifiCredentials(const std::string& ssid, const std::string& password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return;
    
    err = nvs_set_str(nvs_handle, "ssid", ssid.c_str());
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return;
    }
    
    err = nvs_set_str(nvs_handle, "passwd", password.c_str());
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return;
    }
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Saved WiFi credentials for SSID: %s", ssid.c_str());
}

void BlufiBoard::WiFiEventHandler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data) {
    BlufiBoard* board = static_cast<BlufiBoard*>(arg);
    
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi station started");
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            board->wifi_connected_ = false;
            ESP_LOGI(TAG, "WiFi disconnected, trying to reconnect...");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
            board->wifi_channel_ = event->channel;
            ESP_LOGI(TAG, "WiFi connected to channel %d", board->wifi_channel_);
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            // 修复类型转换错误，将 esp_ip4_addr_t* 转换为 const ip4_addr_t*
            board->wifi_ip_ = ip4addr_ntoa((const ip4_addr_t*)&event->ip_info.ip);
            board->wifi_connected_ = true;
            ESP_LOGI(TAG, "Got IP: %s", board->wifi_ip_.c_str());
            
            // 获取RSSI
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                board->wifi_rssi_ = ap_info.rssi;
                ESP_LOGI(TAG, "WiFi RSSI: %d", board->wifi_rssi_);
            }
        }
    }
}

void BlufiBoard::StartBlufiConfig() {
    // 在StartNetwork()中已经完成netif初始化，此处只检查不重复初始化
    esp_err_t err;
    
    // 检查网络接口是否已初始化
    bool netif_initialized = false;
    if (esp_netif_get_nr_of_ifs() > 0) {
        netif_initialized = true;
        ESP_LOGI(TAG, "Network interfaces already initialized, count: %d", esp_netif_get_nr_of_ifs());
    } else {
        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(err));
            return;
        }
    }
    
    // 创建默认事件循环（如果已存在会返回ESP_ERR_INVALID_STATE，我们忽略此错误）
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        return;
    }
    
    // 检查是否需要创建网络接口
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (!sta_netif) {
            ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
            return;
        }
    } else {
        ESP_LOGI(TAG, "Using existing STA netif");
    }
    
    // esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    // if (ap_netif == NULL) {
    //     ap_netif = esp_netif_create_default_wifi_ap();
    //     if (!ap_netif) {
    //         ESP_LOGE(TAG, "Failed to create default WiFi AP netif");
    //         return;
    //     }
    // } else {
    //     ESP_LOGI(TAG, "Using existing AP netif");
    // }
    
    // // 在重新注册事件处理程序前，先注销现有的处理程序，避免重复注册
    // esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiEventHandler);
    // esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiEventHandler);
    
    // // 重新注册事件处理程序
    // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiEventHandler, this));
    // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiEventHandler, this));
    
    // 检查 WiFi 是否已初始化
    wifi_mode_t mode;
    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        // WiFi 未初始化，需要初始化
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
            return;
        }
    } else {
        ESP_LOGI(TAG, "WiFi already initialized in mode %d", mode);
    }
    
    // 设置 WiFi 模式为 STA 模式
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        return;
    }
    
    // 启动 WiFi
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return;
    }
    
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    // 初始化 BluFi
    auto& blufi = BlufiManager::GetInstance();
    esp_err_t ret = blufi.Initialize();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BluFi initialize failed");
        return;
    }

    // 显示蓝牙配网提示
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += "\n";
    hint += SystemInfo::GetMacAddress();
    hint += "\n\n";
    
    // 播报配置 WiFi 的提示
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
    
    // 等待配网完成
    // while (!blufi.IsConnected() || blufi.GetStaSsid().empty()) {
    //     int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    //     int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    //     ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
    //     vTaskDelay(pdMS_TO_TICKS(2000));
    // }
     // 创建配网模式定时器
     esp_timer_create_args_t timer_args = {
        .callback = [](void*) {
            ESP_LOGI(TAG, "Config mode timeout, shutting down...");
            gpio_set_level(GPIO_NUM_11, 0);  // 关机
        },
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "config_mode_timer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &config_mode_timer_));
    ESP_ERROR_CHECK(esp_timer_start_once(config_mode_timer_, 300000000));  // 10分钟 = 600秒 = 600000000微秒
    while (blufi.IsWifiConnected() == false) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    // 保存 WiFi 配置
    SaveWifiCredentials(blufi.GetStaSsid(), blufi.GetStaPassword());
    ESP_LOGI(TAG, "WiFi SSID: %s", blufi.GetStaSsid().c_str());
    ESP_LOGI(TAG, "WiFi Password: %s", blufi.GetStaPassword().c_str());
    // 设置配网成功重启标记
    SetConfigModeReboot(true);

    // 清理资源
    blufi.Deinitialize();

    // 重启设备以连接新的 WiFi
    esp_restart();
}

void BlufiBoard::EnterWifiConfigMode() {
    // 先注销 WiFi 事件处理程序，再停止 WiFi
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiEventHandler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiEventHandler);
    ESP_LOGI(TAG, "Unregistered WiFi event handlers before entering config mode");
    
    // 停止 WiFi STA 模式
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(err));
    }

    // 重要: 不要在此处停止网络接口或删除它们，只需停止 WiFi
    StartBlufiConfig();
}

void BlufiBoard::StartNetwork() {
    // 初始化网络堆栈 - 严格参考 initialise_wifi 函数
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(err));
        return;
    }
    
    // 创建默认事件循环
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        return;
    }
    
    // 创建默认 WiFi STA 接口
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
        return;
    }
    
    // 参考代码中同时创建 AP 接口
    // esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    // if (!ap_netif) {
    //     ESP_LOGE(TAG, "Failed to create default WiFi AP netif");
    //     return;
    // }
    
    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiEventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiEventHandler, this));
    
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        return;
    }
    
    // 用户按下BOOT按钮可以强制进入WiFi配置模式
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // 从NVS加载WiFi凭据并尝试连接
    LoadWifiCredentials();
    
    // 如果没有WiFi凭据或无法连接，则进入配网模式
    if (wifi_ssid_.empty()) {
        ESP_LOGI(TAG, "No WiFi credentials found");
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }

    // 等待WiFi连接
    int timeout_ms = 10 * 1000; // 延长超时时间
    int wait_ms = 0;
    auto display = Board::GetInstance().GetDisplay();
    display->ShowNotification(Lang::Strings::CONNECT_TO + wifi_ssid_ + "...", 30000);
    
    while (!wifi_connected_ && wait_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_ms += 500;
        ESP_LOGI(TAG, "Waiting for WiFi connection... (%d/%d ms)", wait_ms, timeout_ms);
    }
    
    // 连接失败，进入配网模式
    if (!wifi_connected_) {
        ESP_LOGI(TAG, "Failed to connect to WiFi");
        wifi_config_mode_ = true;
        
        // 在进入配网模式前注销WiFi事件处理程序
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiEventHandler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiEventHandler);
        ESP_LOGI(TAG, "Unregistered WiFi event handlers before entering config mode");
        
        EnterWifiConfigMode();
        return;
    }
    
    display->ShowNotification((Lang::Strings::CONNECTED_TO + wifi_ssid_).c_str(), 3000);
     // 连接成功后获取API地址
     const int MAX_RETRY = 3;
     int retry_count = 0;
     while (!FetchApiUrl()) {
         retry_count++;
         if (retry_count >= MAX_RETRY) {
             ESP_LOGE(TAG, "Failed to get API URL after %d retries", MAX_RETRY);
             auto& app = Application::GetInstance();
             app.Alert(Lang::Strings::ERROR, "Failed to get API URL", "sad");
             break;
         }
         ESP_LOGW(TAG, "Retrying to get API URL in 5 seconds (%d/%d)", retry_count, MAX_RETRY);
         vTaskDelay(pdMS_TO_TICKS(5000));
     }
}

bool BlufiBoard::FetchApiUrl() {
    ESP_LOGI(TAG, "Fetching API URL...");
    
    if (CONFIG_API_URL_ENDPOINT == nullptr || strlen(CONFIG_API_URL_ENDPOINT) < 10) {
        ESP_LOGE(TAG, "API URL endpoint is not properly set");
        return false;
    }

    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return false;
    }

    // 设置请求头
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress());
    http->SetHeader("Client-Id", GetUuid());
    http->SetHeader("Content-Type", "application/json");
    auto& board = Board::GetInstance();
    // Check if there is a new firmware version available
    post_data_= board.GetJson();
    // 发送GET请求
    if (!http->Open("GET", CONFIG_API_URL_ENDPOINT, post_data_)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        delete http;
        return false;
    }

    // 读取响应内容
    auto response = http->GetBody();
    http->Close();
    delete http;

    // 解析JSON响应
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return false;
    }

       // 获取data对象
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        ESP_LOGE(TAG, "No data field in response");
        cJSON_Delete(root);
        return false;
    }

    // 获取API URL
    cJSON *api = cJSON_GetObjectItem(data, "api");
    if (!api || !api->valuestring) {
        ESP_LOGE(TAG, "No api field in data");
        cJSON_Delete(root);
        return false;
    }

    api_url_ = api->valuestring;
    ESP_LOGI(TAG, "Got API URL: %s", api_url_.c_str());

    // 可选：保存 OTA URL
    cJSON *ota = cJSON_GetObjectItem(data, "ota");
    if (ota && ota->valuestring) {
        // 可以保存 OTA URL 供后续使用
        ota_url_ = ota->valuestring;
        ESP_LOGI(TAG, "Got OTA URL: %s", ota_url_.c_str());
    
    }
    cJSON_Delete(root);
    return true;
}
Http* BlufiBoard::CreateHttp() {
    return new EspHttp();
}

WebSocket* BlufiBoard::CreateWebSocket() {
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    // 使用获取到的API地址
    ESP_LOGI(TAG, "API URL: %s", api_url_.c_str());
    // std::string url = "wss://ws.tdstar.net:443/";
    // std::string url = CONFIG_WEBSOCKET_URL;
    std::string url = api_url_.empty() ? CONFIG_WEBSOCKET_URL : api_url_;
    if (url.find("wss://") == 0) {
        return new WebSocket(new TlsTransport());
    } else {
        return new WebSocket(new TcpTransport());
    }
#endif
    return nullptr;
}

Mqtt* BlufiBoard::CreateMqtt() {
    return new EspMqtt();
}

Udp* BlufiBoard::CreateUdp() {
    return new EspUdp();
}

const char* BlufiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    
    if (!wifi_connected_) {
        return FONT_AWESOME_WIFI_OFF;
    }
    
    if (wifi_rssi_ >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (wifi_rssi_ >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string BlufiBoard::GetBoardJson() {
    // Set the board type for OTA
    std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
    board_json += "\"name\":\"" BOARD_NAME "\",";
    if (!wifi_config_mode_ && wifi_connected_) {
        board_json += "\"ssid\":\"" + wifi_ssid_ + "\",";
        board_json += "\"rssi\":" + std::to_string(wifi_rssi_) + ",";
        board_json += "\"channel\":" + std::to_string(wifi_channel_) + ",";
        board_json += "\"ip\":\"" + wifi_ip_ + "\",";
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";
    return board_json;
}

void BlufiBoard::SetPowerSaveMode(bool enabled) {
    if (enabled) {
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    }
}

void BlufiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
        settings.SetInt("current_reason", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}
void BlufiBoard::SetConfigModeReboot(bool value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return;
    
    err = nvs_set_u8(nvs_handle, "config_reboot", value ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
}

bool BlufiBoard::IsConfigModeReboot() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;
    
    uint8_t value = 0;
    err = nvs_get_u8(nvs_handle, "config_reboot", &value);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && value == 1);
}