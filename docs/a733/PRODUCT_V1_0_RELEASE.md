# 桌宠 v1.0 / v217 发布与恢复

## 当前推荐：v217-audit2（2026-10-31）

本轮复审修正了取消／清理优先级、前台协作 SIGINT、动作前取消和路由失配策略，重新构建和回读，推荐使用新审计镜像。

```text
openvela-a733-cubie-a7z-sd-v1.0-v217-product-audited.img
2147483648 字节
SHA256 98daee1f3b4653d3d65a14a3e92c2713560aac2fd6183946d2b06b7a3c79bc21
源码 8c5a7b3b267128b7859c37d1e1000ec9054732f4
标签 a733-v1.0-v217-audited-20261031
恢复 out/checkpoints/a733-v1-product-audited-recovery-20261031
```

检查结论、实际报告目录和待验收项目见 [本轮审计报告](PRODUCT_V1_0_AUDIT.md)。
部署见 [项目说明书](../PROJECT_MANUAL_V1_0.md)，技术总览见 [PROJECT_FULL_SUMMARY](../PROJECT_FULL_SUMMARY.md)。
软件和镜像检查已经完成，仍不代表本审计候选整机／云 API／3–5 秒首音通过。
本版策略文件缺失／损坏时默认 local，开启自动云路由需明确 `aipet mode auto`。

核验／恢复使用新的恢复目录：

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v1-product-audited-recovery-20261031 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v1-product-audited-recovery-20261031 out/v1-audited-recovered-new --with-git
```

**以下保留首次 v217 候选的完整历史发布记录，其中旧镜像、旧源码和旧恢复目录不是本次审计镜像。**

---

2026-10-31。状态：**软件集成、最终链接和镜像回读通过；等待整机实测的候选版**。
接线、模型路径、账号配置和完整顺序测试见 [产品指南](PRODUCT_V1_0.md)。

## 镜像

仓库父目录：`openvela-a733-cubie-a7z-sd-v1.0-v217-product-candidate.img`。
大小 **2147483648 字节（2 GiB）**，旁边有同名 `.sha256` 文件。

```text
SHA256: ffe8078be4a41be6b336b062ea4ff49e28f9a4ea452f3335fcb520c232ddc4b4
kernel: f73e9104c2435ff5b5a9879e02485c40857882d28d3e8330237013448ba1d350
```

最终固件源码：`77c5c6fe91cdcc1f5a021063b2b0c7788e736e94`。
发布文档、打包脚本和恢复工具的后续提交不代表重新编译过固件，源码绑定以此 commit 和封存摘要为准。
恢复标签：`a733-v1.0-v217-product-candidate-20261031`。

镜像内含通用 ASR、CPU 声学模型、NPU vocoder、COCO80 YOLO 及 MiniCPM5 SD 备用权重。
完整 UFS 模型仍优先使用；本轮没有写实物 SD/UFS，没有擦除 UFS 旧引导，也没有给 STM32 烧固件。
刷写前备份原 SD 数据，确认目标是 SD 卡。模型镜像用于私有板测，对外分发前须核对各权重许可证。

## 首次本地启动

先确认正确刷入本候选版，I2S 和 SPI 接线按产品指南，执行：

```sh
aipet local model auto
aipet local on
aipet local context 3
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet mode local
aipet run
aipet audio service status
```

观察串口分段加载，等 **READY MIC ON** 后说话；首次装载与热轮次延迟分开记录。
可以说“你好”“我有点难过”“看看这是什么”。识物是新采集、同帧画面/框和实际类别播报，不调用 LLM 猜图。
使用 `aipet stop` 协作停止，等 `active=0`。若 ownership uncertain / quarantine，保留日志并重启，不强杀 NPU 所有者。

联网、英文、多轮、连续视频及 STM32 验收按产品指南后续章节执行。
云语音需要独立火山 ASR/TTS 凭据；DeepSeek/MiMo 的 LLM Key 不通用。
STM32 先检查物理接线和状态，再显式 `aipet mcu arm session`；停止、失联或故障后不自动恢复使能。
语音 NPU 会话与连续摄像头互斥，识物轮次进行受控交接，不宣称两套模型无约束并行。

## 构建与封存

本次实际通过的缓存：`../../quickly-openvela/cmake_out/cubie-a7z_nsh_v217_product`。
首次须用新缓存并确认 `.config` 的 `CONFIG_A733_PRODUCT_V1=y`；不能把旧缓存的成功编译当作启用了新功能。
不要覆盖已有报告目录；新开发选择新的目录名。WSL 中的入口如下：

```sh
bash tools/build-a733-product-v1.sh out/你的新构建目录 out/ort-static-v185-r2
```

依赖与 ORT 构建沿用历史文档。重用已合格的同一缓存才用 `--incremental`。
本次最终报告和封存文件：

```text
out/product-v1-voice-regression-r1       19 项既有语音回归
out/product-v1-gates-r3                 追加产品、服务、MCU、9 个 ARM 单元，总计 23 项门禁
out/product-v1-build-r4                 最终构建，SDK 309 个路径逐字节恢复
out/product-v1-full-r1                  完整相机/ABI/ELF/ISA 与产品快照检查
out/product-v1-package-r2               镜像封装、模型回读和最终摘要
out/v217-product-release-20261031-r2    内核、map、config、启动脚本与 IMX214 模式资产
docs/evidence/product-v1               可提交的软件证据摘要，不含模型和凭据
```

首次打包被严格摘要检查拦下：脚本中的 YOLO SHA256 多抄了一个字符。
原始模型与首次镜像回读的 8017024 字节模型实际摘要一致，修正记录后重新封装，未更换模型或放宽校验。
失败镜像留在 `out/product-v1-package-r1/FAILED-IMAGE.img.not-for-flashing`，**不可烧写**。
最终模型回读、EXT4/GPT/启动保护检查及独立 Windows SHA256 均通过，正式文件以上述最终摘要为准。

## Git 与本地恢复

```text
集成前标签: a733-v1-before-product-integration-20261030
发布标签:   a733-v1.0-v217-product-candidate-20261031
本地恢复:   out/checkpoints/a733-v1-product-recovery-20261031
```

恢复包包含项目源码、Git 历史 bundle、工作树/索引补丁、校验清单及 `COMPLETE`。
用户原有未提交总结也保留在快照，不擅自纳入提交或删除。
大模型、镜像、静态库等在清单中是**外部文件摘要引用，不是复制进 Git 或源码压缩包**，请一并保留引用文件。
恢复包及完整历史可能含项目运行记录，按私有资料保存。

WSL 中核验及恢复到**全新目录**：

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v1-product-recovery-20261031 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v1-product-recovery-20261031 /一个尚不存在的新目录 --with-git
```

不对原仓库 `reset --hard`，不覆盖现有目录，也不从恢复命令写设备。
v204 通过镜像和 v216 候选镜像再次核验未改变：

```text
v204 f21a9d0bd8aab20264db3e44273ebf372c629f33ee7bba6c49659f0e2e82326d
v216 14aaa2dad1c0b0edd0814f2680ea712a8a8b02695047c5dbe3dafc899ccc7e40
```

## 尚待实测

真实麦克风多轮/中英文、扬声器完整播放、真实拍摄与六路 YOLO、STM32 物理动作和失联保护、云 API 与切网恢复。
当前没有本候选版的新硬件/云端验收，也没有整机首音 3–5 秒的测量证明。
无全双工、AEC、播报中语音打断、开机自动录音或自动运动使能；ACK 不等于动作完成。
