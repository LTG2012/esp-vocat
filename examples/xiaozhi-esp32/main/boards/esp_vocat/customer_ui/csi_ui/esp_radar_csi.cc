/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_radar_csi.h"
#include <math.h>
#include <cstring>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "customer_ui/csi_ui/ui/ui.h"

// Wi‑Fi / CSI / Radar / Ping 相关
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_csi_gain_ctrl.h"
#include "app_ifft.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "esp_lv_adapter.h"

#define DISPLAY_SAMPLE_STEP 3
#define LVGL_CHART_POINTS   (300 / DISPLAY_SAMPLE_STEP)

// CSI 增益控制
#define CONFIG_GAIN_CONTROL 1   // 1:enable gain control, 0:disable gain control
#define CONFIG_FORCE_GAIN   0   // 1:force gain control, 0:automatic gain control

// 与路由器之间 ping 的频率（次/秒）
// #define CONFIG_SEND_FREQUENCY 50
#define CONFIG_SEND_FREQUENCY 20

static const char *TAG = "radar_csi";

namespace esp_brookesia::apps {

// ------------------------ CSI 接收与计算流水线 ------------------------

typedef struct {
    uint32_t id;
    uint32_t time;
    int8_t fft_gain;
    uint8_t agc_gain;
    int8_t buf[256];
} csi_recv_queue_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t sta_mac[6];
} csi_filter_ctx_t;

static QueueHandle_t s_csi_recv_queue = nullptr;
static int64_t s_time_zero = 0;
static bool s_csi_pipeline_started = false;
static volatile bool s_csi_pipeline_active = false;
static bool s_csi_capture_enabled = false;
static bool s_ping_requested = false;
static uint32_t s_csi_frame_id = 0;
static uint32_t s_csi_callback_count = 0;
static uint32_t s_csi_ap_match_count = 0;
static uint32_t s_csi_sample_enqueue_count = 0;
static uint32_t s_csi_sample_drop_count = 0;
static uint32_t s_csi_ui_queue_count = 0;
static TaskHandle_t s_csi_process_task = nullptr;
static TaskHandle_t s_csi_wait_task = nullptr;
static csi_filter_ctx_t s_csi_filter_ctx = {};

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    constexpr size_t kRequiredIqBytes = 64 * 2;

    if (!s_csi_pipeline_active || !info || !info->buf || info->len < kRequiredIqBytes) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    s_csi_callback_count++;

    // 只处理当前 AP 发给本机 STA 的数据，避免广播和其它客户端流量污染基线。
    if (ctx) {
        const auto *filter = static_cast<const csi_filter_ctx_t *>(ctx);
        if (memcmp(info->mac, filter->bssid, sizeof(filter->bssid)) ||
            memcmp(info->dmac, filter->sta_mac, sizeof(filter->sta_mac))) {
            return;
        }
    }
    s_csi_ap_match_count++;

    if ((s_csi_ap_match_count % 200) == 0) {
        ESP_LOGI(TAG,
                 "CSI callbacks=%lu ap_match=%lu queued=%lu dropped=%lu ui_queued=%lu len=%u payload_len=%u rx_seq=%u",
                 (unsigned long)s_csi_callback_count,
                 (unsigned long)s_csi_ap_match_count,
                 (unsigned long)s_csi_sample_enqueue_count,
                 (unsigned long)s_csi_sample_drop_count,
                 (unsigned long)s_csi_ui_queue_count,
                 (unsigned)info->len,
                 (unsigned)info->payload_len,
                 (unsigned)info->rx_seq);
    }

    static uint8_t agc_gain = 0;
    static int8_t fft_gain = 0;

#if CONFIG_GAIN_CONTROL
    static int s_count = 0;
    static uint8_t agc_gain_baseline = 0;
    static int8_t fft_gain_baseline = 0;

    esp_csi_gain_ctrl_get_rx_gain(info, &agc_gain, &fft_gain);
    if (s_count < 100) {
        esp_csi_gain_ctrl_record_rx_gain(agc_gain, fft_gain);
    } else if (s_count == 100) {
        esp_csi_gain_ctrl_get_rx_gain_baseline(&agc_gain_baseline, &fft_gain_baseline);
        ESP_LOGI(TAG, "agc_gain_baseline: %d, fft_gain_baseline: %d", agc_gain_baseline, fft_gain_baseline);
#if CONFIG_FORCE_GAIN
        esp_csi_gain_ctrl_set_rx_force_gain(agc_gain_baseline, fft_gain_baseline);
        ESP_LOGI(TAG, "fft_force %d, agc_force %d", fft_gain_baseline, agc_gain_baseline);
#endif
    }
    s_count++;
#endif

    if (!s_csi_recv_queue) {
        ESP_LOGW(TAG, "<%s> s_csi_recv_queue is not initialized", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return;
    }

    // CSI 回调由同一个 Wi-Fi 任务串行调用。使用固定 PSRAM 队列，避免
    // 每个 CSI 包都分配/释放一次内存造成的延迟和堆碎片。
    static csi_recv_queue_t csi_send_queuedata;
    memset(&csi_send_queuedata, 0, sizeof(csi_send_queuedata));

    /*
     * The CSI callback is also invoked for packets without an application
     * payload.  Never dereference payload unless the complete four-byte
     * sequence field is present.  The fallback sequence is only a local
     * diagnostic identifier and is not used for signal processing.
     */
    if (info->payload != nullptr && info->payload_len >= 19) {
        memcpy(&(csi_send_queuedata.id), info->payload + 15, sizeof(uint32_t));
    } else {
        csi_send_queuedata.id = ++s_csi_frame_id;
    }
    csi_send_queuedata.time = info->rx_ctrl.timestamp;
    csi_send_queuedata.agc_gain = agc_gain;
    csi_send_queuedata.fft_gain = fft_gain;

    memcpy(csi_send_queuedata.buf, info->buf, kRequiredIqBytes);
    if (info->first_word_invalid) {
        // ESP32-S3 可能标记首个 32-bit CSI word 无效；保持子载波位置不变并清零该 word。
        memset(csi_send_queuedata.buf, 0, sizeof(uint32_t));
    }

    if (xQueueSend(s_csi_recv_queue, &csi_send_queuedata, 0) != pdTRUE) {
        s_csi_sample_drop_count++;
    } else {
        s_csi_sample_enqueue_count++;
    }
}

static bool radar_wifi_csi_init()
{
    if (!s_csi_recv_queue) {
        s_csi_recv_queue = xQueueCreateWithCaps(8, sizeof(csi_recv_queue_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_csi_recv_queue) {
            ESP_LOGE(TAG, "Failed to create CSI receive queue in PSRAM");
            return false;
        }
    }

    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable promiscuous mode: %s", esp_err_to_name(err));
        return false;
    }

    // 参考 get-started/csi_recv_copy 的配置，针对 C5/C6
#if CONFIG_IDF_TARGET_ESP32C5
    wifi_csi_config_t csi_config = {
        .enable                   = true,
        .acquire_csi_legacy       = false,
        .acquire_csi_force_lltf   = 0,
        .acquire_csi_ht20         = true,
        .acquire_csi_ht40         = true,
        .acquire_csi_vht          = false,
        .acquire_csi_su           = false,
        .acquire_csi_mu           = false,
        .acquire_csi_dcm          = false,
        .acquire_csi_beamformed   = false,
        .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg            = false,
        .dump_ack_en              = false,
        .reserved                 = false
    };
#elif CONFIG_IDF_TARGET_ESP32C6
    wifi_csi_config_t csi_config = {
        .enable                 = true,
        .acquire_csi_legacy     = false,
        .acquire_csi_ht20       = true,
        .acquire_csi_ht40       = true,
        .acquire_csi_vht        = false,
        .acquire_csi_su         = false,
        .acquire_csi_mu         = false,
        .acquire_csi_dcm        = false,
        .acquire_csi_beamformed = false,
        .acquire_csi_he_stbc    = 2,
        .val_scale_cfg          = false,
        .dump_ack_en            = false,
        .reserved               = false
    };
#else
    // ESP32-S3 路由器场景只采集格式固定的 LLTF，避免不同 LTF 布局互相比较。
    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = false,
        .stbc_htltf2_en    = false,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = false,
    };
#endif

    static wifi_ap_record_t s_ap_info = {0};
    err = esp_wifi_sta_get_ap_info(&s_ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CSI start deferred: AP information unavailable: %s", esp_err_to_name(err));
        return false;
    }
    memcpy(s_csi_filter_ctx.bssid, s_ap_info.bssid, sizeof(s_csi_filter_ctx.bssid));
    err = esp_wifi_get_mac(WIFI_IF_STA, s_csi_filter_ctx.sta_mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get STA MAC: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CSI: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "CSI config applied");
    err = esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, &s_csi_filter_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CSI callback: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "CSI callback registered for AP " MACSTR, MAC2STR(s_ap_info.bssid));
    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable CSI: %s", esp_err_to_name(err));
        return false;
    }
    s_csi_capture_enabled = true;
    ESP_LOGI(TAG, "CSI capture enabled");
    return true;
}

static void process_csi_data_task(void *pvParameter)
{
    csi_recv_queue_t csi_recv_queue_data;
    Complex_Iq x_iq[64];
    float tap_history[3][64] = {};
    float previous_filtered_taps[64] = {};
    size_t tap_history_index = 0;
    size_t tap_history_count = 0;
    bool has_previous_filtered_taps = false;
    float motion_baseline = 0.0f;
    float motion_noise = 0.0f;
    int calibration_frames = 0;
    float cir = 0;
    float pha = 0;

    s_time_zero = esp_timer_get_time();

    while (s_csi_pipeline_active &&
           xQueueReceive(s_csi_recv_queue, &csi_recv_queue_data, portMAX_DELAY) == pdTRUE) {

#if !CONFIG_FORCE_GAIN && CONFIG_GAIN_CONTROL
        float scaling_factor = 0;
        esp_csi_gain_ctrl_get_gain_compensation(&scaling_factor, csi_recv_queue_data.agc_gain, csi_recv_queue_data.fft_gain);
#else
        float scaling_factor = 1.0f;
#endif

        // 前 64 点
        for (int i = 0; i < 64; i++) {
            x_iq[i].real = _IQ16(csi_recv_queue_data.buf[2 * i]);
            x_iq[i].imag = _IQ16(csi_recv_queue_data.buf[2 * i + 1]);
        }
        fft_iq(x_iq, 1);

        /*
         * 使用整段 CIR 幅度向量的余弦距离，而不是逐点绝对差。
         * 该指标会消除整体增益变化，专注于多径形状是否变化。
         */
        for (int i = 0; i < 64; ++i) {
            float magnitude = complex_magnitude_iq(x_iq[i]) * scaling_factor;
            if (!isfinite(magnitude) || magnitude < 0.0f) {
                magnitude = 0.0f;
            }
            tap_history[tap_history_index][i] = magnitude;
        }
        tap_history_index = (tap_history_index + 1) % 3;
        if (tap_history_count < 3) {
            tap_history_count++;
        }

        float raw_motion = 0.0f;
        bool raw_motion_valid = false;
        if (tap_history_count == 3) {
            float current_energy = 0.0f;
            float previous_energy = 0.0f;
            float correlation = 0.0f;
            float filtered_taps[64];

            for (int i = 0; i < 64; ++i) {
                const float a = tap_history[0][i];
                const float b = tap_history[1][i];
                const float c = tap_history[2][i];
                const float filtered = fmaxf(fminf(a, b), fminf(fmaxf(a, b), c));
                filtered_taps[i] = filtered;
                current_energy += filtered * filtered;
                if (has_previous_filtered_taps) {
                    correlation += filtered * previous_filtered_taps[i];
                    previous_energy += previous_filtered_taps[i] * previous_filtered_taps[i];
                }
            }

            if (has_previous_filtered_taps && current_energy > 1e-8f && previous_energy > 1e-8f) {
                float cosine_similarity = correlation / sqrtf(current_energy * previous_energy);
                cosine_similarity = fminf(1.0f, fmaxf(-1.0f, cosine_similarity));
                raw_motion = fmaxf(0.0f, 1.0f - cosine_similarity);
                raw_motion_valid = true;
            }
            memcpy(previous_filtered_taps, filtered_taps, sizeof(previous_filtered_taps));
            has_previous_filtered_taps = true;
        }

        /*
         * 进入 CSI 后先用约 1 秒数据建立静态噪声基线。后续使用双向限幅
         * 更新，既跟随环境缓慢变化，又不会因只吸收负偏差而越跑越敏感。
         */
        constexpr int kCalibrationFrames = 20;
        constexpr float kNoiseFloor = 0.0035f;
        constexpr float kBaselineAlpha = 0.02f;
        if (!raw_motion_valid) {
            cir = 0.0f;
        } else if (calibration_frames < kCalibrationFrames) {
            calibration_frames++;
            float delta = raw_motion - motion_baseline;
            motion_baseline += delta / calibration_frames;
            motion_noise += (fabsf(delta) - motion_noise) / calibration_frames;
            motion_noise = fmaxf(motion_noise, kNoiseFloor);
            cir = 0.0f;
            if (calibration_frames == kCalibrationFrames) {
                ESP_LOGI(TAG, "CSI motion calibrated: baseline=%.5f noise=%.5f",
                         motion_baseline, motion_noise);
            }
        } else {
            float delta = raw_motion - motion_baseline;
            float motion_threshold = fmaxf(motion_noise * 4.0f, 0.014f);
            float score = (delta - motion_threshold) / 0.11f;
            cir = fminf(100.0f, fmaxf(0.0f, score * 100.0f));

            /*
             * 只用静止窗口内的样本学习基线，并对正负偏差使用相同条件。
             * 动作尖峰不会抬高基线，较大的负偏差也不会单向压低基线。
             */
            const float quiet_limit = fmaxf(motion_noise * 4.0f, 0.020f);
            if (fabsf(delta) <= quiet_limit) {
                motion_baseline += kBaselineAlpha * delta;
                const float residual = fabsf(raw_motion - motion_baseline);
                motion_noise += kBaselineAlpha * (residual - motion_noise);
                motion_noise = fmaxf(motion_noise, kNoiseFloor);
            }

            static uint32_t s_motion_log_count = 0;
            if ((++s_motion_log_count % 100) == 0) {
                ESP_LOGI(TAG,
                         "motion raw=%.5f baseline=%.5f noise=%.5f threshold=%.5f activity=%.2f",
                         raw_motion, motion_baseline, motion_noise, motion_threshold, cir);
            }
        }
        pha = raw_motion;
        if (!isfinite(cir) || cir < 0.0f) {
            cir = 0.0f;
        }
        if (!isfinite(pha)) {
            pha = 0.0f;
        }
        // ESP_LOGI(TAG, "cir: %f, pha: %f, scaling_factor: %f", cir, pha, scaling_factor);
        // 将结果打包成 csi_data_t，交给 RadarCSI 画图（单通道）
        csi_data_t data = {
            .start = {0xAA, 0x55},
            .id = csi_recv_queue_data.id,
            .time_delta = (int64_t)csi_recv_queue_data.time - s_time_zero,
            .cir = cir,
            .pha = pha,
            .end = {0x55, 0xAA},
        };

        RadarCSI *radar = RadarCSI::getInstance();
        if (radar && radar->pushData(data)) {
            s_csi_ui_queue_count++;
            if (s_csi_ui_queue_count == 1) {
                ESP_LOGI(TAG, "First CSI sample queued for UI");
            }
        }

    }

    s_csi_process_task = nullptr;
    vTaskDelete(NULL);
}

// 参考 get-started/csi_recv_copy 的 wifi_ping_router_start，实现与路由器之间的持续 ping
static esp_ping_handle_t s_ping_handle = NULL;

// 前向声明
esp_err_t wifi_ping_router_stop(void);

esp_err_t wifi_ping_router_start(void)
{
    ESP_LOGW(TAG, "wifi_ping_router_start");

    // 如果已经在运行，先停止
    if (s_ping_handle != NULL) {
        ESP_LOGW(TAG, "Ping already running, stopping first");
        wifi_ping_router_stop();
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.count           = 0;                               // 无限次
    ping_config.interval_ms     = 1000 / CONFIG_SEND_FREQUENCY;    // 发送频率
    ping_config.task_stack_size = 3072;
    ping_config.data_size       = 1;

    esp_netif_ip_info_t local_ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);
    ESP_LOGI(TAG, "got ip:" IPSTR ", gw: " IPSTR, IP2STR(&local_ip.ip), IP2STR(&local_ip.gw));
    ping_config.target_addr.u_addr.ip4.addr = ip4_addr_get_u32(&local_ip.gw);
    ping_config.target_addr.type = ESP_IPADDR_TYPE_V4;

    esp_ping_callbacks_t cbs = { 0 };
    esp_ping_new_session(&ping_config, &cbs, &s_ping_handle);
    esp_ping_start(s_ping_handle);

    return ESP_OK;
}

esp_err_t wifi_ping_router_stop(void)
{
    if (s_ping_handle != NULL) {
        ESP_LOGW(TAG, "wifi_ping_router_stop");
        esp_ping_stop(s_ping_handle);
        esp_ping_delete_session(s_ping_handle);
        s_ping_handle = NULL;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

static void radar_wifi_csi_deinit()
{
    if (s_csi_capture_enabled) {
        esp_err_t err = esp_wifi_set_csi(false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to disable CSI: %s", esp_err_to_name(err));
        }
        err = esp_wifi_set_csi_rx_cb(nullptr, nullptr);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unregister CSI callback: %s", esp_err_to_name(err));
        }
        s_csi_capture_enabled = false;
    }

    esp_err_t err = esp_wifi_set_promiscuous(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable promiscuous mode: %s", esp_err_to_name(err));
    }
}

static void radar_csi_start_pipeline()
{
    if (s_csi_pipeline_active) {
        return;
    }
    s_csi_pipeline_active = true;
    // 等待主工程把 Wi‑Fi STA 连上 AP，并拿到 IP 之后再启动 CSI
    BaseType_t wait_task_ret = xTaskCreateWithCaps(
    [](void *pv) {
        // 轮询检查是否已经连上 AP 并获取到 IP
        while (s_csi_pipeline_active) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                    // 已经获取到 IP，说明 Wi‑Fi 已经连上 AP
                    ESP_LOGI(TAG, "WiFi STA connected, got IP: " IPSTR, IP2STR(&ip_info.ip));

                    // 初始化 CSI（注册 csicb，开始采集）
                    if (!radar_wifi_csi_init()) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        continue;
                    }

                    if (!s_csi_pipeline_active) {
                        break;
                    }

                    // 注意：不在这里自动启动 ping，而是由应用根据当前页面决定是否启动 ping
                    // 开始 ping 路由器，产生稳定的 CSI 数据流量
                    // extern esp_err_t wifi_ping_router_start(void);
                    // wifi_ping_router_start();

                    // 创建 CSI 处理任务，把采样数据转成 cir/pha 并喂给 UI
                    BaseType_t task_ret = xTaskCreateWithCaps(
                        process_csi_data_task,
                        "process_csi_data_task",
                        6144,
                        NULL,
                        6,
                        &s_csi_process_task,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (task_ret != pdPASS) {
                        ESP_LOGE(TAG, "Failed to create CSI processing task: %d", (int)task_ret);
                    } else {
                        s_csi_pipeline_started = true;
                        ESP_LOGI(TAG, "CSI processing task started");
                    }

                    if (s_csi_pipeline_started && s_ping_requested) {
                        wifi_ping_router_start();
                    }

                    ESP_LOGI(TAG, "CSI pipeline started");
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        s_csi_wait_task = nullptr;
        vTaskDelete(NULL);
    },
    "csi_wait_wifi",
    4096,
    nullptr,
    5,
    &s_csi_wait_task,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wait_task_ret != pdPASS) {
        s_csi_pipeline_active = false;
        ESP_LOGE(TAG, "Failed to create CSI Wi-Fi wait task: %d", (int)wait_task_ret);
    } else {
        ESP_LOGI(TAG, "CSI Wi-Fi wait task started");
    }
}

static void radar_csi_stop_pipeline()
{
    s_csi_pipeline_active = false;
    s_ping_requested = false;
    (void)wifi_ping_router_stop();
    radar_wifi_csi_deinit();

    if (s_csi_wait_task) {
        vTaskDelete(s_csi_wait_task);
        s_csi_wait_task = nullptr;
    }
    if (s_csi_process_task) {
        vTaskDelete(s_csi_process_task);
        s_csi_process_task = nullptr;
    }
    if (s_csi_recv_queue) {
        vQueueDeleteWithCaps(s_csi_recv_queue);
        s_csi_recv_queue = nullptr;
    }
    s_csi_pipeline_started = false;
    ESP_LOGI(TAG, "CSI pipeline stopped and resources released");
}

// ------------------------ RadarCSI 类实现 ------------------------

// 定时器回调函数（用于在数据更新超时时补充相同数据）
static void update_timer_callback(void* arg)
{
    RadarCSI* radar = static_cast<RadarCSI*>(arg);
    if (radar) {
        // 只设置标志，不直接更新图表（避免LVGL渲染冲突）
        radar->supplementLastData();
    }
}

RadarCSI *RadarCSI::_instance = nullptr;

RadarCSI *RadarCSI::getInstance()
{
    if (_instance == nullptr) {
        _instance = new RadarCSI();
    }
    return _instance;
}

RadarCSI::RadarCSI():
    csi_display_queue(nullptr),
    ser(nullptr),
    motion_trend(0.0f),
    motion_trend_valid(false),
    chart_initialized(false),
    chart_count(0),
    is_processing_stopped(true),  // 初始化时默认停止，等UI准备好后再恢复
    update_timer(nullptr),
    chart_m_queue(nullptr),
    chart_series_m(nullptr),
    chart_m_avg_index(0),
    chart_m_avg_count(0),
    chart_m_count(0),
    chart_m_initialized(false),
    behavior_occupied(false),
    behavior_active_since_us(0),
    behavior_inactive_since_us(0),
    behavior_activity(0.0f)
{
    memset(range, 0, sizeof(range));
    memset(y_range, 0, sizeof(y_range));
    memset(&last_data, 0, sizeof(last_data));

    memset(chart_m_range, 0, sizeof(chart_m_range));
    memset(chart_m_y_range, 0, sizeof(chart_m_y_range));
    memset(chart_m_avg_buffer, 0, sizeof(chart_m_avg_buffer));
}

RadarCSI::~RadarCSI()
{
    if (update_timer) {
        esp_timer_stop(update_timer);
        esp_timer_delete(update_timer);
        update_timer = nullptr;
    }
    if (csi_display_queue) {
        vQueueDelete(csi_display_queue);
        csi_display_queue = nullptr;
    }
    if (chart_m_queue) {
        vQueueDelete(chart_m_queue);
        chart_m_queue = nullptr;
    }
    _instance = nullptr;
}

bool RadarCSI::init()
{
    // 创建 CSI 数据队列（RadarCSI 内部用于 UI 刷新）
    if (!csi_display_queue) {
        csi_display_queue = xQueueCreate(1, sizeof(csi_data_t));
        if (csi_display_queue == nullptr) {
            ESP_LOGE(TAG, "Failed to create CSI display queue");
            return false;
        }
    }

    // 创建 ScreenM 图表数据队列
    if (!chart_m_queue) {
        chart_m_queue = xQueueCreate(20, sizeof(uint16_t));
        if (chart_m_queue == nullptr) {
            ESP_LOGE(TAG, "Failed to create chart M queue");
            return false;
        }
    }

    ESP_LOGI(TAG, "RadarCSI initialized");
    return true;
}

void RadarCSI::startPipeline()
{
    // 启动 Wi‑Fi + ESP‑NOW + CSI + 计算流水线
    radar_csi_start_pipeline();
}

void RadarCSI::stopPipeline()
{
    radar_csi_stop_pipeline();
}

void RadarCSI::startPing()
{
    s_ping_requested = true;
    if (s_csi_pipeline_started) {
        wifi_ping_router_start();
    }
}

void RadarCSI::stopPing()
{
    s_ping_requested = false;
    (void)wifi_ping_router_stop();
}

void RadarCSI::initCharts()
{
    if (chart_initialized) {
        return;
    }

    // 初始化波形图表 (ui_ScreenW_Chart) - 单条曲线
    if (ui_ScreenW_Chart) {
        lv_chart_set_range(ui_ScreenW_Chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_point_count(ui_ScreenW_Chart, LVGL_CHART_POINTS);
        ser = lv_chart_add_series(ui_ScreenW_Chart, lv_color_hex(0x388E3C), LV_CHART_AXIS_PRIMARY_Y); // 深绿
        lv_chart_refresh(ui_ScreenW_Chart);
        // 禁止滚动
        lv_obj_clear_flag(ui_ScreenW_Chart, LV_OBJ_FLAG_SCROLLABLE);
    }

    // 创建25ms定时器，用于在数据更新超时时补充数据
    if (!update_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = update_timer_callback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "csi_update_timer",
            .skip_unhandled_events = true
        };
        esp_timer_create(&timer_args, &update_timer);
    }

    chart_initialized = true;
    chart_count = 0;

    ESP_LOGI(TAG, "Charts initialized with 25ms update timer");
}

void RadarCSI::doUpdateChart(const csi_data_t &data, bool reset_timer)
{
    if (!chart_initialized) {
        return;
    }

    /*
     * 逐帧运动强度会包含多径和接收噪声。指数趋势滤波相较固定窗口平均
     * 对新的动作立即响应，同时不会把曲线压成复杂的高频锯齿。
     */
    if (!motion_trend_valid) {
        motion_trend = data.cir;
        motion_trend_valid = true;
    } else {
        motion_trend += MOTION_TREND_ALPHA * (data.cir - motion_trend);
    }

    // 0–100 固定活动度量表；不再因为自动缩放把本底噪声铺满整个图表。
    range[chart_count] = motion_trend;
    y_range[0] = 0.0f;
    y_range[1] = 100.0f;

    // 周期性打印图表相关数据，便于调试
    static int s_log_count = 0;
    if ((s_log_count++ % 50) == 0) {
        ESP_LOGI(TAG,
                 "chart_idx=%u, activity=%.2f, raw=%.5f, display=%.2f, y_min=%d, y_max=%d",
                 chart_count,
                 data.cir,
                 data.pha,
                 range[chart_count],
                 (int)y_range[0],
                 (int)y_range[1]);
    }

    // 更新波形图表 - 显示1条曲线
    if (ui_ScreenW_Chart && ser) {
        lv_chart_set_next_value(ui_ScreenW_Chart, ser, (uint16_t)(range[chart_count]));
        lv_chart_set_range(ui_ScreenW_Chart, LV_CHART_AXIS_PRIMARY_Y, y_range[0], y_range[1]);

        // 同步更新Y轴刻度标签的范围
        if (ui_ScreenW_Chart_Yaxis1) {
            lv_scale_set_range(ui_ScreenW_Chart_Yaxis1, y_range[0], y_range[1]);
        }
    }

    updateBehavior(motion_trend);

    // 更新计数器
    chart_count++;
    if (chart_count >= LVGL_CHART_POINTS) {
        chart_count = 0;
    }
}

void RadarCSI::updateBehavior(float activity)
{
    /*
     * CSI 活动度反映的是环境中的动态变化，而非摄像头式的人体识别。
     * 使用滞回和保持时间：动作需要连续 1 秒才确认“在位”，确认后要
     * 连续静止 15 秒才切回“离位”，这样挥动、噪声和单帧异常都不会
     * 让状态频繁闪烁。
     */
    const lv_color_t kActiveColor = lv_color_hex(0x6B9FF8);
    const lv_color_t kInactiveColor = lv_color_hex(0x656565);
    const lv_color_t kTextActiveColor = lv_color_hex(0xFFFFFF);
    const lv_color_t kTextInactiveColor = lv_color_hex(0xB0B0B0);

    if (activity < 0.0f) {
        activity = 0.0f;
    } else if (activity > 100.0f) {
        activity = 100.0f;
    }
    behavior_activity = activity;

    const int64_t now_us = esp_timer_get_time();
    bool state_changed = false;
    if (!behavior_occupied) {
        behavior_inactive_since_us = 0;
        if (activity >= BEHAVIOR_ON_THRESHOLD) {
            if (behavior_active_since_us == 0) {
                behavior_active_since_us = now_us;
            } else if (now_us - behavior_active_since_us >= BEHAVIOR_ON_HOLD_US) {
                behavior_occupied = true;
                behavior_active_since_us = 0;
                state_changed = true;
            }
        } else {
            behavior_active_since_us = 0;
        }
    } else {
        behavior_active_since_us = 0;
        if (activity <= BEHAVIOR_OFF_THRESHOLD) {
            if (behavior_inactive_since_us == 0) {
                behavior_inactive_since_us = now_us;
            } else if (now_us - behavior_inactive_since_us >= BEHAVIOR_OFF_HOLD_US) {
                behavior_occupied = false;
                behavior_inactive_since_us = 0;
                state_changed = true;
            }
        } else {
            behavior_inactive_since_us = 0;
        }
    }

    if (state_changed) {
        ESP_LOGI(TAG, "Behavior state: %s (activity=%.1f)",
                 behavior_occupied ? "Occupied" : "Unoccupied", activity);
    }

    /*
     * 此函数由 processData() 在 LVGL 锁内调用。行为页与曲线页共用
     * CSI 生命周期，因此页面不可见时也更新状态；用户滑入行为页即可
     * 看到最新结果，不需要额外任务或队列。
     */
    if (!ui_csi_behav_Screen) {
        return;
    }

    if (ui_csi_3_pie_arc) {
        lv_arc_set_value(ui_csi_3_pie_arc, (int32_t)(activity * 3.6f));
    }
    if (ui_csi_3_occupied_pan) {
        lv_obj_set_style_bg_color(ui_csi_3_occupied_pan,
                                  behavior_occupied ? kActiveColor : kInactiveColor,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_csi_3_unoccupied_pan) {
        lv_obj_set_style_bg_color(ui_csi_3_unoccupied_pan,
                                  behavior_occupied ? kInactiveColor : kActiveColor,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_csi_3_occupied_lab) {
        lv_obj_set_style_text_color(ui_csi_3_occupied_lab,
                                    behavior_occupied ? kTextActiveColor : kTextInactiveColor,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_csi_3_unoccupied_lab) {
        lv_obj_set_style_text_color(ui_csi_3_unoccupied_lab,
                                    behavior_occupied ? kTextInactiveColor : kTextActiveColor,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void RadarCSI::updateChart(const csi_data_t &data)
{
    if (!chart_initialized) {
        ESP_LOGW(TAG, "Charts not initialized");
        return;
    }

    // 检查是否是补充的数据（与上次数据相同）
    bool is_supplemented = (memcmp(&last_data, &data, sizeof(csi_data_t)) == 0);

    // 如果是新数据，保存并重置定时器
    if (!is_supplemented) {
        memcpy(&last_data, &data, sizeof(csi_data_t));

        // 重置定时器 - 25ms内没有新数据将触发补充
        if (update_timer) {
            esp_timer_stop(update_timer);
            esp_timer_start_once(update_timer, 25000);  // 25ms = 25000us
        }
    } else {
        // 补充数据时也重置定时器，以便持续补充
        if (update_timer) {
            esp_timer_stop(update_timer);
            esp_timer_start_once(update_timer, 25000);
        }

        // 周期性打印补充日志（降低频率避免刷屏）
        static int s_supplement_count = 0;
        if ((s_supplement_count++ % 20) == 0) {
            ESP_LOGD(TAG, "Supplemented last data to prevent waveform freeze");
        }
    }

    // 执行实际的图表更新
    doUpdateChart(data, true);
}

bool RadarCSI::pushData(const csi_data_t &data)
{
    // 如果当前不处理数据（应用未打开或已退出），直接丢弃，不入队
    if (is_processing_stopped || csi_display_queue == nullptr) {
        return false;
    }

    // UI 只需要最新采样。长度为 1 的覆盖队列避免数据积压后集中重绘。
    return xQueueOverwrite(csi_display_queue, &data) == pdPASS;
}

void RadarCSI::processData()
{
    if (!csi_display_queue) {
        return;
    }

    csi_data_t csi_display_data;
    if (xQueueReceive(csi_display_queue, &csi_display_data, 0) == pdTRUE &&
        !is_processing_stopped) {
        // 每个 50ms UI tick 最多重绘一次，避免 CSI 数据突发时抢占音频和 SPI。
        esp_lv_adapter_lock(-1);
        updateChart(csi_display_data);
        esp_lv_adapter_unlock();
    }
}

void RadarCSI::supplementLastData()
{
    // 定时器回调中调用，将上一次的数据重新推送到队列
    // 这样所有图表更新都在processData()中统一处理，避免LVGL渲染冲突
    if (chart_initialized && !is_processing_stopped && csi_display_queue) {
        // 将last_data重新入队，让processData统一处理
        if (xQueueSend(csi_display_queue, &last_data, 0) != pdTRUE) {
            // 队列满了就算了，不强制补充
        }
    }
}

// ========== ScreenM 图表相关实现 ==========

void RadarCSI::initChartM()
{
    if (chart_m_initialized) {
        return;
    }

    // 初始化 ScreenM 图表
    if (ui_ScreenM_Chart) {
        lv_chart_set_range(ui_ScreenM_Chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_point_count(ui_ScreenM_Chart, CHART_M_POINTS);
        chart_series_m = lv_chart_add_series(ui_ScreenM_Chart, lv_color_hex(0x388E3C), LV_CHART_AXIS_PRIMARY_Y); // 深绿
        lv_chart_refresh(ui_ScreenM_Chart);
        // 禁止滚动
        lv_obj_clear_flag(ui_ScreenM_Chart, LV_OBJ_FLAG_SCROLLABLE);
    }

    chart_m_initialized = true;
    chart_m_count = 0;

    ESP_LOGI(TAG, "Chart M initialized");
}

bool RadarCSI::pushChartMData(uint16_t value)
{
    if (chart_m_queue == nullptr) {
        return false;
    }

    if (xQueueSend(chart_m_queue, &value, 0) != pdTRUE) {
        // 队列已满时，丢弃最旧的数据，再尝试写入最新数据
        uint16_t dummy;
        (void)xQueueReceive(chart_m_queue, &dummy, 0);
        (void)xQueueSend(chart_m_queue, &value, 0);
        return false;
    }

    return true;
}

void RadarCSI::updateChartM(uint16_t value)
{
    if (!chart_m_initialized) {
        ESP_LOGW(TAG, "Chart M not initialized");
        return;
    }

    // 滑动平均滤波
    chart_m_avg_buffer[chart_m_avg_index] = (float)value;
    chart_m_avg_index = (chart_m_avg_index + 1) % CHART_M_AVG_WINDOW;
    if (chart_m_avg_count < CHART_M_AVG_WINDOW) {
        chart_m_avg_count++;
    }

    float value_avg = 0.0f;
    for (int i = 0; i < chart_m_avg_count; ++i) {
        value_avg += chart_m_avg_buffer[i];
    }
    value_avg /= (chart_m_avg_count > 0 ? chart_m_avg_count : 1);

    // 存储滤波后的数据点
    chart_m_range[chart_m_count] = value_avg;

    // 计算Y轴范围
    chart_m_y_range[0] = 65535;  // min
    chart_m_y_range[1] = 0;      // max
    for (int i = 0; i < CHART_M_POINTS; i++) {
        if (chart_m_y_range[0] > chart_m_range[i]) {
            chart_m_y_range[0] = chart_m_range[i];
        }
        if (chart_m_y_range[1] < chart_m_range[i]) {
            chart_m_y_range[1] = chart_m_range[i];
        }
    }

    // 确保Y轴范围至少为30
    uint8_t y_range_size = 200;
    if ((chart_m_y_range[1] - chart_m_y_range[0]) < y_range_size) {
        chart_m_y_range[1] += (y_range_size - chart_m_y_range[1] + chart_m_y_range[0]) / 2;
        chart_m_y_range[0] -= (y_range_size - chart_m_y_range[1] + chart_m_y_range[0]) / 2;
    }

    // 周期性打印数据
    static int s_log_count_m = 0;
    if ((s_log_count_m++ % 200) == 0) {
        ESP_LOGI(TAG,
                 "chart_m_idx=%u, value=%u, avg=%.3f, y_min=%d, y_max=%d",
                 chart_m_count,
                 value,
                 value_avg,
                 (int)chart_m_y_range[0],
                 (int)chart_m_y_range[1]);
    }

    // 更新图表
    if (ui_ScreenM_Chart && chart_series_m) {
        lv_chart_set_next_value(ui_ScreenM_Chart, chart_series_m, (uint16_t)(chart_m_range[chart_m_count]));
        lv_chart_set_range(ui_ScreenM_Chart, LV_CHART_AXIS_PRIMARY_Y, chart_m_y_range[0], chart_m_y_range[1]);
        // 同步更新Y轴刻度标签的范围
        if (ui_ScreenM_Chart_Yaxis1) {
            lv_scale_set_range(ui_ScreenM_Chart_Yaxis1, chart_m_y_range[0], chart_m_y_range[1]);
        }
    }

    // 更新计数器
    chart_m_count++;
    if (chart_m_count >= CHART_M_POINTS) {
        chart_m_count = 0;
    }
}

void RadarCSI::processChartMData()
{
    if (!chart_m_queue) {
        return;
    }

    uint16_t value;
    while (xQueueReceive(chart_m_queue, &value, 0) == pdTRUE) {
        updateChartM(value);
    }

    // 批量更新后刷新一次
    if (ui_ScreenM_Chart && chart_series_m) {
        lv_chart_refresh(ui_ScreenM_Chart);
    }
}

void RadarCSI::stopDataProcessing()
{
    is_processing_stopped = true;
    motion_trend = 0.0f;
    motion_trend_valid = false;
    behavior_occupied = false;
    behavior_active_since_us = 0;
    behavior_inactive_since_us = 0;
    behavior_activity = 0.0f;

    // 停止定时器
    if (update_timer) {
        esp_timer_stop(update_timer);
    }

    // 清空队列中的所有数据
    if (csi_display_queue) {
        vQueueDelete(csi_display_queue);
        csi_display_queue = nullptr;
    }
    if (chart_m_queue) {
        vQueueDelete(chart_m_queue);
        chart_m_queue = nullptr;
    }

    ESP_LOGI(TAG, "Data processing stopped");
}

void RadarCSI::resumeDataProcessing()
{
    is_processing_stopped = false;
    ESP_LOGI(TAG, "Data processing resumed");
}

void RadarCSI::resetChartState()
{
    // 停止定时器
    if (update_timer) {
        esp_timer_stop(update_timer);
    }

    // 重置图表相关状态
    chart_initialized = false;
    chart_count = 0;
    ser = nullptr;

    // 重置滤波器状态
    motion_trend = 0.0f;
    motion_trend_valid = false;
    memset(range, 0, sizeof(range));
    memset(y_range, 0, sizeof(y_range));
    memset(&last_data, 0, sizeof(last_data));

    ESP_LOGI(TAG, "Chart state reset");
}

} // namespace esp_brookesia::apps

// C interface implementations
extern "C" {
    void radar_csi_process_data(void)
    {
        esp_brookesia::apps::RadarCSI::getInstance()->processData();
    }

    void radar_csi_process_chart_m_data(void)
    {
        esp_brookesia::apps::RadarCSI::getInstance()->processChartMData();
    }
}
