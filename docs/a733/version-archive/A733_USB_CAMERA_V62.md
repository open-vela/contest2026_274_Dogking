# A733 USB camera bring-up v62

This revision replaces the incomplete ET7304 host-role checkpoint in v61
with the initialization sequence used by the Linux RT1715/RT1711H TCPCI
driver.  ET7304 is register-compatible with RT1715.

## Changes

- Software-reset ET7304 and wait for TCPCI initialization to complete.
- Exit the reset-default shutdown state (`WORKINGMODE=0x2a`).
- Program the vendor I2C timeout, CC filter, DRP period, and duty registers.
- Configure fixed DFP default-current Rp on both CC pins.
- Remove the unsupported `0x77` command; Cubie A7Z controls source VBUS with
  the PL2 `USB0-DRVVBUS` load switch.
- Poll CC1/CC2 through the debounce interval and expose working mode, fault,
  alert mask, interrupt level, and poll count in `/dev/a733-uvc`.

## Board test

Connect the UVC camera to the USB-C 3.1/DP port before power-on, then run:

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
ls /dev
```

Expected Type-C evidence is `work=2a`, one non-zero CC state, and
`orientation=CC1` or `orientation=CC2`.  A connected xHCI root port then
allows the next host-controller enumeration checkpoint to run.
