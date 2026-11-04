# v229 云连接修复候选封装与恢复证据

2026-11-04。候选版，未执行新版真实云账号/声学/多轮板测；稳定版仍为用户验收的v1.1/v219。问题输入及结论边界见 [v228传输证据](../voice-v228-transport-20261104/README.md)，修复和升级操作见 [v229说明](../../PRODUCT_V3_0PRE_FIX2_V229.md)。

## 制品与源码

- 整卡镜像：`openvela-a733-cubie-a7z-sd-v3.0pre-fix2-v229-candidate.img`，2147483648字节。
- 镜像SHA256：`5d8ae2fe97176b05a411f0e38a5ff5db5e0a6c519a47f07207a35f2d53e400f1`。
- 内核SHA256：`936f9b72edec6193477ca30b37446935e0fddfbc8b22ea2d7d3db2536c160439`，7267240字节。
- 最终固件源码冻结：`ba0d722a965ed59a8aa8bef49dbf64fc1f8731e5`。
- 封存：`out/v229-product-v30prefix2-release-r1`，含ELF、map、config、启动脚本、镜像清单及`Image`；`Image`与`nuttx.bin`字节相同。
- 最终构建报告：`out/product-v30prefix2-v229-build-r2`；门禁：`out/v229-product-gates-r1`；完整审计：`out/v229-full-link-r1`；镜像回读：`out/product-v30prefix2-v229-package-r1`。
- Windows独立读取最终镜像和Image，SHA256均与Linux封存一致。构建时暂存的cloud_voice_socket.cxx与冻结源码哈希相同，为`35c6c268cf68fbd738d25553f836250f25e38c6c85fa45cffe8a373d65575cc0`。

初次完整构建后，补充了对端主动关闭的响应和poll等待裁剪；最终r2重编实际执行了cloud_voice_socket.cxx与lib_dnsquery.c。交付不是直接使用补丁前的r1二进制。最终源码与冻结提交的overlay对照无差异。

## 验证结果

44项继承/新增软件门禁通过；最终ARM链接、栈帧、构造器/内存布局、NPU ABI、ISA、摄像头/显示/CRC/绑核/流水线保护审计通过。SDK320项暂存内容按字节恢复，并在封装前再次检查；SDK LLM原文件也再次校验恢复。

实际mbedTLS与本机认证TLS服务执行同一进程50轮、150条连接，跨主线程/工作线程各自独占生命周期，FD增长0，ASan/UBSan检查。覆盖TCP拒绝实际错误、socket资源失败、取消、对端主动关闭、静默关闭预算，以及第一地址TLS失败后第二地址成功。服务消息为模拟，不是50轮真实讯飞对话。

从真实SDK抽取DNS callback，旧版可控复现8个UDP fd泄漏，修正版成功和失败各100次TCP回退均opens==closes。IPv4/IPv6两条源码分支均有修正；测试使用模拟DNS/socket，不能据此声称现场故障由它唯一触发。

最终镜像内模型、词表、许可证、内核及启动文件回读验证通过。v219/v220/v221/v223/v224/v225/v226/v227/v228共9个旧镜像封装前后哈希保持一致。严格CLOUD不回退本地，不改变NPU/I2S/摄像头硬件代码或全局调度；不重放LLM动作或已播音频。

## 升级与恢复

整卡烧录会覆盖目标SD分区及板上配置，先备份。本轮没有烧写物理SD/UFS，也没有删除UFS引导内容。当前若为v228且有可靠的ext4编辑工具，可正常关机后备份SD第3分区 `/boot/openvela/a733/Image`，仅替换成封存目录同名Image，以保留第4分区云凭据/网络配置；不要放到FAT数据卷冒充内核升级，不改DTB或UFS。

本地恢复标签：`a733-v3.0pre-fix2-v229-image-20261104`。源码分支：`preview/v3.0pre-fix2-v229`。恢复包保存在`out/checkpoints/`。不创建PR、不推送。

真实板上仍需10轮以上云对话、长回复、停止重启、网络恢复及声音检查。新phase/SO_ERROR/wire计数用于进一步区分DNS、TCP、TLS、HTTP升级及服务读写，不在板测前宣称间歇故障已经彻底消失。
