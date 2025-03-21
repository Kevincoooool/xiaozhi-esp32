#include "wifi_board.h"

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

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>

static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";
    
    // 播报配置 WiFi 的提示
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
    
    // Wait forever until reset after configuration
    while (true) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void WifiBoard::StartNetwork() {
    // User can press BOOT button while starting to enter WiFi configuration mode
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // If no WiFi SSID is configured, enter WiFi configuration mode
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }

    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.Start();

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }

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

bool WifiBoard::FetchApiUrl() {
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

Http* WifiBoard::CreateHttp() {
    return new EspHttp();
}

WebSocket* WifiBoard::CreateWebSocket() {
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    // 使用获取到的API地址
    std::string url = api_url_.empty() ? CONFIG_WEBSOCKET_URL : api_url_;
    if (url.find("wss://") == 0) {
        return new WebSocket(new TlsTransport());
    } else {
        return new WebSocket(new TcpTransport());
    }
#endif
    return nullptr;
}

Mqtt* WifiBoard::CreateMqtt() {
    return new EspMqtt();
}

Udp* WifiBoard::CreateUdp() {
    return new EspUdp();
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;
    }
    int8_t rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
    board_json += "\"name\":\"" BOARD_NAME "\",";
    if (!wifi_config_mode_) {
        board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";
        board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";
        board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";
        board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
        // 添加重启原因标记
        settings.SetInt("current_reason", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}
