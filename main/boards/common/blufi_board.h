#ifndef BLUFI_BOARD_H
#define BLUFI_BOARD_H

#include "board.h"
#include <esp_wifi.h>
#include <esp_blufi_api.h>

class BlufiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    bool wifi_connected_ = false;
    std::string wifi_ssid_;
    std::string wifi_ip_;
    int8_t wifi_rssi_ = 0;
    uint8_t wifi_channel_ = 0;
    esp_timer_handle_t config_mode_timer_ = nullptr;  // 添加定时器句柄
    std::string post_data_;
    BlufiBoard();
    void EnterWifiConfigMode();
    void StartBlufiConfig();
    virtual std::string GetBoardJson() override;
    bool FetchApiUrl();
    // WiFi event handlers
    static void WiFiEventHandler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data);

public:
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual Http* CreateHttp() override;
    virtual WebSocket* CreateWebSocket() override;
    virtual Mqtt* CreateMqtt() override;
    virtual Udp* CreateUdp() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
     // 添加获取WiFi连接状态的函数
    virtual bool IsWifiConnected() const { return wifi_connected_; }
    // 从NVS加载和保存WiFi信息
    void LoadWifiCredentials();
    void SaveWifiCredentials(const std::string& ssid, const std::string& password);
    virtual void SetConfigModeReboot(bool value);
    virtual bool IsConfigModeReboot();
};

#endif // BLUFI_BOARD_H
