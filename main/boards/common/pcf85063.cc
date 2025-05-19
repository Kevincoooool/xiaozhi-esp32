#include "pcf85063.h"
#include <esp_log.h>

#define TAG "PCF85063"

PCF85063::PCF85063(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}
void PCF85063::Initialize() {
    // 控制寄存器1: 正常模式，无外部时钟测试模式，无周期性中断
    WriteReg(PCF85063_REG_CONTROL_1, 0x00);
    
    // 读取控制寄存器2，检查是否有闹钟设置
    uint8_t ctrl2 = ReadReg(PCF85063_REG_CONTROL_2);
    
    // 检查闹钟标志位是否被设置（表示闹钟已触发）
    bool alarm_triggered = (ctrl2 & PCF85063_CTRL2_AF) != 0;
    
    if (alarm_triggered) {
        // 获取闹钟时间和当前时间
        struct tm alarm_time;
        struct tm current_time;
        GetTimeStruct(&current_time);
        bool has_alarm = GetAlarmTime(&alarm_time);
        
        if (has_alarm) {
            // 计算时间差（秒）
            alarm_time.tm_mon = current_time.tm_mon;
            alarm_time.tm_year = current_time.tm_year;
            time_t alarm_timestamp = mktime(&alarm_time);
            time_t current_timestamp = mktime(&current_time);
            int time_diff = alarm_timestamp - current_timestamp;
            
            // 如果闹钟时间在当前时间20秒内，则认为是真正的闹钟触发
            // 否则可能是由于其他原因（如掉电）导致的标志位被设置
            if (abs(time_diff) <= 20) {
                ESP_LOGI(TAG, "检测到闹钟已触发，时间为: %02d:%02d:%02d, 日期: %d, 星期: %d",
                         alarm_time.tm_hour, alarm_time.tm_min, alarm_time.tm_sec,
                         alarm_time.tm_mday, alarm_time.tm_wday);
                
                // 清除闹钟标志和设置
                ctrl2 &= ~(PCF85063_CTRL2_AF | PCF85063_CTRL2_AIE);
                WriteReg(PCF85063_REG_CONTROL_2, ctrl2);
                
                // 禁用所有闹钟字段
                WriteReg(PCF85063_REG_SECOND_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_MINUTE_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_HOUR_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_DAY_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_WEEKDAY_ALARM, PCF85063_ALARM_DISABLE);
                
                ESP_LOGI(TAG, "闹钟已触发，闹钟设置已清除");
            } else {
                // 仅清除标志位，保留闹钟设置
                ctrl2 &= ~PCF85063_CTRL2_AF;
                WriteReg(PCF85063_REG_CONTROL_2, ctrl2);
                ESP_LOGI(TAG, "检测到闹钟标志但时间不匹配，仅清除标志位");
            }
        }
    } else {
        // 检查是否有闹钟设置
        struct tm alarm_time;
        bool has_alarm = GetAlarmTime(&alarm_time);
        
        if (has_alarm) {
            // 获取当前时间
            struct tm current_time;
            GetTimeStruct(&current_time);
            
            // 比较闹钟时间和当前时间
            bool is_expired = false;
            
            // 如果设置了具体日期，则进行完整的日期时间比较
            if (alarm_time.tm_mday > 0) {
                // 由于闹钟不存储月份和年份，我们假设闹钟设置在当前月份
                alarm_time.tm_mon = current_time.tm_mon;
                alarm_time.tm_year = current_time.tm_year;
                
                time_t alarm_timestamp = mktime(&alarm_time);
                time_t current_timestamp = mktime(&current_time);
                
                is_expired = (alarm_timestamp < current_timestamp);
            } else {
                // 只设置了时间，比较时间部分
                is_expired = (alarm_time.tm_hour < current_time.tm_hour ||
                            (alarm_time.tm_hour == current_time.tm_hour &&
                             alarm_time.tm_min < current_time.tm_min) ||
                            (alarm_time.tm_hour == current_time.tm_hour &&
                             alarm_time.tm_min == current_time.tm_min &&
                             alarm_time.tm_sec <= current_time.tm_sec));
            }
            
            if (is_expired) {
                ESP_LOGI(TAG, "检测到过期闹钟，时间为: %02d:%02d:%02d, 日期: %d, 星期: %d",
                         alarm_time.tm_hour, alarm_time.tm_min, alarm_time.tm_sec,
                         alarm_time.tm_mday, alarm_time.tm_wday);
                
                // 清除闹钟设置
                ctrl2 &= ~PCF85063_CTRL2_AIE;
                WriteReg(PCF85063_REG_CONTROL_2, ctrl2);
                
                // 禁用所有闹钟字段
                WriteReg(PCF85063_REG_SECOND_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_MINUTE_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_HOUR_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_DAY_ALARM, PCF85063_ALARM_DISABLE);
                WriteReg(PCF85063_REG_WEEKDAY_ALARM, PCF85063_ALARM_DISABLE);
                
                ESP_LOGI(TAG, "过期闹钟已清除");
            } else {
                ESP_LOGI(TAG, "检测到有效闹钟，时间为: %02d:%02d:%02d, 日期: %d, 星期: %d",
                         alarm_time.tm_hour, alarm_time.tm_min, alarm_time.tm_sec,
                         alarm_time.tm_mday, alarm_time.tm_wday);
            }
        } else {
            ESP_LOGI(TAG, "未检测到闹钟设置");
        }
    }
    
    ESP_LOGI(TAG, "PCF85063 RTC 初始化完成");
}
uint8_t PCF85063::BcdToDec(uint8_t val) {
    return (val / 16 * 10) + (val % 16);
}

uint8_t PCF85063::DecToBcd(uint8_t val) {
    return (val / 10 * 16) + (val % 10);
}

time_t PCF85063::GetTime() {
    struct tm time_info;
    GetTimeStruct(&time_info);
    return mktime(&time_info);
}

void PCF85063::GetTimeStruct(struct tm* time_info) {
    // 读取所有时间寄存器
    uint8_t buffer[7];
    ReadRegs(PCF85063_REG_SECONDS, buffer, 7);
    
    // 转换BCD到十进制并填充tm结构
    time_info->tm_sec = BcdToDec(buffer[0] & 0x7F);  // 去掉最高位(停止位)
    time_info->tm_min = BcdToDec(buffer[1] & 0x7F);
    time_info->tm_hour = BcdToDec(buffer[2] & 0x3F); // 24小时制
    time_info->tm_mday = BcdToDec(buffer[3] & 0x3F);
    time_info->tm_wday = BcdToDec(buffer[4] & 0x07) - 1; // 转换为tm格式(0-6)
    time_info->tm_mon = BcdToDec(buffer[5] & 0x1F) - 1;  // 转换为tm格式(0-11)
    time_info->tm_year = BcdToDec(buffer[6]) + 100;      // 年份从2000年开始，转为tm格式(从1900年开始)
    
    time_info->tm_isdst = -1; // 不使用夏令时
    
    ESP_LOGI(TAG, "Get time: %04d-%02d-%02d %02d:%02d:%02d",
             time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
             time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
}

void PCF85063::SetTime(time_t time) {
    struct tm time_info;
    localtime_r(&time, &time_info);
    SetTimeStruct(&time_info);
}

void PCF85063::SetTimeStruct(const struct tm* time_info) {
    // 转换时间到BCD格式
    uint8_t seconds = DecToBcd(time_info->tm_sec);
    uint8_t minutes = DecToBcd(time_info->tm_min);
    uint8_t hours = DecToBcd(time_info->tm_hour);
    uint8_t days = DecToBcd(time_info->tm_mday);
    uint8_t weekdays = DecToBcd(time_info->tm_wday + 1); // 转换为PCF85063格式(1-7)
    uint8_t months = DecToBcd(time_info->tm_mon + 1);    // 转换为PCF85063格式(1-12)
    uint8_t years = DecToBcd(time_info->tm_year - 100);  // 转换为PCF85063格式(从2000年开始)
    
    // 写入时间寄存器
    WriteReg(PCF85063_REG_SECONDS, seconds);
    WriteReg(PCF85063_REG_MINUTES, minutes);
    WriteReg(PCF85063_REG_HOURS, hours);
    WriteReg(PCF85063_REG_DAYS, days);
    WriteReg(PCF85063_REG_WEEKDAYS, weekdays);
    WriteReg(PCF85063_REG_MONTHS, months);
    WriteReg(PCF85063_REG_YEARS, years);
    
    ESP_LOGI(TAG, "Set time: %04d-%02d-%02d %02d:%02d:%02d",
             time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
             time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
}

void PCF85063::SetAlarm(const struct tm* alarm_time, bool enable_seconds, 
                       bool enable_minutes, bool enable_hours, 
                       bool enable_day, bool enable_weekday) {
    // 转换闹钟时间到BCD格式
    uint8_t seconds = enable_seconds ? DecToBcd(alarm_time->tm_sec) : PCF85063_ALARM_DISABLE;
    uint8_t minutes = enable_minutes ? DecToBcd(alarm_time->tm_min) : PCF85063_ALARM_DISABLE;
    uint8_t hours = enable_hours ? DecToBcd(alarm_time->tm_hour) : PCF85063_ALARM_DISABLE;
    uint8_t days = enable_day ? DecToBcd(alarm_time->tm_mday) : PCF85063_ALARM_DISABLE;
    
    // 如果启用了某个字段，需要清除AEN位
    if (enable_seconds) seconds &= ~PCF85063_ALARM_AEN;
    if (enable_minutes) minutes &= ~PCF85063_ALARM_AEN;
    if (enable_hours) hours &= ~PCF85063_ALARM_AEN;
    if (enable_day) days &= ~PCF85063_ALARM_AEN;
    
    // 写入闹钟寄存器
    WriteReg(PCF85063_REG_SECOND_ALARM, seconds);
    WriteReg(PCF85063_REG_MINUTE_ALARM, minutes);
    WriteReg(PCF85063_REG_HOUR_ALARM, hours);
    WriteReg(PCF85063_REG_DAY_ALARM, days);
    WriteReg(PCF85063_REG_WEEKDAY_ALARM, PCF85063_ALARM_DISABLE);
    
    ESP_LOGI(TAG, "Set alarm: %02d:%02d:%02d, day: %d",
             enable_hours ? alarm_time->tm_hour : -1,
             enable_minutes ? alarm_time->tm_min : -1,
             enable_seconds ? alarm_time->tm_sec : -1,
             enable_day ? alarm_time->tm_mday : -1);
}

void PCF85063::EnableAlarm(bool enable) {
    uint8_t ctrl2 = ReadReg(PCF85063_REG_CONTROL_2);
    
    if (enable) {
        // 设置AIE位启用闹钟中断
        ctrl2 |= PCF85063_CTRL2_AIE;
    } else {
        // 清除AIE位禁用闹钟中断
        ctrl2 &= ~PCF85063_CTRL2_AIE;
    }
    
    // 写回控制寄存器2
    WriteReg(PCF85063_REG_CONTROL_2, ctrl2);
    
    ESP_LOGI(TAG, "Alarm interrupt %s", enable ? "enabled" : "disabled");
}
bool PCF85063::IsAlarmEnabled() {
    // 读取控制寄存器2，检查AIE位
    uint8_t ctrl2 = ReadReg(PCF85063_REG_CONTROL_2);
    return (ctrl2 & PCF85063_CTRL2_AIE) != 0;
}
bool PCF85063::IsAlarmTriggered() {
    // 读取控制寄存器2，检查AF位
    uint8_t ctrl2 = ReadReg(PCF85063_REG_CONTROL_2);
    return (ctrl2 & PCF85063_CTRL2_AF) != 0;
}

void PCF85063::ClearAlarmFlag() {
    // 读取控制寄存器2
    uint8_t ctrl2 = ReadReg(PCF85063_REG_CONTROL_2);
    
    // 清除AF位
    ctrl2 &= ~PCF85063_CTRL2_AF;
    
    // 写回控制寄存器2
    WriteReg(PCF85063_REG_CONTROL_2, ctrl2);
    
    ESP_LOGI(TAG, "Alarm flag cleared");
}

bool PCF85063::GetAlarmTime(struct tm* alarm_time) {
    // 读取闹钟寄存器
    uint8_t buffer[5];
    ReadRegs(PCF85063_REG_SECOND_ALARM, buffer, 5);
    
    // 检查是否有任何闹钟字段被启用（AEN位为0表示启用）
    bool any_enabled = false;
    
    // 秒
    if ((buffer[0] & PCF85063_ALARM_AEN) == 0) {
        alarm_time->tm_sec = BcdToDec(buffer[0] & 0x7F);
        any_enabled = true;
    } else {
        alarm_time->tm_sec = 0;
    }
    
    // 分
    if ((buffer[1] & PCF85063_ALARM_AEN) == 0) {
        alarm_time->tm_min = BcdToDec(buffer[1] & 0x7F);
        any_enabled = true;
    } else {
        alarm_time->tm_min = 0;
    }
    
    // 时
    if ((buffer[2] & PCF85063_ALARM_AEN) == 0) {
        alarm_time->tm_hour = BcdToDec(buffer[2] & 0x3F);
        any_enabled = true;
    } else {
        alarm_time->tm_hour = 0;
    }
    
    // 日
    if ((buffer[3] & PCF85063_ALARM_AEN) == 0) {
        alarm_time->tm_mday = BcdToDec(buffer[3] & 0x3F);
        any_enabled = true;
    } else {
        alarm_time->tm_mday = 1;
    }
    
     // 星期
     if ((buffer[4] & PCF85063_ALARM_AEN) == 0) {
        uint8_t weekday = BcdToDec(buffer[4] & 0x07);
        if (weekday == 7) weekday = 0;  // 将星期日(7)转换为0
        alarm_time->tm_wday = weekday;
        any_enabled = true;
    } else {
        alarm_time->tm_wday = 0;
    }
    
    // 其他字段设置为默认值
    alarm_time->tm_mon = 0;
    alarm_time->tm_year = 100; // 2000年
    alarm_time->tm_isdst = -1;
    
    if (any_enabled) {
        ESP_LOGI(TAG, "Get alarm: %02d:%02d:%02d, day: %d, weekday: %d",
                 alarm_time->tm_hour, alarm_time->tm_min, alarm_time->tm_sec,
                 alarm_time->tm_mday, alarm_time->tm_wday);
    } else {
        ESP_LOGI(TAG, "No alarm is set");
    }
    
    return any_enabled;
}