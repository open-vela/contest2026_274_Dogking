# LLM v87：可选核心列表与矩阵行并行

恢复基准为 v86 前向实机通过标签。保留系统默认 CPUSET=0xff，不将
Wi-Fi/SSH/FTP 强行绑定 CPU0/1；操作系统仍使用已有 SMP 调度器，
这不是移植 Linux 调度器。默认只限制 LLM 使用 CPU2..7，因此 CPU0/1
不承担本命令的矩阵工作，但其他服务仍可使用全部八核。

命令：`aipetllm forwardfast model.gguf token-id [core-list]`。
默认列表 `2,3,4,5,6,7`，掩码 fc。支持 `6,7`、`6`、`2,3,6,7` 等。
核心编号必须为0..7、逗号分隔且不可重复，不支持范围写法或带空格列表。
单核心沿用 v86 串行算法，多核心对 Q4_K/Q6_K 矩阵行创建 pthread。

调用者只在所选核心上运行，结束后恢复原亲和掩码。主线程独占 FILE
读取，将当前矩阵复制到共享只读 RAM；每个工作线程绑定一个选定核心，
先确认迁移完成，再写入不重叠输出行。输入只量化一次，点积次序不变。
工作行按 A76:A55=3:1 初始比例分配，等待全部线程结束再消费输出。
线程创建/绑核失败明确报错，不将未完成输出当作成功结果。

注意：按矩阵创建线程，尚非持久线程池。模型仍每次完整载入并释放，
当前矩阵另需临时内存（上限256 MiB）。这可能增加内存带宽和线程创建
开销；六核心未必快于两 A76，必须实测。不得宣称完整生成已经可用。

## 实机对照

先备份 TF 卡模型和配置，再烧写。镜像不包含后来传入的 GGUF 模型。
单核对照可先跑；每个命令正常完成后检查 free，避免并发多份大模型。

```text
aipetllm cpucheck
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 6"
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 6,7"
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
free
```

三种模式应有相同逐层CRC、final-state-crc=17a04f7d、
logits-crc=7fdfee67、argmax=6233。比较 compute 而非包含模型读取的
总时间。测试同时检查另一终端 Wi-Fi/SSH 响应，长期稳定性仍待验证。
下一步采用长期模型/目录缓存和线程池，再实现多 token KV cache。

## 候选构建存档

源提交 `a61b953`，WSL 构建通过；System.map 确认并行函数与工作线程
已链接。官方 arch.h 临时补丁已恢复，没有 .orig 残留。行分配公式
255 个有效掩码×6 种行数共1530组检查，无缺口或重叠；此为公式检查，
不替代开发板线程/数值/压力测试。

- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-llm-multicore-v87-candidate.img`
- 大小：2147483648 字节，内核1713552字节。
- 内核 SHA256：`95686b5938597743048dd0c4f0873be9f8a0b9c70e06c5a3c268bbefb130108b`。
- 镜像 SHA256：`e992f46cba766cd9f0ba336b46edc17d429adec5dcb66feca6dfe6dc4c9acb22`。
- ext4 检查、嵌入内核回读、GPT 检查通过；继承的分区4尾部对齐提示
  仍保留。v86 镜像不变。
- 标签：`a733-llm-multicore-v87-candidate`。
- 恢复包：`A733-A7Z-ALL-Files/archives/llm-v87/a733-llm-v87.bundle`。
- 构建/打包/检查日志：同目录 build.log、package.log、verify.log。

多核心实机数值、速度和内存恢复均待测试，不宣称六核加速已实测通过。

## v87 多核心实机数值回归通过

用户回传同一 token=9707 的两次完整输出：

| 模式 | 模型加载（秒） | compute（秒） | 命令总耗时（秒） |
| --- | --- | --- | --- |
| 默认 CPU2..7，六线程，mask=fc | 174.8024 | 1.0739 | 175.8934 |
| CPU6/7，双 A76，mask=c0 | 174.8645 | 1.0239 | 175.9056 |

两次28层CRC全部一致，且与v86单核基线一致：最终state=17a04f7d、
logits=7fdfee67、argmax=6233、logit=10.4909735。
因此本次默认六核和双 A76 数值回归通过。双 A76 此次计算耗时低约
4.66%，只有每模式一次测量，不将其宣称为稳定统计结论。
相较v86单A76的1.1492秒，两次compute分别低约6.55%和10.90%；
跨版本数据包含不同矩阵读取/复制及线程开销，不能当作纯CPU加速比。

目前每次载入约175秒，重复命令仍重新读模型。下一步优先持久模型与
解析目录生命周期、直接访问驻留权重（避免再次复制整矩阵）、持久线程
池；再接多token KV cache与生成。六核默认和可选核心接口保持不变，
不凭此单次数据擅自改变用户要求。内存恢复、长时间/并发测试和v87
单核心对照尚未提供结果，仍待验证。

恢复标签：`a733-llm-multicore-v87-forward-verified`。
独立恢复包：`A733-A7Z-ALL-Files/archives/llm-v87/a733-llm-v87-forward-verified.bundle`。
