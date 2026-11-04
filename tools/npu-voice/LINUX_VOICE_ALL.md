# Linux 可用语音资产一次性合并测试

一次运行收集当前已有 KWS/声码器资产的剩余对照，不分成多轮手工命令。
**不承诺缺失的 ASR NBG、转换器或 openvela 后端会因运行此脚本自动具备。**
原通过包、固件和回传文件不覆盖；厂商资产只放本地私有包，不加入 Git 或再次公开分发。

## 上一轮实际发现

第三阶段221次进程调用完成，218次实际推理记录及所有输入/输出/CRC、状态连接
经只读审查通过。语音和静音各48个贪心输出均为blank，不能算识别通过。
语音最高非blank概率约0.2518，静音约0.001012，说明不能仅看空字符串判为无信号。
模型标量输出为输入+8，前一轮默认输入为特征偏移0/16/32；两种策略要实测对照。
原生调用时间：encoder中位26713.5us，decoder631.5us，joiner340.5us；
它们不含模型加载和文件往返，不能用来宣称完整ASR速度。

## 开始

另用官方 Linux 卡启动，关闭其他 NPU 程序，确保供电、散热稳定，所在分区至少2GiB空闲。
复制压缩包，解压进入对应目录：

```sh
sudo bash test-linux-voice-all.sh --run --npu-idle-confirmed
```

无需说话、联网、API key、安装依赖或连接麦克风。只需Python3标准库和前几轮基础命令。
本轮比之前长；会打印原生调用及矩阵块号，不要因为音素为空中断或另开重复测试。
结束时只回传一个 `REPORT:` 对应的 `results/voice-report-*.tar.gz`。
错误/中断也保留整包。只看环境可用 `--env-only`，不会调用NPU。

## 一次覆盖的项目

| 项目 | 自动执行与记录 |
|---|---|
| 原生合同 | 四模型接口重新查询；每路类型、维度、字节、CRC/哈希、有限值、清理检查 |
| 非法输入 | 仅一字节输入必须在运行前拒绝；后续正向任务证明可继续使用，不故意挂起硬件 |
| 暖态/重载 | encoder/decoder/joiner/vocoder各准备一次连续推理10次，逐次重填输出哨兵；再启动两个独立进程重载比较所有输出 |
| KWS完整矩阵 | 3条唤醒正样本+开灯命令/噪声/静音3条负样本 × normalized/PCM-units两种前端幅度 × feature-offset/feedback两种标量策略，共24个场景 |
| 解码对照 | 同时记录无偏置贪心音素和参考工程关键词路径规则，输出命中、漏检和误报，不因语义失败跳过其他安全场景 |
| 状态回灌 | 每块完整38路原始状态；每个场景首块额外重复并逐字节比较全部39路输出 |
| 声码器 | 在开发主机CPU运行已有Matcha模型生成“你好”“测试成功”的真实mel；板端NPU运行声码器，每组输入重复比较；另测mel静音底值 |
| WAV | 输出22.05kHz单声道WAV及原始FP32、峰值/RMS、裁剪长度；只做必要安全衰减，不自动放大或播放 |
| 资源 | 各阶段温度/CPU频率、环境、dmesg、全张量轨迹、阶段摘要、解码缓存来源 |

解码器按两token上下文缓存其确定性输出（暖态/重载另测）；每帧同上下文的joiner结果
仅复用该帧，并在帧日志中标明实际原生调用来源，不冒充每个算法都重新执行过硬件。
特征在开发主机由同一Kaldi代码预先计算；PCM-units是显式对照分支，不改v153应用默认值。
不把前端幅度/标量策略的扫描选择当作独立数据集准确率，全部组合都保留。

## 结果怎么判断

`COLLECTED_AVAILABLE_ASSET_BATCH` 只表示这一包完成采集。看 `batch-summary.json`：

- `configs`：每组6个正负样本分类结果、精确短语结果，不隐藏差的组合。
- `matrix`/各场景JSON：每个命中和逐帧音素、上下文、状态、调用来源。
- `warm-summary.json`：每次原生调用时间、首次与后9次统计、全部输出重载一致性。
- `vocoder-summary.json` 和 `vocoder-tts-*.wav`：请回传后再听音，尚非合成质量通过。
- `blocked_or_not_claimed`：缺失/未覆盖项，不能把 collected 视为 end-to-end accepted。

参考关键词规则保留原工程的后续音素+5启发式，以及其“读入threshold但命中时未使用”的
行为，日志明确标记 legacy。这是为了对照，不是产品算法审批，不靠修改boost强行报通过。
正样本命中任一配置中的关键词算粗分类正确；指定短语另列精确结果，短词抢先触发不会隐瞒。

## 安全边界和未完成项

- 一般NPU进程60秒，TERM后5秒KILL；网络内5秒超时；总调度预算90分钟/20000次调用，
  另有有界清理宽限。运行中硬件/CRC/合同/重复性异常立即停止；不会为“测完”继续用异常设备。
- 不主动注入NPU死锁、MMU错误或硬件复位；杀用户进程不等于硬件恢复。
  驱动不可中断等待不能由用户态保证终止。超时/卡死后保存日志、冷重启再判断。
- 每块检查已有温度读数；任一达到75°C则保存日志并停止，等待人工冷却。
  75°C是本测试的保守软件上限，不是芯片额定极限；不改系统温控或频率。
- 暖态统计只计原生运行调用，文件dump在调用外但两次运行间有磁盘操作；非最大吞吐压测。
- 声学模型在开发主机CPU运行，非A7Z CPU性能或NPU声学模型通过；板端运行的是声码器。
  Matcha训练数据Baker限非商业使用，原参考NOTICE保留。CPU Runtime按
  [ONNX Runtime官方安装说明](https://onnxruntime.ai/docs/install/)隔离安装在开发工作区，未加入目标板。
- ASR只有上游ONNX，尚无已验证/转换的NBG；此包不假装把KWS263音素替代ASR6254输出。
- 现场I2S采集、实时队列、UART/I2S播放、官方Agent与openvela NPU多张量ABI仍需目标系统集成验收。
  Linux不能证明另一张卡上的openvela功能；本轮不生成冒称全通过的openvela镜像。

## 开发主机复现

```sh
python3 tools/npu-voice/audit-kws-return.py RETURNED_STAGE3 ORIGINAL_STAGE3_KIT
python3 tools/npu-voice/prepare-voice-batch.py tts REFERENCE MODEL NEW_HOST_TTS_DIR
bash tools/npu-voice/build-all-voice-tests.sh /absolute/path/to/NEW_KIT NEW_HOST_TTS_DIR
python3 tools/test-voice-batch.py
python3 tools/test-linux-kws.py
python3 tools/test-linux-joiner.py
python3 tools/test-linux-voice-script.py
bash tools/test-npu-voice-golden.sh
```

CPU TTS准备使用已有本地参考文本前端、模型和lexicon，包内记录源文件/模型/词表/频谱
哈希与ORT版本；模型可能有随机算子，固定的是本次保存的实际mel，不声称CPU重跑逐字节一致。
所有目录/归档均新建，不覆盖旧证据。实际本轮板测待回传。
