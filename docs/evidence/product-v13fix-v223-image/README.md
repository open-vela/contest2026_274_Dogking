# v1.3fix／v223 镜像封存证据

2026-11-02。结果 `V223_FULL_SOFTWARE_IMAGE_VERIFIED_NOT_BOARD_TESTED`，不等于硬件验收；[测试与恢复](../../PRODUCT_V1_3FIX_V223.md)。

- 固件源码 `fb824fbfa9a3db6d4d64fa87be612849b275e49f`，新完整ARM构建；SDK316项字节还原。
- 镜像 `openvela-a733-cubie-a7z-sd-v1.3fix-v223-candidate.img`，2,147,483,648字节，SHA256 `43380317ae7958d0d2eb12e61984603d411b05d478fbfa1db8d7ad20b522bb31`。
- 内核7,238,568字节，SHA256 `828f52d9ec92f3d2e3fab1fd1ad57ad5b58dfa8a4903816a1113add454574c19`，与v221不同；二进制验证v223产品／服务banner和端点修复标识。
- Windows独立Get-FileHash与最终Linux封装摘要、旁置sha256一致。`package.log`中的中间基底哈希不是最终发布哈希；最终值以本目录summary及最终文件为准。
- 31项门禁：基础19项、产品／lease／Wi-Fi／ARM／v220预览策略／固定上游MCU等继承门禁，加新端点／provider、真实CPU声学、6单元ARM修复编译。旧版测试名如v219/v220/v221保留，不冒充新版本实现。
- 初次基础回归因并行构建尚无build.ninja失败，日志在 `out/v223-voice-base-r1`；重新完整运行19项于r2后通过，再运行产品门禁。失败证据不删除。

## 文件

`summary.json`是最终发布摘要；`host-arm-summary.json`为31门禁；`full-link-summary.json`／`elf-audit.json`为完整链接、ABI、ISA、摄像头软件检查；`package-summary.json`为最终镜像、模型、启动脚本和分区回读；`sdk-restored.json`为SDK恢复。

`old-new-endpoint.json`为同输入旧新生产门控对照；`duration-boundaries.log`为旧10秒／新60秒／取消／错误边界；`real-acoustic-summary.json`和`.log`为真实CPU ORT声学、既有黄金输出及长文本，NPU使用测试桩、未保存可验收音频；`voicefix-arm.json`为6个实际目标单元编译。

`inherited-real-gguf-*`明确继承v220，逐项验证数学源码同哈希，仅版本banner允许归一化；没有声称在新镜像重新做实机LLM oracle。MCU固定上游 `efdb437948eda62cbc782692eb8fd291e5d914ae`，不自动解锁、不重发运动。

封装前后对v219、v220和v221原镜像SHA逐个复验未变。没有烧写SD/UFS、修改稳定指针、提交PR或推送。

## 仍未验收

新端点弱尾音和真实噪声、整机连续多轮与长句音频、3～5秒声学首音；自然英文不支持，仅中文读音字母拼读；MiMo Token Plan新集成未加入，云账号调用未执行。v1.1/v219仍是最新稳定。
