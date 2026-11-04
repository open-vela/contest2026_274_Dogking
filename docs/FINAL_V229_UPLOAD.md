# v229 最终提交与上传准备

2026-11-04。按作者要求停止 dslocal 开发，将 v229 作为本阶段最后交付版本。**本轮没有推送远端、创建PR、重编固件、烧写SD/UFS或擦除UFS引导。**

## 1. 最终版本与可恢复性

- 固件源码冻结：`ba0d722a965ed59a8aa8bef49dbf64fc1f8731e5`。
- 原封存提交：`d409c1a89d112f730f23c13aa0568840e707eae1`；原标签 `a733-v3.0pre-fix2-v229-image-20261104` 保留。
- 本轮仅新增最终提交索引、目录说明、上传检查工具和答辩亮点图文案，不改 `board/`、`app/`、`patches/` 的 v229 实现。
- 最终提交版本与已获整机验收的稳定版分别管理。`FINAL_RELEASE.json` 指向 v229；`STABLE_RELEASE.json` 保留历史 v1.1/v219 回退依据，不伪造 v229 新板测。
- dslocal/v230 没有交付镜像、不进入最终包。10个跟踪文件原稿、差异补丁和7个新增文件已逐文件校验备份，归档在仓库外 `../archives/dslocal-v230-stopped-20261104/`。恢复仅在新分支进行，不能直接覆盖最终工作区。

## 2. 制品与哈希

原2GiB镜像保持文件名及字节不变，`candidate` 是历史文件名，不通过重命名假装新验收。

| 制品 | 位置（相对仓库） | SHA256 |
| --- | --- | --- |
| SD整卡镜像 | `../openvela-a733-cubie-a7z-sd-v3.0pre-fix2-v229-candidate.img` | `5d8ae2fe97176b05a411f0e38a5ff5db5e0a6c519a47f07207a35f2d53e400f1` |
| Image | `out/v229-product-v30prefix2-release-r1/Image` | `936f9b72edec6193477ca30b37446935e0fddfbc8b22ea2d7d3db2536c160439` |

封存证据：[v229镜像记录](evidence/product-v30prefix2-v229-image/README.md)。44项软件门禁、完整ARM链接/ABI/ISA/摄像头审计、320项SDK字节恢复和镜像文件回读已通过。本机认证TLS的50轮/150连接无FD增长不是50轮真实讯飞成功，真实云连续多轮仍须板测。

## 3. 上传包分工

源码包只从最终已提交的 Git 快照生成，不包含 `.git`、未跟踪文件、`out/`、SDK、模型、物理板配置、私有Key、2GiB镜像或停用草稿。不重写 Git 历史，不删除历史源码/证据。

镜像作为独立 Release/网盘附件上传，**不要 `git add *.img`**。GGUF和厂商模型按各自许可证另行部署，引用路径、准备脚本和许可证随源码保留。

运行（输出目录必须不存在，避免覆盖已有包）：

```text
python tools/prepare-v229-upload.py --output ../upload-v229-20261104-r1
```

工具核对最终指针、固件差异、镜像/内核哈希、跟踪大文件与常见凭据特征，使用 `git archive HEAD` 创建快照，再回读全部ZIP条目并绑定原Git对象。输出源码ZIP、逐文件清单、哈希、检查结果和上传说明。自动扫描不是完整隐私/法律审计；正式发布前人工确认原始日志和第三方授权，尤其不要上传板上 `/data` 私密配置。工具不执行push/PR。

如需保留本机历史恢复，使用已有Git仓库、原标签、`out/checkpoints/`与外部草稿归档；公开源码ZIP故意不含 `.git`。

## 4. 当前功能口径

- `local`：本地ASR + CPU MiniCPM5 + CPU声学/NPU vocoder；无音频上传。
- `cloud`：所选云ASR/LLM/TTS，严格云端，不因失败暗中转本地；仅显式 `auto` 可降级。
- 常驻半双工多轮、情绪与SPI表情、规则识物、真实摄像头/YOLO同帧框、UART4 STM32，沿用v229。
- 模型ACTION默认关闭，显式开启及本轮邀请后才接受一项有界动作提议。桌宠步×20；ACK不是机械运动完成证明。
- 不包含 dslocal/MiMo新Token Plan未确认方案；不宣称自然英文TTS、30FPS、全双工/AEC或整机3–5秒首音已验收。

## 5. 答辩材料

三张亮点图单独交付到工作区 `决赛答辩/亮点生图版_20261104/`，用于PPT整页插图，非可编辑文本对象。原PPT、原模板和实拍素材不覆盖。流程、文案及测试数据索引见 [亮点图记录](presentations/HIGHLIGHT_IMAGES_20261104.md)。
