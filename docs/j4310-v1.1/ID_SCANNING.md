# DM-J4310-2EC V1.1 — DM-MC02 双 CAN ID 扫描

## 版本和安全
- 本仓库保留只读扫描版快照，原上游依赖由 `upstream` 固定版本子模块获取；不要与后续持续旋转版混用。
- 扫描程序只通过 CAN1/2 接收以及在显式 `SCAN` 后发送达妙寄存器 **只读** 请求（CAN 0x7FF、操作码 0x33、寄存器 RID 8/7）。绝不发送电机使能、位置、速度、转矩、清错、写入、保存参数命令。
- 固件启动时两个 24V 电源控制 GPIO（PC13/PC14）输出低电平，用户发 `POWER ON WIRED` 才拉高。**没有实物电压测量，GPIO 低不等于验证物理电源已断电。** 外部24V供电需用户控制，`POWER OFF` 不是紧急停车。

## 接线（先断电接线，禁止带电插拔）
1. 彻底切断外部 24V，检查电机连接器无电压。固定电机，提供足够的轴运动空间；每条总线首次仅接一台电机。
2. 按原厂 J4310 V1.1 的 XT30(2+2) 线束核对 VCC 24V、GND、CAN_H、CAN_L，切勿凭接头颜色或外形猜极性。
3. 连接同名 CAN_H/L 并共地，按 CAN 总线实际布线核对两端终端电阻；检查电源电流限制和急停。
4. 再连接 SWD 调试器和 STM32 USB CDC；烧录前核对目标 STM32H723、固件哈希。测量受控 24V 通道在固件默认低电平时的真实电压。
5. 所有接线完成后，现场确认并接通电源，打开串口依次发送 `STATUS`、`POWER ON WIRED`，再次测量电源输出，随后 `SCAN`。
6. 读取 CAN1/2 发现的 `ESC_ID` 与 `MST_ID`；扫描完成后发送 `POWER OFF` 并 **实际切断外部电源** 才能换线。

## 工程参数和协议
- FDCAN1 RX=PD0、TX=PD1；FDCAN2 RX=PB5、TX=PB6，经典标准 CAN 帧；J4310 V1.1 官方标称 1Mbps，实际时序/硬件尚未验证。
- `SCAN` 后 CAN1/CAN2 分别从 0x000 到 0x7FE 枚举 ESC_ID，每 20ms 对每个 ID 发送只读帧：`CAN_ID=0x7FF, DATA=[ESC_L,ESC_H,0x33,0x08,0,0,0,0]`；获得 RID8 响应后再读 RID7 作为 MST_ID。
- 全范围扫描约 41 秒，完成不保证找得到：部分旧版驱动可能不支持 CAN 读参数。此时可使用达妙调试助手经电机调试串口读取 ID，不要猜测或使能电机。
- `STATUS` 显示两个 CAN 口收到的帧数量及已知 ID；`POWER ON WIRED` 请求拉高电源GPIO；`SCAN` 开始只读查询；`POWER OFF` 请求关闭电源 GPIO 和停止查询。

## 编译（不烧录）
```bash
cd workspace
make -f Makefile.j4310-scan -j6
sha256sum build/j4310-v11-scan/J4310-v11-id-scan.hex
```

## 当前验证
2026-11-05：ARM GCC 编译成功、`.hex/.elf/.bin` 均生成。ELF 的符号中没有 `RobotInit`、`GimbalInit`、`DJIMotorEnable` 运行入口。没有接入实物，尚未烧录、未测电源极性、未收到CAN帧。读寄存器语义依据达妙官方公开 `motor-sdk`，V1.1 固件兼容性待实测。
