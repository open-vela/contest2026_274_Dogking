# ST7735 v126 实机证据

测试日期：2026-09-27  
目标：Cubie A7Z / A733，1.8 英寸 128×160 ST7735，SPI1 Mode 0，12 MHz  
镜像：`openvela-a733-cubie-a7z-sd-st7735-v126-raw8-cs-fix-candidate.img`  
镜像 SHA-256：`f173ad215c58486e52fc315577666375013fbda6528ec6ecaa350ab63ef73042`

验证命令：

```text
display status
display raw8
display test 30
display status
display raw8
dmesg
```

结果：direct 8-bit 初始化通过；raw8 两次自动返回并显示白框与 RGB 三色块；
LVGL 桌宠界面持续 30 秒并返回 NSH；测试后无 SPI timeout、FIFO overflow、
Data Abort 或堆损坏。功能等级为 hardware-passed，尚未完成长时间压力测试。

照片 SHA-256：

```text
77505237c3b6f8ad3074fe3ff982fb358a063590629e1269147389e80245f248  01-pre-first-full-refresh.jpg
c36a7d715fe8c4ddc2e67171fb055670ace363e5a152397b07633e6d8a834c76  02-lvgl-face.jpg
1c52b95c28f8f1d58c99d302a58cca02ec7494f3721bef52579f2c4837f5b527  03-raw8-rgb.jpg
1c52b95c28f8f1d58c99d302a58cca02ec7494f3721bef52579f2c4837f5b527  04-raw8-rgb-repeat.jpg
```

`01` 是首次全帧覆盖前的面板随机 GRAM 内容；`02` 是 LVGL 场景；`03`、`04`
是同一次重复 raw8 验收的相同照片文件，保留以对应用户提交的四项附件。
