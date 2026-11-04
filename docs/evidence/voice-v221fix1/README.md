# v221fix1 软件证据；不是板测或新镜像

2026-11-02。

- `old-new-gate.json`：实际旧版与修复header的同输入对照，旧8秒丢弃，新700ms安静后正常结束。
- `resident.log`：实际门控、采集/FIR和服务生命周期，ASan／UBSan，保留超长丢弃、噪声、脉冲、预滚、暂停、取消、owner保留等；设备／ASR模型为显式mock。
- `provider.log`：旧缓冲10秒上限保留，显式流式11秒通过，60秒上限仍拒绝。
- `audio.log`／`stream.log`：真实应用契约、重采样、单次11秒完整输出、短尾、断流、终包、失败／取消／恢复及词典前端回归；PCM／云／硬件均不是实机。
- `acoustic-summary.json`／`acoustic.log`：真实ORT CPU声学模型，保留五组旧中文golden；新增英文拼读、两条实际板测诊断文本完成。NPU用零输出桩验证接口，未保存或交付这种零音频，不能当作声音质量证明。
- `arm-summary.json`：最终6个实际NuttX ARM编译单元，源码SHA和静态栈检查。门控header另由old-new报告绑定。

新源码未生成整卡镜像，未执行目标板ASR／自然英文／声音验收。Baker模型仍是中文，英文字母拼读不是自然英文。所有旧镜像保留，不推送、不提交PR。
