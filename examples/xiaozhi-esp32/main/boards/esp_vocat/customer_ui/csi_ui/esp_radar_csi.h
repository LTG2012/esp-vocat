/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// CSI数据结构定义（单通道）
typedef struct {
    uint8_t start[2];
    uint32_t id;
    int64_t time_delta;
    float cir;             // 振幅（单通道）
    float pha;             // 相位（单通道）
    uint8_t end[2];
} __attribute__((packed)) csi_data_t;

// C interface for RadarCSI (for calling from C code)
void radar_csi_process_data(void);
void radar_csi_process_chart_m_data(void);

#ifdef __cplusplus
} // extern "C"

namespace esp_brookesia::apps {

/**
 * @brief RadarCSI类 - 处理CSI数据并显示波形
 */
class RadarCSI {
public:
    /**
     * @brief 获取单例实例
     *
     * @return RadarCSI* 单例指针
     */
    static RadarCSI *getInstance();

    /**
     * @brief 析构函数
     */
    ~RadarCSI();

    /**
     * @brief 初始化RadarCSI
     *
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool init();

    /**
     * @brief 启动 CSI 采集与处理流水线
     *
     * 注意：仅在真正进入 CSI 应用（UI 已经创建）后调用，
     * 避免一连上 Wi‑Fi 就自动开始采集。
     */
    void startPipeline();

    /**
     * @brief 启动ping路由器
     */
    void startPing();

    /**
     * @brief 停止ping路由器
     */
    void stopPing();

    /**
     * @brief 初始化图表
     */
    void initCharts();

    /**
     * @brief 推送CSI数据到队列
     *
     * @param data CSI数据
     * @return true 推送成功
     * @return false 推送失败
     */
    bool pushData(const csi_data_t &data);

    /**
     * @brief 处理CSI数据并更新图表
     */
    void processData();

    /**
     * @brief 停止数据处理（清空队列但不更新UI）
     */
    void stopDataProcessing();

    /**
     * @brief 恢复数据处理
     */
    void resumeDataProcessing();

    /**
     * @brief 重置图表状态（在 UI 被销毁后调用）
     */
    void resetChartState();

    // ========== ScreenM 图表相关方法 ==========
    /**
     * @brief 初始化 ScreenM 图表
     */
    void initChartM();

    /**
     * @brief 推送 ScreenM 图表数据到队列
     *
     * @param value uint16_t 值
     * @return true 推送成功
     * @return false 推送失败
     */
    bool pushChartMData(uint16_t value);

    /**
     * @brief 处理 ScreenM 图表数据并更新
     */
    void processChartMData();

    /**
     * @brief 补充上一次的数据（由定时器触发）
     */
    void supplementLastData();

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    RadarCSI();

    /**
     * @brief 更新图表
     *
     * @param data CSI数据
     */
    void updateChart(const csi_data_t &data);

    /**
     * @brief 内部图表更新函数（不重置定时器）
     *
     * @param data CSI数据
     * @param reset_timer 是否重置定时器
     */
    void doUpdateChart(const csi_data_t &data, bool reset_timer);

    /**
     * @brief 更新 ScreenM 图表
     *
     * @param value uint16_t 值
     */
    void updateChartM(uint16_t value);

    // 单例实例
    static RadarCSI *_instance;

    // ========== ScreenW 图表相关 ==========
    // 队列句柄
    QueueHandle_t csi_display_queue;

    // 图表序列（单条曲线）
    lv_chart_series_t *ser;

    // 数据范围（单通道）
    // 与 esp_radar_csi.cpp 中的 LVGL_CHART_POINTS 保持一致，避免数组越界
    static constexpr int CHART_POINTS = 300 / 3;   // 必须等于 LVGL_CHART_POINTS
    float range[CHART_POINTS];
    // 使用 float 存放 Y 轴范围，避免负值写入 uint16_t 产生 655xx 这样的溢出
    float y_range[2];

    // 简单滑动平均滤波窗口大小（最近 AVG_WINDOW 个点求平均）
    static constexpr int AVG_WINDOW = 10;
    float avg_buffer[AVG_WINDOW];
    int avg_index;
    int avg_count;

    // 状态变量
    bool chart_initialized;
    uint8_t chart_count;
    bool is_processing_stopped;

    // 定时器相关（防止波形卡顿）
    esp_timer_handle_t update_timer;
    csi_data_t last_data;  // 保存上一次的数据

    // ========== ScreenM 图表相关 ==========
    QueueHandle_t chart_m_queue;
    lv_chart_series_t *chart_series_m;

    static constexpr int CHART_M_POINTS = 30;
    float chart_m_range[30];
    float chart_m_y_range[2];

    static constexpr int CHART_M_AVG_WINDOW = 15;
    float chart_m_avg_buffer[15];
    int chart_m_avg_index;
    int chart_m_avg_count;
    int chart_m_count;
    bool chart_m_initialized;
};

} // namespace esp_brookesia::apps

#endif // __cplusplus
