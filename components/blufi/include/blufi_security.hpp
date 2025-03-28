#pragma once

#include <memory>
#include <cstdint>
#include <esp_err.h>

class BlufiSecurity {
public:
    BlufiSecurity();
    ~BlufiSecurity();

    esp_err_t Initialize();
    void Deinitialize();

    void HandleNegotiateData(uint8_t* data, int len, uint8_t** output_data, int* output_len, bool* need_free);
    int Encrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len);
    int Decrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len);
    uint16_t CalculateChecksum(uint8_t iv8, uint8_t* data, int len);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};