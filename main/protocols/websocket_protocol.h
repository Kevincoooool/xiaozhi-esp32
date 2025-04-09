#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    void Start() override;
    void SendAudio(const std::vector<uint8_t>& data) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

    bool IsConnected() const { return is_connected_; }
private:
    EventGroupHandle_t event_group_handle_;
    WebSocket* websocket_ = nullptr;
    bool is_connected_ = false;  // 添加连接状态标志

    void ParseServerHello(const cJSON* root);
    void SendText(const std::string& text) override;
};

#endif
