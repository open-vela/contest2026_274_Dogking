# v154 KWS 搜索修正：一次补齐缺失分支

保留 v153 通过包、原始回传和两段用户试听正确的声码器 WAV，不修改模型、幅度、
状态策略和采样率。ASR 缺少 NBG、openvela NPU 多张量执行器尚缺的事实不变。

## 本次定位

在 `voice-report-fXyxidmo` 的 normalized/feature-offset 原始结果：

- 完整“你好小瑞”命中；两个短词漏检，三条负样本不误触发。
- “小瑞”第10输出帧选中 x（概率约0.1616），第12帧选中 iǎo（约0.7228）。
  后续 r 最高约0.00560，旧规则在浅层不能保留这条低分分支。
- “小瑞小瑞”根节点 x 最高约0.06645，达不到旧硬门槛0.08，后续上下文根本没有执行。
- 旧参考实现是每条假设只保留一条贪心路径，名字有 beam 不等于保留 blank/扩展分支。
  它还忽略词条 threshold、缺少残留路径超时、可能由短词抢占长词。
- 上述是观察到的搜索问题，不等于证明模型/前端/识别精度已完全正确；NPU低分原因仍需对照。

## 新核心与策略边界

`voice_kws.c/h` 为应用内独立 C 模块：固定16条路径、8个词、每词最多16音素，
最多17个不同两token上下文，无动态分配、网络或硬件访问；单实例单所有者。
每帧同时保留 blank 和扩展，根节点持续可重新开始，路径有总时长和空白间隔上限。
词条 threshold 真正作用于未修改的声学概率算术均值；至少2个音素达到0.12；
使用每音素相对blank分数加词条原始bias，不使用旧的额外+5强推。
首音素及每次扩展有0.0001数值下限，不代表可以无条件触发；最终还须均值、
强音素计数、累计相对分数通过。该策略属**待标定实验算法**，不能据这6条样本宣称准确率。

输出帧约40ms；路径最长75帧，空白最多15帧，短词前缀延迟12帧，命中冷却12帧。
策略固定且完整写入结果，不按每条测试结果调整；短词响应会增加最多约480ms等待，
需要用连续语音和更多负样本评估。数值来自诊断设计，不是产品延迟保证。
错误行数、NaN/Inf和超范围logit在改动状态前拒绝；reset不涉及声学模型/硬件复位。

## 一次运行

使用官方 Linux 卡、稳定供电散热，关闭其他 NPU 程序，分区至少1GiB空闲。
解压交付包、进入目录，运行：

```sh
sudo bash test-linux-kws-search.sh --run --npu-idle-confirmed
```

不需要麦克风、联网、安装依赖或配API key。回传唯一 `REPORT:` 归档。
报告区分已验证的 frozen 输出与新鲜 native 分支；没有输出时绝不伪造logit或自动云回退。
每个原生进程60s、TERM后5s KILL，原生网络内5s，总预算60分钟/12000进程。
温度达到75°C保守停止，非芯片额定温度，不改温控；不自动reset/重启/重复异常任务。

为节省时间，重用本次**真实 NPU 编码器**的6组归一化输入输出及已执行的上下文输出，
均从回传中按SHA-256、decoder上下文、encoder/decoder→joiner字节连接核对后提取。
新搜索需要的 decoder/joiner 分支才执行硬件、保留完整输入输出和CRC。
这是固定编码器输出上的搜索回归，不是重新执行整段前端/encoder或实时声学验收。

`search-summary.json` 列出全部命中/漏检/负样本误报、使用策略及实际新调用次数。
`SEARCH_REGRESSION_COLLECTED` 仅表示收集完，不表示每项通过，
`end_to_end_accepted=false`、`openvela_hardware_executed=false` 保持明确。
CPU离线 replay 遇缺失上下文输出会标记 `NEEDS_NATIVE_BRANCH`，不冒充推理完成。

## 主机复现

```sh
bash tools/test-aipet-kws.sh
bash tools/npu-voice/build-kws-search-kit.sh ORIGINAL_ALL_KIT RETURNED_REPORT NEW_ABSOLUTE_KIT
```

厂商模型/库仅在本地私有包，不加入Git。C源同时加入openvela应用构建清单，
但当前仅CPU核心可编译/测试，尚未接到openvela真实NPU后端或Agent命中事件。
不修改启动、MMU、驱动、原镜像和SDK，不交付冒称已通过的全语音固件。
