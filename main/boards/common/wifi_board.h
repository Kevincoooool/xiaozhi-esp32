#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"

// 添加配网方式选择宏


class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    enum {
        WIFI_CONFIG_MODE_AP = 0,
        WIFI_CONFIG_MODE_BLUFI,
    } ;
#define WIFI_CONFIG_MODE WIFI_CONFIG_MODE_BLUFI  // 在这里切换配网模式

#if WIFI_CONFIG_MODE == WIFI_CONFIG_MODE_BLUFI
    void StartBlufiConfig();  // 添加 BluFi 配网方法
#endif

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
};

#endif // WIFI_BOARD_H