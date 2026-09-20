# A733 openvela 复制清单

## 必须复制

- `A733_OPENVELA_HANDOFF_20260910.tar.gz`：交接核心包。
- `A733_OPENVELA_HANDOFF_20260910.tar.gz.sha256`：核心包校验。
- `openvela-a733-cubie-a7z-sd-usb-camera-v65-candidate.img`：最新 2 GiB 烧写镜像。
- `v65-image-sha256.txt`：镜像/内核权威校验。
- 整个 `quickly-openvela`：当前可构建工作环境；若新 AI 仍在本机当前目录工作，则保持原位，无需重复复制。

## USB/摄像头阶段必须复制

- `linux-5.15-product-aiot-stable`
- `bsp-product-aiot-stable`
- `u-boot-2018-product-aiot-stable`
- `spl-pub-product-aiot-stable`
- `a733-product-aiot-stable`
- `allwinner-device-device-a733-v1.4.6`
- `docs-product-aiot-stable-Software 软件类文档`
- `A733_User Manual_V0.92.pdf`
- Radxa A7Z 原理图和 placement map 的全部文件
- `CAMERA_IMX214_RESEARCH.md`
- 最新真机 USB 日志：`C:\Users\Lenovo\.codex\attachments\416af5a3-cfd6-46f9-bd79-72044f9c8fee\pasted-text.txt`

## NPU 后续必须复制

- `prepared-models`
- `a733-viplite-golden`
- `yolov8-openvela`
- `viptrace-lenet-20260413-072201.tar.gz`
- `viptrace-yolov5s-20260829-030008.tar.gz`
- `viptrace-yolov8n-pcq-20260830-153436.tar.gz`
- `allwinner-model-zoo.tar.gz` 和/或 `model-zoo-extracted`
- `docker_images_v2.0.x.zip` 和/或 `docker-images-extracted`
- `a733-ai-sdk-reference`

## Wi-Fi/Bluetooth 后续必须复制

- `fcu760k-official-reference`
- 官方 Debian 镜像（用于 Linux 驱动/固件行为对照）
- 当前板上 `/data/aic/` 中使用的 D80-U02 固件文件（若它们只存在于 SD 卡）

## 建议复制

- 整个 `archives`，包含 NPU v43-v50 verified 和网络/服务 candidate 恢复点。
- `radxa-a733_bullseye_cli_r6.output_512.img`，用于官方 Linux golden 测试。
- 所有用户保存的串口日志和测试截图；这些不一定在工作区。

## 不要复制/不要泄露

- Wi-Fi 密码。
- SSH/FTP 登录密码。
- AI API key、token 或私钥。
- Windows `known_hosts`、用户私有 SSH key。

接手前对核心包和 v65 镜像运行 SHA-256 校验。不要用压缩包里的局部 source 目录覆盖一个未知版本的 openvela；优先继续使用本机完整 `quickly-openvela`，交接包是恢复和比对快照。
