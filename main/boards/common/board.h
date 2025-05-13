#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <mqtt.h>
#include <udp.h>
#include <string>

#include "led/led.h"
#include "backlight.h"
#include "pcf85063.h"
void* create_board();
class AudioCodec;
class Display;
class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作
    virtual std::string GetBoardJson() = 0;

protected:
    Board();
    std::string GenerateUuid();

    // 软件生成的设备唯一标识
    std::string uuid_;
    bool has_alarm_ = false;  // 是否有闹钟设置
    int alarm_count_ = 0;     // 当前设置的闹钟数量
public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual Display* GetDisplay();
    virtual Http* CreateHttp() = 0;
    virtual WebSocket* CreateWebSocket() = 0;
    virtual Mqtt* CreateMqtt() = 0;
    virtual Udp* CreateUdp() = 0;
    virtual void StartNetwork() = 0;
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual std::string GetJson();
    virtual void SetPowerSaveMode(bool enabled) = 0;
    // 同步系统时间到RTC芯片
    virtual void SyncTimeToRTC(time_t time) {}
    // 获取RTC对象
    virtual PCF85063* GetRTC() { return nullptr; }
    // 新增：进入深度休眠模式
    virtual void EnterDeepSleep(int64_t sleep_time_us = 0) {}
    // 新增：检查是否有闹钟需要唤醒
    virtual bool CheckAlarmWakeup() { return false; }
     // 获取闹钟状态
    virtual bool HasAlarm() { return has_alarm_; }
    
    // 获取闹钟数量
    virtual int GetAlarmCount() { return alarm_count_; }
    
    // 设置闹钟状态
    virtual void SetAlarmState(bool has_alarm, int count = 1) {
        has_alarm_ = has_alarm;
        alarm_count_ = count;
    }
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H
