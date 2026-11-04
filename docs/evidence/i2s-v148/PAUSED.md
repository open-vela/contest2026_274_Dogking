# v148 用户要求暂停（2026-10-15）

这是历史暂停记录。2026-10-16用户明确恢复，后续全量构建/镜像验证见
[交付记录](build-summary.md)；下面不改写当时状态。原中断日志可在77f8343提交中查看。

用户“先停止，我有事”。不继续开发、不启动新构建、不封装/烧录/推送。

- 已通过基线：v147首次基础收音，恢复标签 a733-i2s-v147-basic-recording-pass，
  提交bf3d772；旧镜像及所有原始失败记录保留。
- 新功能源码提交：bff039e7efe212ce2f5695c95502565ca8efc3db。
  icsvoice左槽mono、默认×4/可调1..16、饱和/削顶诊断、原始接口不变。
- 已通过：实际C函数ASan/UBSan、1048576个S16/增益组合、原始接口、
  13类新语音失败保护、矩阵、分轨/波形合成、home、BLE生命周期与FAT路径主机回归。
  原始日志在本目录，均非新模式硬件验收。
- ARM64全量构建停在CMake配置/生成阶段，由助手向本次已确认的CMake进程发送TERM。
  build.log中的Terminated/config fail来自用户要求的中断，不据此声称源码编译失败。
- 构建包装器已退出，Official source restoration: PASS：
  129个官方暂存路径与home目录身份恢复一致，HOME_ABSENT。
  official-before.txt与official-after.txt已cmp一致；未留下SDK临时overlay。
- 当前没有v148可烧录镜像、完整ELF或布局验收，不把半成品当成构建通过。
- 已生成配置SHA与v147相同：
  d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189。

用户明确恢复后，在仓库根目录重新运行完整构建（本次尚未完成配置，不用incremental）：

```sh
bash tools/build-a733-i2s-v148.sh
bash tools/package-a733-i2s-v148.sh
```

包装器只清理指定v148生成目录，不碰v147镜像。重跑会更新本目录本次构建日志；
如需逐次保留中断日志，重跑前先在Git历史/独立新目录归档。
随后核对完整链接/布局、129路径恢复、内嵌内核、GPT/ext4与SHA，再交付候选镜像。
测试与边界见 ../../a733/ICS43434_V148_VOICE_GAIN.md。
