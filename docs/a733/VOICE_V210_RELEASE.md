# v210 私有测试镜像与恢复

2026-10-30；固件源码 e400da868c59ce447b6f17e410535057b33e0f19。
版本为 candidate，不是板测通过版。v209 源码、镜像、封存和实际恢复目录均保留；
历史开发文档不删除；本次未推送远端。

## 镜像

位于仓库父目录：openvela-a733-cubie-a7z-sd-v210-voice-latency-candidate.img

- 大小 2,147,483,648 字节。
- SHA256：53816b0e9f6a111c6346ef477a8eaebaa59273770360fe916fba8654bb1024e1。
- 内核 7,160,424 字节，SHA256：
  677d11dcef344c171b0f6b7ff8e1aa28ea98f64a3d306772d719bf658376de95；
  CRC32 6b908545。24 构造器、runtime end 0x45de6000。
- 49 个私有模型/词表/许可文件原样回读；新增三条标准测试 WAV。
  根分区内核/受控 SWOUT1 脚本更新，GPT/固件与卷外保护区比较一致。
  没有物理 SD/UFS 写入、格式化、迁移或清理旧引导器。
- 这是整卡镜像，烧录前必须备份原 SD 数据。MiniCPM5 权重不随镜像分发；
  已有 UFS 模型保留，未挂载时按测试文档显式启动。

镜像内受控引导脚本沿用 camera-v210 命名，版本继承 v209 完整配置；
不是另一个摄像头固件。SWOUT1 读/受控开/读回护栏不变。
启动中仍有历史 boot188/M0 诊断前缀，识别镜像以本页哈希/CRC 为准。
没有开机自动录音、摄像头、NPU 推理或 MCU 动作。

## 通过的离线检查

- 12组语音回归：真实前端、35状态跨块反馈及216条Linux捕获重放、
  CPU声学实执行＋NPU stub、流式播放/首短语完整性、会话所有权/取消/
  隔离、音频应用、LLM绑定与数值循环不变、FIR、Tensor、ARM CRC。
- 16个语音相关真实SDK/ARM单元编译，静态栈限制通过。
- 22组摄像头回归、真实目标对象、最终ELF/布局/可选CRC指令/
  实际身份读取和阻塞迁移路径通过；SDK297路径逐字节恢复。
- 图像中的三条WAV、49个私有文件逐个回读；e2fsck、GPT、内核回读、
  保护区比较、Linux和Windows独立全镜像SHA一致。
- 不代表新ASR/NPU硬件实执行、识别率、完整语音闭环、声学起音、
  A76实机效果或3–5秒通过；这些由下一轮板测确认。

最终有效输出为 out/voice-regression-v210-r4 和 out/voice-check-v210-r3。
更早回归暴露了两个旧脚本不允许嵌套输出目录，以及新增路径测试把目录
误当作“非目录”的断言；已修正工具后从新目录全部重跑。
两个早期ELF审计误要求被GCC内联的函数符号，最终改为检查实际pool_worker
回调及身份/阻塞callee，并非放宽硬件检查。早期输出未删除，不作为验收。

## 构建和验证

WSL Ubuntu，使用已锁定的本地SDK、ORT静态库及私有模型。禁止临时下载替代模型。
首次新缓存不加 --incremental；下面为明确复用完整缓存的本次实际命令：

```sh
A733_V210_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf" bash tools/build-a733-voice-v210.sh out/voice-build-v210-r1 out/ort-static-v185-r2 --incremental
python3 tools/test-voice-v210.py out/voice-regression-v210-r4 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf
python3 tools/check-a733-voice-v210.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/voice-build-v210-r1 --output out/voice-check-v210-r3 --firmware-commit e400da868c59ce447b6f17e410535057b33e0f19
python3 tools/package-a733-voice-v210.py out/v210-release-20261030 ../openvela-a733-cubie-a7z-sd-v210-voice-latency-candidate.img out/voice-package-v210-r1 --firmware-commit e400da868c59ce447b6f17e410535057b33e0f19 --voice-gates out/voice-regression-v210-r4/summary.json --full-gates out/voice-check-v210-r3/summary.json
```

这些是已存在的不可覆盖输出；重跑必须使用新的输出目录/镜像名。
封存目录 out/v210-release-20261030 包含ELF、bin、map、config、模式/许可3件、
脚本/manifest3件，共10件。新构建先复制这4个编译产物并沿用模式3件，
打包工具从精确ELF生成新脚本，再复制新脚本3件封存；不能借用旧脚本CRC。

## 恢复

源码标签：a733-v210-voice-source-20261030；
封存及恢复标签：a733-v210-voice-sealed-20261030、
a733-v210-voice-recovery-r6-20261030。
checkpoint：out/checkpoints/a733-v210-voice-recovery-r6-20261030。
早期 recovery 标签保留；最终工具修正 Windows 无法 stat 的 WSL soname
别名及长命令行限制，直接调用既有归档实现，引用实际版本化 ELF/原 wheel。
这些恢复工具修复不改变固件、镜像或测试结果。
新目录回归还发现漏列 ASR Linux kit 的词表/原始样例目录；
r4 将该目录加入锁定引用与测试链接，不仅保留 NPU 捕获张量。
r5 补齐摄像头回归对早期模式fixture的两个路径；模式内容已锁定，
不是改变摄像头驱动。最终新目录回归使用11个明确的私有参考目录。
r6 在回归后逐文件验证归档原始字节。Windows历史CRLF记录与Git的LF
blob可能被WSL标记为25项修改；只在完整归档哈希相符且每项严格仅EOL
变化时接受并明确列出，不重写历史文件，也不谎称WSL Git完全干净。

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v210-voice-recovery-r6-20261030 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v210-voice-recovery-r6-20261030 ../contest2026_274_Dogking-restore-v210-r4-20261030 --with-git
```

只恢复到全新目录，不重置当前仓库。源码和Git历史在checkpoint中；
大镜像、模型、库是本地外部文件的锁定哈希引用，不是全部复制进源码归档。
因此要保留相应外部文件，不能只带走源码归档就宣称完整离线复现。
板测流程见 VOICE_V210_TEST.md。物理延迟仍需同步实拍/采音测量。
