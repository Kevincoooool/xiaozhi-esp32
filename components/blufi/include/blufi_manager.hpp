#pragma once

#include <memory>
#include <string>
#include <functional>
#include <esp_err.h>
#include <esp_blufi_api.h>
#include "esp_gap_ble_api.h"

class BlufiManager {
public:
    // 单例模式
    static BlufiManager& GetInstance();

    // 初始化和反初始化
    esp_err_t Initialize();
    esp_err_t Deinitialize();

    // 获取配置信息
    const std::string& GetStaSsid() const;
    const std::string& GetStaPassword() const;
    const std::string& GetSoftApSsid() const;
    const std::string& GetSoftApPassword() const;
    uint8_t GetSoftApChannel() const;
    uint8_t GetSoftApMaxConnNum() const;
    uint8_t GetSoftApAuthMode() const;

    // 自定义数据回调
    using CustomDataCallback = std::function<void(const uint8_t* data, size_t len)>;
    void SetCustomDataCallback(CustomDataCallback callback);

    // 发送自定义数据
    esp_err_t SendCustomData(const uint8_t* data, size_t len);

    // 获取连接状态
    bool IsConnected() const;

private:
    BlufiManager();
    ~BlufiManager();
    BlufiManager(const BlufiManager&) = delete;
    BlufiManager& operator=(const BlufiManager&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};