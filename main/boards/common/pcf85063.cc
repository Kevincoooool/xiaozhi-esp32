#include "pcf85063.h"
#include <esp_log.h>

#define TAG "PCF85063"

PCF85063::PCF85063(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}

void PCF85063::Initialize() {
    // 控制寄存器1: 正常模式，无外部时钟测试模式，无周期性中断
    WriteReg(PCF85063_REG_CONTROL_1, 0x00);
    
    // 控制寄存器2: 无报警中断，无倒计时中断
    WriteReg(PCF85063_REG_CONTROL_2, 0x00);
    
    ESP_LOGI(TAG, "PCF85063 RTC initialized");
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