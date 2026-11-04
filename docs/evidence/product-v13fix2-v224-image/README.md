# v1.3fix2／v224最终封存

2026-11-02，`V224_FULL_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_TESTED`。[详细修改／测试／恢复](../../PRODUCT_V1_3FIX2_V224.md)，[原v223板测](../voice-v223-board-20261102/README.md)。

镜像 `openvela-a733-cubie-a7z-sd-v1.3fix2-v224-candidate.img`，2,147,483,648字节，SHA256 `30ca81ce9d0aeb846207b0f816603bfcafb389804763a393c4a18186cb0be25b`。内核7,242,664字节，SHA256 `49c7f8dca641bdeefd335e5d73732e22c5eee34a7c3226079c3fa327ffc36443`。固件提交 `e54e867cfb567e1cc6c124572e0861dacd538135`。

## 证据分层

- `summary.json`：最终制品、源码、33项门禁、旧镜像保护、未板测状态。
- `host-arm-summary.json`：19基础＋产品／配置租约／Wi-Fi／MCU／预览配置等旧门禁，加TTS边界、真实CPU声学、8实际ARM修复单元、新脉冲端点对照和ASR状态重放，共33项。继承测试名称v219/v220/v221保留，不冒充重新实现。
- `temporal-endpoint.log`：同一合成输入，v223录到800块limited；v224172块正常结束。不是实录PCM，不能认定真实噪声/弱尾音已验收。
- `asr-native-discard-replay.*`：生产ASR适配器、参考216条Linux张量捕获，三次部分状态丢弃再做6轮真实样例重放；丢弃无额外tensor调用，下一轮仍有原始decoder初始化。所有输入输出逐项比较；518次调用为主机捕获匹配，不是新NPU执行。首轮测试误将新轮初始化也断言为零调用，r1失败保留、修正语义后r2通过，未放宽任何数值容差。
- `full-link-summary.json`、`elf-audit.json`：最终ELF、ABI、ISA、内存布局、CPU调度边界和摄像头软件审计。
- `package-summary.json`：最终镜像内核／模型／词表／启动脚本／分区回读。package.log中间哈希不是最终发布值。
- `voicefix-arm.json`：8个真实目标编译单元。`real-acoustic*`：真实CPU ORT声学，vocoder为测试桩，不能验收声音。`duration-boundaries.log`保留旧10秒／新60秒／取消／错误限制。
- `inherited-real-gguf-*`：继承v220真实数学参考，验证源码完全同哈希，仅产品版本banner归一化，不冒充新实机oracle。
- `sdk-restored.json`：SDK316项字节还原。`windows-hash.json`：最终普通镜像文件独立Get-FileHash，与Linux及旁置sha256相同。

封装前后v219/v220/v221/v223原镜像SHA均未变化；稳定指针仍v1.1/v219。没有SD/UFS物理盘写入、硬件复位／强杀、自动arm、运动重发、PR或远端推送。

## 仍未验收

新镜像常驻短句／真实环境／弱尾音／多轮声音和最后一次卡死是否消失；3～5秒真实首音。能量门控不是训练VAD或AEC；不支持讲话打断。英文仍为中文读音字母拼读，MiMo新集成未加入。按NPU生命周期技能严格区分参考重放、软件检查与硬件运行，不把其中任一项冒充整机通过。
