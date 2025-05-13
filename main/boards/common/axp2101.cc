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
void Axp2101::EnableAldo3(bool enable) {
    // ALDO3 控制位在 0x90 寄存器的 bit 2
    uint8_t value = ReadReg(0x90);
    if (enable) {
        value |= (1 << 2);  // 设置 bit 2
    } else {
        value &= ~(1 << 2); // 清除 bit 2
    }
    WriteReg(0x90, value);
    
    ESP_LOGI(TAG, "ALDO3 已%s", enable ? "启用" : "禁用");
}

bool Axp2101::IsAldo3Enabled() {
    // 读取 0x90 寄存器，检查 bit 2 的状态
    uint8_t value = ReadReg(0x90);
    return (value & (1 << 2)) != 0;
}
