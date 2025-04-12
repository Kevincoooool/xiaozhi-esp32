#pragma once
#include <vector>
#include <stdint.h>
#include "config.h"

class EyeAnimation {
public:
// 添加眼球类型枚举
    enum EyeType {
        DEFAULT_EYE = 0,
        CAT_EYE,
        DRAGON_EYE,
        GOAT_EYE,
        NEWT_EYE,
        TERMINATOR_EYE,
        MAX_EYE_TYPE
    };
// 情感状态枚举
    enum EmotionState {
        EMOTION_NORMAL,    // 正常状态
        EMOTION_ANGRY,     // 生气
        EMOTION_SLEEPY,    // 困倦
        EMOTION_EXCITED,   // 兴奋
        EMOTION_SAD        // 悲伤
    };

    // 情感控制函数
    void setEmotion(EmotionState emotion);
    void setEyelidGap(uint8_t gap);     // 控制眼睑间距
    void setIrisScale(uint16_t scale);   // 直接控制瞳孔大小
    void setBlinkRate(uint32_t rate);    // 控制眨眼频率
    // 眨眼状态常量
    static constexpr uint8_t NOBLINK = 0;
    static constexpr uint8_t ENBLINK = 1;
    static constexpr uint8_t DEBLINK = 2;

    EyeAnimation();
    ~EyeAnimation();
    
    void begin();
    void update();
    // 添加眼球切换方法
    void switchEyeType(EyeType type);
    EyeType getCurrentEyeType() const { return current_eye_type_; }
    // 获取渲染缓冲区
    const uint16_t* getBuffer() const { return render_buffer_; }
    uint16_t getWidth() const { return screen_width_; }
    uint16_t getHeight() const { return screen_height_; }
    // 添加眼睑间距设置方法
    // void setEyelidGap(uint8_t gap) { eyelid_gap_ = gap; }
    uint8_t getEyelidGap() const { return eyelid_gap_; }
    const uint16_t* getScaledBuffer() const { return scaled_buffer_; }
    void setScale(float scale) { scale_ = scale; }
    float getScale() const { return scale_; }
    float smoothstep(float edge0, float edge1, float x);
    uint16_t blend_color(uint16_t c1, uint16_t c2, float alpha);
    float eyelidSmooth(uint8_t value, uint32_t x, uint32_t screenWidth);
private:
    EmotionState current_emotion_ = EMOTION_NORMAL;
    uint8_t eyelid_base_gap_ = 10;      // 基础眼睑间距
    uint32_t blink_interval_ = 2000000;  // 基础眨眼间隔(微秒)
    struct Eye {
        struct {
            uint8_t state;     // NOBLINK/ENBLINK/DEBLINK
            uint32_t duration;  // Duration of blink state
            uint32_t startTime; // Time of last state change
        } blink;
        int16_t xposition;     // Eye X position 
    };
    inline uint16_t swapRedBlue(uint16_t color);
    void drawEye(uint8_t eye_index, 
                 uint32_t iris_scale,
                 uint32_t sclera_x, 
                 uint32_t sclera_y,
                 uint32_t upper_threshold,
                 uint32_t lower_threshold);


    
    std::vector<Eye> eyes_;
    uint16_t* render_buffer_; // RGB565格式缓冲区
    uint32_t last_update_;    // Last animation update time
    uint32_t last_blink_;
    uint32_t next_blink_delay_;
    uint16_t iris_scale_;
    uint8_t eyelid_gap_ = 20;  // 默认眼睑间距
    void scaleBuffer();  // 缩放缓冲区的方法
    
    uint16_t* scaled_buffer_;  // 缩放后的缓冲区
    float scale_;             // 缩放比例


  // 替换原来的宏定义为成员变量
    uint16_t sclera_width_;    // 替换 SCLERA_WIDTH
    uint16_t sclera_height_;   // 替换 SCLERA_HEIGHT
    uint16_t iris_width_;      // 替换 IRIS_WIDTH
    uint16_t iris_height_;     // 替换 IRIS_HEIGHT
    uint16_t iris_map_width_;  // 替换 IRIS_MAP_WIDTH
    uint16_t iris_map_height_; // 替换 IRIS_MAP_HEIGHT
    uint16_t screen_width_;    // 替换 SCREEN_WIDTH
    uint16_t screen_height_;   // 替换 SCREEN_HEIGHT
    uint16_t iris_min_;        // 替换 IRIS_MIN
    uint16_t iris_max_;        // 替换 IRIS_MAX
    
    // 添加眼球类型相关成员
    EyeType current_eye_type_ = DEFAULT_EYE;
    const uint16_t* current_sclera_ = defaulteye_sclera;
    const uint16_t* current_iris_ = defaulteye_iris;
    const uint8_t* current_upper_ = defaulteye_upper;
    const uint8_t* current_lower_ = defaulteye_lower;
    const uint16_t* current_polar_ = defaulteye_polar;
    uint16_t current_iris_width_ = defaulteye_IRIS_WIDTH;
    uint16_t current_iris_height_ = defaulteye_IRIS_HEIGHT;
    uint16_t current_sclera_width_ = defaulteye_SCLERA_WIDTH;
    uint16_t current_sclera_height_ = defaulteye_SCLERA_HEIGHT;
};
