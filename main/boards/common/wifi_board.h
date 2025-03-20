#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    std::string api_url_;  // 存储API地址
    std::string ota_url_;  // 存储API地址
    std::string post_data_;
    
    // 新增获取API地址的函数
    bool FetchApiUrl();
    WifiBoard();
    void EnterWifiConfigMode();
    virtual std::string GetBoardJson() override;

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
    virtual const std::string& GetOtaUrl() override { return ota_url_; }
};

#endif // WIFI_BOARD_H
