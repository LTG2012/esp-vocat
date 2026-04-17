# 磁吸滑动开关 UI 联动示例

[English Version](./README.md)

本示例演示了如何通过 UART 串口接收喵伴底座发来的磁吸事件消息，并在屏幕上驱动 LVGL UI 做出对应的动画响应。

- [嘉立创 esp-vocat 开源硬件链接](https://oshwhub.com/esp-college/echoear)
- [嘉立创旋转底座开源硬件链接](https://oshwhub.com/esp-college/esp-echoear-base)
- [配套旋转底座 gitee 代码仓库](https://gitee.com/esp-friends/esp-vocat-base)

## 功能简介

- 通过 UART 接收旋转底座发送的磁吸事件帧
- 解析自定义串口帧协议（帧头 `AA 55`，含校验和）
- 根据事件类型触发不同的 LVGL UI 动画（铃铛、鱼、雪糕、甜甜圈、手机等）
- 事件发生时屏幕顶部短暂显示事件名称文字（800ms 后自动淡出）

## 硬件准备

- **开发板：** esp-vocat
- **显示屏：** 3.5寸 LCD 显示屏
- **旋转底座：** 通过 UART 与主板通信
  - TX（主板接收）：GPIO4
  - RX（主板发送）：GPIO5
  - 波特率：115200

## 串口帧协议

```
帧格式: [AA][55][LEN_H][LEN_L][CMD][DATA...][CHECKSUM]

字段说明:
  AA 55       - 帧头（固定两字节）
  LEN_H/LEN_L - 数据段长度（大端序，包含 CMD 字节）
  CMD         - 命令码（磁吸事件：0x03）
  DATA        - 数据字节（磁吸事件为 2 字节事件码，大端序）
  CHECKSUM    - 从 CMD 开始到 DATA 末尾所有字节之和（8位截断）
```

**磁吸事件帧示例（滑块向下，事件码 0x0001）：**

```
AA 55 00 03 03 00 01 04
```

## 支持的磁吸事件

| 事件码 | 事件名称 | UI 动画描述 |
|--------|---------|------------|
| 0 | INIT | 上电初始状态，无动画 |
| 1 | SLIDE_DOWN | 铃铛图片从左上角平移到右下角（200ms） |
| 2 | SLIDE_UP | 铃铛图片从右下角平移到左上角（200ms） |
| 3 | REMOVE_FROM_UP | 铃铛从左上角飞出屏幕右上方（200ms） |
| 4 | REMOVE_FROM_DOWN | 铃铛从右下角飞出屏幕右侧（200ms） |
| 5 | PLACE_FROM_UP | 铃铛从屏幕右上方飞入左上角（200ms） |
| 6 | PLACE_FROM_DOWN | 铃铛从屏幕右侧飞入右下角（200ms） |
| 7 | SINGLE_CLICK | 铃铛晃动并旋转 45°，然后恢复（300ms） |
| 8 | FISH_ATTACHED | 鱼形配件图片淡入动画 |
| 9 | FISH_DETACHED | 鱼形配件图片淡出动画 |
| 10 | PAIRING | EchoEar 底座侧移 + 配对图标出现（延迟 500ms） |
| 11 | PAIRING_CANCELLED | 配对图标消失 + EchoEar 底座归位 |
| 12 | ICE_CREAM_ATTACHED | 显示雪糕配件图片，播放入场动画 |
| 13 | ICE_CREAM_DETACHED | 雪糕配件图片播放离场动画 |
| 14 | DONUT_ATTACHED | 显示甜甜圈配件图片，播放入场动画 |
| 15 | DONUT_DETACHED | 甜甜圈配件图片播放离场动画 |
| 16 | IPHONE_LEAN_FRONT | iPhone 从正面倚靠底座动画（Z 轴增大）|
| 17 | IPHONE_LEAN_FRONT_DETACHED | iPhone 从正面离开底座动画 |
| 18 | IPHONE_UNDER_BASE | iPhone 被压在底座底下动画（Z 轴为负）|
| 19 | IPHONE_UNDER_BASE_DETACHED | iPhone 从底座下离开动画 |

## 文件结构

```
mag_slide_switch/
├── main/
│   ├── main.c                         # 主入口：UART 初始化、帧解析、事件分发
│   ├── CMakeLists.txt
│   └── mag_slide_switch_ui/           # SquareLine Studio 1.5.4 生成的 LVGL UI
│       ├── ui.c / ui.h                # UI 对象声明与动画函数定义
│       ├── ui_events.c / ui_events.h  # 各事件的处理函数（触发动画）
│       ├── ui_helpers.c / ui_helpers.h# LVGL 动画辅助工具函数
│       ├── images/                    # 图片资源（铃铛、鱼、雪糕、甜甜圈等）
│       └── screens/
│           └── ui_MagSlideSwitchScreen.c  # 主屏幕布局定义
├── partitions.csv                     # 分区表（factory 分区 15MB）
├── sdkconfig.defaults                 # 默认编译配置
└── README_CN.md / README.md
```

## 编译与烧录

**依赖：** ESP-IDF `release/v5.3` 及以上版本。

```bash
# 进入示例目录
cd examples/mag_slide_switch

# 编译、烧录并打开串口监视
idf.py -p PORT flash monitor
```

退出监视器：按 `Ctrl-]`。

## 串口日志示例

```
I (xxx) mag_slide_switch: UART initialized: Port=1, Baud=115200, TX=5, RX=4
I (xxx) mag_slide_switch: Display initialized
I (xxx) mag_slide_switch: UI initialized successfully
I (xxx) mag_slide_switch: Magnetic slide switch UI is running
I (xxx) mag_slide_switch: Waiting for magnetic switch events...
...
I (xxx) mag_slide_switch: Slider moved down
I (xxx) mag_slide_switch: Slider moved up
I (xxx) mag_slide_switch: Fish attached detected
```

## 技术支持与反馈

- 技术问题请访问 [esp32.com](https://esp32.com) 论坛
- 功能建议或 Bug 报告请提交 Gitee Issue