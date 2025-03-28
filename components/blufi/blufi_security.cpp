#include "blufi_security.hpp"
#include <esp_log.h>
#include <esp_system.h>
#include <mbedtls/aes.h>
#include <mbedtls/dhm.h>
#include <mbedtls/md5.h>
#include <esp_crc.h>
#include <cstring>

#include "esp_random.h"
static const char* TAG = "BlufiSecurity";

class BlufiSecurity::Impl {
public:
    static constexpr size_t DH_SELF_PUB_KEY_LEN = 128;
    static constexpr size_t DH_SELF_PUB_KEY_BIT_LEN = DH_SELF_PUB_KEY_LEN * 8;
    static constexpr size_t SHARE_KEY_LEN = 128;
    static constexpr size_t SHARE_KEY_BIT_LEN = SHARE_KEY_LEN * 8;
    static constexpr size_t PSK_LEN = 16;

    Impl() : dh_param_(nullptr), dh_param_len_(0) {
        mbedtls_dhm_init(&dhm_);
        mbedtls_aes_init(&aes_);
        memset(iv_, 0x0, sizeof(iv_));
    }

    ~Impl() {
        Deinitialize();
    }

    esp_err_t Initialize() {
        return ESP_OK;
    }

    void Deinitialize() {
        if (dh_param_) {
            free(dh_param_);
            dh_param_ = nullptr;
        }
        mbedtls_dhm_free(&dhm_);
        mbedtls_aes_free(&aes_);
    }

    void HandleNegotiateData(uint8_t* data, int len, uint8_t** output_data, 
        int* output_len, bool* need_free) {
        uint8_t type = data[0];

        switch (type) {
        case 0x00:  // SEC_TYPE_DH_PARAM_LEN
            dh_param_len_ = ((data[1] << 8) | data[2]);
            if (dh_param_) {
                free(dh_param_);
            }
            dh_param_ = (uint8_t*)malloc(dh_param_len_);
            break;

        case 0x01:  // SEC_TYPE_DH_PARAM_DATA
            if (!dh_param_) {
                ESP_LOGE(TAG, "DH param is null");
                return;
            }

            memcpy(dh_param_, &data[1], dh_param_len_);
            uint8_t* param = dh_param_;

            if (mbedtls_dhm_read_params(&dhm_, &param, &param[dh_param_len_])) {
                ESP_LOGE(TAG, "Read param failed");
                return;
            }

            free(dh_param_);
            dh_param_ = nullptr;

            int dhm_len = mbedtls_dhm_get_len(&dhm_);
            if (mbedtls_dhm_make_public(&dhm_, dhm_len, self_public_key_, dhm_len,
                [](void*, unsigned char* output, size_t len) {
                    esp_fill_random(output, len);
                    return 0;
                }, nullptr)) {
                ESP_LOGE(TAG, "Make public failed");
                return;
            }

            if (mbedtls_dhm_calc_secret(&dhm_, share_key_, SHARE_KEY_BIT_LEN,
                &share_len_, [](void*, unsigned char* output, size_t len) {
                    esp_fill_random(output, len);
                    return 0;
                }, nullptr)) {
                ESP_LOGE(TAG, "Calculate secret failed");
                return;
            }

            if (mbedtls_md5(share_key_, share_len_, psk_)) {
                ESP_LOGE(TAG, "Calculate MD5 failed");
                return;
            }

            mbedtls_aes_setkey_enc(&aes_, psk_, 128);

            *output_data = self_public_key_;
            *output_len = dhm_len;
            *need_free = false;
            break;
        }
    }

    int Encrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len) {
        size_t iv_offset = 0;
        uint8_t iv0[16];

        memcpy(iv0, iv_, sizeof(iv_));
        iv0[0] = iv8;

        if (mbedtls_aes_crypt_cfb128(&aes_, MBEDTLS_AES_ENCRYPT, crypt_len,
            &iv_offset, iv0, crypt_data, crypt_data)) {
            return -1;
        }

        return crypt_len;
    }

    int Decrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len) {
        size_t iv_offset = 0;
        uint8_t iv0[16];

        memcpy(iv0, iv_, sizeof(iv_));
        iv0[0] = iv8;

        if (mbedtls_aes_crypt_cfb128(&aes_, MBEDTLS_AES_DECRYPT, crypt_len,
            &iv_offset, iv0, crypt_data, crypt_data)) {
            return -1;
        }

        return crypt_len;
    }

    uint16_t CalculateChecksum(uint8_t iv8, uint8_t* data, int len) {
        return esp_crc16_be(0, data, len);
    }

private:
    uint8_t self_public_key_[DH_SELF_PUB_KEY_LEN];
    uint8_t share_key_[SHARE_KEY_LEN];
    size_t share_len_;
    uint8_t psk_[PSK_LEN];
    uint8_t* dh_param_;
    int dh_param_len_;
    uint8_t iv_[16];
    mbedtls_dhm_context dhm_;
    mbedtls_aes_context aes_;
};

BlufiSecurity::BlufiSecurity() : impl_(new Impl()) {}
BlufiSecurity::~BlufiSecurity() = default;

esp_err_t BlufiSecurity::Initialize() { return impl_->Initialize(); }
void BlufiSecurity::Deinitialize() { impl_->Deinitialize(); }

void BlufiSecurity::HandleNegotiateData(uint8_t* data, int len, uint8_t** output_data, 
    int* output_len, bool* need_free) {
    impl_->HandleNegotiateData(data, len, output_data, output_len, need_free);
}

int BlufiSecurity::Encrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len) {
    return impl_->Encrypt(iv8, crypt_data, crypt_len);
}

int BlufiSecurity::Decrypt(uint8_t iv8, uint8_t* crypt_data, int crypt_len) {
    return impl_->Decrypt(iv8, crypt_data, crypt_len);
}

uint16_t BlufiSecurity::CalculateChecksum(uint8_t iv8, uint8_t* data, int len) {
    return impl_->CalculateChecksum(iv8, data, len);
}