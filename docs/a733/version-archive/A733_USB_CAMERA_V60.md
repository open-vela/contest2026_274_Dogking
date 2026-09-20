# A733 Cubie A7Z USB camera v60 checkpoint

This candidate adds the missing Type-C host-role checkpoint for the external
USB-C 3.1 connector.

- enables S-TWI1 at `0x07084000` on PL12/PL13;
- identifies the board ET7304 TCPC at address `0x4e`;
- applies fixed source/host CC policy and enables 5 V VBUS;
- accepts both DWC3 and DWC3.1 GSNPSID signatures;
- waits for xHCI physical attach and reports CC, power and orientation state.

Keep the USB camera connected to the USB-C 3.1/DP connector during a cold
boot. After NSH starts, run:

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

Success for this checkpoint requires `typec ... checkpoint=0` and bit 0 set
in at least one xHCI `ports=` word. Descriptor enumeration and UVC streaming
remain gated until that physical-link result is validated on the board.
