#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <settings.h>

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    bool Start() override;
    void SendAudio(const AudioStreamPacket& packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;
// 处理闹钟设置请求
    void HandleClockCommand(const cJSON* root);
    // 处理服务器返回的任务信息
    void HandleTaskResponse(const cJSON* root);
private:
    EventGroupHandle_t event_group_handle_;
    WebSocket* websocket_ = nullptr;
    int version_ = 1;

    void ParseServerHello(const cJSON* root);
    bool SendText(const std::string& text) override;
    void HandleSetClock(const cJSON* root);
    void HandleEndClock(const cJSON* root);
    esp_timer_handle_t clock_timer_ = nullptr;
    // ... existing code ...
    std::string session_id_; // 存储当前会话ID
    void CheckAlarmTimer();
    void StartAlarmTimer(int64_t end_timer);
    void StopAlarmTimer();
    esp_timer_handle_t alarm_timer_ = nullptr;
    int64_t alarm_end_time_ = 0;
};

#endif
