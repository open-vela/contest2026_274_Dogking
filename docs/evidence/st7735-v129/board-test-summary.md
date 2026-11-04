# ST7735/LVGL v129 实机摘要

日期：2026-09-27

镜像 SHA-256：
`406697b0da50ba18809ed016fb215f7933e023a32a08fdac04ad05aca5719f5c`

原始串口记录 SHA-256：
`75a03bf69f2c77c223d78f9b42f3f966a7f70de2e0753a86b5d3ed349f63135f`

```text
display status
display: direct=ready controller=ST7735 resolution=128x160
format=RGB565-MSB spi=1 mode=0 frequency=12000000

display fps 10
display: lvgl-fps frames=296 elapsed=10011 ms fps=29.564
flushes=4736 pixels=6062080 throughput=1182 KiB/s

display fps 30
display: lvgl-fps frames=887 elapsed=30002 ms fps=29.563
flushes=14192 pixels=18165760 throughput=1182 KiB/s

free after test: 4239806816 bytes
```

`display demo 30` 随后完整输出并显示开心、难过、惊讶、生气、思考、安慰和普通。
`dmesg` 无 SPI、LVGL、异常或堆错误。结论：v129 显示性能门禁 hardware-passed，
stress-passed 仍为否。
