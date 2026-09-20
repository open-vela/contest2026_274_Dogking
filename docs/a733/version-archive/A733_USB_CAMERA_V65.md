# A733 USB camera bring-up v65

v64 exposed a reset-order regression: before the added DWC3 core/PHY reset,
USBSTS was `0x00000001` (halted and ready); afterwards it was `0x00000801`
(halted but Controller Not Ready), and CNR never cleared.  The USB2 PHY still
reported a valid J state and the ET7304 still reported an attached Type-C sink.

## Changes

- Preserve the BL31/U-Boot DWC3 and Cadence PHY handoff state.
- Retain the vendor application-register setup, USB2 PHY wake quirk and DWC3
  host-mode selection.
- Establish the ET7304 source/host role before updating the DWC3 host state.
- Use only the standard xHCI HCRST and continue to wait for CNR to clear.
- Continue to defer USBCMD.RUN until all xHCI DMA structures are installed.

## Board test

Connect the camera to USB-C 3.1/DP before power-on and run:

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

Expected evidence is `reset=0`, `sts` without bit `0x800`, and CCS set in one
of the two PORTSC values.  That is the gate for adding xHCI command/event rings
and reading the camera's UVC descriptors.
