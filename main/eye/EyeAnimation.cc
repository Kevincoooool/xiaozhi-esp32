#include "EyeAnimation.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <algorithm> // 添加这行来使用 std::min

#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) ({ \
    typeof(addr) _addr = (addr); \
    *(const unsigned short *)(_addr); \
  })

const uint8_t ease[] = {                                                            // Ease in/out curve for eye movements 3*t^2-2*t^3
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3,                                 // T
    3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10,                               // h
    11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23,                 // x
    24, 25, 26, 27, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39,                 // 2
    40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 56, 57, 58,                 // A
    60, 61, 62, 63, 65, 66, 67, 69, 70, 72, 73, 74, 76, 77, 78, 80,                 // l
    81, 83, 84, 85, 87, 88, 90, 91, 93, 94, 96, 97, 98, 100, 101, 103,              // e
    104, 106, 107, 109, 110, 112, 113, 115, 116, 118, 119, 121, 122, 124, 125, 127, // c
    128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143, 145, 146, 148, 149, 151, // J
    152, 154, 155, 157, 158, 159, 161, 162, 164, 165, 167, 168, 170, 171, 172, 174, // a
    175, 177, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 195, // c
    197, 198, 199, 201, 202, 203, 204, 205, 207, 208, 209, 210, 211, 213, 214, 215, // o
    216, 217, 218, 219, 220, 221, 222, 224, 225, 226, 227, 228, 228, 229, 230, 231, // b
    232, 233, 234, 235, 236, 237, 237, 238, 239, 240, 240, 241, 242, 243, 243, 244, // s
    245, 245, 246, 246, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252, // o
    252, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255}; // n
/**
 * @brief 将一个数从一个范围映射到另一个范围
 *
 * @param x 输入值
 * @param in_min 输入范围的最小值
 * @param in_max 输入范围的最大值
 * @param out_min 输出范围的最小值
 * @param out_max 输出范围的最大值
 * @return int32_t 映射后的输出值
 */
int32_t map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
/**
 * @brief 限制一个数值在指定范围内
 *
 * @param x 需要限制的值
 * @param min 最小值
 * @param max 最大值
 * @return int32_t 限制后的值
 */
int32_t constrain(int32_t x, int32_t min, int32_t max) {
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

EyeAnimation::EyeAnimation() 
    : iris_scale_((IRIS_MIN + IRIS_MAX) / 2)
    , scale_(REAL_SCREEN_WIDTH *1.0f / SCREEN_WIDTH)  // 默认缩放到240x240
{
    render_buffer_ = new uint16_t[SCREEN_WIDTH * SCREEN_HEIGHT];
    scaled_buffer_ = new uint16_t[REAL_SCREEN_WIDTH * REAL_SCREEN_HEIGHT];  // 分配缩放后的缓冲区
}


EyeAnimation::~EyeAnimation()
{
    delete[] render_buffer_;
    delete[] scaled_buffer_;
}

#include "esp_dsp.h"
#include "esp_attr.h"

// 添加SIMD优化的scaleBuffer实现
void IRAM_ATTR EyeAnimation::scaleBuffer() {
    const int src_width = SCREEN_WIDTH;
    const int src_height = SCREEN_HEIGHT;
    const int dst_width = REAL_SCREEN_WIDTH;
    const int dst_height = REAL_SCREEN_HEIGHT;
    // 如果源尺寸和目标尺寸相同，直接复制数据
    if (src_width == dst_width && src_height == dst_height) {
        memcpy(scaled_buffer_, render_buffer_, src_width * src_height * sizeof(uint16_t));
        return;
    }
    // 使用SIMD寄存器处理4个像素
    const int pixels_per_simd = 4;
    alignas(16) uint16_t temp_pixels[4];
    
    for (int y = 0; y < dst_height; y++) {
        float src_y = y / scale_;
        int y1 = (int)src_y;
        int y2 = std::min(y1 + 1, src_height - 1);
        float wy = src_y - y1;
        float wy1 = 1.0f - wy;
        
        // SIMD处理x方向
        for (int x = 0; x < dst_width; x += pixels_per_simd) {
            // 计算4个源像素位置
            float src_x[4];
            int x1[4], x2[4];
            float wx[4];
            
            for (int i = 0; i < pixels_per_simd; i++) {
                if (x + i < dst_width) {
                    src_x[i] = (x + i) / scale_;
                    x1[i] = (int)src_x[i];
                    x2[i] = std::min(x1[i] + 1, src_width - 1);
                    wx[i] = src_x[i] - x1[i];
                }
            }
            
            // 使用SIMD加载4个像素
            uint32_t *p1 = (uint32_t*)temp_pixels;
            uint32_t *p2 = (uint32_t*)(temp_pixels + 2);
            
            // 处理每个像素的颜色分量
            for (int i = 0; i < pixels_per_simd && (x + i) < dst_width; i++) {
                // 获取源像素
                uint16_t src_p1 = render_buffer_[y1 * src_width + x1[i]];
                uint16_t src_p2 = render_buffer_[y1 * src_width + x2[i]];
                uint16_t src_p3 = render_buffer_[y2 * src_width + x1[i]];
                uint16_t src_p4 = render_buffer_[y2 * src_width + x2[i]];
                
                // 解包RGB565
                uint32_t r1 = (src_p1 >> 11) & 0x1F;
                uint32_t g1 = (src_p1 >> 5) & 0x3F;
                uint32_t b1 = src_p1 & 0x1F;
                
                uint32_t r2 = (src_p2 >> 11) & 0x1F;
                uint32_t g2 = (src_p2 >> 5) & 0x3F;
                uint32_t b2 = src_p2 & 0x1F;
                
                uint32_t r3 = (src_p3 >> 11) & 0x1F;
                uint32_t g3 = (src_p3 >> 5) & 0x3F;
                uint32_t b3 = src_p3 & 0x1F;
                
                uint32_t r4 = (src_p4 >> 11) & 0x1F;
                uint32_t g4 = (src_p4 >> 5) & 0x3F;
                uint32_t b4 = src_p4 & 0x1F;
                
                // 使用SIMD指令进行插值计算
                float wx1 = 1.0f - wx[i];
                __asm__ volatile (
                    "wsr.acchi %0\n"
                    "wsr.acclo %1\n"
                    :: "r"(0), "r"(0)
                );
                
                uint32_t r = (uint32_t)(
                    r1 * wx1 * wy1 +
                    r2 * wx[i] * wy1 +
                    r3 * wx1 * wy +
                    r4 * wx[i] * wy
                );
                uint32_t g = (uint32_t)(
                    g1 * wx1 * wy1 +
                    g2 * wx[i] * wy1 +
                    g3 * wx1 * wy +
                    g4 * wx[i] * wy
                );
                uint32_t b = (uint32_t)(
                    b1 * wx1 * wy1 +
                    b2 * wx[i] * wy1 +
                    b3 * wx1 * wy +
                    b4 * wx[i] * wy
                );
                
                // 打包回RGB565
                uint16_t color = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
                // scaled_buffer_[y * dst_width + x + i] = __builtin_bswap16(color);
                scaled_buffer_[y * dst_width + x + i] = (color);
            }
        }
    }
}
void EyeAnimation::begin()
{
    // 创建眼球实例
    Eye eye;
    eye.blink.state = NOBLINK;
    eye.xposition = 0;
    eyes_.push_back(eye);
}

void EyeAnimation::drawEye(uint8_t eye_index,
                          uint32_t iScale, 
                          uint32_t scleraX,
                          uint32_t scleraY,
                          uint32_t uThreshold,
                          uint32_t lThreshold) {
    // 预计算常量值
    const int32_t iris_offset_x = (SCLERA_WIDTH - IRIS_WIDTH) / 2;
    const int32_t iris_offset_y = (SCLERA_HEIGHT - IRIS_HEIGHT) / 2;
    
    // 预计算虹膜位置
    int32_t irisY = scleraY - iris_offset_y;
    const int32_t initial_irisX = scleraX - iris_offset_x;

    #pragma GCC unroll 4
    for(uint32_t screenY = 0; screenY < SCREEN_HEIGHT; screenY++) {
        int32_t irisX = initial_irisX;
        uint32_t current_scleraX = scleraX;
        
        // 预取眼睑数据
        const uint8_t* upper_row = upper + screenY * SCREEN_WIDTH;
        const uint8_t* lower_row = lower + screenY * SCREEN_WIDTH;
        
        uint16_t* row = render_buffer_ + screenY * SCREEN_WIDTH;
        
        for(uint32_t screenX = 0; screenX < SCREEN_WIDTH; screenX++) {
             // 计算对称位置的X坐标
             uint32_t mirror_x = SCREEN_WIDTH - 1 - screenX;
            
             // 对称处理眼睑，添加平滑过渡
            uint8_t upper_value = (upper_row[screenX] + upper_row[mirror_x]) / 2;
            uint8_t lower_value = (lower_row[screenX] + lower_row[mirror_x]) / 2;
            
            // 应用眼睑间距调整
            if (upper_value > eyelid_gap_) {
                upper_value -= eyelid_gap_;
            }
            if (lower_value > eyelid_gap_) {
                lower_value -= eyelid_gap_;
            }
             
            // 计算边缘平滑因子 (0.0 - 1.0)
            float upper_alpha = smoothstep(uThreshold - 8, uThreshold + 8, upper_value);
            float lower_alpha = smoothstep(lThreshold - 8, lThreshold + 8, lower_value);
            
            // 如果完全在眼睑外，直接绘制黑色
            if(upper_alpha <= 0.0f || lower_alpha <= 0.0f) {
                row[screenX] = 0;
                irisX++;
                current_scleraX++;
                continue;
            }
            
            // 计算眼球颜色
            uint16_t eye_color;
            if(irisY >= 0 && irisY < IRIS_HEIGHT && 
               irisX >= 0 && irisX < IRIS_WIDTH) {
                uint16_t p = pgm_read_word(polar + irisY * IRIS_WIDTH + irisX);
                uint32_t dist = (iScale * (p & 0x7F)) >> 7;
                
                if(dist < IRIS_MAP_HEIGHT) {
                    uint32_t angle = (IRIS_MAP_WIDTH * (p >> 7)) >> 9;
                    eye_color = pgm_read_word(iris + dist * IRIS_MAP_WIDTH + angle);
                } else {
                    eye_color = pgm_read_word(sclera + scleraY * SCLERA_WIDTH + current_scleraX);
                }
            } else {
                eye_color = pgm_read_word(sclera + scleraY * SCLERA_WIDTH + current_scleraX);
            }
            
            // 应用边缘平滑
            float alpha = std::min(upper_alpha, lower_alpha);
            uint16_t final_color = blend_color(0, eye_color, alpha);
            row[screenX] = final_color;
            
            irisX++;
            current_scleraX++;
        }
        scleraY++;
        irisY++;
    }
}

// 添加辅助函数

// 平滑过渡函数
float EyeAnimation::smoothstep(float edge0, float edge1, float x) {
    x = constrain((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

// 颜色混合函数
uint16_t EyeAnimation::blend_color(uint16_t c1, uint16_t c2, float alpha) {
    // 解包RGB565
    uint8_t r1 = (c1 >> 11) & 0x1F;
    uint8_t g1 = (c1 >> 5) & 0x3F;
    uint8_t b1 = c1 & 0x1F;
    
    uint8_t r2 = (c2 >> 11) & 0x1F;
    uint8_t g2 = (c2 >> 5) & 0x3F;
    uint8_t b2 = c2 & 0x1F;
    
    // 线性插值
    uint8_t r = r1 + (r2 - r1) * alpha;
    uint8_t g = g1 + (g2 - g1) * alpha;
    uint8_t b = b1 + (b2 - b1) * alpha;
    
    // 打包回RGB565
    return (r << 11) | (g << 5) | b;
}

void EyeAnimation::update()
{
    uint32_t t = esp_timer_get_time();
    static bool eyeInMotion = false;
    static int16_t eyeOldX = 512, eyeOldY = 512;
    static int16_t eyeNewX = 512, eyeNewY = 512;
    static uint32_t eyeMoveStartTime = 0;
    static int32_t eyeMoveDuration = 0;
    static uint8_t currentEye = 0;

    // 更新眼球位置
    int16_t eyeX, eyeY;
    int32_t dt = t - eyeMoveStartTime;

    if (eyeInMotion)
    {
        if (dt >= eyeMoveDuration)
        {
            // 到达目标位置,停止移动
            eyeInMotion = false;
            eyeMoveDuration = esp_random() % 3000000; // 0-3秒停留
            eyeMoveStartTime = t;
            eyeX = eyeOldX = eyeNewX;
            eyeY = eyeOldY = eyeNewY;
        }
        else
        {
            // 插值计算当前位置
            int16_t e = ease[255 * dt / eyeMoveDuration] + 1;
            eyeX = eyeOldX + (((eyeNewX - eyeOldX) * e) / 256);
            eyeY = eyeOldY + (((eyeNewY - eyeOldY) * e) / 256);
        }
    }
    else
    {
        eyeX = eyeOldX;
        eyeY = eyeOldY;

        if (dt > eyeMoveDuration)
        {
            // 开始新的移动
            int16_t dx, dy;
            uint32_t d;
            do
            {
                // 在圆形区域内选择新目标点
                eyeNewX = esp_random() % 1024;
                eyeNewY = esp_random() % 1024;
                dx = (eyeNewX * 2) - 1023;
                dy = (eyeNewY * 2) - 1023;
                d = dx * dx + dy * dy;
            } while (d > (1023 * 1023));

            eyeMoveDuration = 72000 + (esp_random() % 72000); // ~1/14 - ~1/7 秒
            eyeMoveStartTime = t;
            eyeInMotion = true;
        }
    }

    // 处理眨眼
    for (auto &eye : eyes_)
    {
        // 更新眨眼状态
        if (eye.blink.state)
        {
            uint32_t s = t - eye.blink.startTime;
            if (s >= eye.blink.duration)
            {
                if (eye.blink.state == ENBLINK)
                {
                    eye.blink.state = DEBLINK;
                    eye.blink.duration *= 2;
                    eye.blink.startTime = t;
                }
                else
                {
                    eye.blink.state = NOBLINK;
                }
            }
        }
        else if ((t - last_blink_) > next_blink_delay_)
        {
            // 开始新的眨眼
            eye.blink.state = ENBLINK;
            eye.blink.startTime = t;
            eye.blink.duration = 36000 + (esp_random() % 36000);
            last_blink_ = t;
            next_blink_delay_ = eye.blink.duration * 3 + (esp_random() % 4000000);
        }
    }

    // 映射坐标到实际像素范围
    eyeX = map(eyeX, 0, 1023, 0, SCLERA_WIDTH - SCREEN_WIDTH);
    eyeY = map(eyeY, 0, 1023, 0, SCLERA_HEIGHT - SCREEN_HEIGHT);

    // 限制移动范围
    eyeX = constrain(eyeX, 0, SCLERA_WIDTH - SCREEN_WIDTH);
    eyeY = constrain(eyeY, 0, SCLERA_HEIGHT - SCREEN_HEIGHT);

    // 修改眼睑阈值计算
    static uint8_t uThreshold = 128;
    uint8_t lThreshold;

#ifdef TRACKING
    int16_t sampleX = SCLERA_WIDTH / 2 - (eyeX / 2);
    int16_t sampleY = SCLERA_HEIGHT / 2 - (eyeY + IRIS_HEIGHT / 4);

    uint8_t n = (sampleY < 0) ? 0 : (pgm_read_byte(upper + sampleY * SCREEN_WIDTH + sampleX) + 
                                    pgm_read_byte(upper + sampleY * SCREEN_WIDTH + (SCREEN_WIDTH - 1 - sampleX))) / 2;

    uThreshold = (uThreshold * 3 + n) / 4;
    lThreshold = 254 - uThreshold;
#else
    uThreshold = 128;
    lThreshold = 128;
#endif

    // 处理眨眼
    auto& current_eye = eyes_[currentEye];
    if(current_eye.blink.state) {
        uint32_t s = (t - current_eye.blink.startTime);
        s = (s >= current_eye.blink.duration) ? 255 : 255 * s / current_eye.blink.duration;
        s = (current_eye.blink.state == DEBLINK) ? 1 + s : 256 - s;
        
        uint8_t n = (uThreshold * s + 254 * (257 - s)) / 256;
        uThreshold = n;
        lThreshold = n;
    }

    drawEye(currentEye, iris_scale_, eyeX, eyeY, uThreshold, lThreshold);
    scaleBuffer();
    // 添加性能测试代码
    static uint32_t frame_count = 0;
    static uint32_t last_fps_update = 0;
    frame_count++;
    if (t - last_fps_update >= 1000000) {  // 每秒更新一次
        ESP_LOGI("EyeAnimation", "FPS: %ld", frame_count);
        frame_count = 0;
        last_fps_update = t;
    }
}