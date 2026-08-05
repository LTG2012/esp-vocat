#include "dev_tools.h"
#include "esp_vocat.h"
#include "vocat_base_control.h"
#include "audio_analysis.h"
#include "mcp_server.h"
#include "board.h"
#include "assets/lang_config.h"
#include "display/emote_display.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>
#include "customer_ui/alarm_api.h"
#include "ui_bridge.h"
#include "magnetic_monitor.h"

#define TAG "DevTools"

void DevTools::Initialize(EspS3Cat* board)
{
    auto &mcp_server = McpServer::GetInstance();
    char buffer[1024];
    const bool is_zh = (std::strncmp(Lang::CODE, "zh", 2) == 0);

    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "查询设备实时电池信息。用户询问“当前电量”“还剩多少电”“是否在充电”“充电电流多少”“电池电压”等问题时，必须调用此工具。"
                  :
                  "Get real-time battery information. Use this tool whenever the user asks about battery level, charging state, charge current, or battery voltage.");
    mcp_server.AddTool("self.battery.get_status", buffer, PropertyList(), [board, is_zh](const PropertyList& properties) -> ReturnValue {
        BatteryStatus status;
        if (!board->GetBatteryStatus(status)) {
            return is_zh ? "暂时无法读取电池信息。" : "Battery information is currently unavailable.";
        }

        char response[160];
        const int current_ma = status.current_ma >= 0 ? status.current_ma : -status.current_ma;
        if (status.charging) {
            std::snprintf(response, sizeof(response), is_zh ?
                          "当前电量%d%%，电池电压%d毫伏，正在充电，充电电流%d毫安。" :
                          "Battery level is %d%%, voltage is %d mV, charging at %d mA.",
                          status.level, status.voltage_mv, current_ma);
        } else if (status.discharging) {
            std::snprintf(response, sizeof(response), is_zh ?
                          "当前电量%d%%，电池电压%d毫伏，正在放电，放电电流%d毫安。" :
                          "Battery level is %d%%, voltage is %d mV, discharging at %d mA.",
                          status.level, status.voltage_mv, current_ma);
        } else if (status.level >= 100) {
            std::snprintf(response, sizeof(response), is_zh ?
                          "当前电量已满，电池电压%d毫伏，当前电流%d毫安。" :
                          "Battery is full, voltage is %d mV, current is %d mA.",
                          status.voltage_mv, current_ma);
        } else {
            std::snprintf(response, sizeof(response), is_zh ?
                          "当前电量%d%%，电池电压%d毫伏，当前未充电，电流%d毫安。" :
                          "Battery level is %d%%, voltage is %d mV, not charging, current is %d mA.",
                          status.level, status.voltage_mv, current_ma);
        }

        if (auto* display = board->GetDisplay(); display != nullptr) {
            display->ShowNotification(response, 5000);
        }
        return std::string(response);
    });

    // Echo base action control
    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "底座动作控制。可选动作:\n"
                  "shark_head: 摇头动作\n"
                  "shark_head_decay: 缓慢摇头动作\n"
                  "look_around: 环顾四周动作\n"
                  "beat_swing: 节拍摇摆动作\n"
                  "cat_nuzzle: 蹭头撒娇动作\n"
                  "calibrate: 校准底座\n"
                  "go_home: 返回主页面\n"
                  :
                  "Echo base action control. Available actions:\n"
                  "shark_head: shake head\n"
                  "shark_head_decay: slow shake head\n"
                  "look_around: look around\n"
                  "beat_swing: beat swing\n"
                  "cat_nuzzle: cat nuzzle\n"
                  "calibrate: calibrate base\n"
                  "go_home: switch to home page\n");
    mcp_server.AddTool("self.echo_base.set_action", buffer,
    PropertyList({
        Property("action", kPropertyTypeString),
    }), [board](const PropertyList & properties) -> ReturnValue {
        const std::string &action = properties["action"].value<std::string>();
        int action_value = -1;

        ESP_LOGI(TAG, "&&& Do Action: %s", action.c_str());
        if (action == "shark_head") {
            action_value = VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD;
        } else if (action == "shark_head_decay") {
            action_value = VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD_DECAY;
        } else if (action == "look_around")
        {
            action_value = VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND;
        } else if (action == "beat_swing")
        {
            action_value = VOCAT_BASE_CMD_SET_ACTION_BEAT_SWING;
        } else if (action == "cat_nuzzle")
        {
            action_value = VOCAT_BASE_CMD_SET_ACTION_CAT_NUZZLE;
        } else if (action == "calibrate")
        {
            vocat_base_control_set_calibrate();
            BaseControl* base_control = board->GetBaseControl();
            if (base_control != nullptr) {
                bool completed = base_control->WaitForCalibrationComplete(30000);
                if (!completed) {
                    ESP_LOGW(TAG, "Calibration wait timeout");
                    return false;
                }
            }
        } else if (action == "go_home")
        {
            if (magnetic_monitor_is_active()) {
                vocat_base_control_set_magnetic_monitor(false);
                magnetic_monitor_hide();
            }
            ui_bridge_switch_page(UI_BRIDGE_PAGE_HOME);
        } else
        {
            return false;
        }

        if (action_value != -1)
        {
            esp_err_t ret = vocat_base_control_set_action(action_value);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set action: %d", ret);
                return false;
            }
        }
        return true;
    });

    // Emotion-triggered base action switch
    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "控制表情触发底座动作的开关，默认关闭。用户说‘打开表情摇头开关’时 enabled=true；说‘关闭表情摇头开关’时 enabled=false。"
                  :
                  "Control the switch for emotion-triggered base actions; disabled by default. Set enabled=true for ‘open emotion head-shake switch’ and false for ‘close emotion head-shake switch’.");
    mcp_server.AddTool("self.echo_base.set_emotion_shake", buffer,
    PropertyList({
        Property("enabled", kPropertyTypeBoolean),
    }), [board](const PropertyList & properties) -> ReturnValue {
        const bool enabled = properties["enabled"].value<bool>();
        Display* display = board->GetDisplay();
        emote::EmoteDisplay* emote_display = dynamic_cast<emote::EmoteDisplay*>(display);
        if (emote_display == nullptr) {
            ESP_LOGE(TAG, "Emote display is not available");
            return false;
        }

        emote_display->SetEmotionShakeEnabled(enabled);
        return true;
    });

    // Echo base relative angle control
    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "按相对中心角度控制底座。direction 使用 left/right/center，angle 为 0-90 度。\n"
                  "例如 direction=left、angle=45 表示转到中心左侧 45 度；direction=right、angle=30 表示转到中心右侧 30 度。"
                  :
                  "Set the echo base target angle relative to center. Use direction left/right/center and angle 0-90 degrees.\n"
                  "For example, left + 45 means 45 degrees left of center, and right + 30 means 30 degrees right of center.");
    mcp_server.AddTool("self.echo_base.set_angle", buffer,
    PropertyList({
        Property("direction", kPropertyTypeString),
        Property("angle", kPropertyTypeInteger, 0, 0, 90),
    }), [](const PropertyList & properties) -> ReturnValue {
        const std::string &direction = properties["direction"].value<std::string>();
        const int angle = properties["angle"].value<int>();
        int target_angle = 90;
        int relative_angle = 0;

        if (direction == "left" || direction == "向左" || direction == "左") {
            relative_angle = -angle;
            target_angle = 90 + relative_angle;
        } else if (direction == "right" || direction == "向右" || direction == "右") {
            relative_angle = angle;
            target_angle = 90 + relative_angle;
        } else if (direction != "center" && direction != "middle" && direction != "中心") {
            ESP_LOGE(TAG, "Unknown base angle direction: %s", direction.c_str());
            return false;
        }

        ESP_LOGI(TAG, "Set base angle: direction=%s, relative=%d, target=%d", direction.c_str(),
                 relative_angle, target_angle);
        return vocat_base_control_set_angle(target_angle) == ESP_OK;
    });

    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "控制滑块磁场监测页面。用户说‘进入滑块监测’或‘打开滑块监测’时 enabled=true；说‘退出滑块监测’、‘关闭滑块监测’或‘返回主页’时 enabled=false。监测期间滑块动作只展示，不执行日常对话或技能。"
                  :
                  "Control the magnetic slider monitor page. Enable it to enter the monitor page and disable it to leave it.");
    mcp_server.AddTool("self.echo_base.set_magnetic_monitor", buffer,
    PropertyList({
        Property("enabled", kPropertyTypeBoolean),
    }), [](const PropertyList & properties) -> ReturnValue {
        const bool enabled = properties["enabled"].value<bool>();
        if (vocat_base_control_set_magnetic_monitor(enabled) != ESP_OK) {
            return false;
        }

        if (enabled) {
            magnetic_monitor_show();
        } else {
            magnetic_monitor_hide();
        }
        return true;
    });

    // Audio analysis mode control
    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "设置音频分析模式。可选模式:\n"
                  "beat_detection: 鼓点检测模式\n"
                  "doa_follow: 声源方向跟随模式；退出 DOA 测试环境时使用\n"
                  "doa_test: DOA 测试环境，只显示当前角度，不控制底座；用户说进入 DOA 测试环境时使用\n"
                  "disabled: 关闭音频分析"
                  :
                  "Set audio analysis mode. Available modes:\n"
                  "beat_detection: beat detection mode\n"
                  "doa_follow: DOA follow mode; use this to exit DOA test mode\n"
                  "doa_test: DOA test mode; display the current angle only and do not control the base\n"
                  "disabled: disable audio analysis");
    mcp_server.AddTool("self.echo_base.set_audio_mode", buffer,
    PropertyList({
        Property("mode", kPropertyTypeString),
    }), [board](const PropertyList & properties) -> ReturnValue {
        const std::string &mode = properties["mode"].value<std::string>();
        AudioAnalysisMode analysis_mode = AudioAnalysisMode::DISABLED;

        if (mode == "beat_detection") {
            analysis_mode = AudioAnalysisMode::BEAT_DETECTION;
        } else if (mode == "doa_follow") {
            analysis_mode = AudioAnalysisMode::DOA_FOLLOW;
        } else if (mode == "doa_test") {
            analysis_mode = AudioAnalysisMode::DOA_TEST;
        } else if (mode == "disabled")
        {
            analysis_mode = AudioAnalysisMode::DISABLED;
        } else
        {
            ESP_LOGE(TAG, "Unknown audio analysis mode: %s", mode.c_str());
            return false;
        }

        board->SetAudioAnalysisMode(analysis_mode);
        ESP_LOGI(TAG, "Audio analysis mode set to: %s", mode.c_str());
        return true;
    });

    // Pomodoro timer control
    mcp_server.AddTool("self.pomodoro.start",
    is_zh ? "开启番茄钟定时器，设置倒计时时间（1-60分钟，默认5分钟）"
          : "Start pomodoro timer with countdown minutes (1-60, default 5).",
    PropertyList({
        Property("minutes", kPropertyTypeInteger, 5, 1, 60),
    }), [](const PropertyList& properties) -> ReturnValue {
        int minutes = properties["minutes"].value<int>();
        ESP_LOGI(TAG, "Starting pomodoro timer with %d minutes", minutes);
        alarm_start_pomodoro(minutes);
        return true;
    });

    // Pomodoro timer control (start/pause)
    mcp_server.AddTool("self.pomodoro.control",
    is_zh ? "控制番茄钟运行状态。参数：start-启动，pause-暂停"
          : "Control pomodoro running state. action: start or pause.",
    PropertyList({
        Property("action", kPropertyTypeString),
    }), [](const PropertyList& properties) -> ReturnValue {
        const std::string &action = properties["action"].value<std::string>();
        
        bool success = false;
        if (action == "start") {
            ESP_LOGI(TAG, "Starting pomodoro timer");
            success = alarm_resume_pomodoro();
        } else if (action == "pause") {
            ESP_LOGI(TAG, "Pausing pomodoro timer");
            success = alarm_pause_pomodoro();
        } else {
            ESP_LOGE(TAG, "Unknown pomodoro action: %s (expected 'start' or 'pause')", action.c_str());
            return false;
        }
        return success;
    });

    // Sleep timer control
    mcp_server.AddTool("self.sleep.start",
    is_zh ? "设置睡眠闹钟，从当前时间到指定结束时间（24小时制）。参数：end_hour(0-23), end_min(0-59)"
          : "Set sleep timer from current time to specified end time (24h). params: end_hour(0-23), end_min(0-59).",
    PropertyList({
        Property("end_hour", kPropertyTypeInteger, 8, 0, 23),
        Property("end_min", kPropertyTypeInteger, 0, 0, 59),
    }), [](const PropertyList& properties) -> ReturnValue {
        int end_hour = properties["end_hour"].value<int>();
        int end_min = properties["end_min"].value<int>();
        
        // Validate parameters
        if (end_hour < 0 || end_hour >= 24) {
            ESP_LOGE(TAG, "Invalid end_hour: %d (must be 0-23)", end_hour);
            return false;
        }
        if (end_min < 0 || end_min >= 60) {
            ESP_LOGE(TAG, "Invalid end_min: %d (must be 0-59)", end_min);
            return false;
        }
        
        ESP_LOGI(TAG, "Setting sleep timer: current time -> %02d:%02d", end_hour, end_min);
        alarm_start_sleep(end_hour, end_min);
        return true;
    });
}
