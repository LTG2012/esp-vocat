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
#define CONFIG_SEND_FREQUENCY 10

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

static QueueHandle_t s_csi_recv_queue = nullptr;
static int64_t s_time_zero = 0;
static bool s_csi_pipeline_started = false;

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    // 只接收来自当前连接 AP（路由器）的 CSI，ctx 里传入的是 AP 的 BSSID
    if (ctx && memcmp(info->mac, ctx, 6)) {
        return;
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

    csi_recv_queue_t *csi_send_queuedata = (csi_recv_queue_t *)heap_caps_calloc(1, sizeof(csi_recv_queue_t), MALLOC_CAP_SPIRAM);
    if (!csi_send_queuedata) {
        ESP_LOGW(TAG, "Failed to allocate memory for csi_send_queuedata in PSRAM");
        return;
    }

    memcpy(&(csi_send_queuedata->id), info->payload + 15, sizeof(uint32_t));
    csi_send_queuedata->time = info->rx_ctrl.timestamp;
    csi_send_queuedata->agc_gain = agc_gain;
    csi_send_queuedata->fft_gain = fft_gain;

    memset(csi_send_queuedata->buf, 0, sizeof(csi_send_queuedata->buf));
    size_t copy_len = info->len > sizeof(csi_send_queuedata->buf) - 8 ? sizeof(csi_send_queuedata->buf) - 8 : info->len;
    memcpy(csi_send_queuedata->buf + 8, info->buf, copy_len);

    if (xQueueSend(s_csi_recv_queue, &csi_send_queuedata, 0) != pdTRUE) {
        free(csi_send_queuedata);
    }
}

static void radar_wifi_csi_init()
{
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    if (!s_csi_recv_queue) {
        s_csi_recv_queue = xQueueCreate(20, sizeof(csi_recv_queue_t *));
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
    // 其它芯片（如 ESP32）保持默认 legacy 配置
    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = true,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = false,
    };
#endif

    static wifi_ap_record_t s_ap_info = {0};
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&s_ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    // 把 AP 的 BSSID 通过 ctx 传进回调，用来过滤 CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, s_ap_info.bssid));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void process_csi_data_task(void *pvParameter)
{
    csi_recv_queue_t *csi_recv_queue_data = NULL;
    Complex_Iq x_iq[64];
    float cir = 0;
    float pha = 0;

    s_time_zero = esp_timer_get_time();

    while (xQueueReceive(s_csi_recv_queue, &csi_recv_queue_data, portMAX_DELAY) == pdTRUE) {
        if (!csi_recv_queue_data) {
            continue;
        }

#if !CONFIG_FORCE_GAIN && CONFIG_GAIN_CONTROL
        float scaling_factor = 0;
        esp_csi_gain_ctrl_get_gain_compensation(&scaling_factor, csi_recv_queue_data->agc_gain, csi_recv_queue_data->fft_gain);
#else
        float scaling_factor = 1.0f;
#endif

        // 前 64 点
        for (int i = 0; i < 64; i++) {
            x_iq[i].real = _IQ16(csi_recv_queue_data->buf[2 * i]);
            x_iq[i].imag = _IQ16(csi_recv_queue_data->buf[2 * i + 1]);
        }
        fft_iq(x_iq, 1);
        cir = complex_magnitude_iq(x_iq[0]) * scaling_factor;
        pha = complex_phase_iq(x_iq[0]);
        // ESP_LOGI(TAG, "cir: %f, pha: %f, scaling_factor: %f", cir, pha, scaling_factor);
        // 将结果打包成 csi_data_t，交给 RadarCSI 画图（单通道）
        csi_data_t data = {
            .start = {0xAA, 0x55},
            .id = csi_recv_queue_data->id,
            .time_delta = (int64_t)csi_recv_queue_data->time - s_time_zero,
            .cir = cir,
            .pha = pha,
            .end = {0x55, 0xAA},
        };

        RadarCSI *radar = RadarCSI::getInstance();
        if (radar) {
            radar->pushData(data);
        }

        heap_caps_free(csi_recv_queue_data);
    }
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

static void radar_csi_start_pipeline()
{
    if (s_csi_pipeline_started) {
        return;
    }
    // 等待主工程把 Wi‑Fi STA 连上 AP，并拿到 IP 之后再启动 CSI
    xTaskCreate(
    [](void *pv) {
        // 轮询检查是否已经连上 AP 并获取到 IP
        while (1) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                    // 已经获取到 IP，说明 Wi‑Fi 已经连上 AP
                    ESP_LOGI(TAG, "WiFi STA connected, got IP: " IPSTR, IP2STR(&ip_info.ip));

                    // 初始化 CSI（注册 csicb，开始采集）
                    radar_wifi_csi_init();

                    // 注意：不在这里自动启动 ping，而是由应用根据当前页面决定是否启动 ping
                    // 开始 ping 路由器，产生稳定的 CSI 数据流量
                    // extern esp_err_t wifi_ping_router_start(void);
                    // wifi_ping_router_start();

                    s_csi_pipeline_started = true;

                    // 创建 CSI 处理任务，把采样数据转成 cir/pha 并喂给 UI
                    xTaskCreate(process_csi_data_task, "process_csi_data_task", 4096, NULL, 6, NULL);

                    ESP_LOGI(TAG, "CSI pipeline started (without auto-ping)");
                    vTaskDelete(NULL);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    },
    "csi_wait_wifi",
    4096,
    nullptr,
    5,
    nullptr);
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
    avg_index(0),
    avg_count(0),
    chart_initialized(false),
    chart_count(0),
    is_processing_stopped(true),  // 初始化时默认停止，等UI准备好后再恢复
    update_timer(nullptr),
    chart_m_queue(nullptr),
    chart_series_m(nullptr),
    chart_m_avg_index(0),
    chart_m_avg_count(0),
    chart_m_count(0),
    chart_m_initialized(false)
{
    memset(range, 0, sizeof(range));
    memset(y_range, 0, sizeof(y_range));
    memset(avg_buffer, 0, sizeof(avg_buffer));
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
        csi_display_queue = xQueueCreate(20, sizeof(csi_data_t));
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

void RadarCSI::startPing()
{
    extern esp_err_t wifi_ping_router_start(void);
    wifi_ping_router_start();
}

void RadarCSI::stopPing()
{
    extern esp_err_t wifi_ping_router_stop(void);
    wifi_ping_router_stop();
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

    // 最近 AVG_WINDOW 个点做简单滑动平均滤波，平滑振幅波形
    avg_buffer[avg_index] = data.cir;
    avg_index = (avg_index + 1) % AVG_WINDOW;
    if (avg_count < AVG_WINDOW) {
        avg_count++;
    }

    float cir_avg = 0.0f;
    for (int i = 0; i < avg_count; ++i) {
        cir_avg += avg_buffer[i];
    }
    cir_avg /= (avg_count > 0 ? avg_count : 1);

    // 存储滤波后的当前数据点（单通道）
    range[chart_count] = cir_avg * 5;

    // 计算Y轴范围
    y_range[0] = 500;
    y_range[1] = 0;
    for (int i = 0; i < LVGL_CHART_POINTS; i++) {
        if (y_range[0] > range[i]) {
            y_range[0] = range[i];
        }
        if (y_range[1] < range[i]) {
            y_range[1] = range[i];
        }
    }
    uint8_t y_range_size = 30;
    // 确保Y轴范围至少为100
    if ((y_range[1] - y_range[0]) < y_range_size) {
        y_range[1] += (y_range_size - y_range[1] + y_range[0]) / 2;
        y_range[0] -= (y_range_size - y_range[1] + y_range[0]) / 2;
    }

    // 周期性打印图表相关数据，便于调试
    static int s_log_count = 0;
    if ((s_log_count++ % 200) == 0) {
        ESP_LOGI(TAG,
                 "chart_idx=%u, cir=%.3f, pha=%.3f, range=%.3f, y_min=%d, y_max=%d",
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

    // 更新计数器
    chart_count++;
    if (chart_count >= LVGL_CHART_POINTS) {
        chart_count = 0;
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
        ESP_LOGW(TAG, "is_processing_stopped: %d, csi_display_queue: %p", is_processing_stopped, csi_display_queue);
        return false;
    }

    if (xQueueSend(csi_display_queue, &data, 0) != pdTRUE) {
        // 队列已满时，丢弃最旧的数据，再尝试写入最新数据，避免一直处于 full 状态
        csi_data_t dummy;
        (void)xQueueReceive(csi_display_queue, &dummy, 0);
        (void)xQueueSend(csi_display_queue, &data, 0);
        return false;
    }

    return true;
}

void RadarCSI::processData()
{
    if (!csi_display_queue) {
        ESP_LOGE(TAG, "CSI display queue not initialized");
        return;
    }

    csi_data_t csi_display_data;

    while (xQueueReceive(csi_display_queue, &csi_display_data, 0) == pdTRUE) {
        // 如果已停止处理，只清空队列，不更新UI
        if (is_processing_stopped) {
            ESP_LOGW(TAG, "Processing stopped");
            continue;
        }

        UBaseType_t queueLength = uxQueueMessagesWaiting(csi_display_queue);
        if (queueLength > 10) {
            ESP_LOGW(TAG, "UI queue length: %d", queueLength);
        }

        // 更新图表（包括正常数据和定时器补充的数据）
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

    // 停止定时器
    if (update_timer) {
        esp_timer_stop(update_timer);
    }

    // 清空队列中的所有数据
    if (csi_display_queue) {
        csi_data_t dummy_data;
        while (xQueueReceive(csi_display_queue, &dummy_data, 0) == pdTRUE) {
            // 只是清空队列
        }
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
    avg_index = 0;
    avg_count = 0;
    memset(avg_buffer, 0, sizeof(avg_buffer));
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
