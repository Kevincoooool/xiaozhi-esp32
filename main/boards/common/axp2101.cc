#include "axp2101.h"
#include "board.h"
#include "display.h"

#include <esp_log.h>

#define TAG "Axp2101"

Axp2101::Axp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}

int Axp2101::GetBatteryCurrentDirection() {
    return (ReadReg(0x01) & 0b01100000) >> 5;
}

bool Axp2101::IsCharging() {
    return GetBatteryCurrentDirection() == 1;
}

bool Axp2101::IsDischarging() {
    return GetBatteryCurrentDirection() == 2;
}

bool Axp2101::IsChargingDone() {
    uint8_t value = ReadReg(0x01);
    return (value & 0b00000111) == 0b00000100;
}

int Axp2101::GetBatteryLevel() {
    return ReadReg(0xA4);
}

void Axp2101::PowerOff() {
    uint8_t value = ReadReg(0x10);
    value = value | 0x01;
    WriteReg(0x10, value);
}
float Axp2101::GetTsTemperature() {
    uint8_t ts_high = ReadReg(0x3c) & 0x3F;
    uint8_t ts_low = ReadReg(0x3d);
    uint16_t ts_value = (ts_high << 8) | ts_low;
    
    // 转换为实际温度值 (根据AXP2101数据手册的温度计算公式)
    float die_voltage = ts_value * 0.1f;  // 单位mV

    float die_temp =  (1000.0f - die_voltage) / 4.0f + 25.0f;
    return die_temp / 2.0f;
}