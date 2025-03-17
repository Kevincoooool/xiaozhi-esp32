#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>

#include "display.h"
#include "board.h"
#include "application.h"
#include "font_awesome_symbols.h"
#include "audio_codec.h"
#include "settings.h"
#include "assets/lang_config.h"

#define TAG "Display"

Display::Display() {
    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback = [](void *arg) {
            Display *display = static_cast<Display*>(arg);
            DisplayLockGuard lock(display);
            lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Update display timer
    esp_timer_create_args_t update_display_timer_args = {
        .callback = [](void *arg) {
            Display *display = static_cast<Display*>(arg);
            display->Update();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_update_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&update_display_timer_args, &update_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(update_timer_, 1000000));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

Display::~Display() {
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }
    if (update_timer_ != nullptr) {
        esp_timer_stop(update_timer_);
        esp_timer_delete(update_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
        lv_obj_del(notification_label_);
        lv_obj_del(status_label_);
        lv_obj_del(mute_label_);
        lv_obj_del(battery_label_);
        lv_obj_del(emotion_label_);
    }

    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

void Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
}

void Display::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void Display::Update() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // 如果静音状态改变，则更新图标
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, FONT_AWESOME_VOLUME_MUTE);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    esp_pm_lock_acquire(pm_lock_);
    // 更新电池图标
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = FONT_AWESOME_BATTERY_CHARGING;
        } else {
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY, // 0-19%
                FONT_AWESOME_BATTERY_1,    // 20-39%
                FONT_AWESOME_BATTERY_2,    // 40-59%
                FONT_AWESOME_BATTERY_3,    // 60-79%
                FONT_AWESOME_BATTERY_FULL, // 80-99%
                FONT_AWESOME_BATTERY_FULL, // 100%
            };
            icon = levels[battery_level / 20];
        }
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }

        if (low_battery_popup_ != nullptr) {
            if (strcmp(icon, FONT_AWESOME_BATTERY_EMPTY) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) { // 如果低电量提示框隐藏，则显示
                    lv_obj_clear_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    auto& app = Application::GetInstance();
                    app.PlaySound(Lang::Sounds::P3_LOW_BATTERY);
                }
            } else {
                // Hide the low battery popup when the battery is not empty
                if (!lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) { // 如果低电量提示框显示，则隐藏
                    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // 升级固件时，不读取 4G 网络状态，避免占用 UART 资源
    auto device_state = Application::GetInstance().GetDeviceState();
    static const std::vector<DeviceState> allowed_states = {
        kDeviceStateIdle,
        kDeviceStateStarting,
        kDeviceStateWifiConfiguring,
        kDeviceStateListening,
    };
    if (std::find(allowed_states.begin(), allowed_states.end(), device_state) != allowed_states.end()) {
        icon = board.GetNetworkStateIcon();
        if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
            DisplayLockGuard lock(this);
            network_icon_ = icon;
            lv_label_set_text(network_label_, network_icon_);
        }
    }

    esp_pm_lock_release(pm_lock_);
}


void Display::SetEmotion(const char* emotion) {
    struct Emotion {
        const char* icon;
        const char* text;
    };

    static const std::vector<Emotion> emotions = {
        {FONT_AWESOME_EMOJI_NEUTRAL, "neutral"},
        {FONT_AWESOME_EMOJI_HAPPY, "happy"},
        {FONT_AWESOME_EMOJI_LAUGHING, "laughing"},
        {FONT_AWESOME_EMOJI_FUNNY, "funny"},
        {FONT_AWESOME_EMOJI_SAD, "sad"},
        {FONT_AWESOME_EMOJI_ANGRY, "angry"},
        {FONT_AWESOME_EMOJI_CRYING, "crying"},
        {FONT_AWESOME_EMOJI_LOVING, "loving"},
        {FONT_AWESOME_EMOJI_EMBARRASSED, "embarrassed"},
        {FONT_AWESOME_EMOJI_SURPRISED, "surprised"},
        {FONT_AWESOME_EMOJI_SHOCKED, "shocked"},
        {FONT_AWESOME_EMOJI_THINKING, "thinking"},
        {FONT_AWESOME_EMOJI_WINKING, "winking"},
        {FONT_AWESOME_EMOJI_COOL, "cool"},
        {FONT_AWESOME_EMOJI_RELAXED, "relaxed"},
        {FONT_AWESOME_EMOJI_DELICIOUS, "delicious"},
        {FONT_AWESOME_EMOJI_KISSY, "kissy"},
        {FONT_AWESOME_EMOJI_CONFIDENT, "confident"},
        {FONT_AWESOME_EMOJI_SLEEPY, "sleepy"},
        {FONT_AWESOME_EMOJI_SILLY, "silly"},
        {FONT_AWESOME_EMOJI_CONFUSED, "confused"}
    };
    
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });
    
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    if (it != emotions.end()) {
        lv_label_set_text(emotion_label_, it->icon);
    } else {
        lv_label_set_text(emotion_label_, FONT_AWESOME_EMOJI_NEUTRAL);
    }
}

void Display::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_label_set_text(emotion_label_, icon);
}
void Display::ShowClockView(bool show) {

}
// void Display::SetChatMessage(const char* role, const char* content) {
    // DisplayLockGuard lock(this);
    // if (chat_message_label_ == nullptr) {
    //     return;
    // }
    // lv_label_set_text(chat_message_label_, content);
// }
void Display::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_messages_container_ == nullptr || content == nullptr || strlen(content) < 4) {
        return;
    }
 // 在添加新消息前获取当前滚动位置
    lv_coord_t scroll_y = lv_obj_get_scroll_y(chat_messages_container_);
    lv_coord_t scroll_max = lv_obj_get_scroll_bottom(chat_messages_container_);
    bool should_scroll = (scroll_max - scroll_y <= 30);

    // 创建新的消息气泡
    lv_obj_t* msg_bubble = lv_obj_create(chat_messages_container_);
    lv_obj_set_style_radius(msg_bubble, 10, 0);  // 圆角半径
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(msg_bubble, 12, 0);  // 增加内边距
    
    // 设置消息容器的flex布局
    lv_obj_set_flex_flow(chat_messages_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chat_messages_container_, 12, 0);  // 增加消息间距
    
    bool is_user = (strcmp(role, "user") == 0);
    
    // 将气泡宽度设置为屏幕宽度的75%
    
    // 创建一个临时标签来计算文本宽度
    lv_obj_t* temp_label = lv_label_create(lv_screen_active());
    lv_label_set_text(temp_label, content);
    lv_obj_update_layout(temp_label);  // 更新布局以获取实际宽度
    
    // 获取文本实际宽度，并添加一些边距
    lv_coord_t text_width = lv_obj_get_width(temp_label) + 30;  // 30为左右内边距总和
    
    // 设置气泡宽度范围
    lv_coord_t min_width = LV_HOR_RES * 30 / 100;  // 最小宽度为屏幕宽度的30%
    lv_coord_t max_width = LV_HOR_RES * 85 / 100;  // 最大宽度为屏幕宽度的85%
    
    // 计算实际使用的宽度
    lv_coord_t bubble_width = text_width;
    if (bubble_width < min_width) bubble_width = min_width;
    if (bubble_width > max_width) bubble_width = max_width;
    
    // 删除临时标签
    lv_obj_del(temp_label);
    
    // 设置气泡宽度
    lv_obj_set_width(msg_bubble, bubble_width);
    // 根据角色设置样式和位置
    if (is_user) {
        // 用户消息 - 绿色背景，靠右对齐
        lv_obj_set_style_bg_color(msg_bubble, lv_color_make(149, 236, 105), 0);
        lv_obj_set_style_border_width(msg_bubble, 0, 0);
        lv_obj_set_style_margin_left(msg_bubble, LV_HOR_RES - bubble_width - 30, 0);  // 左边留空
        lv_obj_set_style_margin_right(msg_bubble, 30, 0);  // 右边距12像素
    } else {
        // AI助手消息 - 白色背景，靠左对齐
        lv_obj_set_style_bg_color(msg_bubble, lv_color_white(), 0);
        lv_obj_set_style_border_width(msg_bubble, 1, 0);
        lv_obj_set_style_border_color(msg_bubble, lv_color_make(220, 220, 220), 0);
        lv_obj_set_style_margin_left(msg_bubble, 12, 0);  // 左边距12像素
        lv_obj_set_style_margin_right(msg_bubble, LV_HOR_RES - bubble_width - 12, 0);  // 右边留空
    }
    // 创建消息文本标签
    lv_obj_t* msg_label = lv_label_create(msg_bubble);
    lv_obj_set_width(msg_label, LV_PCT(100));  // 设置标签宽度为气泡的100%
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);  // 允许文本换行
    lv_obj_set_style_text_color(msg_label, lv_color_black(), 0);
    lv_label_set_text(msg_label, content);

    // 设置文本对齐方式
    if (is_user) {
        lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_RIGHT, 0);
    } else {
        lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_LEFT, 0);
    }

    // 设置消息容器的flex布局
    lv_obj_set_flex_flow(chat_messages_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chat_messages_container_, 12, 0);  // 消息间距
    lv_obj_set_style_pad_bottom(chat_messages_container_, 100, 0);  // 底部留白100像素
    
    // 强制布局更新
    lv_obj_update_layout(chat_messages_container_);
    
    // 滚动到最新消息
    if (should_scroll) {
        lv_obj_scroll_to_view(msg_bubble, LV_ANIM_ON);
    }
}