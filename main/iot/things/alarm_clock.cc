#include "iot/thing.h"
#include "board.h"

#include <esp_log.h>
#include <time.h>
#include <esp_sleep.h>

#define TAG "AlarmClock"

namespace iot {

// 闹钟IoT组件，使用PCF85063 RTC芯片的闹钟功能
class AlarmClock : public Thing {
private:
    bool alarm_enabled_ = false;
    struct tm alarm_time_ = {};
    bool alarm_triggered_ = false;

public:
    AlarmClock() : Thing("AlarmClock", "闹钟管理") {
        // 定义设备的属性
        properties_.AddBooleanProperty("enabled", "闹钟是否启用", [this]() -> bool {
            auto rtc = Board::GetInstance().GetRTC();
            if (!rtc) return false;
            
            // 使用 IsAlarmEnabled 方法检查闹钟是否启用
            alarm_enabled_ = rtc->IsAlarmEnabled();
            return alarm_enabled_;
        });

        properties_.AddBooleanProperty("triggered", "闹钟是否已触发", [this]() -> bool {
            auto rtc = Board::GetInstance().GetRTC();
            if (!rtc) return false;
            
            alarm_triggered_ = rtc->IsAlarmTriggered();
            return alarm_triggered_;
        });

        properties_.AddStringProperty("time", "已设置的闹钟时间", [this]() -> std::string {
            auto rtc = Board::GetInstance().GetRTC();
            if (!rtc) return "未设置";
            
            if (rtc->GetAlarmTime(&alarm_time_)) {
                char time_str[64];
                const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
                snprintf(time_str, sizeof(time_str), "%04d年%02d月%02d日 %s %02d:%02d:%02d", 
                         alarm_time_.tm_year + 1900,  // 年份需要加上1900
                         alarm_time_.tm_mon + 1,      // 月份需要加1（0-11变为1-12）
                         alarm_time_.tm_mday,
                         weekdays[alarm_time_.tm_wday],
                         alarm_time_.tm_hour, 
                         alarm_time_.tm_min, 
                         alarm_time_.tm_sec);
                return time_str;
            }
            return "未设置";
        });

        // 设置闹钟方法
        methods_.AddMethod("SetAlarm", "设置闹钟时间", ParameterList({
            Parameter("hour", "小时(0-23)", kValueTypeNumber, true),
            Parameter("minute", "分钟(0-59)", kValueTypeNumber, true),
            Parameter("second", "秒(0-59)", kValueTypeNumber, false),
            Parameter("day", "日期(1-31)", kValueTypeNumber, false),
            Parameter("weekday", "星期(0-6,0为周日)", kValueTypeNumber, false),
            Parameter("enabled", "是否启用闹钟", kValueTypeBoolean, true)
        }), [this](const ParameterList& parameters) {
            int hour = static_cast<int>(parameters["hour"].number());
            int minute = static_cast<int>(parameters["minute"].number());
            int second = static_cast<int>(parameters["second"].number());
            int day = static_cast<int>(parameters["day"].number());
            int weekday = static_cast<int>(parameters["weekday"].number());
            bool enabled = parameters["enabled"].boolean();
            
            SetAlarm(hour, minute, second, day, weekday, enabled);
        });

        // 启用/禁用闹钟
        methods_.AddMethod("EnableAlarm", "启用或禁用闹钟", ParameterList({
            Parameter("enabled", "是否启用", kValueTypeBoolean, true)
        }), [this](const ParameterList& parameters) {
            bool enabled = parameters["enabled"].boolean();
            EnableAlarm(enabled);
        });

        // 清除闹钟触发标志
        // methods_.AddMethod("ClearAlarm", "清除闹钟触发标志", ParameterList(), 
        // [this](const ParameterList& parameters) {
        //     ClearAlarm();
        // });

        // 检查是否是RTC唤醒
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
            ESP_LOGI(TAG, "系统从外部中断唤醒，检查是否是闹钟触发");
            CheckAlarm();
        }
    }

private:
    // 设置闹钟
    void SetAlarm(int hour, int minute, int second = 0, int day = 0, int weekday = -1, bool enabled = true) {
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) {
            ESP_LOGE(TAG, "RTC不可用，无法设置闹钟");
            return;
        }

        // 验证时间有效性
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
            ESP_LOGE(TAG, "无效的闹钟时间: %d:%d:%d", hour, minute, second);
            return;
        }

        // 如果指定了日期，验证日期有效性
        if (day > 0 && (day < 1 || day > 31)) {
            ESP_LOGE(TAG, "无效的闹钟日期: %d", day);
            return;
        }

        // 如果指定了星期，验证星期有效性
        if (weekday >= 0 && weekday > 6) {
            ESP_LOGE(TAG, "无效的闹钟星期: %d", weekday);
            return;
        }

        // 设置闹钟时间
        struct tm alarm_time = {};
        alarm_time.tm_hour = hour;
        alarm_time.tm_min = minute;
        alarm_time.tm_sec = second;
        alarm_time.tm_mday = day > 0 ? day : 1;
        alarm_time.tm_wday = weekday >= 0 ? weekday : 0;

        // 设置闹钟
        rtc->SetAlarm(&alarm_time, 
                     second > 0,  // 是否启用秒
                     true,        // 启用分钟
                     true,        // 启用小时
                     day > 0,     // 是否启用日期
                     weekday >= 0 // 是否启用星期
        );

        // 启用闹钟
        if (enabled) {
            rtc->EnableAlarm(true);
            ESP_LOGI(TAG, "闹钟已设置并启用: %02d:%02d:%02d, 日期: %d, 星期: %d", 
                     hour, minute, second, day, weekday);
        } else {
            ESP_LOGI(TAG, "闹钟已设置但未启用: %02d:%02d:%02d, 日期: %d, 星期: %d", 
                     hour, minute, second, day, weekday);
        }

        // 更新闹钟状态
        Board::GetInstance().SetAlarmState(enabled);
    }

    // 启用/禁用闹钟
    void EnableAlarm(bool enable) {
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) {
            ESP_LOGE(TAG, "RTC不可用，无法%s闹钟", enable ? "启用" : "禁用");
            return;
        }

        rtc->EnableAlarm(enable);
        ESP_LOGI(TAG, "闹钟已%s", enable ? "启用" : "禁用");

        // 更新闹钟状态
        Board::GetInstance().SetAlarmState(enable);
    }

    // 清除闹钟触发标志
    void ClearAlarm() {
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) {
            ESP_LOGE(TAG, "RTC不可用，无法清除闹钟标志");
            return;
        }

        rtc->ClearAlarmFlag();
        ESP_LOGI(TAG, "闹钟触发标志已清除");
    }

    // 检查闹钟是否触发
    void CheckAlarm() {
        auto rtc = Board::GetInstance().GetRTC();
        if (!rtc) return;

        if (rtc->IsAlarmTriggered()) {
            ESP_LOGI(TAG, "检测到闹钟已触发");
            
            // 获取闹钟时间并打印
            struct tm alarm_time;
            if (rtc->GetAlarmTime(&alarm_time)) {
                ESP_LOGI(TAG, "触发的闹钟时间为: %02d:%02d:%02d, 日期: %d, 星期: %d",
                         alarm_time.tm_hour, alarm_time.tm_min, alarm_time.tm_sec,
                         alarm_time.tm_mday, alarm_time.tm_wday);
            }
            
            // 这里可以添加闹钟触发后的操作，比如播放提示音等
            // ...

            // 如果需要自动清除闹钟标志，取消下面的注释
            // rtc->ClearAlarmFlag();
        }
    }
};

} // namespace iot

DECLARE_THING(AlarmClock);