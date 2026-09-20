# 桌宠 vision-4 移植记录

## 原始代码与边界

源代码只读位置：`D:\My-Program\AI-Agent-Program\cc-program\Radxa_A7Z_Sencond_Pro\Now-Program\vision-4`。
本轮未修改原开发目录，也没有复制包含凭据的完整配置。
此前 `goal` 原型不是最新版，后续以 vision-4 为行为依据。

## 已实现，主机测试通过

- 保留已有路由、回复解析、动作白名单及离线本地降级核心。
- 表情由 7 种扩展到新版 20 种。
- `SentenceStream` 接收增量字节，分句并将句尾动作和表情归入当前句。
- 动作标签内的标点不触发分句；未闭合标签不交付动作。
- `cancel` 丢弃待输出内容；`finish` 为终态，不能再次输入。
- 总回复限制 4096 字节，避免无界缓存。
- 对样例每个字节切分位置运行测试，覆盖 UTF-8 与标签跨块。

实现：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_core.{hxx,cxx}`。
测试：`tools/test-aipet-core.cxx`，使用 C++17、Wall/Wextra/Werror 编译通过。
测试二进制：`archives/aipet-vision4/test-core`（生成文件，不纳入源码）。

## 有意偏离与待接入

新版 Python 分句器仍仅判断旧 7 种表情前缀，且可能切开动作内标点；移植使用标签感知分句避免不完整动作。
原 Python 打断线程与键盘输入同时读取 stdin，存在竞争；不能直接照搬。
原 `_stop_all` 并未真正停止 TTS，仅打印提示；必须在媒体适配器中实现真实停止。

当前仍是业务库，不是完整可运行桌宠，也没有更新可烧写镜像。
下一步需要提供 LLM 字节回调接口与自定义系统提示词，再注册 `aipet` 应用。
其后接入官方 Media API、UIkit、KVDB，并移植 ST7735 与动作 HAL。
屏幕为 128×160 ST7735；Linux GPIO 编号不能直接作为 openvela 引脚编号。
语音、显示、执行器、摄像头链路均需独立板测；不能用 Mock 成功代替硬件验收。
历史 NSH Ctrl+C 强制退出导致 LLM 锁未释放的风险仍未在本轮修复。
