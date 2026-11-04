# v221 镜像软件与封装证据（非板测）

2026-11-02：[发布、烧录注意和完整测试顺序](../../PRODUCT_V1_2FIX_V221_IMAGE.md)。

- `summary.json`：最终镜像大小、SHA256、内核SHA、源码提交、28项门禁、旧镜像保留结果。
- `host-arm-summary.json`：本轮重跑19项语音基础＋产品／Wi-Fi／预览及新MCU，合计28项；没有将旧门禁改名。
- `full-link-summary.json`／`elf-audit.json`：本轮完整固件、摄像头保护、NPU ABI、ISA、构造函数、内存布局与源变更边界审计。
- `package-summary.json`：最终镜像内核、SWOUT1启动脚本、ASR／TTS私有资产、LLM备用模型、YOLO及人设回读；未改分区跨度验证。
- `sdk-restored.json`：316项SDK暂存文件字节还原。
- `mcu-summary.json`：新版固定上游归档及生产源码哈希、新命令／事件／授权边界、真实编码器603组对照和虚拟串口联合回归。
- `windows-image-hash.json`：Windows独立核对，与Linux封装校验和一致。
- `inherited-real-gguf-*`：明确继承v220数值参考。全部数学／人设文件逐字节同SHA，product_runtime仅版本banner变更经过完整文件逆替换同SHA验证；不是重新测得板上速度。

实际软件日志使用原测试名称（含v219、v220），它们是本次源码重跑，不是新硬件驱动或真实AP／PCM测试。软件和镜像回读通过不等于机械动作、声音、云API或延迟通过。v220最新长回复超时反馈仍是已知限制，v221只修正该例中“电机状态”的MCU意图路由。

最新稳定指针仍为v1.1。没有烧录物理SD、擦写UFS、提交PR或推送Git。
