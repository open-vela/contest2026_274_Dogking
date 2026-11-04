# v157 openvela 四模型固定/动态输入通过（2026-10-20 归档）

来源为用户实机日志，不是本轮助手 SSH 执行。
[原始日志](openvela-v157-user-fixed-dynamic.txt) SHA256：
`40e043585e20151d2219d19aa1ef7d6250d42ec80ca6d4ccc062ec4190df731b`。

离线核验命令：

```sh
python3 tools/npu-voice/audit-openvela-npuref.py \
  out/a733-openvela-voice-assets-v156-r1/voice/expected-outputs.json \
  docs/evidence/local-voice-v153/openvela-v157-user-fixed-dynamic.txt
```

结果 `NPUREF_SUPPLIED_LOG_CRCS_MATCH`：4 固定 + 14 动态调用，18 次、170 路原生
输出的长度和 raw CRC 全匹配；动态输入输出发生相应变化，重复输入确定性通过。
expected-outputs.json SHA256：
`2b57224f710628164007c64931d7cd24797db9bc4b88f224fa3b4fa69f613032`。

用户随后在聊天另行提供最终资源快照（不是原始附件的一部分）：

```text
A733 VIP2 ABI v4 CID=1000003b cores=1 page=4096 pool=2093056 free=1654784 buffers=16 timeout=5000 ms
      total       used       free    maxused    maxfree  nused  nfree name
 4239151104      63456 4239087648     708976 4143972320     98      4 Umem
```

NPU free=1654784、堆 used=63456 均回到该轮测试前值；本轮未观察到残留分配。
不据此声明长稳/所有取消路径无泄漏。

通过镜像保留不覆盖：`../openvela-a733-cubie-a7z-sd-v157-npu-pool-candidate.img`，
SHA256 `be08b22602208546a2248a7c362b73f59f98377bdaaa5e95fac1c68f53f45ec7`。
原封存候选目录 `out/v157-candidate-20261019/`、源码包、失败记录和恢复镜像全部保留。

边界：动态输入是已准备的文件，尚不是 openvela 连续实时 encoder 回灌，也不是
KWS 应用、中文 ASR、完整本地 TTS 或语音助手闭环验收。时延含每次文件加载等开销。
