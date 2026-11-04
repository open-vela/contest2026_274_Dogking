# A733 官方 Linux 语音 NPU 检查脚本

仅用于另一张SD卡启动的官方Linux；保留openvela卡。脚本不安装驱动/软件、
不联网、不改设备节点权限、不复位NPU、不重启，也不访问麦克风或调用云服务。
会给已校验的测试包内二进制增加用户执行位，并写入独立测试结果。

## 本地测试包布局

```text
test-linux-voice.sh
voice-golden-linux-aarch64
lib/libNBGlinker.so
lib/libVIPhal.so
models/kws/encoder_float_a733.nb
models/kws/decoder_float_a733.nb
models/kws/joiner_float_a733.nb
models/vocoder/vocoder_int16_a733.nb
```

脚本可在此目录执行，也可用 `--bundle-dir /absolute/path` 指定。需要Bash、
GNU timeout、sha256sum、ldd、cmp、grep、tee、tar、mktemp、flock等基础工具。
fuser可选；若发现其他进程打开vipcore则停止，不会自动杀进程。包内加锁仅防止
同一包并发测试，不能代替全局NPU互斥，必须手动停止其他NPU应用。
必须只运行可信、未篡改的测试包；不要将未知脚本或共享库以root身份运行。

## 使用

```sh
# 先看环境，无NPU调用，也可不使用sudo（dmesg可能无权限）。
bash test-linux-voice.sh --env-only

# 确认其他NPU应用均已停止后，才执行硬件检查。
sudo bash test-linux-voice.sh --run --npu-idle-confirmed
```

硬件模式先检查Linux/aarch64、字符设备/dev/vipcore、访问权限、7个资产的
固定SHA-256以及动态库依赖；随后串行查询KWS三模型和vocoder接口，
要求I/O数量39/39、1/1、2/1、1/1。最后只用较小KWS joiner执行两次零输入，
要求成功清理、run-status=0、输出已改变且完整落盘、两次输出逐字节一致。
每次子进程上限60秒，TERM后最多再等5秒；模型内另有5秒超时设置。
第一次错误立即停止，不自动重试、不继续下一模型。

每次创建唯一的 `results/run-UTC-随机串/`，保存环境、温度、依赖、模型接口、
进程状态、原始输出、输出哈希和dmesg。正常结束、错误和Ctrl+C均尽力归档为
`results/voice-report-随机串.tar.gz`；通过sudo执行时将该归档交回原登录用户。
普通结果目录保持原权限。归档失败时保留原始目录，不清理证据。
不采集shell环境变量、API key、SSH私钥或AI配置文件。

请发回结束时 `REPORT:` 对应的压缩包，以及终端最后的错误/结果。

## 结果含义与限制

- `ENV_ONLY_NO_NPU_CALLS`：只收集环境，不表示模型/硬件通过。
- `PASS_INTERFACE_AND_ZERO_REPEAT_ONLY`：接口与零输入重复性通过，
  **不是唤醒、ASR、合成、动态输入、语音准确率或openvela验收**。
- `FAILED`：停止。若超时、卡死、驱动异常，保存日志，冷重启后再由开发者判断；
  杀掉用户进程不等于NPU已复位。

ASR ONNX不能直接交给此脚本，ASR NBG转换、真实语音张量/状态回灌、
Matcha CPU和完整Agent链路仍是后续工作。现有硬件通过镜像不变。

主机 `python3 tools/test-linux-voice-script.py` 使用mock runner覆盖停止、
归档、超时、坏计数、未改变输出、输出差异、tee错误和资产校验；
它从不访问NPU，不能充当A733板测。真实Linux首次执行状态待用户回传。
