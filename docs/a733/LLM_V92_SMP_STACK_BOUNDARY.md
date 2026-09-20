# v92：SMP 启动栈与堆边界隔离修复候选

## 证据与假设

v91 CPU7 异常现场：ELR/LR 均为 0，X25=`40201720`，X24=`424004a0`，SP_ELX 约为 `42400490`，X29=0。对应 ELF 的 X25 是 `arm64_boot_secondary_c_routine`，入口先在栈上保存 LR，并调用 MMU 初始化。

链接地址表显示 CPU7 栈结束于 `424004a0`。ARM64 `up_allocate_heap()` 将 `g_idle_topstack` 直接作为堆起始地址，因此堆头与栈顶共用包含 `42400480..424004bf` 的 64 字节缓存行。辅核在数据缓存关闭时使用启动栈，CPU0 已经开启缓存并管理堆；这一共享边界存在缓存可见性风险，应用新增 BSS 会改变其对齐。

这与启动返回地址变为零的现场吻合，但目前仍是待板端确认的根因假设，不能仅凭布局分析认定全部故障已解决。

## 修复

只在仓库板级 `dramboot.ld` 中，将 initstack 起点和结束（堆起点）按 4096 字节对齐。每核 8192 字节 idle 栈及 4096 字节 interrupt 栈保持原尺寸。加入链接 ASSERT，确保 idle 栈数组和堆起点页对齐，避免以后 BSS 增长再次使边界落在缓存行内部。

构建脚本临时映射此链接脚本，退出时恢复原官方文件。保留 v91 全寄存器异常输出，保留八核、系统 0xff 调度及 LLM fc 默认亲和性，没有更改零地址执行权限或模型计算实现。

## 验证门槛

离线：链接断言通过；核对 System.map 的 idle 栈与堆起点；归档对应 ELF；内核回读、文件系统/GPT 校验。

板端：至少三次冷启动进入 NSH、cpucheck 八核通过、sleep 5 计时、Wi-Fi/ping 正常。再运行 cache-hit forwardfast 并比较 argmax=6233、final-state-crc=17a04f7d、logits-crc=7fdfee67，检查多次调用和 unload。不要用一次成功取代这些验证。

若仍出现 F0，发送全部 FAULT 行，使用 v92 ELF 映射地址；不能继续沿用 v91 地址表。若故障依然存在，检查缓存开启前后的实时栈内容和辅核异常上下文切换。

恢复文件和前版本不覆盖：`a733-v91-before-stack-align-fix.bundle`；稳定镜像仍是 v88。

## 离线构建结果

构建退出 0，链接断言通过。System.map 中 `_s_initstack=423e9000`、`g_cpu_idlestackalloc=423f1000`、`g_idle_topstack=42401000`，栈顶/堆边界不再落在缓存行内部。官方链接脚本和异常处理文件临时覆盖均已恢复。

镜像 `openvela-a733-cubie-a7z-sd-smp-stackfix-v92-candidate.img` 为 2147483648 字节。内核回读一致，ext4 检查通过、GPT 无错误（继承分区 4 尾部对齐提示）。

- 内核 SHA256：`a7765e5fb63adee07e3ea48ec1881c46911837385a3a865d31aa654bb0f7a91f`。
- 镜像 SHA256：`a8fd52d55634a1beb7710c5b8c719b6d54d4887e1369ebed30d63e6ea86ebde4`。
- ELF、Image、System.map 与日志：`archives/llm-v92/`。

这些是离线校验；板端根因验证及稳定性回归仍待用户测试。

## 板端基础验证通过

用户随后提供 v92 实机日志：正常到达 N8/X1 和 NSH，没有 F0；cpucheck 检测全部八核，CPU0..5 为 A55、CPU6..7 为 A76，checksum 均为 81e62f98，亲和性恢复为 ff；sleep 5 实测 5.0009 秒。

Wi-Fi 双频扫描可见 2.4/5 GHz 网络，5 GHz WPA2/DHCP 成功，地址 192.168.1.176、网关 192.168.1.1；网关 ping 6/6，0% 丢包，平均 15.833 ms。未记录密码。

此次测试支持栈边界修复有效，但日志只展示一次启动，不视为三次冷启动压力验证。LLM 持久线程池、cache-hit 数值一致性、切核及 unload 资源释放仍待测试，不能把基础通过扩大为所有 LLM 功能通过。

此源码标记为 `a733-v92-basic-verified`，作为后续开发恢复点；v88 仍保留。

## LLM 持久线程池基本验证通过

随后用户实测首次加载 175.0800 秒、计算 0.6184 秒；第二次 cache-hit 不读模型，计算 0.6150 秒、整条命令 0.6320 秒。所有层 CRC 及最终 state=17a04f7d、logits=7fdfee67、argmax=6233 与此前一致。

pool pid=14、mask=fc、workers=6、created=6 保持不变，matrices 从 141 增到 282。unload 后 model empty、mask=00、workers=0，协调服务 pid=14 保留。内存 used 从 36080 到驻留 1117597552，卸载后 342352，残留相对起点 306272 字节，尚不能视为泄漏全部解决，重复加载/卸载压力仍需验证。

恢复标记：`a733-v92-llm-pool-basic-verified`。多 token KV cache 和生成仍待实现。
