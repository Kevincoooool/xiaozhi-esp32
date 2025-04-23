#include "blufi_manager.hpp"
#include "blufi_security.hpp"
#include "blufi_config.hpp"
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <cstring> // 添加此行
#include "esp_random.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_blufi.h"
#include "esp_wifi.h" // 包含 esp_wifi.h
#include "esp_system.h"
#include <esp_mac.h>
#include "esp_event.h"
static const char *TAG = "BlufiManager";
static uint8_t blufi_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb,
    0x34,
    0x9b,
    0x5f,
    0x80,
    0x00,
    0x00,
    0x80,
    0x00,
    0x10,
    0x00,
    0x00,
    0xFF,
    0xFF,
    0x00,
    0x00,
};
class BlufiManager::Impl
{
private:
    // ... 现有成员变量 ...
    wifi_config_t sta_config_; // 添加 STA 配置
    wifi_config_t ap_config_;  // 添加 AP 配置
public:
    // 在 BlufiManager::Impl 类中添加 wifi_config_t 成员变量

    // 在构造函数中初始化这些变量
    Impl()
        : initialized_(false), connected_(false), wifi_connected_(false), wifi_got_ip_(false),
          sta_connecting_(false), sta_ssid_len_(0), wifi_retry_(0),
          security_(std::make_unique<BlufiSecurity>()), config_(std::make_unique<BlufiConfig>())
    {
        memset(sta_bssid_, 0, 6);
        memset(sta_ssid_, 0, 32);
        memset(&sta_conn_info_, 0, sizeof(esp_blufi_extra_info_t));
        memset(&sta_config_, 0, sizeof(wifi_config_t));  // 确保完全初始化
        memset(&ap_config_, 0, sizeof(wifi_config_t));   // 确保完全初始化
        // 初始化广播参数
        memset(&adv_params_, 0, sizeof(adv_params_));
        adv_params_.set_scan_rsp = false;
        adv_params_.include_name = true;
        adv_params_.include_txpower = true;
        adv_params_.min_interval = 0x0006; // slave connection min interval, Time = min_interval * 1.25 msec
        adv_params_.max_interval = 0x0010; // slave connection max interval, Time = max_interval * 1.25 msec
        adv_params_.appearance = 0x00;
        adv_params_.manufacturer_len = 0;
        adv_params_.p_manufacturer_data = NULL;
        adv_params_.service_data_len = 0;
        adv_params_.p_service_data = NULL;
        adv_params_.service_uuid_len = 16;
        adv_params_.p_service_uuid = blufi_service_uuid128;
        adv_params_.flag = 0x6;
    }
    // 更新 RecordWifiConnInfo 方法
    void RecordWifiConnInfo(int rssi, uint8_t reason)
    {
        memset(&sta_conn_info_, 0, sizeof(esp_blufi_extra_info_t));
        if (sta_connecting_)
        {
            sta_conn_info_.sta_max_conn_retry_set = true;
            sta_conn_info_.sta_max_conn_retry = 5;
        }
        else
        {
            sta_conn_info_.sta_conn_rssi_set = true;
            sta_conn_info_.sta_conn_rssi = rssi;
            sta_conn_info_.sta_conn_end_reason_set = true;
            sta_conn_info_.sta_conn_end_reason = reason;
        }
    }
    uint8_t softap_get_current_connection_number()
    {
        wifi_sta_list_t sta_list;
        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK)
        {
            return sta_list.num;
        }
        return 0;
    }
    esp_err_t Initialize()
    {
        if (initialized_)
        {
            return ESP_OK;
        }

        esp_err_t ret;
#if CONFIG_IDF_TARGET_ESP32
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
#endif

        // 初始化蓝牙控制器
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        if (ret)
        {
            ESP_LOGE(TAG, "Initialize controller failed");
            return ret;
        }

        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (ret)
        {
            ESP_LOGE(TAG, "Enable controller failed");
            return ret;
        }

        ret = esp_bluedroid_init();
        if (ret)
        {
            ESP_LOGE(TAG, "Initialize bluedroid failed");
            return ret;
        }

        ret = esp_bluedroid_enable();
        if (ret)
        {
            ESP_LOGE(TAG, "Enable bluedroid failed");
            return ret;
        }

        // 注册回调
        static esp_blufi_callbacks_t callbacks = {
            .event_cb = HandleEvent,
            .negotiate_data_handler = HandleNegotiateData,
            .encrypt_func = HandleEncrypt,
            .decrypt_func = HandleDecrypt,
            .checksum_func = HandleChecksum};

        ret = esp_blufi_register_callbacks(&callbacks);
        if (ret)
        {
            ESP_LOGE(TAG, "Register callbacks failed");
            return ret;
        }

        ret = esp_ble_gap_register_callback(esp_blufi_gap_event_handler);
        if (ret)
        {
            return ret;
        }
        ret = esp_blufi_profile_init();
        if (ret)
        {
            ESP_LOGE(TAG, "Profile init failed");
            return ret;
        }
        initialized_ = true;
        // 注册 WiFi 和 IP 事件处理程序
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, [](void *arg, esp_event_base_t base, int32_t id, void *data)
                                                   { static_cast<Impl *>(arg)->OnWifiEvent(static_cast<wifi_event_t>(id), data); }, this));

        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, [](void *arg, esp_event_base_t base, int32_t id, void *data)
                                                   { static_cast<Impl *>(arg)->OnIpEvent(static_cast<ip_event_t>(id), data); }, this));

        return ESP_OK;
    }

    void OnWifiEvent(wifi_event_t event, void *event_data)
    {
        wifi_mode_t mode;

        switch (event)
        {
        case WIFI_EVENT_STA_START:
            sta_connecting_ = (esp_wifi_connect() == ESP_OK);
            RecordWifiConnInfo(-128, 255); // 使用无效值
            break;

        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *event_data_connected = (wifi_event_sta_connected_t *)event_data;
            wifi_connected_ = true;
            sta_connecting_ = false;
            memcpy(sta_bssid_, event_data_connected->bssid, 6);
            memcpy(sta_ssid_, event_data_connected->ssid, event_data_connected->ssid_len);
            sta_ssid_len_ = event_data_connected->ssid_len;
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            /* Only handle reconnection during connecting */
            if (!wifi_connected_ && sta_connecting_ && wifi_retry_++ < 5)
            {
                ESP_LOGI(TAG, "BLUFI WiFi starts reconnection");
                sta_connecting_ = (esp_wifi_connect() == ESP_OK);
                RecordWifiConnInfo(-128, 255); // 使用无效值
            }
            else
            {
                wifi_event_sta_disconnected_t *event_data_disconnected = (wifi_event_sta_disconnected_t *)event_data;
                RecordWifiConnInfo(event_data_disconnected->rssi, event_data_disconnected->reason);
                sta_connecting_ = false;
            }

            wifi_connected_ = false;
            wifi_got_ip_ = false;
            memset(sta_ssid_, 0, 32);
            memset(sta_bssid_, 0, 6);
            sta_ssid_len_ = 0;
            break;
        }

        case WIFI_EVENT_SCAN_DONE:
        {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count == 0)
            {
                ESP_LOGI(TAG, "Nothing AP found");
                break;
            }

            wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (!ap_list)
            {
                ESP_LOGE(TAG, "malloc error, ap_list is NULL");
                break;
            }

            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));
            esp_blufi_ap_record_t *blufi_ap_list = (esp_blufi_ap_record_t *)malloc(ap_count * sizeof(esp_blufi_ap_record_t));
            if (!blufi_ap_list)
            {
                free(ap_list);
                ESP_LOGE(TAG, "malloc error, blufi_ap_list is NULL");
                break;
            }

            for (int i = 0; i < ap_count; ++i)
            {
                blufi_ap_list[i].rssi = ap_list[i].rssi;
                memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
            }

            if (connected_)
            {
                esp_blufi_send_wifi_list(ap_count, blufi_ap_list);
            }
            else
            {
                ESP_LOGI(TAG, "BLUFI BLE is not connected yet");
            }

            esp_wifi_scan_stop();
            free(ap_list);
            free(blufi_ap_list);
            break;
        }

        case WIFI_EVENT_AP_START:
            esp_wifi_get_mode(&mode);
            if (connected_)
            {
                if (wifi_connected_)
                {
                    esp_blufi_extra_info_t info;
                    memset(&info, 0, sizeof(esp_blufi_extra_info_t));
                    memcpy(info.sta_bssid, sta_bssid_, 6);
                    info.sta_bssid_set = true;
                    info.sta_ssid = sta_ssid_;
                    info.sta_ssid_len = sta_ssid_len_;
                    esp_blufi_send_wifi_conn_report(mode, wifi_got_ip_ ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP, 0, &info);
                }
                else if (sta_connecting_)
                {
                    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, 0, &sta_conn_info_);
                }
                else
                {
                    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, &sta_conn_info_);
                }
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            BLUFI_INFO("station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            BLUFI_INFO("station " MACSTR " leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
            break;
        }
        default:
            break;
        }
    }

    void OnIpEvent(ip_event_t event, void *event_data)
    {
        wifi_mode_t mode;

        switch (event)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            wifi_got_ip_ = true;
            esp_wifi_get_mode(&mode);

            if (connected_)
            {
                esp_blufi_extra_info_t info;
                memset(&info, 0, sizeof(esp_blufi_extra_info_t));
                memcpy(info.sta_bssid, sta_bssid_, 6);
                info.sta_bssid_set = true;
                info.sta_ssid = sta_ssid_;
                info.sta_ssid_len = sta_ssid_len_;
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
            }
            break;
        }
        default:
            break;
        }
    }

    void OnEvent(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
    {
        ESP_LOGI(TAG, "BLUFI event: %d", event);
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        char blufi_device_name[23];
        snprintf(blufi_device_name, sizeof(blufi_device_name), "CGAI_%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        switch (event)
        {
        case ESP_BLUFI_EVENT_INIT_FINISH:
        {
            ESP_LOGI(TAG, "BLUFI init finish");
            // esp_blufi_adv_start();

            esp_ble_gap_set_device_name(blufi_device_name);
            esp_ble_gap_config_adv_data(&adv_params_);
        }
        break;

        case ESP_BLUFI_EVENT_DEINIT_FINISH:
            ESP_LOGI(TAG, "BLUFI deinit finish");
            break;

        case ESP_BLUFI_EVENT_BLE_CONNECT:
            ESP_LOGI(TAG, "BLUFI ble connect");
            connected_ = true;
            esp_blufi_adv_stop();
            security_->Initialize();
            break;

        case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        {
            ESP_LOGI(TAG, "BLUFI ble disconnect");
            connected_ = false;
            security_->Deinitialize();

            esp_ble_gap_set_device_name(blufi_device_name);
            esp_ble_gap_config_adv_data(&adv_params_);
        }
        break;

        case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
            /* TODO */
            break;
        case ESP_BLUFI_EVENT_GET_WIFI_STATUS:
        {
            wifi_mode_t mode;
            esp_blufi_extra_info_t info;

            esp_wifi_get_mode(&mode);

            if (wifi_connected_)
            {
                memset(&info, 0, sizeof(esp_blufi_extra_info_t));
                memcpy(info.sta_bssid, sta_bssid_, 6);
                info.sta_bssid_set = true;
                info.sta_ssid = sta_ssid_;
                info.sta_ssid_len = sta_ssid_len_;
                // 获取当前 SoftAP 连接数量，这里暂时使用0
                uint8_t softap_conn_num = 0;
                esp_blufi_send_wifi_conn_report(mode, wifi_got_ip_ ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP, softap_conn_num, &info);
            }
            else if (sta_connecting_)
            {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, 0, &sta_conn_info_);
            }
            else
            {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, &sta_conn_info_);
            }
            BLUFI_INFO("BLUFI get wifi status from AP\n");

            break;
        }

        case ESP_BLUFI_EVENT_RECV_STA_BSSID:
            memcpy(sta_config_.sta.bssid, param->sta_bssid.bssid, 6);
            sta_config_.sta.bssid_set = 1;
            ESP_LOGI(TAG, "Recv STA BSSID: " MACSTR, MAC2STR(sta_config_.sta.bssid));
            break;

        case ESP_BLUFI_EVENT_RECV_STA_SSID:
            config_->SetStaSsid(std::string((char *)param->sta_ssid.ssid,
                                           param->sta_ssid.ssid_len));
            // 保存到 sta_config_
            memset(sta_config_.sta.ssid, 0, sizeof(sta_config_.sta.ssid));
            memcpy(sta_config_.sta.ssid, param->sta_ssid.ssid, param->sta_ssid.ssid_len);
            sta_config_.sta.ssid[param->sta_ssid.ssid_len] = '\0';
            ESP_LOGI(TAG, "Recv STA SSID: %s", sta_config_.sta.ssid);
            break;

        case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
            config_->SetStaPassword(std::string((char *)param->sta_passwd.passwd,
                                           param->sta_passwd.passwd_len));
            // 保存到 sta_config_
            memset(sta_config_.sta.password, 0, sizeof(sta_config_.sta.password));
            memcpy(sta_config_.sta.password, param->sta_passwd.passwd, param->sta_passwd.passwd_len);
            sta_config_.sta.password[param->sta_passwd.passwd_len] = '\0';
            ESP_LOGI(TAG, "Recv STA PASSWORD: %s", sta_config_.sta.password);
            break;

        case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
            ESP_LOGI(TAG, "BLUFI connect to AP");
             // 设置认证模式阈值 - 这是关键修复
            if (strlen((const char*)sta_config_.sta.password)) {
                sta_config_.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            } else {
                sta_config_.sta.threshold.authmode = WIFI_AUTH_OPEN;
            }
            // 设置PMF参数
            sta_config_.sta.pmf_cfg.capable = true;
            sta_config_.sta.pmf_cfg.required = false;

            // 应用配置并连接
            esp_wifi_set_config(WIFI_IF_STA, &sta_config_);
            esp_wifi_disconnect();
            esp_wifi_connect();
            break;

        // 同样修改 SoftAP 相关的处理
        case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
            config_->SetSoftApSsid(std::string((char *)param->softap_ssid.ssid,
                                               param->softap_ssid.ssid_len));
            ESP_LOGI(TAG, "BLUFI recv softap ssid: %s", config_->GetSoftApSsid().c_str());

            // 保存到 ap_config_
            memset(ap_config_.ap.ssid, 0, sizeof(ap_config_.ap.ssid));
            memcpy(ap_config_.ap.ssid, param->softap_ssid.ssid, param->softap_ssid.ssid_len);
            ap_config_.ap.ssid_len = param->softap_ssid.ssid_len;
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
            config_->SetSoftApPassword(std::string((char *)param->softap_passwd.passwd,
                                                   param->softap_passwd.passwd_len));
            ESP_LOGI(TAG, "BLUFI recv softap password: %s", config_->GetSoftApPassword().c_str());

            // 保存到 ap_config_
            memset(ap_config_.ap.password, 0, sizeof(ap_config_.ap.password));
            memcpy(ap_config_.ap.password, param->softap_passwd.passwd, param->softap_passwd.passwd_len);
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
            if (param->softap_max_conn_num.max_conn_num <= 4)
            {
                config_->SetSoftApMaxConnNum(param->softap_max_conn_num.max_conn_num);
                ap_config_.ap.max_connection = param->softap_max_conn_num.max_conn_num;
            }
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
            if (param->softap_auth_mode.auth_mode < WIFI_AUTH_MAX)
            {
                config_->SetSoftApAuthMode(param->softap_auth_mode.auth_mode);
                ap_config_.ap.authmode = param->softap_auth_mode.auth_mode;
            }
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
            if (param->softap_channel.channel <= 13)
            {
                config_->SetSoftApChannel(param->softap_channel.channel);
                ap_config_.ap.channel = param->softap_channel.channel;
            }
            break;

        case ESP_BLUFI_EVENT_GET_WIFI_LIST:
        {
            ESP_LOGI(TAG, "BLUFI Get wifi list");
            wifi_scan_config_t scanConf = {
                .ssid = NULL,
                .bssid = NULL,
                .channel = 0,
                .show_hidden = false};
            esp_err_t ret = esp_wifi_scan_start(&scanConf, true);
            if (ret != ESP_OK)
            {
                esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
            }
            break;
        }

        case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
            ESP_LOGI(TAG, "BLUFI set wifi opmode %d", param->wifi_mode.op_mode);
            esp_wifi_set_mode(static_cast<wifi_mode_t>(param->wifi_mode.op_mode));
            break;

        case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
            ESP_LOGI(TAG, "BLUFI disconnect from AP");
            esp_wifi_disconnect();
            break;
        case ESP_BLUFI_EVENT_REPORT_ERROR:
            ESP_LOGI(TAG, "BLUFI report error, error code %d\n", param->report_error.state);
            esp_blufi_send_error_info(param->report_error.state);
            break;

        case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
            BLUFI_INFO("Recv Custom Data %" PRIu32 "\n", param->custom_data.data_len);
            ESP_LOG_BUFFER_HEX("Custom Data", param->custom_data.data, param->custom_data.data_len);
            break;
        case ESP_BLUFI_EVENT_RECV_USERNAME:
            /* Not handle currently */
            break;
        case ESP_BLUFI_EVENT_RECV_CA_CERT:
            /* Not handle currently */
            break;
        case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
            /* Not handle currently */
            break;
        case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
            /* Not handle currently */
            break;
        case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
            /* Not handle currently */
            break;
            ;
        case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
            /* Not handle currently */
            break;
        default:
            break;
        }
    }

    static void HandleEvent(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
    {
        // 使用单例获取实例
        if (auto *instance = GetInstance().impl_.get())
        {
            instance->OnEvent(event, param);
        }
    }

    static void HandleNegotiateData(uint8_t *data, int len, uint8_t **output_data,
                                    int *output_len, bool *need_free)
    {
        if (auto *instance = GetInstance().impl_.get())
        {
            instance->security_->HandleNegotiateData(data, len, output_data, output_len, need_free);
        }
    }

    static int HandleEncrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len)
    {
        if (auto *instance = GetInstance().impl_.get())
        {
            return instance->security_->Encrypt(iv8, crypt_data, crypt_len);
        }
        return -1;
    }

    static int HandleDecrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len)
    {
        if (auto *instance = GetInstance().impl_.get())
        {
            return instance->security_->Decrypt(iv8, crypt_data, crypt_len);
        }
        return -1;
    }

    static uint16_t HandleChecksum(uint8_t iv8, uint8_t *data, int len)
    {
        if (auto *instance = GetInstance().impl_.get())
        {
            return instance->security_->CalculateChecksum(iv8, data, len);
        }
        return 0;
    }

    bool initialized_{false};
    bool connected_{false};
    bool wifi_connected_{false};
    bool wifi_got_ip_{false};
    bool sta_connecting_{false};
    size_t sta_ssid_len_{0};
    uint8_t wifi_retry_{0};
    uint8_t sta_bssid_[6]{};
    uint8_t sta_ssid_[32]{};
    esp_blufi_extra_info_t sta_conn_info_{};
    std::unique_ptr<BlufiSecurity> security_;
    std::unique_ptr<BlufiConfig> config_;
    CustomDataCallback custom_data_cb_;
    esp_ble_adv_data_t adv_params_; // 添加广播参数
};

BlufiManager &BlufiManager::GetInstance()
{
    static BlufiManager instance;
    return instance;
}

BlufiManager::BlufiManager() : impl_(new Impl()) {}
BlufiManager::~BlufiManager() = default;

esp_err_t BlufiManager::Initialize() { return impl_->Initialize(); }
esp_err_t BlufiManager::Deinitialize()
{
    // return impl_->Deinitialize();
    return ESP_OK;
}
bool BlufiManager::IsConnected() const { return impl_->connected_; }

const std::string &BlufiManager::GetStaSsid() const { return impl_->config_->GetStaSsid(); }
const std::string &BlufiManager::GetStaPassword() const { return impl_->config_->GetStaPassword(); }
const std::string &BlufiManager::GetSoftApSsid() const { return impl_->config_->GetSoftApSsid(); }
const std::string &BlufiManager::GetSoftApPassword() const { return impl_->config_->GetSoftApPassword(); }
uint8_t BlufiManager::GetSoftApChannel() const { return impl_->config_->GetSoftApChannel(); }
uint8_t BlufiManager::GetSoftApMaxConnNum() const { return impl_->config_->GetSoftApMaxConnNum(); }
uint8_t BlufiManager::GetSoftApAuthMode() const { return impl_->config_->GetSoftApAuthMode(); }

void BlufiManager::SetCustomDataCallback(CustomDataCallback callback)
{
    impl_->custom_data_cb_ = std::move(callback);
}

esp_err_t BlufiManager::SendCustomData(const uint8_t *data, size_t len)
{
    if (!impl_->connected_)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_blufi_send_custom_data(const_cast<uint8_t *>(data), len);
}

// 添加缺失的方法实现
bool BlufiManager::IsWifiConnected() const { return impl_->wifi_connected_; }
bool BlufiManager::IsWifiGotIp() const { return impl_->wifi_got_ip_; }
bool BlufiManager::IsStaConnecting() const { return impl_->sta_connecting_; }
const uint8_t *BlufiManager::GetStaBssid() const { return impl_->sta_bssid_; }
uint8_t BlufiManager::GetStaSsidLen() const { return impl_->sta_ssid_len_; }
