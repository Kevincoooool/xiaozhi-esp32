#ifndef WAKE_WORD_DETECT_H
#define WAKE_WORD_DETECT_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_afe_sr_models.h>
#include <esp_nsn_models.h>

#include <list>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_process_sdkconfig.h"
class WakeWordDetect {
public:
    WakeWordDetect();
    ~WakeWordDetect();

    void Initialize(int channels, bool reference);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void StartDetection();
    void StopDetection();
    bool IsDetectionRunning();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }
    void SetOfflineMode(bool enabled);
    bool IsOfflineMode() const { return offline_mode_; }
    void OnCommandDetected(std::function<void(const std::string& command, int command_id)> callback) {
        command_detected_callback_ = callback;
    }
private:
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    char* wakenet_model_ = NULL;
    std::vector<std::string> wake_words_;
    std::vector<int16_t> input_buffer_;
    EventGroupHandle_t event_group_;
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    int channels_;
    bool reference_;
    std::string last_detected_wake_word_;

    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t wake_word_encode_task_buffer_;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::list<std::vector<int16_t>> wake_word_pcm_;
    std::list<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;
    bool offline_mode_ = false;
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* model_data_ = nullptr;
    std::function<void(const std::string& command, int command_id)> command_detected_callback_;    void StoreWakeWordData(uint16_t* data, size_t size);
    void AudioDetectionTask();
};

#endif
