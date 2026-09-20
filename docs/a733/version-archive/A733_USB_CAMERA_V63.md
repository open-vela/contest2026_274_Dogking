# A733 USB camera bring-up v63

v62 proved that the Cubie A7Z ET7304 Type-C controller entered source/host
mode, detected the camera plug on CC2, and observed VBUS without a fault.
The remaining failure was below Type-C: both xHCI ports stayed powered in
RxDetect with CCS clear.

## Changes

- Reproduce the official Allwinner DWC3 core/USB2/USB3 PHY soft-reset order.
- Configure the vendor USB application block for external VBUS and enable its
  PHY/PIPE controls before attach detection.
- Keep the USB2 PHY awake as required by the board DT
  `snps,dis_u2_susphy_quirk` property.
- Preserve the safety boundary: xHCI is not run until DMA rings exist.
- Report DWC3 USB2/USB3 PHY configuration, application registers, USB2 ISCR
  line state, and PHY VBUS state through `/dev/a733-uvc`.

## Board test

Connect the UVC camera to the USB-C 3.1/DP port before power-on, then run:

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
ls /dev
```

Success at this checkpoint is `checkpoint=0` with CCS set in either xHCI
PORTSC value.  If CCS remains clear, the new `iscr`, `line`, and `vbus`
fields identify whether the USB2 PHY sees the camera pull-up independently
of xHCI.
