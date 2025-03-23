#pragma once
#include <vector>
#include <stdint.h>
#include "config.h"

class EyeAnimation {
public:
    // 眨眼状态常量
    static constexpr uint8_t NOBLINK = 0;
    static constexpr uint8_t ENBLINK = 1;
    static constexpr uint8_t DEBLINK = 2;

    EyeAnimation();
    ~EyeAnimation();
    
    void begin();
    void update();
    
    // 获取渲染缓冲区
    const uint16_t* getBuffer() const { return render_buffer_; }
    uint16_t getWidth() const { return SCREEN_WIDTH; }
    uint16_t getHeight() const { return SCREEN_HEIGHT; }
    // 添加眼睑间距设置方法
    void setEyelidGap(uint8_t gap) { eyelid_gap_ = gap; }
    uint8_t getEyelidGap() const { return eyelid_gap_; }
    const uint16_t* getScaledBuffer() const { return scaled_buffer_; }
    void setScale(float scale) { scale_ = scale; }
    float getScale() const { return scale_; }
    float smoothstep(float edge0, float edge1, float x);
    uint16_t blend_color(uint16_t c1, uint16_t c2, float alpha);
private:
    struct Eye {
        struct {
            uint8_t state;     // NOBLINK/ENBLINK/DEBLINK
            uint32_t duration;  // Duration of blink state
            uint32_t startTime; // Time of last state change
        } blink;
        int16_t xposition;     // Eye X position 
    };

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
};