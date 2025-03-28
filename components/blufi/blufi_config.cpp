#include "blufi_config.hpp"
#include <algorithm>

void BlufiConfig::SetStaSsid(const std::string& ssid) {
    sta_ssid_ = ssid.substr(0, 32);
}

void BlufiConfig::SetStaPassword(const std::string& password) {
    sta_password_ = password.substr(0, MAX_PASSWORD_LEN);
}

void BlufiConfig::SetSoftApSsid(const std::string& ssid) {
    softap_ssid_ = ssid.substr(0, 32);
}

void BlufiConfig::SetSoftApPassword(const std::string& password) {
    softap_password_ = password.substr(0, MAX_PASSWORD_LEN);
}

void BlufiConfig::SetStaBssid(const uint8_t* bssid) {
    std::copy_n(bssid, BSSID_LEN, sta_bssid_.begin());
}

void BlufiConfig::SetSoftApChannel(uint8_t channel) {
    softap_channel_ = channel;
}

void BlufiConfig::SetSoftApMaxConnNum(uint8_t num) {
    softap_max_conn_num_ = num;
}

void BlufiConfig::SetSoftApAuthMode(uint8_t mode) {
    softap_auth_mode_ = mode;
}