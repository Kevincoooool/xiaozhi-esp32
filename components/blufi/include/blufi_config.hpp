#pragma once

#include <string>
#include <esp_wifi_types.h>

class BlufiConfig {
public:
    BlufiConfig();
    ~BlufiConfig();

    // STA 配置
    const std::string& GetStaSsid() const;
    void SetStaSsid(const std::string& ssid);
    const std::string& GetStaPassword() const;
    void SetStaPassword(const std::string& password);

    // SoftAP 配置
    const std::string& GetSoftApSsid() const;
    void SetSoftApSsid(const std::string& ssid);
    const std::string& GetSoftApPassword() const;
    void SetSoftApPassword(const std::string& password);
    uint8_t GetSoftApChannel() const;
    void SetSoftApChannel(uint8_t channel);
    uint8_t GetSoftApMaxConnNum() const;
    void SetSoftApMaxConnNum(uint8_t max_conn_num);
    wifi_auth_mode_t GetSoftApAuthMode() const;
    void SetSoftApAuthMode(wifi_auth_mode_t auth_mode);

private:
    std::string sta_ssid_;
    std::string sta_password_;
    std::string softap_ssid_;
    std::string softap_password_;
    uint8_t softap_channel_;
    uint8_t softap_max_conn_num_;
    wifi_auth_mode_t softap_auth_mode_;
};