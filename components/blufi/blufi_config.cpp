#include "blufi_config.hpp"

BlufiConfig::BlufiConfig()
    : softap_channel_(1)
    , softap_max_conn_num_(4)
    , softap_auth_mode_(WIFI_AUTH_OPEN) {
}

BlufiConfig::~BlufiConfig() = default;

const std::string& BlufiConfig::GetStaSsid() const {
    return sta_ssid_;
}

void BlufiConfig::SetStaSsid(const std::string& ssid) {
    sta_ssid_ = ssid;
}

const std::string& BlufiConfig::GetStaPassword() const {
    return sta_password_;
}

void BlufiConfig::SetStaPassword(const std::string& password) {
    sta_password_ = password;
}

const std::string& BlufiConfig::GetSoftApSsid() const {
    return softap_ssid_;
}

void BlufiConfig::SetSoftApSsid(const std::string& ssid) {
    softap_ssid_ = ssid;
}

const std::string& BlufiConfig::GetSoftApPassword() const {
    return softap_password_;
}

void BlufiConfig::SetSoftApPassword(const std::string& password) {
    softap_password_ = password;
}

uint8_t BlufiConfig::GetSoftApChannel() const {
    return softap_channel_;
}

void BlufiConfig::SetSoftApChannel(uint8_t channel) {
    softap_channel_ = channel;
}

uint8_t BlufiConfig::GetSoftApMaxConnNum() const {
    return softap_max_conn_num_;
}

void BlufiConfig::SetSoftApMaxConnNum(uint8_t max_conn_num) {
    softap_max_conn_num_ = max_conn_num;
}

wifi_auth_mode_t BlufiConfig::GetSoftApAuthMode() const {
    return softap_auth_mode_;
}

void BlufiConfig::SetSoftApAuthMode(wifi_auth_mode_t auth_mode) {
    softap_auth_mode_ = auth_mode;
}