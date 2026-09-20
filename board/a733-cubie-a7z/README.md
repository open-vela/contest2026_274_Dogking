# A733 / 瑞莎 Cubie A7Z 板级适配

本目录是比赛仓库中由参赛者维护的 A733 板级适配源码。`contest2026_274_Dogking.xml`
会把 `openvela-overlay` 下的内容映射到标准 openvela 工作树。

## 硬件约束

- SoC：全志 A733，ARM64，2× Cortex-A76 + 6× Cortex-A55
- 内存：4 GiB LPDDR4/4X，物理地址从 `0x40000000` 开始
- openvela 加载/入口地址：`0x40200000`
- BL31 保留区域：`0x48000000..0x48ffffff`
- 控制台：UART0，地址 `0x02500000`，GIC IRQ 34，115200 8N1
- GICv3：分发器 `0x03400000`，重分发器 `0x03460000`
- 启动存储：SDMMC0，地址 `0x04020000`，microSD 四线模式
- 无线模块：FCU760K/AIC8800D80-U02，通过专用 USB1 连接
- NPU：全志 VIP2，硬件 CID `0x1000003b`
- 当前测试板未安装 UFS；UFS 缺失必须保持非阻塞

## 覆盖层结构

- `vendor/allwinnertech/chips/a733`：芯片启动、UART、SDMMC、Wi-Fi、NPU、
  TRNG、THS、看门狗和 USB 摄像头诊断。
- `vendor/allwinnertech/boards/a733/cubie-a7z`：defconfig、链接脚本、板级
  初始化、存储、GPIO 和电源控制。
- `apps/system/a733wifi`：双频 Wi-Fi 交互命令。
- `apps/system/a733services`：持久化的可选开机服务管理。
- `apps/system/a733ftpd`：FTP 服务封装。
- `apps/system/a733npu`：VIP2 NPU ABI v4 用户命令。
- `apps/system/aipet`：基于官方 Agent 的桌宠路由、初始化和输出适配。
- `apps/system/aipetllm`：Qwen2.5-1.5B GGUF ARM64 CPU 推理应用。
- `apps/system/aipetasr`：本地流式 ASR 扩展接口骨架，尚未形成板端识别链路。
- 其他映射文件：本板适配所需的、逐文件列出的兼容性修改。

权威功能配置文件为：

```text
openvela-overlay/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig
```

## 启动边界

本适配复用已经验证的厂商 Boot0/SCP/BL31/U-Boot 启动链。生成的
`nuttx.bin` 带 ARM64 Image 头，通过 `booti` 进入；本仓库不重新分发厂商启动
固件。

## 完成边界

UART/NSH、8 核 SMP、内存、SD/GPT/FAT、基础外设、Wi-Fi/网络服务、VIP2 模型
执行、官方 Agent 联网对话和本地 Qwen 基础生成已经在真机验证。UART4 TTS 已
构建待板测；I2S PCM、USB UAC/UVC、Bluetooth HCI、GPU 和 UFS 尚未完成，不能
在外部材料中声称这些功能已经完成。
