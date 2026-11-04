# v203-r2 两次实拍及SPI显示通过

2026-10-29，用户确认颜色正常、两次测试均通过。新证据补充此前候选状态，固件和镜像未改。
对应原交付镜像SHA256：7f30ba7597b9a87a5d4ac99018040ddcfb57e275ab64a6154a51c0c11e5ed481。
本次摘录从构造器末段开始，没有完整引导CRC，故镜像身份由交付关系/用户报告和v203运行特征关联，
不伪称本次日志含完整镜像身份或新增冷启动供电测量。

- 两次均为IMX214 ID0214，模式153次写入、14/14核验；published=1，validation/error/cleanup=0，owned=0。
- generation=1→2，readers=0，ready=1，两个4147200字节RAW的驱动FNV指纹分别为5b9d2742、7a12fbec。
- 主机重算两份实际RAW的FNV，与本次串口绑定；两帧SHA不同，不是重复展示旧帧。
- 每份九文件完整；除来源标记差异外，八个payload经生产C从RAW重算后与板上导出逐字节一致。
- 原始RAW、模型输入、增强输入CRC均吻合；源文件摘要不变。仅归档日志/哈希/元数据，不归档图片。
- readback：帧长2128、曝光2118、模拟增益代码0000；本次不写曝光/电压，不把软件校色称为AE/AWB。

serial.txt保留原日志；audit.json为可重放核验结果。首轮未附FNV计算的审计仍保留在相邻无r2目录。
新增重放命令（WSL，输出必须是新的docs/evidence目录）：

```text
python3 tools/audit-a733-camera-v203-board.py ../摄像头测试文件/3/camera203a ../摄像头测试文件/3/camera203b --serial <本次原始串口txt> --output docs/evidence/camera-v203-board-NEW
```

通过范围：两次手动真实单帧采集、清理恢复、CPU增强图像及ST7735显示，颜色由用户确认。
未覆盖：32次实拍、冷启动循环、长稳、完整ISP/AE/AWB、连续视频、摄像头YOLO识别准确率。
本轮不生成另一份相同固件镜像，不改变语音/UFS/STM32/BLE的既有待测范围。
