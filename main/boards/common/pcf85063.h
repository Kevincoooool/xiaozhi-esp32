#ifndef PCF85063_H
#define PCF85063_H

#include "i2c_device.h"
#include <time.h>

// PCF85063 寄存器地址
#define PCF85063_REG_CONTROL_1       0x00
#define PCF85063_REG_CONTROL_2       0x01
#define PCF85063_REG_OFFSET          0x02
#define PCF85063_REG_RAM_BYTE        0x03
#define PCF85063_REG_SECONDS         0x04
#define PCF85063_REG_MINUTES         0x05
#define PCF85063_REG_HOURS           0x06
#define PCF85063_REG_DAYS            0x07
#define PCF85063_REG_WEEKDAYS        0x08
#define PCF85063_REG_MONTHS          0x09
#define PCF85063_REG_YEARS           0x0A
// 闹钟寄存器
#define PCF85063_REG_SECOND_ALARM    0x0B
#define PCF85063_REG_MINUTE_ALARM    0x0C
#define PCF85063_REG_HOUR_ALARM      0x0D
#define PCF85063_REG_DAY_ALARM       0x0E
#define PCF85063_REG_WEEKDAY_ALARM   0x0F

// 控制寄存器2位定义
#define PCF85063_CTRL2_AIE           0x80  // 闹钟中断使能位
#define PCF85063_CTRL2_AF            0x40  // 闹钟标志位

// 闹钟寄存器位定义
#define PCF85063_ALARM_AEN           0x80  // 闹钟使能位
#define PCF85063_ALARM_DISABLE       0x80  // 禁用特定闹钟字段

class PCF85063 : public I2cDevice {
public:
    // 构造函数，默认I2C地址为0x51
    PCF85063(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x51);
    
    // 初始化RTC
    void Initialize();
    
    // 获取当前时间
    time_t GetTime();
    
    // 设置时间
    void SetTime(time_t time);
    
    // 获取时间结构体
    void GetTimeStruct(struct tm* time_info);
    
    // 设置时间结构体
    void SetTimeStruct(const struct tm* time_info);
    
    // 设置闹钟
    void SetAlarm(const struct tm* alarm_time, bool enable_seconds = false, 
                 bool enable_minutes = true, bool enable_hours = true, 
                 bool enable_day = false, bool enable_weekday = false);
    
    // 启用闹钟中断
    void EnableAlarm(bool enable);
    
    // 检查闹钟是否触发
    bool IsAlarmTriggered();
    // 检查闹钟是否启用
    bool IsAlarmEnabled();
    
    // 清除闹钟标志
    void ClearAlarmFlag();
    
    // 获取闹钟时间
    bool GetAlarmTime(struct tm* alarm_time);

private:
    // BCD转十进制
    uint8_t BcdToDec(uint8_t val);
    
    // 十进制转BCD
    uint8_t DecToBcd(uint8_t val);
};

#endif // PCF85063_H