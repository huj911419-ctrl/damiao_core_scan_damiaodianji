# 达妙 DM-J4310-2EC V1.1 双 CAN 电机 ID 只读扫描工程

> **STM32H723（DM-MC02） · CAN1/CAN2 · ESC_ID / MST_ID 自动探测 · 不发送电机运动命令**

本仓库保存已经在真实 J4310 V1.1 上完成 ID 探测的**只读扫描版本**，不是后续开发的速度模式/连续旋转固件。适用于学习 STM32 HAL、FDCAN、USB CDC 和达妙的 CAN 寄存器查询协议。

## 1. 实机验证结果

2026-11-05，通过 DM-MC02 控制板的 **CAN2** 读取到：

| 项目 | 结果 |
| --- | --- |
| 电机型号 | 达妙 DM-J4310-2EC V1.1 |
| 主控 MCU | STM32H723（Cortex-M7） |
| 使用总线 | CAN2 |
| ESC_ID（电机命令/参数标识） | **0x000**（十进制 0） |
| MST_ID（电机反馈标识） | **0x010**（十进制 16） |
| 应答 CAN 标识符 | 0x010 |
| CAN1 扫描结果 | 本次未观测到帧；不代表 CAN1 硬件损坏 |

详见 [实测记录](docs/j4310-v1.1/ID_RESULT_2026-11-05.md)。**其他电机的 ID 不一定相同，不要直接照抄此结果。**

## 2. 项目是怎样组织的？

本扫描功能基于原 [Meta-Team/Meta-Embedded-NG](https://github.com/Meta-Team/Meta-Embedded-NG) 工程。为了保留完整的 STM32 HAL、CMSIS、USB Device、FreeRTOS、链接脚本及其他原始依赖，同时避免把上游几百 MB 的依赖库重复上传，本仓库采用 **固定版本 Git 子模块 + 本项目修改文件** 的方式：

```text
.
├── README.md                         # 本说明
├── .gitmodules                      # 锁定原 Meta-Embedded-NG 完整工程
├── upstream/                        # Git 子模块：完整原工程及厂商依赖
├── Core/Src/main.c                  # 扫描固件独立入口
├── Core/Src/gpio.c                  # 24V 控制引脚启动时默认不使能
├── application/j4310_scan/
│   ├── j4310_scan.c                 # 双 CAN 扫描、USB 命令、CAN 响应解析
│   └── j4310_scan.h
├── Makefile                          # 上游 Makefile 的扫描模式适配
├── Makefile.j4310-scan              # 仅构建扫描固件
├── cmsis-dap-h7.cfg                 # OpenOCD H7 SWD 配置
├── docs/j4310-v1.1/                 # 接线/协议说明与实测结果
├── scripts/prepare_project.sh      # 组合上游源码与扫描模块
├── scripts/build_scan.sh           # 一条命令编译
└── workspace/                       # 执行脚本生成的完整可编译项目（不提交）
```

这不是仅有几段代码的示例：执行准备脚本后，`workspace/` 会有**完整的原工程源码和依赖**，再覆盖本仓库经实机验证的扫描文件。上游固定在 `a0ebb9596b9cf5a827140eb4209af1628ea67525`，扫描来源快照为 `3fda6129c8c0429585b7d9e6500d1d85583c38eb`，确保版本可重复构建。

## 3. 所需硬件与软件

- 达妙 DM-MC02 控制板（STM32H723），以及 DM-J4310-2EC V1.1 电机。
- **外部 24V 电机电源和实体断电装置**，匹配的线束、终端电阻与电压测量工具。
- DAPLink / CMSIS-DAP（SWD 编程），**另外还需 STM32 控制板自身的 USB 数据线**，用于 USB CDC 输入命令和查看扫描结果。
- Ubuntu / Linux、`git`、`make`、`arm-none-eabi-gcc`、OpenOCD。扫描命令推荐 `picocom` 或 Python 的 `pyserial`。

**注意：DAPLink 的 `/dev/ttyACM*` 通常是调试器自身端口，不等于 STM32 固件的 USB CDC。** 请通过 `/dev/serial/by-id/` 分辨控制板的 `STM32 Virtual ComPort`。

## 4. 下载并准备完整工程

```bash
# 直接克隆（子模块可以由准备脚本自动初始化）
git clone https://github.com/huj911419-ctrl/damiao_core_scan_damiaodianji.git
cd damiao_core_scan_damiaodianji

# 下载固定版本原工程，组合成完整可编译源码
./scripts/prepare_project.sh
```

首次执行会下载上游依赖，时间取决于网络。准备完成后，原工程和本项目扫描文件位于 `workspace/`，**不要到后来用于连续旋转的 J4310 工程里编译**。

## 5. 编译（只编译，不烧录）

```bash
./scripts/build_scan.sh
```

编译使用 `Makefile.j4310-scan` 和 `J4310_SCAN_ONLY=1`，不会进入原始的机器人任务/云台运动控制逻辑。生成：

```text
workspace/build/j4310-v11-scan/J4310-v11-id-scan.elf
workspace/build/j4310-v11-scan/J4310-v11-id-scan.hex
workspace/build/j4310-v11-scan/J4310-v11-id-scan.bin
```

如果编译器不在 PATH，可设置 `GCC_PATH=/你的/arm-none-eabi/bin ./scripts/build_scan.sh`。默认会查找 STM32Cube bundle 路径并回退到系统 PATH。

## 6. 接线与烧录

**先切断实体 24V 电源，测量确认无电压后再接线；不要带电插拔 CAN/电机线束。**

1. 按电机手册核对 24V、GND、CAN_H、CAN_L；不要仅凭线色判断电源极性。
2. CAN_H 对 CAN_H，CAN_L 对 CAN_L，电源地共地，并检查 CAN 总线两端终端电阻；首次测试建议每条总线只接一台电机。
3. SWD 连接控制板，另外连接 STM32 控制板自身的 USB 数据口以使用 CDC；检查 PC13/PC14 受控24V开关的实际电压。
4. 编译成功后，用 STM32H723 适配的 DAPLink/OpenOCD 烧录扫描版 HEX。示例（按本机 OpenOCD 安装路径调整）：

```bash
cd workspace
openocd -f ../cmsis-dap-h7.cfg \
  -c "program build/j4310-v11-scan/J4310-v11-id-scan.hex verify reset exit"
```

注意：**扫描固件上电时不会自动拉高24V控制引脚，也不会自动使能电机**。它必须收到明确的 USB 命令才会开始查询。

## 7. 串口命令与扫描步骤

找出控制板原生 STM32 USB CDC 端口，例如：

```bash
ls -l /dev/serial/by-id/
# 把下面路径替换为本机实际的 STM32_Virtual_ComPort 项
picocom -b 115200 /dev/serial/by-id/usb-STMicroelectronics_STM32_Virtual_ComPort_...
```

在 CDC 终端里，按顺序输入下面**四条独立命令**，每条以回车结束：

```text
STATUS
POWER ON WIRED
SCAN
POWER OFF
```

**执行 `POWER ON WIRED` 前必须核对线束和电源极性，并确认操作现场安全。** 扫描约需 41 秒（每 20ms 枚举一个 ID）；可在扫描过程中输入 `STATUS` 查看 CAN1/CAN2 接收帧数与 ID。`POWER OFF` 只关闭 GPIO 控制信号，不替代实体断电和电压测量。

扫描仅发送：`CAN ID=0x7FF`，8字节寄存器读取报文 `ESC_L ESC_H 33 RID 00 00 00 00`，其中 `RID=8` 读 ESC_ID，`RID=7` 读 MST_ID；**不会发 `0xFC` 电机使能、目标速度、位置、扭矩或参数写入命令**。

如果无响应，逐一检查 CAN 接线、终端电阻、位速率、外部电源及串口是否接到控制板；不同电机固件不一定支持同一套寄存器查询。

## 8. 核心源码阅读顺序

先读 `Core/Src/main.c` 中的 `J4310ScanInit()` / `J4310ScanPoll()`，再读 `application/j4310_scan/j4310_scan.c` 的 `ParseCommand()`、`Probe()`、`ReadRegister()`、`ReceiveFrames()`。这样能从 STM32 主循环一直追踪到 CAN2 发送与电机反馈解析。

## 9. 安全、来源与许可证

扫描功能只读**指电机 CAN 参数请求不写入、不运动**；`POWER ON WIRED` 仍会接通板上受控电源通道。软件输出不能替代实体急停。

项目建立在 [Meta-Team/Meta-Embedded-NG](https://github.com/Meta-Team/Meta-Embedded-NG) 的 MIT 授权代码基础上，保留原版权和 [`LICENSE`](LICENSE)。ST HAL/CMSIS 等厂商文件在上游工程中具有各自的许可声明，使用时应遵守对应许可证。历史实测结果适用于当时的设备和接线，不构成对其他电机的保证。
