# YOLO/LVGL v130 构建与镜像摘要

日期：2026-09-27

状态：`build-passed`、`image-verified`、`hardware-passed = false`

```text
kernel: quickly-openvela/cmake_out/cubie-a7z_nsh_v130_yolo_lvgl/nuttx.bin
kernel bytes: 2199488
kernel SHA-256: df2ab035a71bc0703fd84370ad1c9e27596e8f662793776681914a18936ac8b9

image: openvela-a733-cubie-a7z-sd-yolo-lvgl-v130-candidate.img
image bytes: 2147483648
image SHA-256: a8f84509632f73805c95a43d8afaf7aff735038a62741b3d2e4d18e55e39d7da

embedded kernel bytes: 2199488
embedded kernel SHA-256: df2ab035a71bc0703fd84370ad1c9e27596e8f662793776681914a18936ac8b9
ext4: clean
GPT: no problems found
```

本候选增加：

- `npu yolo-ui <640x640-rgb.bin>`：调用真实 `/dev/npu0` A7PM 原子 ABI；
- `display vision [seconds]`：唯一 LVGL 所有者，显示状态带和检测框；
- `/a733_vision`：32 字节定长 POSIX 消息，队列深度 8，每帧最多 1+7 条；
- IPC 生产者和消费者均非阻塞，UI 不存在时不改变 NPU 推理结果。

尚未证明 dog/black 实机显示，也未完成 `/dev/video0`。因此不得把本记录解释为
摄像头实时 YOLO 链路通过。
