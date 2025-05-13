#include "iot/thing.h"
#include "board.h"
#include "application.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_sleep.h>
#include <time.h>
#include <string>
#include <vector>
#include "display/display.h"
#define TAG "AlarmClock"

namespace iot {

// 闹钟结构体定义
struct Alarm {
    int id;                 // 闹钟ID
    bool enabled;           // 是否启用
    int hour;               // 小时(0-23)
    int minute;             // 分钟(0-59)
    int year;               // 年份(可选，0表示每天重复)
    int month;              // 月份(1-12，可选)
    int day;                // 日期(1-31，可选)
    bool notified;          // 是否已经提醒过
    std::string sound;      // 提醒声音
    time_t next_time;       // 下次触发时间戳
    
    Alarm() : id(0), enabled(false), hour(0), minute(0), 
              year(0), month(0), day(0), notified(false), 
              sound("default"), next_time(0) {}
              
    // 将闹钟转换为JSON字符串用于存储
    std::string ToJson() const {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
                 "{\"id\":%d,\"enabled\":%s,\"hour\":%d,\"minute\":%d,"
                 "\"year\":%d,\"month\":%d,\"day\":%d,\"notified\":%s,"
                 "\"sound\":\"%s\",\"next_time\":%lld}",
                 id, enabled ? "true" : "false", hour, minute,
                 year, month, day, notified ? "true" : "false",
                 sound.c_str(), (long long)next_time);
        return std::string(buffer);
    }
    
    // 从JSON字符串解析闹钟
    bool FromJson(const std::string& json) {
        // 简单解析，实际项目中可以使用cJSON库
        if (sscanf(json.c_str(), 
                  "{\"id\":%d,\"enabled\":%*[^,],\"hour\":%d,\"minute\":%d,"
                  "\"year\":%d,\"month\":%d,\"day\":%d,\"notified\":%*[^,],"
                  "\"sound\":\"%*[^\"]\",\"next_time\":%lld}",
                  &id, &hour, &minute, &year, &month, &day, (long long*)&next_time) >= 6) {
            // 解析布尔值
            enabled = (json.find("\"enabled\":true") != std::string::npos);
            notified = (json.find("\"notified\":true") != std::string::npos);
            
            // 解析声音
            size_t sound_start = json.find("\"sound\":\"") + 9;
            size_t sound_end = json.find("\"", sound_start);
            if (sound_start != std::string::npos && sound_end != std::string::npos) {
                sound = json.substr(sound_start, sound_end - sound_start);
            }
            return true;
        }
        return false;
    }
    
    // 计算下次触发时间
    void CalculateNextTime() {
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) {
            ESP_LOGE(TAG, "RTC不可用，无法计算下次触发时间");
            return;
        }
        
        // 获取当前时间
        struct tm current_time;
        rtc->GetTimeStruct(&current_time);
        time_t current_time_t = mktime(&current_time);
        
        // 设置闹钟时间
        struct tm alarm_time = current_time;
        alarm_time.tm_hour = hour;
        alarm_time.tm_min = minute;
        alarm_time.tm_sec = 0;
        
        // 如果设置了特定日期
        if (year > 0 && month > 0 && day > 0) {
            alarm_time.tm_year = year - 1900;
            alarm_time.tm_mon = month - 1;
            alarm_time.tm_mday = day;
        }
        
        // 计算时间戳
        time_t alarm_time_t = mktime(&alarm_time);
        
        // 如果闹钟时间已经过去，且没有设置特定日期，则设置为明天
        if (alarm_time_t <= current_time_t && (year == 0 || month == 0 || day == 0)) {
            alarm_time.tm_mday += 1;
            alarm_time_t = mktime(&alarm_time);
        }
        
        next_time = alarm_time_t;
        ESP_LOGI(TAG, "闹钟 #%d 下次触发时间: %s", id, ctime(&next_time));
    }
};

class AlarmClock : public Thing {
private:
    std::vector<Alarm> alarms_;      // 闹钟列表
    int next_alarm_id_ = 1;          // 下一个闹钟ID
    Settings* settings_ = nullptr;   // 设置存储
    
    // 检查当前时间是否与某个闹钟时间匹配
    bool IsAlarmTime(const Alarm& alarm) {
        if (!alarm.enabled) {
            return false;
        }
        
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) {
            ESP_LOGE(TAG, "RTC不可用");
            return false;
        }
        
        struct tm time_info;
        rtc->GetTimeStruct(&time_info);
        
        // 检查小时和分钟是否匹配
        if (time_info.tm_hour == alarm.hour && time_info.tm_min == alarm.minute) {
            // 如果设置了特定日期，还需要检查日期是否匹配
            if (alarm.year > 0 && alarm.month > 0 && alarm.day > 0) {
                if (time_info.tm_year + 1900 == alarm.year && 
                    time_info.tm_mon + 1 == alarm.month && 
                    time_info.tm_mday == alarm.day) {
                    return true;
                }
                return false;
            }
            return true;
        }
        
        return false;
    }
    
    // 设置RTC闹钟唤醒
    void SetupWakeupAlarm() {
        if (alarms_.empty()) {
            ESP_LOGI(TAG, "没有闹钟，不设置唤醒");
            Board::GetInstance().SetAlarmState(false, 0);
            return;
        }
        
        // 更新所有闹钟的下次触发时间
        for (auto& alarm : alarms_) {
            if (alarm.enabled) {
                alarm.CalculateNextTime();
            }
        }
        
        // 找出最近的闹钟
        time_t nearest_time = 0;
        int active_count = 0;
        
        for (const auto& alarm : alarms_) {
            if (alarm.enabled) {
                active_count++;
                if (nearest_time == 0 || alarm.next_time < nearest_time) {
                    nearest_time = alarm.next_time;
                }
            }
        }
        
        if (nearest_time == 0) {
            ESP_LOGI(TAG, "没有启用的闹钟，不设置唤醒");
            Board::GetInstance().SetAlarmState(false, 0);
            return;
        }
        
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) {
            ESP_LOGE(TAG, "RTC不可用，无法设置唤醒");
            return;
        }
        
        // 获取当前时间
        struct tm current_time;
        rtc->GetTimeStruct(&current_time);
        time_t current_time_t = mktime(&current_time);
        
        // 计算唤醒时间（秒）
        int64_t sleep_time_s = nearest_time - current_time_t;
        if (sleep_time_s <= 0) {
            ESP_LOGW(TAG, "计算的唤醒时间已过期，不设置唤醒");
            return;
        }
        
        // 转换为微秒
        int64_t sleep_time_us = sleep_time_s * 1000000LL;
        
        // 提前5分钟唤醒，确保系统有足够时间初始化
        int64_t wakeup_time_us = sleep_time_us > 5*60*1000000LL ? 
                                 sleep_time_us - 5*60*1000000LL : sleep_time_us;
        
        ESP_LOGI(TAG, "设置唤醒时间: %s (提前5分钟)", ctime(&nearest_time));
        esp_sleep_enable_timer_wakeup(wakeup_time_us);
        
        // 更新闹钟状态
        Board::GetInstance().SetAlarmState(true, active_count);
        
        // 保存闹钟设置
        SaveAlarms();
    }
    
    // 检查是否需要播放闹钟声音
    void CheckAndPlayAlarm() {
        bool alarm_triggered = false;
        
        for (auto& alarm : alarms_) {
            if (IsAlarmTime(alarm) && !alarm.notified) {
                ESP_LOGI(TAG, "闹钟 #%d 时间到，播放提醒声音", alarm.id);
                
                // 播放闹钟声音
                auto& app = Application::GetInstance();
                app.PlaySound(alarm.sound);
                
                // 显示闹钟提醒
                auto display = Board::GetInstance().GetDisplay();
                if (display) {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "闹钟 #%d 时间到！", alarm.id);
                    display->ShowNotification(buffer, 5000);
                }
                
                // 标记为已提醒
                alarm.notified = true;
                alarm_triggered = true;
                
                // 如果是特定日期的闹钟，触发后禁用
                if (alarm.year > 0 && alarm.month > 0 && alarm.day > 0) {
                    alarm.enabled = false;
                } else {
                    // 计算下一次触发时间
                    alarm.CalculateNextTime();
                }
            }
        }
        
        // 如果有闹钟触发，保存状态
        if (alarm_triggered) {
            SaveAlarms();
        }
    }
    
    // 保存闹钟设置到NVS
    void SaveAlarms() {
        if (!settings_) {
            settings_ = new Settings("alarms", true);
        }
        
        // 保存闹钟数量
        settings_->SetInt("count", alarms_.size());
        
        // 保存每个闹钟
        for (size_t i = 0; i < alarms_.size(); i++) {
            std::string key = "alarm_" + std::to_string(i);
            settings_->SetString(key, alarms_[i].ToJson());
        }
        
        // 保存下一个ID
        settings_->SetInt("next_id", next_alarm_id_);
    }
    
    // 从NVS加载闹钟设置
    void LoadAlarms() {
        if (!settings_) {
            settings_ = new Settings("alarms", true);
        }
        
        // 加载闹钟数量
        int count = settings_->GetInt("count", 0);
        
        // 加载每个闹钟
        alarms_.clear();
        for (int i = 0; i < count; i++) {
            std::string key = "alarm_" + std::to_string(i);
            std::string json = settings_->GetString(key);
            
            if (!json.empty()) {
                Alarm alarm;
                if (alarm.FromJson(json)) {
                    alarms_.push_back(alarm);
                }
            }
        }
        
        // 加载下一个ID
        next_alarm_id_ = settings_->GetInt("next_id", 1);
        
        // 重新计算所有闹钟的下次触发时间
        for (auto& alarm : alarms_) {
            if (alarm.enabled) {
                alarm.CalculateNextTime();
            }
        }
        
        // 更新Board的闹钟状态
        int active_count = 0;
        for (const auto& alarm : alarms_) {
            if (alarm.enabled) {
                active_count++;
            }
        }
        Board::GetInstance().SetAlarmState(active_count > 0, active_count);
    }
    
    // 通用设置闹钟方法
    Alarm* SetAlarm(int hour, int minute, int year = 0, int month = 0, int day = 0, 
                   bool enabled = true, const std::string& sound = "default") {
        // 验证时间有效性
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            ESP_LOGE(TAG, "无效的闹钟时间: %d:%d", hour, minute);
            return nullptr;
        }
        
        // 如果指定了日期，验证日期有效性
        if (year > 0 || month > 0 || day > 0) {
            if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
                ESP_LOGE(TAG, "无效的闹钟日期: %d-%d-%d", year, month, day);
                return nullptr;
            }
        }
        
        // 创建新闹钟
        Alarm alarm;
        alarm.id = next_alarm_id_++;
        alarm.enabled = enabled;
        alarm.hour = hour;
        alarm.minute = minute;
        alarm.year = year;
        alarm.month = month;
        alarm.day = day;
        alarm.notified = false;
        alarm.sound = sound;
        
        // 计算下次触发时间
        alarm.CalculateNextTime();
        
        // 添加到闹钟列表
        alarms_.push_back(alarm);
        
        // 保存闹钟设置
        SaveAlarms();
        
        // 设置唤醒
        if (enabled) {
            SetupWakeupAlarm();
        }
        
        return &alarms_.back();
    }

public:
    AlarmClock() : Thing("AlarmClock", "闹钟，可设置指定时间唤醒并播放提醒") {
        // 加载闹钟设置
        LoadAlarms();
        
        // 定义设备的属性
        properties_.AddBooleanProperty("enabled", "是否有启用的闹钟", [this]() -> bool {
            for (const auto& alarm : alarms_) {
                if (alarm.enabled) {
                    return true;
                }
            }
            return false;
        });
        
        properties_.AddNumberProperty("count", "闹钟总数", [this]() -> int {
            return alarms_.size();
        });
        
        properties_.AddNumberProperty("active_count", "启用的闹钟数", [this]() -> int {
            int count = 0;
            for (const auto& alarm : alarms_) {
                if (alarm.enabled) {
                    count++;
                }
            }
            return count;
        });
        
        // 定义设备可以被远程执行的指令
        // methods_.AddMethod("SetAlarmFromNow", "从当前时间开始设置闹钟", ParameterList({
        //     Parameter("minutes", "多少分钟后提醒", kValueTypeNumber, true),
        //     Parameter("enabled", "是否启用", kValueTypeBoolean, true),
        //     Parameter("sound", "提醒声音", kValueTypeString, false)
        // }), [this](const ParameterList& parameters) {
        //     int minutes_from_now = static_cast<int>(parameters["minutes"].number());
        //     bool enabled = parameters["enabled"].boolean();
        //     std::string sound = parameters.HasParameter("sound") ? 
        //                        parameters["sound"].string() : "default";
            
        //     // 验证时间有效性
        //     if (minutes_from_now <= 0) {
        //         ESP_LOGE(TAG, "无效的闹钟时间: 必须大于0分钟");
        //         return;
        //     }
            
        //     auto rtc = Board::GetInstance().GetRTC();
        //     if (!rtc) {
        //         ESP_LOGE(TAG, "RTC不可用，无法设置闹钟");
        //         return;
        //     }
            
        //     // 获取当前时间
        //     struct tm current_time;
        //     rtc->GetTimeStruct(&current_time);
            
        //     // 计算闹钟时间
        //     time_t current_time_t = mktime(&current_time);
        //     time_t alarm_time_t = current_time_t + minutes_from_now * 60;
            
        //     // 转换回tm结构
        //     struct tm alarm_time;
        //     localtime_r(&alarm_time_t, &alarm_time);
            
        //     // 设置闹钟
        //     Alarm* alarm = SetAlarm(alarm_time.tm_hour, alarm_time.tm_min, 0, 0, 0, enabled, sound);
            
        //     if (alarm) {
        //         ESP_LOGI(TAG, "闹钟 #%d 设置为 %d 分钟后 (%02d:%02d), 状态: %s", 
        //                  alarm->id, minutes_from_now, alarm->hour, alarm->minute, 
        //                  enabled ? "启用" : "禁用");
        //     }
        // });
        
        // 通用设置闹钟方法
        methods_.AddMethod("SetAlarm", "设置闹钟", ParameterList({
            Parameter("hour", "小时(0-23)", kValueTypeNumber, true),
            Parameter("minute", "分钟(0-59)", kValueTypeNumber, true),
            Parameter("year", "年份(可选，0表示每天重复)", kValueTypeNumber, false),
            Parameter("month", "月份(1-12，可选)", kValueTypeNumber, false),
            Parameter("day", "日期(1-31，可选)", kValueTypeNumber, false),
            Parameter("enabled", "是否启用", kValueTypeBoolean, true)
        }), [this](const ParameterList& parameters) {
            int hour = static_cast<int>(parameters["hour"].number());
            int minute = static_cast<int>(parameters["minute"].number());
            int year =  static_cast<int>(parameters["year"].number()) ;
            int month = static_cast<int>(parameters["month"].number()) ;
            int day =  static_cast<int>(parameters["day"].number());
            bool enabled = parameters["enabled"].boolean();
            std::string sound =  "default";
            
            Alarm* alarm = SetAlarm(hour, minute, year, month, day, enabled, sound);
            
            if (alarm) {
                if (year > 0 && month > 0 && day > 0) {
                    ESP_LOGI(TAG, "闹钟 #%d 设置为 %04d-%02d-%02d %02d:%02d, 状态: %s", 
                             alarm->id, year, month, day, hour, minute, 
                             enabled ? "启用" : "禁用");
                } else {
                    ESP_LOGI(TAG, "闹钟 #%d 设置为每天 %02d:%02d, 状态: %s", 
                             alarm->id, hour, minute, enabled ? "启用" : "禁用");
                }
            }
        });
        
        // 启用/禁用指定闹钟
        methods_.AddMethod("EnableAlarm", "启用或禁用指定闹钟", ParameterList({
            Parameter("id", "闹钟ID", kValueTypeNumber, true),
            Parameter("enabled", "是否启用", kValueTypeBoolean, true)
        }), [this](const ParameterList& parameters) {
            int id = static_cast<int>(parameters["id"].number());
            bool enabled = parameters["enabled"].boolean();
            
            for (auto& alarm : alarms_) {
                if (alarm.id == id) {
                    alarm.enabled = enabled;
                    ESP_LOGI(TAG, "闹钟 #%d 状态: %s", id, enabled ? "启用" : "禁用");
                    
                    // 如果启用，重新计算下次触发时间
                    if (enabled) {
                        alarm.CalculateNextTime();
                    }
                    
                    // 更新唤醒设置
                    SetupWakeupAlarm();
                    return;
                }
            }
            
            ESP_LOGE(TAG, "未找到ID为 %d 的闹钟", id);
        });
        
        // 删除指定闹钟
        methods_.AddMethod("DeleteAlarm", "删除指定闹钟", ParameterList({
            Parameter("id", "闹钟ID", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            int id = static_cast<int>(parameters["id"].number());
            
            for (auto it = alarms_.begin(); it != alarms_.end(); ++it) {
                if (it->id == id) {
                    ESP_LOGI(TAG, "删除闹钟 #%d", id);
                    alarms_.erase(it);
                    
                    // 更新唤醒设置
                    SetupWakeupAlarm();
                    return;
                }
            }
            
            ESP_LOGE(TAG, "未找到ID为 %d 的闹钟", id);
        });
        
        // 获取所有闹钟信息
        // methods_.AddMethod("GetAlarms", "获取所有闹钟信息", ParameterList(), 
        // [this](const ParameterList& parameters) {
        //     ESP_LOGI(TAG, "当前共有 %zu 个闹钟:", alarms_.size());
            
        //     for (const auto& alarm : alarms_) {
        //         if (alarm.year > 0 && alarm.month > 0 && alarm.day > 0) {
        //             ESP_LOGI(TAG, "闹钟 #%d: %04d-%02d-%02d %02d:%02d, 状态: %s, 声音: %s", 
        //                      alarm.id, alarm.year, alarm.month, alarm.day,
        //                      alarm.hour, alarm.minute, 
        //                      alarm.enabled ? "启用" : "禁用", 
        //                      alarm.sound.c_str());
        //         } else {
        //             ESP_LOGI(TAG, "闹钟 #%d: 每天 %02d:%02d, 状态: %s, 声音: %s", 
        //                      alarm.id, alarm.hour, alarm.minute, 
        //                      alarm.enabled ? "启用" : "禁用", 
        //                      alarm.sound.c_str());
        //         }
        //     }
        // });
        
        // 清空所有闹钟
        methods_.AddMethod("ClearAlarms", "清空所有闹钟", ParameterList(), 
        [this](const ParameterList& parameters) {
            ESP_LOGI(TAG, "清空所有闹钟");
            alarms_.clear();
            
            // 更新唤醒设置
            SetupWakeupAlarm();
        });
        
        // 启动定时检查任务
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto self = static_cast<AlarmClock*>(arg);
                self->CheckAndPlayAlarm();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "alarm_check",
            .skip_unhandled_events = false,
        };
        
        esp_timer_handle_t timer;
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 60 * 1000000)); // 每分钟检查一次
        
        // 检查是否是RTC唤醒
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
            ESP_LOGI(TAG, "系统从定时器唤醒，可能是闹钟触发");
            
            // 检查是否有即将触发的闹钟
            time_t now = time(NULL);
            for (auto& alarm : alarms_) {
                if (alarm.enabled && !alarm.notified) {
                    // 如果距离闹钟时间不到5分钟，立即检查
                    if (alarm.next_time - now < 5 * 60) {
                        ESP_LOGI(TAG, "闹钟 #%d 即将触发，立即检查", alarm.id);
                        CheckAndPlayAlarm();
                        break;
                    }
                }
            }
        }
    }
    
    ~AlarmClock() {
        if (settings_) {
            delete settings_;
        }
    }
};

} // namespace iot

DECLARE_THING(AlarmClock);