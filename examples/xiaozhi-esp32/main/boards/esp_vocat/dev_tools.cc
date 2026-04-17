#include "dev_tools.h"
#include "esp_vocat.h"
#include "vocat_base_control.h"
#include "audio_analysis.h"
#include "mcp_server.h"
#include "board.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>
#include "customer_ui/alarm_api.h"
#include "ui_bridge.h"

#define TAG "DevTools"

void DevTools::Initialize(EspS3Cat* board)
{
    auto &mcp_server = McpServer::GetInstance();
    char buffer[1024];
    const bool is_zh = (std::strncmp(Lang::CODE, "zh", 2) == 0);

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

    // Audio analysis mode control
    std::snprintf(buffer, sizeof(buffer), "%s",
                  is_zh ?
                  "设置音频分析模式。可选模式:\n"
                  "beat_detection: 鼓点检测模式\n"
                  "doa_follow: 声源方向跟随模式\n"
                  "disabled: 关闭音频分析"
                  :
                  "Set audio analysis mode. Available modes:\n"
                  "beat_detection: beat detection mode\n"
                  "doa_follow: DOA follow mode\n"
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
