#ifndef PCF85063_H
#define PCF85063_H

#include "i2c_device.h"
#include <time.h>

// PCF85063 寄存器地址
#define PCF85063_REG_CONTROL_1       0x00
#define PCF85063_REG_CONTROL_2       0x01
#define PCF85063_REG_SECONDS         0x04
#define PCF85063_REG_MINUTES         0x05
#define PCF85063_REG_HOURS           0x06
#define PCF85063_REG_DAYS            0x07
#define PCF85063_REG_WEEKDAYS        0x08
#define PCF85063_REG_MONTHS          0x09
#define PCF85063_REG_YEARS           0x0A

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

private:
    // BCD转十进制
    uint8_t BcdToDec(uint8_t val);
    
    // 十进制转BCD
    uint8_t DecToBcd(uint8_t val);
};

#endif // PCF85063_H