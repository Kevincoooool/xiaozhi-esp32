#include "EyeAnimation.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <algorithm> // 添加这行来使用 std::min

#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) ({        \
    typeof(addr) _addr = (addr);      \
    *(const unsigned short *)(_addr); \
})

const uint8_t ease[] = {                                                             // Ease in/out curve for eye movements 3*t^2-2*t^3
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3,                                  // T
    3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10,                                // h
    11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23,                  // x
    24, 25, 26, 27, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39,                  // 2
    40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 56, 57, 58,                  // A
    60, 61, 62, 63, 65, 66, 67, 69, 70, 72, 73, 74, 76, 77, 78, 80,                  // l
    81, 83, 84, 85, 87, 88, 90, 91, 93, 94, 96, 97, 98, 100, 101, 103,               // e
    104, 106, 107, 109, 110, 112, 113, 115, 116, 118, 119, 121, 122, 124, 125, 127,  // c
    128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143, 145, 146, 148, 149, 151,  // J
    152, 154, 155, 157, 158, 159, 161, 162, 164, 165, 167, 168, 170, 171, 172, 174,  // a
    175, 177, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 195,  // c
    197, 198, 199, 201, 202, 203, 204, 205, 207, 208, 209, 210, 211, 213, 214, 215,  // o
    216, 217, 218, 219, 220, 221, 222, 224, 225, 226, 227, 228, 228, 229, 230, 231,  // b
    232, 233, 234, 235, 236, 237, 237, 238, 239, 240, 240, 241, 242, 243, 243, 244,  // s
    245, 245, 246, 246, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252,  // o
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
int32_t constrain(int32_t x, int32_t min, int32_t max)
{
    if (x < min)
        return min;
    if (x > max)
        return max;
    return x;
}

EyeAnimation::EyeAnimation()
    : iris_scale_((IRIS_MIN + IRIS_MAX) / 2), 
      scale_(REAL_SCREEN_WIDTH * 1.0f / defaulteye_SCREEN_WIDTH) ,
      sclera_width_(defaulteye_SCLERA_WIDTH), 
      sclera_height_(defaulteye_SCLERA_HEIGHT), 
      iris_width_(defaulteye_IRIS_WIDTH), 
      iris_height_(defaulteye_IRIS_HEIGHT), 
      iris_map_width_(defaulteye_IRIS_MAP_WIDTH), 
      iris_map_height_(defaulteye_IRIS_MAP_HEIGHT), 
      screen_width_(defaulteye_SCREEN_WIDTH), 
      screen_height_(defaulteye_SCREEN_HEIGHT)
{
    render_buffer_ = new uint16_t[screen_width_ * screen_height_];

    scaled_buffer_ = new uint16_t[REAL_SCREEN_WIDTH * REAL_SCREEN_HEIGHT]; // 分配缩放后的缓冲区
}

EyeAnimation::~EyeAnimation()
{
    delete[] render_buffer_;
    delete[] scaled_buffer_;
}

void EyeAnimation::switchEyeType(EyeType type)
{
    if (type >= MAX_EYE_TYPE)
        return;

    current_eye_type_ = type;

    switch (type)
    {
    case DEFAULT_EYE:
    {
        current_sclera_ = defaulteye_sclera;
        current_iris_ = defaulteye_iris;
        current_polar_ = defaulteye_polar;
        current_upper_ = defaulteye_upper;
        current_lower_ = defaulteye_lower;
        sclera_width_ = defaulteye_SCLERA_WIDTH;
        sclera_height_ = defaulteye_SCLERA_HEIGHT;
        iris_width_ = defaulteye_IRIS_WIDTH;
        iris_height_ = defaulteye_IRIS_HEIGHT;
        iris_map_width_ = defaulteye_IRIS_MAP_WIDTH;
        iris_map_height_ = defaulteye_IRIS_MAP_HEIGHT;
        screen_width_ = defaulteye_SCREEN_WIDTH;
        screen_height_ = defaulteye_SCREEN_HEIGHT;
        iris_min_ = IRIS_MIN;
        iris_max_ = IRIS_MAX;
        iris_scale_ = (iris_min_ + iris_max_) / 2;
        break;
    }
    case CAT_EYE:
    {
        current_sclera_ = cateye_sclera;
        current_iris_ = cateye_iris;
        current_upper_ = cateye_upper;
        current_lower_ = cateye_lower;
        sclera_width_ = cateye_SCLERA_WIDTH;
        sclera_height_ = cateye_SCLERA_HEIGHT;
        iris_map_width_ = cateye_IRIS_MAP_WIDTH;
        iris_map_height_ = cateye_IRIS_MAP_HEIGHT;
        screen_width_ = cateye_SCREEN_WIDTH;
        screen_height_ = cateye_SCREEN_HEIGHT;
        iris_min_ = IRIS_MIN;
        iris_max_ = IRIS_MAX;
        iris_scale_ = iris_min_ + (iris_max_ - iris_min_) / 2; // 猫眼瞳孔较小
        break;
    }
    case DRAGON_EYE:
    {
        current_sclera_ = dragoneye_sclera;
        current_iris_ = dragoneye_iris;
        current_upper_ = dragoneye_upper;
        current_lower_ = dragoneye_lower;
        sclera_width_ = dragoneye_SCLERA_WIDTH;
        sclera_height_ = dragoneye_SCLERA_HEIGHT;
        iris_map_width_ = dragoneye_IRIS_MAP_WIDTH;
        iris_map_height_ = dragoneye_IRIS_MAP_HEIGHT;
        screen_width_ = dragoneye_SCREEN_WIDTH;
        screen_height_ = dragoneye_SCREEN_HEIGHT;
        iris_min_ = IRIS_MIN;
        iris_max_ = IRIS_MAX;
        iris_scale_ = (iris_min_ + iris_max_) / 2;
        break;
    }
    case GOAT_EYE:
    {
        current_sclera_ = goateye_sclera;
        current_iris_ = goateye_iris;
        current_upper_ = goateye_upper;
        current_lower_ = goateye_lower;
        sclera_width_ = goateye_SCLERA_WIDTH;
        sclera_height_ = goateye_SCLERA_HEIGHT;
        iris_map_width_ = goateye_IRIS_MAP_WIDTH;
        iris_map_height_ = goateye_IRIS_MAP_HEIGHT;
        screen_width_ = goateye_SCREEN_WIDTH;
        screen_height_ = goateye_SCREEN_HEIGHT;
        iris_min_ = IRIS_MIN;
        iris_max_ = IRIS_MAX;
        iris_scale_ = (iris_min_ + iris_max_) / 2;
        break;
    }
    case NEWT_EYE:
    {
        current_sclera_ = newteye_sclera;
        current_iris_ = newteye_iris;
        current_polar_ = newteye_polar;
        current_upper_ = newteye_upper;
        current_lower_ = newteye_lower;
        sclera_width_ = newteye_SCLERA_WIDTH;
        sclera_height_ = newteye_SCLERA_HEIGHT;
        iris_width_ = newteye_IRIS_WIDTH;
        iris_height_ = newteye_IRIS_HEIGHT;
        iris_map_width_ = newteye_IRIS_MAP_WIDTH;
        iris_map_height_ = newteye_IRIS_MAP_HEIGHT;
        screen_width_ = newteye_SCREEN_WIDTH;
        screen_height_ = newteye_SCREEN_HEIGHT;
        iris_min_ = IRIS_MIN;
        iris_max_ = IRIS_MAX;
        iris_scale_ = (iris_min_ + iris_max_) / 2;
        break;
    }
    case TERMINATOR_EYE:
    {
        current_sclera_ = terminatoreye_sclera;
        current_iris_ = terminatoreye_iris;
        current_polar_ = terminatoreye_polar;
        current_upper_ = terminatoreye_upper;
        current_lower_ = terminatoreye_lower;
        sclera_width_ = terminatoreye_SCLERA_WIDTH;
        sclera_height_ = terminatoreye_SCLERA_HEIGHT;
        iris_width_ = terminatoreye_IRIS_WIDTH;
        iris_height_ = terminatoreye_IRIS_HEIGHT;
        iris_map_width_ = terminatoreye_IRIS_MAP_WIDTH;
        iris_map_height_ = terminatoreye_IRIS_MAP_HEIGHT;
        screen_width_ = terminatoreye_SCREEN_WIDTH;
        screen_height_ = terminatoreye_SCREEN_HEIGHT;
        iris_min_ = IRIS_MIN;
        iris_max_ = IRIS_MAX;
        iris_scale_ = (iris_min_ + iris_max_) / 2;
        break;
    }
    default:
        break;
        // ... 其他眼球类型的类似设置 ...
    }
}
#include "esp_dsp.h"
#include "esp_attr.h"

// 添加SIMD优化的scaleBuffer实现
void IRAM_ATTR EyeAnimation::scaleBuffer()
{
    const int src_width = screen_width_;
    const int src_height = screen_height_;
    const int dst_width = REAL_SCREEN_WIDTH;
    const int dst_height = REAL_SCREEN_HEIGHT;
    
    // 如果源尺寸和目标尺寸相同，直接复制并交换字节
    if (src_width == dst_width && src_height == dst_height)
    {
        // for (int i = 0; i < src_width * src_height; i++)
        // {
        //     uint16_t color = render_buffer_[i];
        //     scaled_buffer_[i] = (color >> 8) | (color << 8);
        // }
        memcpy(scaled_buffer_, render_buffer_, src_width * src_height * sizeof(uint16_t));
        return;
    }

    // 使用查找表预计算缩放索引，避免每次都计算
    static int16_t *x_map = nullptr;
    static int16_t *y_map = nullptr;

    if (x_map == nullptr)
    {
        x_map = new int16_t[dst_width];
        y_map = new int16_t[dst_height];

        const float x_ratio = src_width / (float)dst_width;
        const float y_ratio = src_height / (float)dst_height;

        for (int x = 0; x < dst_width; x++)
        {
            x_map[x] = (int)(x * x_ratio);
        }

        for (int y = 0; y < dst_height; y++)
        {
            y_map[y] = (int)(y * y_ratio) * src_width;
        }
    }

// 使用查找表加速缩放过程
#pragma GCC unroll 4
    for (int y = 0; y < dst_height; y++)
    {
        uint16_t *dst_line = scaled_buffer_ + y * dst_width;
        const int y_offset = y_map[y];

#pragma GCC unroll 8
        for (int x = 0; x < dst_width; x++)
        {
            uint16_t color = render_buffer_[y_offset + x_map[x]];
            // dst_line[x] = (color >> 8) | (color << 8); // 交换高低字节
            dst_line[x] = color; // 混合颜色
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
                           uint32_t lThreshold)
{
    // 预计算常量值
    const int32_t iris_offset_x = (sclera_width_ - iris_width_) / 2;
    const int32_t iris_offset_y = (sclera_height_ - iris_height_) / 2;

    // 预计算虹膜位置
    int32_t irisY = scleraY - iris_offset_y;
    const int32_t initial_irisX = scleraX - iris_offset_x;

    // 预计算阈值边界，避免每个像素都计算
    // 预计算阈值边界，增加过渡区域宽度
    const float upper_edge0 = uThreshold - 12; // 从8增加到12
    const float upper_edge1 = uThreshold + 12; // 从8增加到12
    const float lower_edge0 = lThreshold - 12; // 从8增加到12
    const float lower_edge1 = lThreshold + 12; // 从8增加到12

// 使用DMA批量处理，每次处理一行
#pragma GCC unroll 4
    for (uint32_t screenY = 0; screenY < screen_height_; screenY++)
    {
        int32_t irisX = initial_irisX;
        uint32_t current_scleraX = scleraX;

        // 预取眼睑数据
        const uint8_t *upper_row = current_upper_ + screenY * screen_width_;
        const uint8_t *lower_row = current_lower_ + screenY * screen_width_;

        uint16_t *row = render_buffer_ + screenY * screen_width_;

// 批量处理一行中的像素
#pragma GCC unroll 8
        for (uint32_t screenX = 0; screenX < screen_width_; screenX++)
        {
            // 计算对称位置的X坐标
            uint32_t mirror_x = screen_width_ - 1 - screenX;

            // 对称处理眼睑，使用加权平均而不是简单平均
            uint8_t upper_value = (upper_row[screenX] * 0.6f + upper_row[mirror_x] * 0.4f);
            uint8_t lower_value = (lower_row[screenX] * 0.6f + lower_row[mirror_x] * 0.4f);

            // 应用眼睑间距调整
            if (upper_value > eyelid_gap_)
            {
                upper_value -= eyelid_gap_;
            }
            if (lower_value > eyelid_gap_)
            {
                lower_value -= eyelid_gap_;
            }

            // 快速检查是否在眼睑外，避免复杂计算
            if (upper_value < upper_edge0 || lower_value < lower_edge0)
            {
                row[screenX] = 0;
                irisX++;
                current_scleraX++;
                continue;
            }

            // 计算边缘平滑因子 (0.0 - 1.0)
            float upper_alpha = smoothstep(upper_edge0, upper_edge1, upper_value);
            float lower_alpha = smoothstep(lower_edge0, lower_edge1, lower_value);

            // 如果完全在眼睑外，直接绘制黑色
            if (upper_alpha <= 0.0f || lower_alpha <= 0.0f)
            {
                row[screenX] = 0;
                irisX++;
                current_scleraX++;
                continue;
            }

            // 计算眼球颜色 - 简化判断逻辑
            uint16_t eye_color;
            if (irisY >= 0 && irisY < iris_height_ &&
                irisX >= 0 && irisX < iris_width_)
            {
                uint16_t p = pgm_read_word(current_polar_ + irisY * iris_width_ + irisX);
                uint32_t dist = (iScale * (p & 0x7F)) >> 7;

                if (dist < iris_map_height_)
                {
                    uint32_t angle = (iris_map_width_ * (p >> 7)) >> 9;
                    eye_color = pgm_read_word(current_iris_ + dist * iris_map_width_ + angle);
                }
                else
                {
                    eye_color = pgm_read_word(current_sclera_ + scleraY * sclera_width_ + current_scleraX);
                }
            }
            else
            {
                eye_color = pgm_read_word(current_sclera_ + scleraY * sclera_width_ + current_scleraX);
            }

            // 应用边缘平滑 - 使用更精确的混合方法
            float alpha = (upper_alpha < lower_alpha) ? upper_alpha : lower_alpha;

            // 对边缘进行额外的平滑处理
            if (alpha < 0.99f)
            {
                // 使用二次方增强边缘平滑度
                alpha = alpha * alpha * (3.0f - 2.0f * alpha);
                row[screenX] = blend_color(0, eye_color, alpha);
            }
            else
            {
                row[screenX] = eye_color;
            }
            irisX++;
            current_scleraX++;
        }
        scleraY++;
        irisY++;
    }
}
// 添加辅助函数

// 平滑过渡函数
float EyeAnimation::smoothstep(float edge0, float edge1, float x)
{
    x = constrain((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

// 颜色混合函数
// 在blend_color函数中增强颜色混合精度
uint16_t EyeAnimation::blend_color(uint16_t c1, uint16_t c2, float alpha)
{
    // 对于常见的alpha值使用查找表
    if (alpha <= 0.0f)
        return c1;
    if (alpha >= 1.0f)
        return c2;

    // 使用更高精度的整数计算
    uint16_t a = (uint16_t)(alpha * 1024);

    // 解包RGB565，扩展到更高位深
    uint16_t r1 = (c1 >> 11) & 0x1F;
    uint16_t g1 = (c1 >> 5) & 0x3F;
    uint16_t b1 = c1 & 0x1F;

    uint16_t r2 = (c2 >> 11) & 0x1F;
    uint16_t g2 = (c2 >> 5) & 0x3F;
    uint16_t b2 = c2 & 0x1F;

    // 扩展颜色深度以提高混合精度
    r1 = (r1 << 3) | (r1 >> 2);
    g1 = (g1 << 2) | (g1 >> 4);
    b1 = (b1 << 3) | (b1 >> 2);

    r2 = (r2 << 3) | (r2 >> 2);
    g2 = (g2 << 2) | (g2 >> 4);
    b2 = (b2 << 3) | (b2 >> 2);

    // 使用更高精度的插值
    uint16_t r = (r1 * (1024 - a) + r2 * a) >> 10;
    uint16_t g = (g1 * (1024 - a) + g2 * a) >> 10;
    uint16_t b = (b1 * (1024 - a) + b2 * a) >> 10;

    // 转换回RGB565
    r = (r >> 3) & 0x1F;
    g = (g >> 2) & 0x3F;
    b = (b >> 3) & 0x1F;

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
    static uint32_t last_eye_switch = 0; // 添加眼球切换计时器

    // 每2秒切换一次眼球类型
    // if (t - last_eye_switch >= 3000000)
    // { // 2000000 微秒 = 2秒
    //     current_eye_type_ = static_cast<EyeType>((static_cast<int>(current_eye_type_) + 1) % MAX_EYE_TYPE);
    //     switchEyeType(current_eye_type_);
    //     last_eye_switch = t;
    //     ESP_LOGI("EyeAnimation", "Switched to eye type: %d", current_eye_type_);
    // }
    // 减少每帧的计算量 - 可以考虑降低更新频率
    static uint32_t last_update = 0;
    if (t - last_update < 16667)
    { // 限制为最多60fps
        return;
    }
    last_update = t;
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
                eyeNewY = esp_random() % 800;
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
    static uint32_t timeOfLastBlink = 0;
    static uint32_t timeToNextBlink = 0;
    
     t = esp_timer_get_time();
    
    // 自动眨眼逻辑
    if (t - timeOfLastBlink >= timeToNextBlink) // 是否该眨眼了
    {
        timeOfLastBlink = t;
        timeToNextBlink = 2000000 + esp_random() % 4000000; // 2-6 秒后再次眨眼
        
        // 开始新的眨眼
        for (auto &eye : eyes_) {
            eye.blink.state = ENBLINK;
            eye.blink.startTime = t;
            eye.blink.duration = 35000 + esp_random() % 35000; // 35-70ms 眨眼持续时间
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
            next_blink_delay_ = eye.blink.duration * 3 + (esp_random() % 6000000);
        }
    }

    // 映射坐标到实际像素范围
    eyeX = map(eyeX, 0, 1023, 0, sclera_width_ - screen_width_);
    eyeY = map(eyeY, 0, 1023, 0, sclera_height_ - screen_height_);

    // 限制移动范围
    eyeX = constrain(eyeX, 0, sclera_width_ - screen_width_);
    eyeY = constrain(eyeY, 0, sclera_height_ - screen_height_-30);

    // 修改眼睑阈值计算
    // 修改眼睑阈值计算
    static uint8_t uThreshold = 128;
    uint8_t lThreshold = 0;

#ifdef TRACKING
    // 修正采样点计算，与原始代码保持一致
    int16_t sampleX = sclera_width_ / 2 - (eyeX / 2);
    int16_t sampleY = sclera_height_ / 2 - (eyeY + iris_height_ / 2);
    
    // 确保采样点在有效范围内
    if (sampleY >= 0 && sampleY < screen_height_ && sampleX >= 0 && sampleX < screen_width_) {
        uint8_t n = pgm_read_byte(current_upper_ + sampleY * screen_width_ + sampleX);
        uThreshold = (uThreshold * 3 + n) / 4;
        lThreshold = 254 - uThreshold;
    }
#else
    uThreshold = 28;
    lThreshold = 158;
#endif

    // 处理眨眼
    auto &current_eye = eyes_[currentEye];
    if (current_eye.blink.state)
    {
        uint32_t s = t - current_eye.blink.startTime;
        if (s >= current_eye.blink.duration) {
            s = 255;
        } else {
            s = (255 * s) / current_eye.blink.duration;
        }
        
        // 修改眨眼状态处理逻辑，参考原始代码
        if (current_eye.blink.state == DEBLINK) {
            s = constrain(s, 0, 255);  // 睁眼过程
        } else {
            s = constrain(255 - s, 0, 255);  // 闭眼过程
        }

        // 计算眼睑阈值，参考原始逻辑
        uint8_t n = (uThreshold * s + 254 * (255 - s)) / 255;
        uThreshold = lThreshold = constrain(n, 0, 254);
    }

    drawEye(currentEye, iris_scale_, eyeX, eyeY, uThreshold, lThreshold);
    scaleBuffer();
    // 添加性能测试代码
    static uint32_t frame_count = 0;
    static uint32_t last_fps_update = 0;
    frame_count++;
    if (t - last_fps_update >= 1000000)
    { // 每秒更新一次
        ESP_LOGI("EyeAnimation", "FPS: %ld", frame_count);
        frame_count = 0;
        last_fps_update = t;
    }
}