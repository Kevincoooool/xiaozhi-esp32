#include "blufi_manager.hpp"
#include "blufi_security.hpp"
#include "blufi_config.hpp"
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <cstring>  // 添加此行
#include "esp_random.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_blufi.h"
static const char* TAG = "BlufiManager";

class BlufiManager::Impl {
public:
    Impl() 
        : initialized_(false)
        , connected_(false)
        , security_(std::make_unique<BlufiSecurity>())
        , config_(std::make_unique<BlufiConfig>()) {
        // 初始化广播参数
        memset(&adv_params_, 0, sizeof(adv_params_));
        adv_params_.adv_int_min = 0x20;
        adv_params_.adv_int_max = 0x40;
        adv_params_.adv_type = ADV_TYPE_IND;
        adv_params_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        adv_params_.channel_map = ADV_CHNL_ALL;
        adv_params_.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    }

    esp_err_t Initialize() {
        if (initialized_) {
            return ESP_OK;
        }

        esp_err_t ret;
        #if CONFIG_IDF_TARGET_ESP32
            ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
        #endif

        // 初始化蓝牙控制器
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        if (ret) {
            ESP_LOGE(TAG, "Initialize controller failed");
            return ret;
        }

        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (ret) {
            ESP_LOGE(TAG, "Enable controller failed");
            return ret;
        }

        ret = esp_bluedroid_init();
        if (ret) {
            ESP_LOGE(TAG, "Initialize bluedroid failed");
            return ret;
        }

        ret = esp_bluedroid_enable();
        if (ret) {
            ESP_LOGE(TAG, "Enable bluedroid failed");
            return ret;
        }

        // 注册回调
        esp_blufi_callbacks_t callbacks = {
            .event_cb = HandleEvent,
            .negotiate_data_handler = HandleNegotiateData,
            .encrypt_func = HandleEncrypt,
            .decrypt_func = HandleDecrypt,
            .checksum_func = HandleChecksum
        };

        ret = esp_blufi_register_callbacks(&callbacks);
        if (ret) {
            ESP_LOGE(TAG, "Register callbacks failed");
            return ret;
        }

        // ret = esp_blufi_profile_init();
        // if (ret) {
        //     ESP_LOGE(TAG, "Profile init failed");
        //     return ret;
        // }
        ret = esp_ble_gap_register_callback(esp_blufi_gap_event_handler);
        if(ret){
            return ret;
        }
        ret = esp_blufi_profile_init();
        if (ret) {
            ESP_LOGE(TAG, "Profile init failed");
            return ret;
        }
        initialized_ = true;
        return ESP_OK;
    }

    esp_err_t Deinitialize() {
        if (!initialized_) {
            return ESP_OK;
        }

        esp_blufi_profile_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();

        initialized_ = false;
        return ESP_OK;
    }

    static void HandleEvent(esp_blufi_cb_event_t event, esp_blufi_cb_param_t* param) {
        // 使用单例获取实例
        if (auto* instance = GetInstance().impl_.get()) {
            instance->OnEvent(event, param);
        }
    }

    static void HandleNegotiateData(uint8_t* data, int len, uint8_t** output_data, 
        int* output_len, bool* need_free) {
        if (auto* instance = GetInstance().impl_.get()) {
            instance->security_->HandleNegotiateData(data, len, output_data, output_len, need_free);
        }
    }

    static int HandleEncrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len) {
        if (auto* instance = GetInstance().impl_.get()) {
            return instance->security_->Encrypt(iv8, crypt_data, crypt_len);
        }
        return -1;
    }

    static int HandleDecrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len) {
        if (auto* instance = GetInstance().impl_.get()) {
            return instance->security_->Decrypt(iv8, crypt_data, crypt_len);
        }
        return -1;
    }

    static uint16_t HandleChecksum(uint8_t iv8, uint8_t* data, int len) {
        if (auto* instance = GetInstance().impl_.get()) {
            return instance->security_->CalculateChecksum(iv8, data, len);
        }
        return 0;
    }

    void OnEvent(esp_blufi_cb_event_t event, esp_blufi_cb_param_t* param) {
        switch (event) {
        case ESP_BLUFI_EVENT_INIT_FINISH:
            ESP_LOGI(TAG, "BLUFI init finish");
            esp_blufi_adv_start();
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
            ESP_LOGI(TAG, "BLUFI ble disconnect");
            connected_ = false;
            security_->Deinitialize();
            esp_blufi_adv_start();
            break;

        case ESP_BLUFI_EVENT_RECV_STA_SSID:
            config_->SetStaSsid(std::string((char*)param->sta_ssid.ssid, 
                param->sta_ssid.ssid_len));
            ESP_LOGI(TAG, "BLUFI recv sta ssid: %s", config_->GetStaSsid().c_str());
            break;

        case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
            config_->SetStaPassword(std::string((char*)param->sta_passwd.passwd, 
                param->sta_passwd.passwd_len));
            ESP_LOGI(TAG, "BLUFI recv sta password: %s", config_->GetStaPassword().c_str());
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
            config_->SetSoftApSsid(std::string((char*)param->softap_ssid.ssid, 
                param->softap_ssid.ssid_len));
            ESP_LOGI(TAG, "BLUFI recv softap ssid: %s", config_->GetSoftApSsid().c_str());
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
            config_->SetSoftApPassword(std::string((char*)param->softap_passwd.passwd, 
                param->softap_passwd.passwd_len));
            ESP_LOGI(TAG, "BLUFI recv softap password: %s", config_->GetSoftApPassword().c_str());
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
            if (param->softap_max_conn_num.max_conn_num <= 4) {
                config_->SetSoftApMaxConnNum(param->softap_max_conn_num.max_conn_num);
            }
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
            if (param->softap_auth_mode.auth_mode < WIFI_AUTH_MAX) {
                config_->SetSoftApAuthMode(param->softap_auth_mode.auth_mode);
            }
            break;

        case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
            if (param->softap_channel.channel <= 13) {
                config_->SetSoftApChannel(param->softap_channel.channel);
            }
            break;

        case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
            if (custom_data_cb_) {
                custom_data_cb_(param->custom_data.data, param->custom_data.data_len);
            }
            break;

        default:
            break;
        }
    }

    bool initialized_{false};
    bool connected_{false};
    std::unique_ptr<BlufiSecurity> security_;
    std::unique_ptr<BlufiConfig> config_;
    CustomDataCallback custom_data_cb_;
    esp_ble_adv_params_t adv_params_;  // 添加广播参数
};

BlufiManager& BlufiManager::GetInstance() {
    static BlufiManager instance;
    return instance;
}

BlufiManager::BlufiManager() : impl_(new Impl()) {}
BlufiManager::~BlufiManager() = default;

esp_err_t BlufiManager::Initialize() { return impl_->Initialize(); }
esp_err_t BlufiManager::Deinitialize() { return impl_->Deinitialize(); }
bool BlufiManager::IsConnected() const { return impl_->connected_; }

const std::string& BlufiManager::GetStaSsid() const { return impl_->config_->GetStaSsid(); }
const std::string& BlufiManager::GetStaPassword() const { return impl_->config_->GetStaPassword(); }
const std::string& BlufiManager::GetSoftApSsid() const { return impl_->config_->GetSoftApSsid(); }
const std::string& BlufiManager::GetSoftApPassword() const { return impl_->config_->GetSoftApPassword(); }
uint8_t BlufiManager::GetSoftApChannel() const { return impl_->config_->GetSoftApChannel(); }
uint8_t BlufiManager::GetSoftApMaxConnNum() const { return impl_->config_->GetSoftApMaxConnNum(); }
uint8_t BlufiManager::GetSoftApAuthMode() const { return impl_->config_->GetSoftApAuthMode(); }

void BlufiManager::SetCustomDataCallback(CustomDataCallback callback) {
    impl_->custom_data_cb_ = std::move(callback);
}

esp_err_t BlufiManager::SendCustomData(const uint8_t* data, size_t len) {
    if (!impl_->connected_) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_blufi_send_custom_data(const_cast<uint8_t*>(data), len);
}