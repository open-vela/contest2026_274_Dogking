# v211封存证据：未板测

2026-10-30。summary.json列出182个payload的原始长度/SHA256；
本README和summary本身不计入payload，以避免自引用。
固件源码d2a061da58aed59b5f7564221a90a6c2dcf841b3。
没有采集新的实机录音，没有运行新的A733硬件推理，不认定3–5秒通过。

- voice：最终out/voice-regression-v211-r3，14组回归；
  voice-arm/summary含16个真实SDK/ARM单元及静态栈检查。
- full：out/voice-check-v211-r1，22组摄像头回归及真实对象/ELF/ISA/内存审计。
- build：完整构建日志、退出0、SDK298路径逐字节恢复。
- package：新普通2GB镜像的内核/脚本/布局/e2fsck/保护区回读，
  三条既有标准WAV及49个模型/词表/许可文件完整回读。
- windows-image-hash.json：Windows独立全镜像SHA，与Linux封装结果相同。

新增ASR A/B用例：90组边界，encoder输入全部浮点字节及调用数量一致，
少算4932个无用零尾帧；host aggregate finish us是主机诊断值，非A733延迟。
216条Linux捕获重放和35状态反馈不等于新的硬件推理验证。
实际GGML Q8量化器覆盖10000次复用和零块/OOM/清零，
数学循环不改；量化前清零避免全零块未写bsums留下旧数据。
起播用例在50ms源PCM后暂停生产，已开始driver读取，最终完整PCM相同；
也继续覆盖短尾、断流、晚关闭、取消、设备错误和join失败隔离。
真实CPU声学对照中NPU部分为stub，明确不是声学质量或端到端验收。

r1工具缺少cerrno声明，修正后重跑；r2随后补入起播专项，
最终仅r3完整回归作为封存依据，早期目录保留，不伪称全部一次通过。
固件源码在构建前冻结；后续仅测试/文档/封装与恢复工具修正，
最终门禁逐文件核对固件源与该commit，没有偷偷替换编译产物。

镜像SHA256：
c168b667d0a22f171d2a2763158de4d05c00898d9c38f55b8fd2f04fd50b2f99。
内核SHA256：
374976b6cec37718154a83e84e7b2708af03f9253ddeba32f6b58c48ddc0b289。
24构造器，runtime end=0x45de6000；摄像头RAW固定池保留。
不修改模型精度/硬件时钟/NPU执行ABI；旧v210及所有恢复点不删除。
