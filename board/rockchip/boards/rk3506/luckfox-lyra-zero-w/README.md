# openvela on RK3506 / Luckfox Lyra Zero W

This is the single-core RK3506 port.  Rockchip U-Boot initializes
DDR, clocks and pin multiplexing, loads `amp.img` from the `amp` partition, and
hands CPU0 to openvela.  Linux and multi-OS AMP coexistence are intentionally
out of scope for this configuration.

## Current hardware contract

- CPU: Cortex-A7, ARMv7-A, hard-float, uniprocessor
- Console: UART0 at `0xff0a0000`, IRQ 66, 24 MHz, 1,500,000 baud, 8-N-1
- Interrupt controller: GICv2 at `0xff581000` / `0xff582000`
- System timer: ARM generic non-secure physical timer, IRQ 30
- Firmware load and vector address: `0x00100000`
- Compatible `nsh` DDR window: `0x00100000` to `0x07ffffff` (127 MiB)
- Zero W-only `nsh_512m` DDR window: `0x00100000` to `0x1fffffff`
- L1 page table: final 16 KiB of the selected window, excluded from the heap
- Board activity LED: GPIO1_A0, active high, `/dev/actled`
- I2C2: `/dev/i2c2`, GPIO0_A0 SDA / GPIO0_A1 SCL through RMIO,
  100 kHz default
- SPI0: `/dev/spi0`, GPIO0_C0/C1/C2/C3 for CLK/MOSI/MISO/CS0
- Onboard wireless: AIC8800DC on USB2 OTG1, power enable GPIO1_C5
- Boot SD card: `/dev/mmcsd0`, fixed-present, 4-bit SDIO
- Storage datapath: PIO for small requests, IDMAC for aligned multi-block I/O
- Writable data partition: GPT `rootfs`, mounted as FAT at `/data`
- Diagnostics: procfs at `/proc`, RAM syslog at `/dev/kmsg`

The `nsh` configuration deliberately uses a 127 MiB DDR window and is valid
for both the 128 MiB Lyra B and the 512 MiB Lyra Zero W.  The `nsh_512m`
configuration owns the remaining Zero W DDR up to `0x1fffffff`, providing a
511 MiB openvela window.  It must not be flashed to a 128 MiB Lyra B.

OP-TEE occupies memory below the openvela load address.  U-Boot relocates near
the top of DDR while loading the image, but it no longer executes after the
AMP hand-off to CPU0.  The Zero W-only configuration may therefore reclaim
that temporary U-Boot area.  Its L1 page table is placed at `0x1fffc000`, so
the allocator never overwrites it.

The SDMMC port uses 400 kHz during card identification and 13 MHz
for transfers.  Multi-block requests are limited to 128 sectors (64 KiB),
which matches the current IDMAC descriptor and bounce-buffer capacity.
Normal 512-byte and larger sector requests, up to 64 KiB, use the internal DMA
controller (IDMAC); short protocol payloads use PIO.  Unaligned buffers use a
cache-aligned bounce buffer, and reset failures fall back to PIO.  The raw
whole-card and firmware partition character devices remain read-only; only the
GPT `rootfs` partition is exposed writable and mounted as FAT.

The 13 MHz transfer clock is a conservative performance step.  The generic
SD-card layer does not yet negotiate SD high-speed mode with CMD6, so the port
does not use the 26/52 MHz settings supported by U-Boot.  To return to the
previous approximately 8.67 MHz setting, set `BOARD_SDMMC_TRANSFER_TARGET` to
`BOARD_SDMMC_SAFE_TARGET` in `include/board.h` and rebuild.

## Build openvela and the Rockchip FIT image

Run from the `quickly-openvela` directory in a Linux environment.  Build the
compatible 127 MiB image with:

```sh
./nuttx/tools/build.sh \
  vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/configs/nsh \
  --cmake -j8

bash vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/tools/pack_amp.sh
```

For a Lyra Zero W with 512 MiB DDR, build and pack the dedicated image with:

```sh
./nuttx/tools/build.sh \
  vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/configs/nsh_512m \
  --cmake -j8

bash vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/tools/pack_amp.sh \
  cmake_out/luckfox-lyra-zero-w_nsh_512m/nuttx.bin
```

The FIT load and entry addresses remain `0x00100000` for both variants; only
the openvela-owned DDR limit and page-table location differ.

The second command:

1. creates `amp.img` beside this README;
2. validates it with the Rockchip SDK's `mkimage`;
3. stages an identical copy at
   `lyra-zero-w/output/openvela/amp.img` for the SDK build.

After changing `amp.img`, the SDK's AMP stage must run before firmware
packing.  `build.sh firmware` alone reuses the previous
`output/firmware/amp.img` and can silently package an old openvela binary.
Use `build.sh all`, or explicitly run:

```sh
RK_ALLOW_NON_NATIVE_FS=1 ./build.sh amp
RK_ALLOW_NON_NATIVE_FS=1 ./build.sh firmware
```

Before flashing, unpacking the final update image or checking the U-Boot FIT
hash is recommended.  A newly generated outer `update.img` does not by itself
prove that its embedded AMP payload changed.

If host `dtc` is unavailable, the script uses the included dependency-free FIT
packer.  The resulting FIT still contains a SHA-256 image hash and is parsed
back by Rockchip `mkimage -l`.

## Build the Zero W SD-card update image

The Rockchip U-Boot scripts require `python2` in addition to the normal host
compiler, `make`, `bison`, `flex`, OpenSSL development files and `dtc`.

For a native Linux/ext4 checkout, select the dedicated SDK configuration and
build:

```sh
cd ../RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w
./build.sh luckfox_lyra_zero-w_openvela_sdmmc_defconfig
./build.sh all
```

This configuration:

- builds U-Boot with the `rk-amp` fragment;
- omits Linux and the root filesystem;
- copies the staged prebuilt `amp.img`;
- uses a 4 MiB `amp` partition;
- packs a Rockchip `update.img`.

The resulting image is `output/update/Image/update.img`.

A native Linux filesystem remains the recommended build environment.  For the
provided SDK extracted on Windows and built through WSL, repair only the known
lost symbolic links and explicitly opt in to the non-native filesystem:

```sh
cd quickly-openvela
bash vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/tools/repair_sdk_links.sh

cd ../RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w
RK_ALLOW_NON_NATIVE_FS=1 \
  ./build.sh luckfox_lyra_zero-w_openvela_sdmmc_defconfig
RK_ALLOW_NON_NATIVE_FS=1 ./build.sh all
```

The repair tool only replaces empty placeholders for links known to the
checkout and refuses to replace a non-empty file or directory.

## First boot

Connect a 3.3 V USB-to-TTL adapter to UART0 and use 1,500,000 baud, 8-N-1.
After flashing the generated update image, the expected hand-off ends at:

```text
NuttShell (NSH) NuttX-...
nsh>
```

At the prompt, `hello` is the first smoke test.

The second-stage smoke tests are:

```sh
free
ps
dmesg
ls /dev
gpio -o 1 /dev/actled
sleep 1
gpio -o 0 /dev/actled
```

For `nsh_512m`, also run `free` and `cat /proc/meminfo`.  The reported heap
must be close to 511 MiB (slightly less after the image and allocator metadata),
not the approximately 127 MiB reported by the compatible configuration.

`gpio -o 1` must turn the blue activity LED on and `gpio -o 0` must turn it
off.  The GPIO driver intentionally initializes the LED low before changing
the pin direction to avoid a boot-time flash.

## SD-card and IDMAC checkpoint

The board registers the GPT partitions and automatically mounts `/dev/rootfs`
at `/data` when it already contains FAT.  Use the read-only `amp` partition as
a non-zero data source, then copy and compare through the writable data
partition:

```sh
dmesg
ls /dev
mount
time "dd if=/dev/amp.raw of=/data/idmac-a.bin bs=16384 count=128"
time "dd if=/data/idmac-a.bin of=/data/idmac-b.bin bs=16384 count=128"
cmp /data/idmac-a.bin /data/idmac-b.bin
umount /data
mount -t vfat /dev/rootfs /data
cmp /data/idmac-a.bin /data/idmac-b.bin
dmesg
```

Both `cmp` commands must return silently.  `dmesg` must contain one
`IDMAC read active` and one `IDMAC write active` message, with no `IDMAC error`,
data CRC, timeout, or short-transfer message.  A `, bounce` suffix is valid and
means the caller's buffer required the driver's cache-aligned bounce area.
It must also report `clock 400000 Hz, bus 1-bit (divider=65)` during card
identification and `clock 13000000 Hz, bus 4-bit (divider=2)` for transfers.
Do not write directly to `/dev/mmcsd0`, `/dev/uboot.raw`, `/dev/boot.raw`, or
`/dev/amp.raw`; these contain the running firmware.

## I2C2 checkpoint

The official Zero W device tree routes I2C2 through Rockchip Matrix IO to
GPIO0_A0 (SDA) and GPIO0_A1 (SCL).  Do not use the RK3506 RTOS BSP's generic
GPIO0_A4/A5 default.  The port selects the 24 MHz oscillator, enables the
I2C2 clocks, applies internal pull-ups and registers `/dev/i2c2`.  The first
bring-up default is 100 kHz; 400 kHz support is present but should only be
used after the 100 kHz hardware test is stable.

After boot, first verify registration without touching any slave:

```sh
ls /dev
dmesg
i2c bus
```

Expected output includes `/dev/i2c2`, `Bus 2: YES`, and:

```text
I2C2: /dev/i2c2 registered at 100 kHz on GPIO0_A0/A1 RMIO
```

Then scan the normal 7-bit address range at 100 kHz:

```sh
i2c dev -b 2 -f 100000 03 77
i2c dev -z -b 2 -f 100000 03 77
dmesg
```

The first command probes with one-byte reads and the second with zero-byte
writes.  An empty scan is valid when no I2C peripheral is populated; it must
complete and return to NSH without hanging.  Some Lyra device-tree variants
describe a GT9271 touchscreen at address `0x5d`, but its presence must not be
assumed on every Zero W board.  A detected address is only accepted after a
repeatable register read using the peripheral's data sheet.

## SPI0 internal-loopback checkpoint

SPI0 is intentionally separate from the FSPI controller that holds the boot
SPI-NAND.  The board port uses the fixed M0 group: GPIO0_C0 clock, GPIO0_C1
MOSI, GPIO0_C2 MISO and GPIO0_C3 CS0.  These pins do not overlap I2C2, SDMMC,
the activity LED or the Zero W Bluetooth power GPIO.

The completed internal-loopback checkpoint used
`CONFIG_RK3506_SPI0_LOOPBACK`.  That option is now disabled in the normal board
configurations.  For the external checkpoint, link GPIO0_C1 (MOSI) directly to
GPIO0_C2 (MISO), confirm `/dev/spi0`, then exchange a known byte pattern in
modes 0 and 3:

```sh
ls /dev
dmesg
spi exch -b 0 -f 1000000 -m 0 -w 8 -x 8 0011223344556677
spi exch -b 0 -f 4000000 -m 3 -w 8 -x 8 a55aa55a12345678
dmesg
```

For each exchange, `Received` must exactly equal `Sending`, and the prompt
must return without a transfer-timeout message.  `CONFIG_NSH_MAXARGUMENTS=16`
is required for these complete command lines.

Disconnect the MOSI-to-MISO jumper before connecting a real peripheral.  Never
attach a device to the FSPI/SPI-NAND signals or treat the boot flash as generic
SPI0.

The internal-loopback tests have passed at mode 0 / 1 MHz and mode 3 / 4 MHz.
The external MOSI-to-MISO checkpoint is intentionally deferred when no jumper
wire is available; it is not a prerequisite for the onboard Wi-Fi work.

## AIC8800DC Wi-Fi hardware checkpoint

The Zero W wireless device is an AIC8800DC connected to USB2 OTG1, not an SDIO
device.  The first Wi-Fi stage therefore powers the module through active-high
GPIO1_C5, enables the RK3506 USB PHY and DWC2 host clocks, applies the vendor
PHY settings, then checks both the DWC2 core ID and the USB line state.  It runs
in a delayed `wifi-probe` worker so NSH and SDMMC initialization are not blocked.

This checkpoint is enabled only by the Zero W `nsh_512m` configuration.  The
127 MiB compatible `nsh` configuration remains suitable for Lyra B and does
not power or probe the Zero W wireless module.

After flashing, wait two seconds and run:

```sh
dmesg
ps
free
```

A successful hardware checkpoint contains output similar to:

```text
WIFI: AIC8800DC power enabled on GPIO1_C5 (USB2 OTG1)
WIFI: OTG1 GSNPSID=4f54.... GINTSTS=........ HPRT=........
WIFI: USB PHY1 linestate=1 hstdet=... after ... ms
WIFI: hardware checkpoint passed; AIC USB device is present
```

The exact low 16 bits of `GSNPSID`, interrupt status and host-port value may
vary.  The high 16 bits of `GSNPSID` must be `4f54`, and `linestate` must be
non-zero.  `invalid DWC2 core ID` indicates a controller clock/register issue;
`no USB device pull-up detected` indicates module power, PHY configuration or
board-variant wiring should be checked.

The next `nsh_512m` checkpoint also enables
`CONFIG_RK3506_AIC8800DC_ENUM_PROBE`.  It performs a DWC2 soft reset, forces
host mode, initializes DMA and the root port, issues the endpoint-zero
SETUP/DATA/STATUS sequence, assigns USB address 1 and reads the device and
configuration descriptors.  Expected additional messages are:

```text
WIFI: DWC2 host initialized, HPRT=........ speed=high
WIFI: USB device VID:PID=....:.... bcdUSB=... EP0=..
WIFI: descriptor=12 01 ...
WIFI: USB enumeration passed, config=... interfaces=... total=...
```

The real board reports a high-speed connection.  DWC2 encodes high/full/low
speed as 0/1/2 in HPRT; the driver prints a name to avoid misreading zero as a
failure.  The VID/PID is reported from the real board and must not be guessed
from a different AIC8800DC firmware revision.  On failure, retain the complete
`HCINT` and `HCTSIZ` diagnostic line; it distinguishes NAK, STALL, transaction
and DMA errors.

The real Zero W result is `VID:PID=1a86:8091`, device class `0x09`, one
interface and a 41-byte configuration descriptor.  This is the onboard USB
hub rather than the AIC8800DC itself.  Seeing it proves root-port enumeration,
but it is not sufficient to start the wireless firmware loader.

The following checkpoint configures that hub, reads its class descriptor,
powers every downstream port, reads and clears port-change status, resets each
connected port, and enumerates high-speed children at address 2 or above.
Expected additional messages are:

```text
WIFI: USB hub ports=... characteristics=.... pwr-good=... ms
WIFI: hub port ... status=........
WIFI: hub port ... reset status=........
WIFI: child port=... addr=... VID:PID=....:.... class=..
WIFI: child descriptor=12 01 ...
WIFI: child port=... enumeration passed, config=... interfaces=... total=...
WIFI: USB hub scan passed, connected=... enumerated=...
```

High-speed children can be accessed directly by the DWC2 host channel.  A
full- or low-speed child behind this high-speed hub requires USB split
transactions; the probe reports that case explicitly instead of attempting an
invalid transfer.  Keep the complete hub port status and error diagnostics if
the child does not enumerate.

This hub-scan image deliberately does not provide `wlan0` yet.  Once the real
downstream AIC VID/PID and descriptors are known, the next stage is the
reusable DWC2 host-controller interface and AIC8800DC firmware loader, followed
by the 802.11 driver and openvela network stack.

The real downstream device is `a69c:88dc`, a high-speed composite device with
three interfaces and a 222-byte configuration descriptor.  The next image
reads that complete descriptor using multi-packet EP0 DMA, prints every
interface and endpoint, validates the vendor-specific WLAN interface and sends
`SET_CONFIGURATION(1)`.  Success ends with:

```text
WIFI: AIC endpoint map passed, interfaces=3 endpoints=...
WIFI: AIC8800DC configured at address 2; bulk stage ready
```

No private command or device-memory write occurs in this checkpoint.  The
printed endpoint map is the source of truth for the subsequent Bulk transport;
endpoint numbers from another board or firmware revision must not be assumed.

On the real board, interface 2 is the `ff/ff/ff` WLAN interface.  Its 512-byte
bulk endpoint pairs are `01/81` for data and `02/82` for messages, matching the
vendor driver's enabled `CONFIG_USB_MSG_OUT_EP` and `CONFIG_USB_MSG_IN_EP`
path.  Alternate settings 0 through 5 on interface 1 belong to Bluetooth
isochronous transport and must not be counted as separate interfaces.

The following read-only Bulk checkpoint sends the vendor-format
`DBG_MEM_READ_REQ` on EP02 and receives its confirmation on EP82.  It reads
address `0x40500000`, the first chip-identification read in the vendor
`system_config_8800dc()` sequence.  Success is reported as:

```text
WIFI: AIC endpoint map passed, interfaces=3 endpoints=19 data=01/81 msg=02/82
WIFI: AIC bulk message passed, [40500000]=........ chip-id=..
```
