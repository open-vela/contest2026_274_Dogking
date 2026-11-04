# v162 用户板测：录音电流声修复通过，KWS 准确率仍未验收

2026-10-20。用户回传串口日志并明确反馈“这次没有电流声了”。
这是用户执行/听音的证据，不是本轮代理 SSH 或现场观察。

原始文件：[完整日志](openvela-v162-user-quiet.txt)。SHA-256：
`850b8797e3aed093123d62babdddf98788de19b01fd9b1b81edd6209f70280d3`。

## 本轮通过范围

- v162 正常进入 NSH；4 次显式 2 秒录音、3 次 WAV 播放、4 次 5 秒 kwsmic。
  最终驱动计数 captures=8、plays=3、timeouts=0；用户未再听到录音期间的电流声。
- 每次采集后 CTL=0，PB7 GPIO 输出低（cfg0=133f7777、data=0），
  播放仍成功。支持本次功放 DIN 静音策略的有效性，不是模拟电源抗扰度或长稳验收。
- 4 次 kwsmic 完成、affinity-restored=1、resident released；真实 NPU 调用总数 1500。
  候选数为 0/1/0/1，两个候选名称均为“你好小瑞”。没有对应逐条真实发音标签，
  且任意声音的 native 输出不在 golden 集合，不能宣称唤醒准确率通过。
- 本轮前/末专用池 free=1654784、heap used=63456 一致；不代表长时间无泄漏证明。

## 待解决项

4 条 kwsmic 中后 3 条 x5 软件增益饱和计数为 1022、296、619。
第 2 条增益前 input-min=-32768、input-max=32767，已经到 S16 满量程；
这不能仅用降低数字增益恢复，也不能据日志就断定麦克风损坏、爆音或采样格式错误。
需要保存对应 WAV、看波形/电平，再和驱动增益前统计对照。保持用户选定 x5 默认值。

v161 的 WAV 前端数值差异仍未证明消除。保持严格 CRC/native oracle，
不以改阈值、偷偷替换输入或只比较候选文字代替验证。

## 保留基线

通过音频行为的原镜像仍为
`openvela-a733-cubie-a7z-sd-v162-capture-quiet-candidate.img`，SHA-256：
`8f67428298d6c764e73082636d8bcd7aa97db4cf708bf3bb7c250341a134ba80`。
不重写已封存 v162 capsule/manifest；其中 hardware=false 是制作时的状态，
后续本次硬件反馈由本文件补充，不抹去历史。
