# v154 有界搜索核心与分支回归交付

2026-10-19。没有连接开发板、没有生成/刷入新固件，不改变SDK/启动/MMU/驱动。
已通过的模型、固件、归档、原始输出与开发目录均保留。

## 源码

应用 `voice_kws.c/h`：无堆分配的有界搜索核心，加入aipet的CMake清单但尚未接入硬件后端。
初始词条boost/threshold保留，删除额外+5强推，实际检查声学概率均值/强音素个数。
blank/扩展分支保留、路径过期、根节点持续重启、短词前缀延迟和冷却均显式有界。
策略待标定，不能凭这组小样本宣称准确率。39路NPU语音执行器和ASR NBG缺口不掩盖。

## 可复核测试

- `bash tools/test-aipet-kws.sh`：8组C测试，ASan/UBSan，C++头文件兼容；无NPU调用。
- `AIPET_KWS_LIBRARY=HOST_LIB python3 tools/test-kws-search.py`：7项真实C绑定/策略/缓存/缺失分支测试。
- 缺少新上下文输出时，纯CPU replay返回 `NEEDS_NATIVE_BRANCH`，分类结果为null而非假阴性通过。
- 从已复核回传提取6场景、344个编码器输出帧、391个已执行joiner上下文结果。
  准备工具再次校验选中原始张量SHA与decoder上下文、encoder/decoder→joiner逐字节连接。
- host replay没有执行任何NPU调用；噪声负例可完全回放且无命中，其他5例需要真实新分支。
  尚未声称2个短唤醒词已修复通过；结构性搜索问题已改，效果待硬件结果。
- AArch64共享库经交叉编译；符号最高依赖GLIBC_2.29（libm的exp），目标参考系统为Debian11。
  这不是NuttX上动态加载glibc；openvela使用同一C源码静态编译。

## 最终私有包

`out/a733-linux-kws-search-v154-r2.tar.gz`，SHA-256
`3f7280fa3b3393e3d2adc9d355520a3997ab0287519ddeb8ef4b7c21ee65d4ad`。
r1和构建目录保留；r2包含跳过无需logit的已完成/已过期路径这一调用节省改动。
编译库来自同一份归档内C源，完整资产清单，模型/厂商库仅本地私有包，不加入Git。

使用官方Linux卡，解压进入目录执行：

```sh
sudo bash test-linux-kws-search.sh --run --npu-idle-confirmed
```

只填充搜索新增上下文的真实decoder/joiner输出，冻结encoder和已有logit清楚标记来源，
不是新的端到端实时录音测试。网络内5s/进程60s及终止宽限、总调度60分钟/12000次上限、
75°C保守软件停止阈值；异常停止，不自动重试/reset/重启。
所有可继续的语义漏检/误报均留在同一次报告内，不以降低门槛填成通过。
