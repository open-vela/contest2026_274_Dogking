# A733 Cubie A7Z USB camera v61 checkpoint

This candidate corrects S-TWI1 access to match the official A733 v101 TWI
controller and Cubie A7Z device tree.

- uses the packet/FIFO driver mode selected by `twi_drv_used = <1>`;
- performs the required explicit driver-mode soft-reset sequence;
- reads and configures the ET7304 at address `0x4e`;
- reports driver control, interrupt, FIFO, bus and physical line state;
- leaves xHCI descriptor enumeration gated until Type-C attach is confirmed.

Cold boot with the USB camera connected to the USB-C 3.1/DP connector, then
run:

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

Expected progress is `typec ... checkpoint=0`, non-zero ET7304 identity, and
bit 0 set in at least one `ports=` value.  If it does not pass, the
`typec-twi-v101` line now distinguishes address NACK, stuck bus lines and a
controller timeout.
