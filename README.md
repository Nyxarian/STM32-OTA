# STM32-OTA — STM32F407 嵌入式掌上终端与 OTA 升级系统

基于 **STM32F407VET6 + FreeRTOS + LVGL + ESP8266** 的带屏嵌入式终端，支持通过 WiFi 远程进行 **OTA 固件升级**。整个系统由两个独立的 Keil 工程组成：

| 工程 | 目录 | 作用 |
|------|------|------|
| **OTA（App）** | `OTA/` | 应用程序：LVGL 图形界面、WiFi 管理、各功能应用、OTA 升级入口 |
| **OTA_Bootloader（Bootloader）** | `OTA_Bootloader/` | 引导程序：上电自检、App 校验、IAP 固件下载/写入、跳转 App |

> 烧录时**必须先烧 Bootloader**，再烧 App 或通过 OTA 升级，二者通过固定的 Flash 分区协作。

---

## 1. 项目简介与功能

### 1.1 应用程序（OTA/App）
- 锁屏界面 → 桌面启动器（黑 + 克莱因蓝主题，6 应用图标网格，状态栏/标题栏/底部信息栏）
- 页面导航框架 `ui_nav`：图标焦点导航、返回键回锁屏
- 应用列表：
  - **WiFi**：WiFi 配置与连接（ESP8266 AT 驱动）、AT 指令测试页
  - **系统升级**：检查 OTA 服务器 manifest、比对版本、发起远程升级
  - **监控**：FreeRTOS 任务统计（CPU 占用/栈使用）等系统信息
  - **天气**、**俄罗斯方块**、**设置** 等示例应用
- 多字号中英文字库（8/12/14），OTA 升级 UI 与进度显示
- 通过 RTC 备份寄存器写入升级请求标志并软复位，进入 Bootloader 升级模式

### 1.2 引导程序（OTA_Bootloader）
- 上电校验 App 有效性：向量表检查（栈指针范围、复位向量落在 App 区、Thumb 位）
- 对 **OTA 提交过**的镜像执行严格 CRC32 校验，直接烧录（IMAGE_INFO 为 0xFF）的镜像仅做向量表检查
- **双通道 IAP 升级**：串口（CH340 直连）与 ESP8266 TCP（无线）共用同一升级协议循环
- 升级状态机：`EMPTY → UPDATING → COMMITTED`，写入失败可回滚/停留 Bootloader
- 升级完成校验通过后，重定位向量表（SCB->VTOR）并跳转 App

---

## 2. 系统架构：OTA 与 OTA Bootloader 的联系

### 2.1 Flash 分区（STM32F407VET6，512 KB）

| 地址范围 | 大小 | 内容 |
|----------|------|------|
| `0x08000000 ~ 0x0800FFFF` | 64 KB（扇区 0-3） | **Bootloader** |
| `0x08010000 ~ 0x0807FFDF` | 448 KB（扇区 4-7） | **App**（最大 458720 B = 0x6FFE0） |
| `0x0807FFE0` | 4 B | **IMAGE_STATE**：`EMPTY(0xFFFFFFFF) / UPDATING(0xFFFFFFFE) / COMMITTED(0xFFFFFFFC)` |
| `0x0807FFF0` | 16 B | **IMAGE_INFO**：magic `"IAP1"`(0x49415031) + 镜像长度 + CRC32 等 |

App 工程链接基址为 `0x08010000`（见 `OTA/MDK-ARM/OTA/OTA.sct`），Bootloader 为 `0x08000000`。

### 2.2 启动流程

```
上电复位
  └─> Bootloader 启动（0x08000000）
       ├─ 检查 RTC 备份寄存器 BOOT_FLAG_MAGIC（0x554F5441 "UOTA"）
       │    ├─ 有升级请求 → 进入 IAP 升级模式（串口 / ESP8266 TCP 下载固件）
       │    └─ 无升级请求 → 校验 App 有效性（向量表 / CRC32）
       │         ├─ 有效 → 设置 SCB->VTOR = 0x08010000，跳转 App
       │         └─ 无效 → 停留在 Bootloader（等待升级）
```

### 2.3 升级流程（App 与 Bootloader 的分工）

```
App（OTA 工程）                                 Bootloader（OTA_Bootloader 工程）
─────────────────                              ────────────────────────────────
用户进入「系统升级」
  │
  ├─ WiFi 请求 GET /ota/manifest.txt（版本/长度/CRC32）
  ├─ 版本比对（manifest.version vs APP_VERSION_STRING）
  ├─ POST /ota/upgrade 通知 OTA 服务器开始传输
  ├─ BootFlag_SetUpdateRequest()  写入 RTC 标志
  └─ NVIC_SystemReset()           软复位
                                                 ┌─ 检测到 RTC 升级标志
                                                 ├─ 串口(CH340) 或 ESP8266 TCP 建立连接
                                                 ├─ 按 IAP 协议分帧接收固件
                                                 ├─ 擦除扇区 4-7（0x08010000 起）
                                                 ├─ 写入固件到 0x08010000
                                                 ├─ 写 IMAGE_INFO（magic+长度+CRC32）
                                                 ├─ IMAGE_STATE: UPDATING → COMMITTED
                                                 ├─ 严格 CRC32 校验
                                                 │    ├─ 通过 → 跳转新 App
                                                 │    └─ 失败 → 回滚 / 停留等待重试
                                                 └─ 清除 RTC 升级标志
```

### 2.4 两个工程协同的关键点
- **跨工程信令**：RTC 备份寄存器（`0x40002850`，即 BKP0R）是 App 通知 Bootloader 的唯一通道——App 写 `"UOTA"` 标志后软复位，Bootloader 读取后进入升级模式，升级完成后清除。
- **共享布局常量**：`IAP_FLASH_APP_START(0x08010000)`、`IMAGE_INFO` 地址等定义在 `OTA_Bootloader/MDK-ARM/IAP/iap_flash.h`，App 侧 OTA 相关路径/尺寸配置在 `OTA/MDK-ARM/Apps/Wifi/wifi_config.h`。
- **双通道升级**：Bootloader 支持有线（CH340 串口）与无线（ESP8266 TCP）两种升级通道，共用同一个协议循环，方便无串口线时远程升级。
- **CH340 与 ESP8266 物理互斥**：两者共用 USART1 引脚，同一时刻只能接一个；App 内长按 **ESC 2 秒**写入升级标志软复位，正是为了让 ESP8266 在升级期间转由 Bootloader 接管。

---

## 3. 硬件平台

| 项目 | 说明 |
|------|------|
| MCU | STM32F407VET6，Cortex-M4F @168 MHz，512 KB Flash，192 KB SRAM（128 KB + 64 KB CCM），LQFP100 |
| 晶振 | 8 MHz HSE（PH0/PH1）；Bootloader 使用内部 HSI 16 MHz |
| 显示 | ST7789 320×240 RGB565，SPI1 + DMA，软件 PWM 背光 |
| 无线 | ESP8266（ESP-01S，Nano AT 固件，仅支持 2.4 GHz），USART1 AT 指令 |
| 调试/升级串口 | USART1 @115200，CH340 USB-TTL（与 ESP8266 物理互斥） |
| 按键 | 6 个：KEY_1~KEY_6（PD0~PD5） |
| 调试接口 | SWD（PA13/PA14） |

### 引脚分配要点

| 引脚 | 功能 |
|------|------|
| PA4 | SPI1_NSS（LCD_CS） |
| PA5/PA6/PA7 | SPI1_SCK / MISO / MOSI（LCD） |
| PC4 / PC5 | LCD_RST / LCD_BLK（背光） |
| PB0 | LCD_DC |
| PD0~PD5 | KEY_1 ~ KEY_6 |
| PA9 / PA10 | USART1_TX / RX（CH340 或 ESP8266，二选一） |
| PH0 / PH1 | 8 MHz HSE 晶振 |
| PA13 / PA14 | SWDIO / SWCLK |

---

## 4. 开发环境

| 工具 | 版本/说明 |
|------|-----------|
| Keil MDK-ARM（uVision 5） | 编译工具链：**ARM Compiler 5（armcc）**，C99 |
| STM32CubeMX | 生成 `.ioc` 工程与 `Core/Drivers` 初始化代码（HAL 库） |
| STM32 HAL 库 | STM32F4xx HAL / CMSIS |
| FreeRTOS | **V10.3.1**，CMSIS-RTOS v2 封装（`Middlewares/Third_Party/FreeRTOS`） |
| LVGL | **9.5.0**（`OTA/lvgl`，含 ST7789 移植 `porting/lv_port_lcd_stm32.c`） |
| EIDE（可选） | VS Code 嵌入式扩展，可用 `MDK-ARM/.eide` 配置构建 |

> 两个工程均使用同一套 HAL/CMSIS 驱动与同一芯片型号（STM32F407VETx）。

---

## 5. 编译与烧录

### 5.1 编译
1. 用 Keil MDK-ARM 打开 **App**：`OTA/MDK-ARM/OTA.uvprojx`，按 F7 编译，产物 `OTA.hex`（链接基址 `0x08010000`）。
2. 打开 **Bootloader**：`OTA_Bootloader/MDK-ARM/OTA_Bootloader.uvprojx`，按 F7 编译，产物 `OTA_Bootloader.hex`（链接基址 `0x08000000`）。

### 5.2 首次烧录（ST-Link）
1. 先烧 **Bootloader**：`OTA_Bootloader.hex` → 0x08000000。
2. 再烧 **App**：`OTA.hex` → 0x08010000（或用下方 OTA 方式升级）。
3. 直接烧录的 App 没有 IMAGE_INFO（全 0xFF），Bootloader 只做向量表检查即可跳转；只有**通过 OTA 提交**的镜像才做严格 CRC32 校验。

### 5.3 ESP8266 AT 固件烧录（`ESP8266_Firmware/Nano_AT_1MB`）
ESP8266 模块需烧录 Nano AT 固件（1 MB Flash），地址如下（详见 `ESP8266_Firmware/Nano_AT_1MB/README.md`）：

| 文件 | Flash 地址 |
|------|-----------|
| `boot_v1.7.bin` | `0x00000` |
| `user1.1024.new.2.bin` | `0x01000` |
| `esp_init_data_default_v08.bin` | `0xFC000` |
| `blank.bin` | `0x7E000` 与 `0xFE000` |

### 5.4 OTA 升级演示
1. 在电脑上启动 OTA 服务器（发布 `manifest.txt` 与 `firmware.bin`，见 App 内 `wifi_config.h` 的服务器地址/端口/路径配置）。
2. 设备连接 WiFi 后进入「系统升级」：检查到新版本 → 确认升级。
3. 设备软复位进入 Bootloader，自动完成下载、写入、校验并跳转新固件。

---

## 6. 目录结构

仓库按两级分层：顶层先分 **OTA（App）** 与 **OTA_Bootloader（Bootloader）** 两个工程，再在每个工程内部向下分 `MDK-ARM/`（Keil 工程）等子目录。

```
STM32-OTA/
├── OTA/                              # ── 应用程序（App）──
│   ├── App/                          #   应用层：OTA UI、升级标志（RTC）、版本号
│   ├── BSP/                          #   板级支持包：按键
│   ├── Core/                         #   CubeMX 生成：main/gpio/usart/spi/tim/freertos/中断
│   ├── Drivers/                      #   CMSIS + STM32F4xx HAL 驱动
│   ├── lvgl/                         #   LVGL 9.5.0 图形库（含 ST7789 移植）
│   ├── Middlewares/                  #   FreeRTOS（CMSIS-RTOS v2）
│   ├── tools/                        #   字体/壁纸生成脚本
│   ├── MDK-ARM/                      #   ── Keil MDK-ARM 工程 ──
│   │   ├── Apps/                     #     应用页面：Monitor/Settings/SystemUpgrade/Tetris/Weather/Wifi
│   │   ├── UI/                       #     LVGL 页面/导航/主题（ui_nav、ui_pages_*、ui_theme）
│   │   ├── OTA.uvprojx               #     Keil 工程文件（打开即编译）
│   │   ├── startup_stm32f407xx.s     #     启动文件
│   │   ├── .eide/                    #     EIDE（VS Code）构建配置
│   │   └── RTE/                      #     （Keil 自动生成，不入库）
│   └── OTA.ioc                       #   STM32CubeMX 工程
│
├── OTA_Bootloader/                   # ── 引导程序（Bootloader）──
│   ├── Core/                         #   main/bootloader/gpio/usart/spi/中断
│   ├── Drivers/                      #   CMSIS + STM32F4xx HAL 驱动
│   └── MDK-ARM/                      #   ── Keil MDK-ARM 工程 ──
│       ├── ESP/                      #     ESP8266 AT 指令 + TCP 驱动
│       ├── IAP/                      #     IAP：Flash 写入/升级协议/传输层/启动显示
│       ├── OTA_Bootloader.uvprojx    #     Keil 工程文件（打开即编译）
│       ├── startup_stm32f407xx.s     #     启动文件
│       └── .eide/                    #     EIDE（VS Code）构建配置
│
└── ESP8266_Firmware/                 # ESP8266 AT 固件（Nano AT 1MB，烧录 WiFi 模块用）
```

---

## 7. 配置要点

### 7.1 WiFi 凭据（已脱敏，需自行填入）
仓库中两处 WiFi 凭据已替换为空字符串，克隆后请填入你自己的网络（ESP8266 仅支持 2.4 GHz，SSID 区分大小写）：
- App：`OTA/MDK-ARM/Apps/Wifi/wifi_config.h` 的 `WIFI_CONFIG_SSID` / `WIFI_CONFIG_PASSWORD`
- Bootloader：`OTA_Bootloader/MDK-ARM/ESP/esp8266_config.h` 的 `ESP8266_CONFIG_WIFI_SSID` / `ESP8266_CONFIG_WIFI_PASSWORD`

> 两处必须使用**同一个网络**，因为软复位进入 Bootloader 后由 Bootloader 自己恢复联网。

### 7.2 OTA 服务器
`OTA/MDK-ARM/Apps/Wifi/wifi_config.h` 中配置：服务器主机/端口、`/ota/manifest.txt`、`/ota/firmware.bin`、`/ota/upgrade` 路径以及产品名（`550W_F407`）与 App 最大尺寸（`458720`）。

### 7.3 时钟配置
- **App**：8 MHz HSE + PLL（M=4, N=168, P=2）→ **SYSCLK 168 MHz**；AHB /1，APB1 /4（42 MHz），APB2 /2（84 MHz）。
- **Bootloader**：内部 **HSI 16 MHz** + PLL（M=8, N=168, P=2）→ **SYSCLK 168 MHz**（不依赖外部晶振，保证无晶振也能升级）。

### 7.4 Flash 布局与版本
- 分区/校验常量：`OTA_Bootloader/MDK-ARM/IAP/iap_flash.h`、`OTA_Bootloader/Core/Inc/bootloader.h`。
- App 版本号：`OTA/App/app_version.h`（`APP_VERSION_STRING`），与服务器 manifest 中的版本比对决定是否升级。

---

## 8. 说明
- 编译产物、Keil 输出目录（`MDK-ARM/OTA/`、`MDK-ARM/OTA_Bootloader/`、`build/`）、`.cmsis` 包缓存、`RTE/` 等均为自动生成，已由 `.gitignore` 排除；`.uvprojx`、`.ioc`、启动文件等工程配置全部入库，克隆后可直接打开编译。
