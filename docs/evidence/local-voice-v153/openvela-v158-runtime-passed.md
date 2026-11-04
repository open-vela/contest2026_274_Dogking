# v158 连续 KWS 实机通过（2026-10-20 归档）

来源为用户提供的 openvela 实机串口日志，不是助手 SSH 执行。
[原始附件副本](openvela-v158-user-kws.txt) SHA256：
`49ee13033890baccc907f7216a379f3360469c28229a449f7b865d19b262b880`。
核验器返回 `NPUKWS_SUPPLIED_LOGS_MATCH`：6 场景、1771 次模型调用，calls/hits/chain
及命中名称全部匹配，三正例正确命中、三个负例未触发。encoder 为实际连续状态回灌。
expected JSON SHA256：`e3aada6156579e06b92533286c710327ada81acb7d04d41e17a3d5bd24f80ef3`。

```sh
python3 -X utf8 tools/npu-voice/audit-openvela-npukws.py \
  out/a733-openvela-kws-v158-r2/application-expected.json \
  docs/evidence/local-voice-v153/openvela-v158-user-kws.txt
```

| 场景 | 冷加载 s | 总耗时 s | 扣除加载 s | calls | 命中 |
| --- | ---: | ---: | ---: | ---: | --- |
| 完整唤醒词 | 1.402280 | 4.031588 | 2.629308 | 161 | 你好小瑞 |
| 短唤醒词 | 1.401985 | 4.650909 | 3.248924 | 278 | 小瑞 |
| 重复唤醒词 | 1.401975 | 4.760281 | 3.358306 | 298 | 小瑞小瑞 |
| 非唤醒命令 | 1.401923 | 5.242111 | 3.840188 | 391 | 无 |
| 噪声 | 1.401935 | 4.674299 | 3.272364 | 96 | 无 |
| 静音 | 1.402050 | 7.091459 | 5.689409 | 547 | 无 |

encoder client 总耗时除块数为127.39～127.53ms/块，仍包括工作区重建、CRC、
cache维护及输出复制，不是裸NPU时间。全链路扣加载时间约为160ms步进预算的
1.08～2.00倍，带强校验的此版本尚不能作为实时唤醒完成证据。

每次返回后 NPU free=1654784；前五次堆 used=63456，最后 used=55248，
没有本轮累计增长；堆 high-water 最大10420016字节。没有首次运行前的 free 快照，
不能声称每个字节相对开机基线都完全一致，也不是长稳/所有取消路径认证。
两个 npuref 命令在本附件中没有输出，不把它们另算新固定模型验收。

保留 v158 通过镜像：`../openvela-a733-cubie-a7z-sd-v158-resident-kws-candidate.img`，
SHA256 `44a1d2bce28ef6776f25d3a9f219b364595809026c06d4ff5bafbcbf911b9bf8`。
既有 `out/v158-candidate-20261020/`、源码包和早期失败记录均不覆盖。

状态仅提升为固定特征连续 KWS hardware-passed。未执行实时麦克风、中文ASR、
完整TTS、Agent语音闭环、温度/长期压力或故障复位验收。
