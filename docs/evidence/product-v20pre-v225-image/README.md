# v2.0pre／v225 镜像封存

2026-11-03。**软件检查通过，尚待新版板测，不替代 v1.1/v219 稳定版。**

## 制品

- 镜像：仓库上一级 `openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img`，2,147,483,648字节。
- 镜像 SHA256：`cd4cd64097720b7da15abef1ee7f1e8291c258888a8683590a8c537998ddc51c`。
- 内核：7,254,952字节，SHA256 `c95c1b47a98feaafd45bcc40eca20645d2d2fc8e1bd3dd964e814145235d86aa`。
- 固件源码：`99f56842a67b68cf66b19233c4fc3a00c5eb7dec`。
- 本地交付标签：`a733-v2.0pre-v225-image-20261103`；恢复包 `out/checkpoints/a733-v20pre-v225-image-20261103.bundle`。
- 构建缓存：SDK `cmake_out/cubie-a7z_nsh_v225_product_v20pre_r2`。
- 封存固件：`out/v225-product-v20pre-release-r1`，包含ELF、内核、map、config和启动脚本。

完整命令见 [版本说明](../../PRODUCT_V2_0PRE_V225.md)。镜像及私有模型不提交Git，不自动上传。

## 证据

`summary.json` 是最后封存检查；`package-summary.json` 包含最终镜像模型／启动文件回读和源文件摘要；`full-link-summary.json`／`elf-audit.json` 是最终实际ELF检查；`host-arm-summary.json` 为36项门禁索引；`sdk-restored.json` 证明316项暂存路径字节还原；`windows-hash.json` 为独立Windows摘要，与Linux一致。

- `v225-command-route.log`：实际确定性分发器，30条正向命令及否定／歧义负例。原日志“识别前面物体”“前面有个什么物体”“前进十五部”“千金二十步”对应路由补全；不采用任意模糊子串执行。
- `mcu-summary.json`／`mcu-v221.log`：固定STM32 `efdb437` 的编码器对照和实际上位机PTY串口回归，数字步数×10、速度档2、STOP后新命令不重复arm、NAK抑制且不重发。**不是机械动作实测。**
- `v225-boot-policy.log`：真实CRC配置与启动分发C代码，默认不监听、显式保存策略、损坏记录关闭、其他服务配置不覆盖、后台stdin不占控制台。
- `v225-boot-worker.log`：实际语音服务，模型准备→欢迎语→MIC顺序、保存音量／静音、欢迎语失败不进入MIC、启动／停止恢复；外部ASR／LLM／PCM替身明确标注。
- `real-acoustic-summary.json`／`real-acoustic.log`：真实CPU声学模型执行，非真实扬声器验收；`voicefix-arm.json` 为实际ARM编译。
- `asr-native-discard-replay.json`／`.log`：真实Linux捕获张量重放的状态回归，不冒充本轮RTOS硬件推理。
- `inherited-real-gguf-*`：继承v220真实GGUF对照。门禁重新核对未改动LLM数学源摘要；新增应用分发／设置／启动行为单独回归，不称整个runtime未改。

最终批次：构建 `out/product-v20pre-v225-build-r2`，36门禁 `out/v225-product-gates-r2`，完整审计 `out/v225-full-link-r3`，封装 `out/product-v20pre-v225-package-r1`。之前的失败／首轮日志保留：完整审计r1遗漏新增mcu_link.hxx清单项，随后精确补齐；新增原话回归 `out/v225-log-phrase-red-r1.log` 先复现失败，再修正源码、全新缓存重编译。没有删除失败证据或覆盖旧制品。

## 边界与恢复

五份原v219/v220/v221/v223/v224镜像封装前后摘要均不变，详见 `summary.json`。没有物理盘写入、UFS擦除／迁移、NPU驱动或全局调度修改，没有PR或推送；用户另存的文档副本不纳入本次提交。

开机监听默认关闭；首次人工配置网络、SSH、真实云服务凭证及路由后，显式 `aipet autostart on`，其他服务也按版本文档各自启用。保存静音优先于欢迎语。本地所选路由自动准备本地模型，云模型在服务端，不谎称全部离线模型已常驻。重刷SD会替换卡上配置，先自行备份；不要把凭证提交Git。

本版需要实际确认：口语识图、UART真实位移／速度、连续多轮与停止恢复、音量可听变化、Wi-Fi／SSH／云账号、断电后自动准备及欢迎语。未进行本轮整机机械／声学／首音延迟测量，不宣称3–5秒已达标。常驻仍半双工，不提供AEC／打断。故障、失联或ACK未知不会自动重发运动；明确的人类命令才有桌宠会话运动授权。
