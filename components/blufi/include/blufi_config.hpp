#pragma once

#include <string>
#include <array>
#include <cstdint>

class BlufiConfig {
public:
    static constexpr size_t MAX_PASSWORD_LEN = 64;
    static constexpr size_t BSSID_LEN = 6;

    void SetStaSsid(const std::string& ssid);
    void SetStaPassword(const std::string& password);
    void SetSoftApSsid(const std::string& ssid);
    void SetSoftApPassword(const std::string& password);
    void SetStaBssid(const uint8_t* bssid);
    void SetSoftApChannel(uint8_t channel);
    void SetSoftApMaxConnNum(uint8_t num);
    void SetSoftApAuthMode(uint8_t mode);

    const std::string& GetStaSsid() const { return sta_ssid_; }
    const std::string& GetStaPassword() const { return sta_password_; }
    const std::string& GetSoftApSsid() const { return softap_ssid_; }
    const std::string& GetSoftApPassword() const { return softap_password_; }
    const std::array<uint8_t, BSSID_LEN>& GetStaBssid() const { return sta_bssid_; }
    uint8_t GetSoftApChannel() const { return softap_channel_; }
    uint8_t GetSoftApMaxConnNum() const { return softap_max_conn_num_; }
    uint8_t GetSoftApAuthMode() const { return softap_auth_mode_; }

private:
    std::string sta_ssid_;
    std::string sta_password_;
    std::string softap_ssid_;
    std::string softap_password_;
    std::array<uint8_t, BSSID_LEN> sta_bssid_;
    uint8_t softap_channel_{1};
    uint8_t softap_max_conn_num_{4};
    uint8_t softap_auth_mode_{0};
};