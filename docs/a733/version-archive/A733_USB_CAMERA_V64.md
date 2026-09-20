# A733 USB camera bring-up v64

v63 proved the complete physical path through the Type-C controller and
USB2 PHY: ET7304 reported CC2/VBUS and USB2 ISCR reported a J-state device
pull-up.  xHCI still showed no CCS because USBSTS.CNR remained set after the
DWC3 core/PHY reset.

## Changes

- Halt xHCI without starting it or installing DMA pointers.
- Issue the xHCI Host Controller Reset after the DWC3/PHY reset.
- Wait for both USBCMD.HCRST and USBSTS.CNR to clear.
- Report reset status and the CNR polling count through `/dev/a733-uvc`.
- Continue to defer USBCMD.RUN until DCBAA, command ring and event ring are
  installed in the following stage.

## Board test

Connect the camera to USB-C 3.1/DP before power-on and run:

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

Expected evidence is `reset=0`, USBSTS.CNR clear, and CCS set in one of the
two PORTSC values.  With that result, the next revision can safely add xHCI
DMA rings and enumerate the UVC descriptors.
