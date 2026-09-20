# openvela on RV1103 / Luckfox Pico Mini

This is the initial single-core Cortex-A7 port for the SPI-NAND model of the
Luckfox Pico Mini.  It deliberately replaces Linux on the application core;
MCU/Linux AMP coexistence is deferred until the standalone system is stable.

## Initial hardware contract

- CPU: single Cortex-A7, ARMv7-A
- Console: UART2 at `0xff4c0000`, IRQ 59, 24 MHz, 115,200 baud
- GICv2 distributor / CPU interface: `0xff1f1000` / `0xff1f2000`
- ARM architectural timer: 24 MHz, physical timer PPI/IRQs 29 and 30
- Execution state on entry: ARM Secure state (vendor U-Boot does not switch
  to Non-secure). The port retains the proven non-TrustZone NuttX
  GIC/exception model and registers both physical-timer PPIs, matching the
  32-bit Linux strategy for firmware-dependent Secure/Non-secure routing.
- Firmware load/vector address: `0x00800000`
- Initial openvela DDR window: `0x00800000` through `0x02ffffff`
- Reserved boundary: `0x03000000`, matching the official TEE load address
- SPI NAND layout: 256 KiB env, 256 KiB idblock, 512 KiB U-Boot, 4 MiB boot,
  30 MiB oem, 6 MiB userdata and 85 MiB rootfs

The first image keeps the official BootROM, DDR blob, SPL/idblock and U-Boot.
Only the 4 MiB `boot` partition payload will be replaced.  This keeps Rockchip
DDR and SPI-NAND bad-block handling in the vendor boot chain.

## Bring-up status

- Stage 0 is hardware-verified: NSH, the 40 MiB RAM window, GIC and the
  24 MHz architectural timer work.  `time "sleep 5"` returns in about five
  seconds and subsequent `sleep`/`uptime` tests continue normally.
- The archived Stage-0 rollback image is
  `boot-stage0-timer-known-good.img`, SHA-256
  `eabcf439bfa3fb409d30c74a8b4ca11aa896936b6e965aa82f1986a5116a3ccc`.
- Stage 1 GPIO/pinctrl is hardware-verified.  The official active-high work
  LED (GPIO3_C6) is registered as `/dev/workled`; output/readback and the
  timer regression passed.  The rollback image is
  `boot-stage1-gpio-known-good.img`, SHA-256
  `1e5df6445dd7d0df4cc5716f5c44c80d24613e4f0b50fbf6f198f06429d10e36`.
- Stage 1 watchdog is hardware-verified.  The reset-mode DesignWare watchdog
  is registered as `/dev/watchdog0`, remains stopped after boot, and starts
  only when an application explicitly requests it.  After feeding stopped,
  the board reset and completed the full BootROM/SPL/U-Boot/openvela chain.
  The rollback image is `boot-stage1-watchdog-known-good.img`, SHA-256
  `c42a1287fd90464d931dd184b56eaeea17f1d4043b6c613bf302603df0bdb907`.
- Stage 2 TSADC is hardware-verified.  The on-chip RV1103/RV1106 TSADC
  channel 0 is exposed as
  `/dev/uorb/sensor_temp0`.  It follows the official v9 analog-power, clock,
  reset and conversion-table sequence.  This first increment is deliberately
  read-only: thermal IRQs, alarm thresholds and both TSHUT routes remain
  disabled.  The initial code was 552 (53.514 C), five standard sensor reads
  returned about 50.54 C, and timer/GPIO/heap regression passed.  The rollback
  image is `boot-stage2-tsadc-known-good.img`, SHA-256
  `68cdab04339c6d0dde51f8f24aae5bb2b746d3b2d3738c5d8dc658d1926083e8`.
- Stage 2 TRNG is hardware-verified.  The standalone non-secure TRNG v1 block
  is exposed as
  `/dev/random` and `/dev/urandom`.  It checks the official `0x46bc` hardware
  version, waits for the seeded state, applies a 50 ms timeout, and rejects
  all-zero or repeated consecutive 256-bit blocks.  Hardware testing confirmed
  the startup self-test, two distinct samples, and successful 64 KiB reads from
  both devices; TSADC, timer, GPIO and heap regressions also passed.  The
  rollback image is `boot-stage2-trng-known-good.img`, SHA-256
  `814fbecfa5d9d14646fdc2966936544b5e492a613febc983e0786a913394ed27`.
- Stage 1 SoC infrastructure is now complete and hardware-verified.  The
  PL330 at `0xff420000` reports ARM peripheral ID `0x00241330`, eight
  channels, sixteen events, a 64-bit data bus and a non-secure manager.  Its
  synchronous channel-0 memcpy passed the 4096-byte cache-coherency and guard
  test; TRNG, TSADC, GPIO and heap regressions also passed.  The rollback
  image is `boot-stage1-pl330-known-good.img`, SHA-256
  `4271b3bfacf25f02a764282a056e5c2b1c818d47d8053c7876bfca5329506b43`.
- The first Stage 2 SPI-NAND/MTD candidate is ready for hardware validation.
  It recognizes only the board's `EF AE 21` 128 MiB device, uses the chip's
  on-die ECC read flow, and exposes the full raw device plus the seven
  official partitions.  This checkpoint is physically read-only: erase,
  block/byte write, mark-bad and protection-changing requests all return
  `-EROFS`, and the controller never emits write-enable, program or erase
  commands.  Hardware confirmed SFC v6, ID `ef ae 21`, the expected geometry
  and `boot[0]=d00dfeed`.  The first image exposed MTD mountpoint inodes but
  direct NSH opens returned `ENXIO`; the corrected image adds NuttX's BCH
  character-access proxy in forced-read-only mode.  Hardware then verified
  the FIT header, a 256 KiB sequential read, write rejection and all previous
  peripheral regressions.  It is archived as
  `boot-stage2-spinand-ro-known-good.img`, SHA-256
  `b33108e6b11f73a777ddfce9c1513c2db28b430425c6d9383b2543d4e9ce8466`.
- Stage 2 SPI-NAND userdata write support is hardware-verified.  Program and
  erase are permitted only inside the physical 6 MiB `userdata` range
  (`0x02300000..0x028fffff`); every other address is rejected before a NAND
  command is issued.  Byte writes, bulk erase and mark-bad remain disabled,
  and each programmed page is immediately read back and compared.  A 2 KiB
  page was erased/programmed, read back, retained across a full reboot, and
  the protected boot FIT magic remained `d00dfeed`.  The rollback image is
  `boot-stage2-spinand-userdata-write-known-good.img`, SHA-256
  `35094618f879d1a07b45e55c8e580ec4db461f7fe8267368428ffcd4f6e3d8e7`.
- Stage 2 persistent storage is hardware-verified.  LittleFS is mounted
  directly on `/dev/nand-userdata` at `/data`; its 2 KiB read/program unit and
  128 KiB erase unit match the NAND geometry exactly.  First-boot formatting,
  file creation, a 64 KiB random-file copy/compare, unmount/remount and a full
  reboot all preserved the data.  The protected boot FIT magic remained
  `d00dfeed`, and no SFC, ECC or LittleFS errors were reported.  `autoformat`
  is confined to the userdata MTD partition, and mount failures do not prevent
  NSH from starting.  The rollback image is
  `boot-stage2-spinand-littlefs-known-good.img`, SHA-256
  `6aaef51f6836427cbcbe939b86024ef548465d6258a659c28a1c5183c24dadf6`.

The GPIO3 register data comes from the official RV1103/RV1106 Linux BSP:
GPIO3 is at `0xff550000`, its PCLK gate is VI CRU gate 1 bit 15, and GPIO3_C6
uses IOC mux register `0xff558054`.  The implementation explicitly opens the
clock, selects mux function zero, writes the initial level before switching
the pin to output, and uses the GPIO v2 write-mask register convention.

## Stage 7 camera status

SC3336 sensor setup, the two-lane MIPI D-PHY, CSI-2 host and one packed RAW10
RKCIF DMA frame are hardware-verified at 2304x1296.  The Stage-7B candidate
adds a bounded 60-frame ping-pong DMA test, independent hashes for both DMA
buffers, FIFO/error and timeout checks, and a read-only standard V4L2 node at
`/dev/video0`.  `/dev/camera0` remains the known-good frozen first-frame
fallback.

The RKISP32 block is deliberately clocked and inspected but not started yet.
Correct ISP output requires the official SC3336 IQ parameters plus validated
MI buffer programming; a register-presence message must not be treated as a
YUV/RGB pipeline success.  The candidate therefore fails safely back to RAW10
without affecting NSH or the previously verified peripherals.

Stage 7C adds the sensor control and recovery layer as one hardware-testable
increment.  The standard V4L2 node implements exposure and gain controls,
frame interval, stream on/off and a fresh bounded RKCIF capture.  `/dev/sc3336`
also accepts simple NSH control commands so this layer can be verified without
a separate ioctl utility:

```text
echo stop > /dev/sc3336
echo exposure=680 > /dev/sc3336
echo gain=256 > /dev/sc3336
echo fps=15 > /dev/sc3336
echo start > /dev/sc3336
echo capture > /dev/sc3336
time "dd if=/dev/video0 of=/dev/null bs=65536 count=61"
dmesg
```

The control ranges are exposure `1..VTS-8`, gain `128..99614`, and frame rate
`5..30`.  Exposure, analog/digital gain and VTS updates use the SC3336 group
hold register.  Repeated capture allocates new ping-pong buffers first, swaps
the last verified frame under a mutex, then releases the old frame.  A failed
capture leaves the previous readable `/dev/video0` frame intact.  This is the
required lifecycle/error-recovery foundation for the subsequent ISP MI path.

The official SDK contains the matching
`sc3336_CMK-OT2119-PC1_30IRC-F16.bin` IQ data.  It is not copied blindly into
the kernel image: the 168 KiB parameter blob is consumed by Linux rkaiq and
is not itself an RKISP register script.  RKISP YUV completion still requires
porting the v3.2 parameter translation and MI programming, followed by real
hardware validation; RAW10 remains the safe fallback until that succeeds.

Stage 7C is hardware-verified.  The board accepted stream stop/start,
exposure 680, gain 256 and a 15-fps VTS of 2720.  A fresh 60-frame capture
completed in 3978 ms (15.0 fps), produced independent buffer hashes
`2c1ab3d9/83164276`, and `/dev/video0` returned the full changing RAW10
payload.  CSI-2 remained error-free.  Steady heap use stayed near 7.6 MiB;
the higher `maxused` value records the intentional allocate-verify-swap
window during recapture rather than a retained allocation.

Stage 7D begins with a reversible RKISP3.2 input checkpoint.  The command
below temporarily routes RKCIF MIPI channel 0 to ISP0, programs the verified
2304x1296 BGGR/10-bit acquisition geometry, starts only the ISP input core,
polls its hardware frame counter, and then restores every modified route and
ISP register.  MI DMA and the IQ-dependent Bayer-to-YUV blocks remain gated.
This keeps the hardware-verified RAW `/dev/video0` path available even when
the ISP check fails:

```text
echo ispcheck > /dev/sc3336
dmesg
echo capture > /dev/sc3336
time "dd if=/dev/video0 of=/dev/null bs=65536 count=61"
```

The first command passes only when `dmesg` reports
`RKISP32 input check frames=` with a non-zero frame count, `err=00000000`,
and `route restored`.  A negative `echo` result is a useful hardware failure,
not permission trouble.  In either case the following RAW recapture must
still pass.  The Stage-7D candidate is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage7d-isp-input/boot.img`
(403968 bytes, SHA-256
`8b3fddb0ba004c59898f9eb87993c5cd49183441492675aa95acf4a234504d47`).
Replace only the SPI-NAND `boot` partition at sector `0x800`; retain the
hardware-verified Stage-7C image as rollback.

Stage 7D is hardware-verified: ISP0 accepted the live RKCIF input with
`err=00000000`, the route was restored, and a subsequent RAW recapture still
completed 60 frames in 2008 ms (29.8 fps) with changing hashes.  The original
diagnostic printed `frames=1023` because the undocumented hardware counter
wrapped; the next image reports this checkpoint as a boolean frame-progress
result instead of presenting that subtraction as a real frame count.

Stage 7E adds a bounded ISP3.2 MI single-frame experiment using Rockchip's
official RT-Thread ISP3 NV12 initialization order.  It does not run at boot.
Trigger it manually, then inspect the independent node only if the command
succeeds:

```text
echo micapture > /dev/sc3336
dmesg
ls /dev
time "dd if=/dev/isp0 of=/dev/null bs=65536 count=61"
hexdump /dev/isp0 count=64
echo capture > /dev/sc3336
dmesg
```

Acceptance requires `RKISP32 MI capture passed`, MI frame bit `ris` bit 0,
no MI bus error, `err=00000000`, a substantial changed-byte count,
`guard=ok`, and `/dev/isp0`.  `/dev/isp0` currently contains one frozen
2304x1296 NV12-sized diagnostic frame; it is not yet the final streaming V4L2
YUV node and its colors are not accepted until IQ translation is enabled.
Failure leaves `/dev/isp0` absent and restores the ISP/RKCIF registers; the
final RAW `capture` command must still work.  The candidate image is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage7e-isp-mi/boot.img`
(406016 bytes, SHA-256
`146eb6c4b0d9f8e2acb1fb2ecab82360409c7c94825d2398025376b81de8ae6f`).

The first Stage-7E hardware run failed safely with `ris=0`, `status=0`,
`err=0`, `changed=0`, and `guard=ok`; RAW recapture then remained healthy at
29.4 fps and the heap returned to its prior steady state.  This rules out a
DMA bus fault, overrun, leak, or broken RKCIF fallback and isolates the fault
to the ISP-core-to-MI path.  Fix 1 adds the official `VI_ISP_PATH=0x400000`,
ISP3.2 ICCL internal ISP/MI/crop/resize clocks, and Linux's MI wrap default,
restoring all three after the bounded test.  Use
`rv1103-spinand-stage7e-isp-mi-fix1/boot.img` (406016 bytes, SHA-256
`7419d0e81c5468a68a86f73599f05f25687d5891f580e50dca7d272472ab8782`)
for the next hardware run; the commands and acceptance criteria above are
unchanged.

One Fix-1 boot observed a transient `0041` sensor ID: the SC3336 low byte was
valid while its high byte was zero, so the board correctly omitted
`/dev/sc3336` and the MI command never ran.  Fix 2 reads both ID bytes in one
SCCB transaction, requires two consecutive `cc41` results, retries the
power-on settling window, and performs one bounded PWDN/MCLK recovery cycle
before returning `-ENODEV`.  This keeps strict device identification while
removing the startup race.  The next candidate is
`rv1103-spinand-stage7e-isp-mi-fix2-probe/boot.img` (406528 bytes, SHA-256
`294b1788c7528c91b8976c0b3f063358af07f8b47282d7ee0fcb15752daaf58d`).
After flashing, first require `/dev/sc3336`, `/dev/camera0`, and `/dev/video0`
to return before running the unchanged Stage-7E MI test above.

Fix 2 hardware-verified the robust sensor probe and restored all three camera
nodes, but its MI result remained identical to the original failure
(`ris/status/err/changed=0`, guard intact).  Fix 3 therefore replaces the
stale RT-Thread MI literal/ordering with the RV1106 Linux `capture_v32`
sequence: initialise all plane offset counters, select ISP32 NV12 output,
commit MI base/offset registers, issue `MPSELF_UPD`, enable MP, then force the
ISP configuration update.  Its diagnostic also reports MI shadow control,
shadow Y address and independent ISP frame progress, separating a failed MI
latch from a stopped ISP input.  Test
`rv1103-spinand-stage7e-isp-mi-fix3-sequence/boot.img` (406528 bytes, SHA-256
`bbbac83b2e06e9a8ea589a71e20d36bbbe8b39d4ae8c486852c3efcc4fe498f6`).

Fix 3 proved that ISP input continued (`frames=1`) and that MI latched the
correct Y address, but `MI_WR_CTRL_SHD` stayed zero.  Thus the route, DMA
address and memory coherency are verified and the remaining defect is solely
the live-route control shadow update.  Fix 4 issues a second idempotent
`MPSELF_UPD`/`MI_SOFT_UPD` after `MP_ENABLE`, matching the update boundary
needed when attaching MI to an ISP input that is already streaming.  Test
`rv1103-spinand-stage7e-isp-mi-fix4-shadow/boot.img` (406528 bytes, SHA-256
`9bb322fdf6a70d04de2c948b7feb2dcca955aba860e2ff31b6108b18b93e2991`).

Build from `quickly-openvela` in WSL/Linux:

```sh
./nuttx/tools/build.sh \
  ../vendor/rockchip/boards/rv1103/luckfox-pico-mini/configs/nsh \
  --cmake -b cmake_out/luckfox-pico-mini_nsh_115200_build -j8
```

Use a fresh build directory when changing console or architecture options.
CMake preserves an existing `.config`; reusing an older 1,500,000-baud build
directory can silently override the 115,200-baud board defconfig.

Build the SPI-NAND `boot.img` after the official kernel baseline has produced
the board DTB and `resource.img`:

```sh
export RV1103_SDK=/home/devcontainers/rv1103-openvela-sdk/luckfox-pico-main
export RV1103_OPENVELA_BUILD="$PWD/cmake_out/luckfox-pico-mini_nsh_115200_build"
export RV1103_OUTPUT_DIR="$PWD/cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand"
vendor/rockchip/boards/rv1103/luckfox-pico-mini/tools/mk-spinand-boot.sh
```

The result is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand/boot.img`. It uses the
Rockchip external-data FIT format (`mkimage -E -p 0x800`), loads openvela at
`0x00800000`, and is checked against the 4 MiB `boot` partition limit.

## First SPI-NAND boot test

Use a 115,200-baud, 8-N-1 serial console. The output directory also contains
the matching official `download.bin`, `env.img`, `idblock.img`, `uboot.img`,
and a `boot-linux-backup.img` recovery copy. The verified SPI-NAND layout
places `boot` at byte offset `0x100000`, or sector offset `0x800` for
Rockchip's upgrade tool.

From Linux, put the board in Rockchip MaskROM/Loader mode and run:

```sh
cd "$RV1103_SDK/tools/linux/Linux_Upgrade_Tool"
sudo ./upgrade_tool ld
sudo ./upgrade_tool db "$RV1103_SDK/output/image/download.bin"
sudo ./upgrade_tool rid
sudo ./upgrade_tool rfi
sudo ./upgrade_tool wl 0x0 \
  /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela/cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand/env.img
sudo ./upgrade_tool wl 0x200 \
  /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela/cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand/idblock.img
sudo ./upgrade_tool wl 0x400 \
  /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela/cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand/uboot.img
sudo ./upgrade_tool wl 0x800 \
  /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela/cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand/boot.img
sudo ./upgrade_tool rd
```

If the factory idblock and U-Boot are known to be intact, later development
iterations only need to replace `boot.img` at sector `0x800`. To restore Linux,
repeat that boot-only write with `boot-linux-backup.img`.

For the Stage-1 GPIO candidate, verify the node and the physical LED while
also checking that the timer baseline did not regress:

```text
ls /dev
gpio /dev/workled
gpio -o 0 /dev/workled
sleep 2
gpio -o 1 /dev/workled
sleep 2
gpio -o 0 /dev/workled
time "sleep 5"
free
dmesg
```

Expected results: `/dev/workled` exists; `gpio` readback follows 0/1/0; the
board work LED visibly follows the commands (active high); the five-second
sleep returns; and no GPIO initialization error appears in `dmesg`.

For the watchdog candidate, first perform the non-destructive regression,
then start the deliberate reset test:

```text
ls /dev
gpio /dev/workled
time "sleep 5"
free
dmesg
wdog -d 5000 -p 500 -t 2000 -s hard
```

`/dev/watchdog0` must be present before the last command.  `wdog` should ping
for about five seconds, stop pinging, and the hardware should reset the board
roughly 2.8 seconds later.  The requested 2000 ms is rounded up to the nearest
DesignWare power-of-two period.  After the automatic reboot, NSH,
`/dev/workled`, `time "sleep 5"`, `free`, and `dmesg` must still work.  Do not
run the reset test while flashing NAND.

For the TSADC candidate, verify the new standard sensor node and take five
readings before doing the existing timer/GPIO regression:

```text
dmesg
ls /dev/uorb
sensortest -n 5 temp0
time "sleep 5"
gpio /dev/workled
free
dmesg
```

Expected results: `dmesg` contains a line beginning with `TSADC:` and a raw
code plus temperature; `/dev/uorb/sensor_temp0` exists; `sensortest` prints
five finite, physically plausible Celsius readings; and timer, GPIO and heap
tests still pass.  A raw code below 363 is rejected instead of being reported
as a false temperature.  Because the candidate never enables alarm or TSHUT
bits, a conversion/configuration problem cannot trigger a thermal reset.

For the TRNG candidate, verify startup, two visibly different samples, a
larger read, and all earlier peripherals:

```text
dmesg
ls /dev
rand 16
rand 16
time "dd if=/dev/random of=/dev/null bs=1024 count=64"
time "dd if=/dev/urandom of=/dev/null bs=1024 count=64"
sensortest -n 3 temp0
time "sleep 5"
gpio /dev/workled
free
dmesg
```

Expected results: `dmesg` reports `TRNG: hardware v1 startup test passed`;
both random devices exist; each `rand 16` prints 64 bytes and the two outputs
are different; both 64 KiB reads complete without timeout or short-read
errors; and TSADC, timer, GPIO and heap behavior do not regress.  A version,
seed, ready, all-zero or continuous-output failure prevents registration
instead of silently returning weak data.

For the PL330 DMA candidate, first verify the controller identity and the
cache/guard protected 4096-byte memory-copy self-test, then regress the
previously verified timer, GPIO, TSADC, TRNG and heap paths:

```text
dmesg
time "dd if=/dev/random of=/dev/null bs=1024 count=64"
sensortest -n 3 temp0
time "sleep 5"
gpio /dev/workled
free
dmesg
```

Expected results: the first `dmesg` contains `PL330: id=` followed by the
reported channel/event/bus-width information and
`PL330: 4096-byte memcpy self-test passed (cache + guards)`.  No new `/dev`
node is expected: this stage provides the internal `rv1103_dma_memcpy()` API.
It deliberately uses channel 0 in synchronous polling mode with PL330 events
masked, because the RV1103 GIC event routing has not yet been verified.  Any
peripheral-ID mismatch, manager/channel fault, final-address mismatch,
timeout, cache coherency error, or guard overwrite causes initialization to
fail and is reported in `dmesg`.

For the first SPI-NAND candidate, flash only the new `boot.img` at sector
`0x800`; do not rewrite env, idblock or U-Boot.  Then perform only read-side
tests:

```text
dmesg
ls /dev
hexdump /dev/nand-boot count=64
time "dd if=/dev/nand-boot of=/dev/null bs=2048 count=128"
dd if=/dev/zero of=/dev/nand-userdata bs=2048 count=1
time "sleep 5"
sensortest -n 3 temp0
time "dd if=/dev/random of=/dev/null bs=1024 count=64"
gpio /dev/workled
free
dmesg
```

Expected results: `dmesg` reports SFC version 4 through 8, ID `ef ae 21`,
the 128 MiB geometry and `boot[0]=d00dfeed`; `/dev/spinand0` and all seven
`/dev/nand-*` nodes exist; the boot partition read completes without timeout
or ECC error; and the deliberate write-open fails as read-only without
changing NAND.  Timer, TSADC, TRNG, GPIO, PL330 and heap regressions must also
remain clean.  Do not proceed to erase/program support or a persistent
filesystem if any one of these checks fails.

For the userdata-write candidate, first record the existing first 64 bytes,
then replace only the first logical 2 KiB sector with zeroes.  BCH/FTL will
preserve the remainder of its erase block.  This test intentionally changes
the Linux `userdata` partition; it never targets a startup partition:

```text
dmesg
hexdump /dev/nand-userdata count=64
time "dd if=/dev/zero of=/dev/nand-userdata bs=2048 count=1"
hexdump /dev/nand-userdata count=64
hexdump /dev/nand-boot count=16
time "sleep 5"
dmesg
```

Expected results: startup reports `userdata writable, all other partitions
protected`; the write finishes without an SFC timeout or program/erase error;
the second userdata dump is all zero; the boot header remains `d00dfeed`;
and the timer and previous peripheral logs remain clean.  Reboot once and
repeat both `hexdump` commands to prove persistence and startup-partition
integrity before enabling a filesystem.

Do not build this SDK from a Windows-extracted tree.  The official ZIP has
160 Unix symbolic links and Linux case-sensitive path semantics. In
particular, NTFS confuses kernel paths such as `Documentation/Kbuild` and
`Documentation/kbuild`. Extract and build it on WSL's native ext4 filesystem.

## Stage 3: board peripheral bundle

The `nsh` configuration enables a conflict-free default bundle in one image:

| Function | Device | Route and pins |
| --- | --- | --- |
| I2C3 | `/dev/i2c3` | M1: GPIO1_D3 SCL, GPIO1_D2 SDA |
| I2C4 | `/dev/i2c4` | M2: GPIO3_C7 SCL, GPIO3_D0 SDA |
| SPI0 | `/dev/spi0` | M0: GPIO1_C1 CLK, C3 MISO, C2 MOSI, C0 CS0 |
| PWM1 | `/dev/pwm0` | M0: GPIO0_A4 |
| SARADC0/1 | `/dev/adc0` | channels 0 and 1 |
| ADC keys | `/dev/buttons` | 0 uV and 400781 uV ladder keys, 1.8 V key-up |
| UART3 | `/dev/ttyS1` | M1: GPIO1_D1 RX, GPIO1_D0 TX |
| UART4 | `/dev/ttyS2` | M1: GPIO1_C4 RX, GPIO1_C5 TX |

SPI1_M0 (GPIO4_A7/A0/A1/A5), every hardware PWM channel from 0 through 11,
and all official PWM M0/M1/M2 routes are implemented as compile-time spare
choices. They are deliberately not all enabled at the same time: pin muxing
is exclusive electrical ownership, not a software sharing mechanism. Select
the PWM channel and route with `CONFIG_RV1103_PWM_CHANNEL` and
`CONFIG_RV1103_PWM_ROUTE`; enable SPI1 with `CONFIG_RV1103_SPI1`.

The board rejects PWM alternatives that collide with the default bundle:
PWM0_M1, PWM2_M2 through PWM7_M2, PWM8_M1, PWM9_M1, PWM10_M2 and PWM11_M2.
PWM7_M2 also owns the GPIO3_C6 work LED. For a product-specific image, disable
the displaced peripheral before selecting one of these routes instead of
removing the guard blindly.

After flashing the Stage 3 `boot.img`, perform this non-destructive smoke test:

The generated image is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage3-peripherals/boot.img`
(369152 bytes, SHA-256
`9b0ecd59e61bfdb9e7f53dd73d4a2cb10b0740dbaf539144dea04cdd458b16a8`).

```text
dmesg
ls /dev
i2c help
spi help
pwm -f 1000 -d 50
adc -n 5
hexdump /dev/buttons count=4
free
```

Expected device nodes are `i2c3`, `i2c4`, `spi0`, `pwm0`, `adc0`, `buttons`,
`ttyS1` and `ttyS2`, in addition to the already validated Stage 2 NAND,
timer, GPIO, TSADC, TRNG and PL330 devices. I2C, SPI and the extension UARTs
still require an external target or loopback for end-to-end electrical
validation. Never connect two push-pull outputs together; cross UART TX to RX
and share ground for loopback.

## Stage 4: removable SD/MMC checkpoint

The Stage 4 checkpoint adds the RV1103 DW-MSHC controller at `0xffaa0000`,
IRQ 84, the official GPIO3_A1 through GPIO3_A7 pin route, card detection and
the controller's internal IDMAC.  The VI clock unit selects the 24 MHz crystal;
the controller uses 400 kHz during identification and up to 12 MHz after card
negotiation.  Initialization runs in a delayed worker so a missing or faulty
card cannot hold up NSH.  This checkpoint does not auto-format or auto-mount a
card and therefore does not intentionally modify its contents.

The generated image is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage4-sdmmc/boot.img`
(384512 bytes, SHA-256
`9257985f432e6b0f11a12cab40ff7dd3c2f8050993e9ce807ebb63ae2fa1925e`).
For the first test replace only the SPI-NAND `boot` partition.  Keep the
validated Stage 3 image as the rollback image.

First boot once without a card and wait at least five seconds:

```text
time "sleep 5"
dmesg
ps
ls /dev
```

NSH must remain responsive.  The expected log ends with `SDMMC: controller
ready at 0xffaa0000, card not present`; `sdmmc-init` should have exited and
`/dev/mmcsd0` should not exist.  A reset timeout, data abort, permanent worker
or lost console is a failure.

Then power off, insert a known-good SD card and boot again.  Do not use a card
whose only copy of important data is on it.  Run read-only checks only:

```text
time "sleep 5"
dmesg
ls /dev
time "dd if=/dev/mmcsd0 of=/dev/null bs=512 count=128"
time "sleep 5"
dmesg
free
```

Success requires `card present; enumeration requested`, `/dev/mmcsd0`, a
completed 64 KiB read with no timeout/CRC/IDMAC error, a responsive timer and
no regression in the Stage 1 through Stage 3 devices.  Do not test writes or
filesystems until this checkpoint passes on hardware.

## Final camera acceptance target

The final board target includes the user's SC3336-3MP-Camera(B), using the
official SDK hardware description as the baseline: I2C4 address `0x30`, chip
ID `0xcc41`, GPIO3_C5 PWDN, GPIO3_C4 MIPI reference clock and two CSI lanes.

The first camera checkpoint is enabled by
`CONFIG_RV1103_SC3336_CHECKPOINT`. It selects the SC3336-supported 24 MHz
2304x1296@30-fps mode, follows the upstream power delay, reads registers
`0x3107/0x3108`, and registers `/dev/sc3336` only after the ID matches. Probe
failure is non-fatal: an absent or incorrectly seated camera is reported in
`dmesg` without delaying NSH. Validate with:

```text
dmesg
ls /dev
cat /dev/sc3336
```

Expected output contains `CAMERA: SC3336 id=cc41 detected`. D-PHY, CSI2,
RKCIF and ISP streaming are the next checkpoint after this electrical/SCCB
baseline.

Final completion means more than probing the sensor: the sensor, D-PHY,
CSI-2 receiver, RKCIF, ISP and DMA buffer path must produce stable, changing
2304x1296 frames. Exposure, gain, frame rate, power cycling, timeouts and
cleanup must work without CSI/ISP errors or leaked buffers.

The official Pico Mini DTS disables GMAC and the board has no on-board RJ45
PHY.  The RV1103 GMAC implementation may be kept as a Pico Plus/external-RMII
variant, but an on-board Pico Mini RJ45 success claim would be electrically
incorrect.

## Stage 4B: USB2 PHY and DWC3 peripheral checkpoint

The Pico Mini USB block is not the DWC2 controller used by RK3506.  The
official board DTS enables the DWC3 core at `0xffb00000`, the RV1106-compatible
USB2 PHY at `0xff3e0000`, and fixes `dr_mode` to `peripheral`.  This checkpoint
enables only the documented ACLK/ref/UTMI/PCLK gates, releases the documented
USB resets, applies the official PHY tuning, validates `GSNPSID`, and selects
the DWC3 device capability.  It does not start endpoints, drive host VBUS or
perform USB DMA yet.

The generated image is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage4-usb-dwc3-checkpoint/boot.img`
(385024 bytes, SHA-256
`0e56e0b56a8735b0845ad09f9885db82d5ec0ce01e4d5e59d4f8f048834794f7`).
Replace only the SPI-NAND `boot` partition.  The validated Stage 4 SDMMC image
remains the rollback image.

Boot once with the USB data connector disconnected and run:

```text
dmesg
time "sleep 5"
dmesg
ls /dev
free
```

Success requires `USB: DWC3 peripheral checkpoint passed`, an ID whose upper
half is `5533`, `gctl` role bits equal to device (`2`), a PHY/hardware-parameter
line, and no data abort or regression in NAND, SDMMC and the Stage 3 devices.
The Pico Mini data connector is also the normal power input, so an unpowered
"cable disconnected" comparison is not practical.  Its common RV1103 DTS also
sets `rockchip,vbus-always-on`; consequently raw PHY `bvalid=0` is permitted
and is not a failure criterion.  Test with the normal cable connected to the
PC.  At this checkpoint the PC is not expected to enumerate a device yet.
The following checkpoint will add DWC3 event buffers, endpoint zero and CDC
ACM descriptors.

## Stage 4C: DWC3 CDC ACM device

**Deferred after hardware test.**  The controller and PHY checkpoint is
stable, but Windows reports code 43 while fetching the first device
descriptor.  One candidate reached repeated bus reset/ConnectDone events;
the EP0-MPS-corrected candidate did not receive a new reset after Windows had
disabled the failed instance.  CDC is therefore retained as optional source
behind `CONFIG_RV1103_USB_CDCACM`, but disabled in the default `nsh` defconfig.
This keeps the USB power/data connector electrically safe and prevents an
unfinished gadget from disturbing camera and other board validation.  Do not
claim CDC completion until a later USB-focused session passes enumeration and
bidirectional traffic on both Windows and Linux hosts.

This stage adds the DWC3 event ring, endpoint-zero standard/control request
state machine, CDC ACM descriptors and high-speed bulk IN/OUT endpoints.  The
target-side character node is `/dev/ttyACM0`; the PC sees the prototype device
as `1209:1103` and should bind its built-in CDC serial driver.  VID `1209` is
suitable for an open-source development prototype only: allocate a production
VID/PID before shipping hardware.

The candidate image is
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage4-usb-cdc/boot.img`
(399872 bytes, SHA-256
`798335befe23bd2f5568ba7f52bc20fec686f2dfdf95e3d3c16e4ad897966fdc`).
Replace only the SPI-NAND `boot` partition.  Keep the validated DWC3 checkpoint
image as rollback.

After boot, retain the UART console and run:

```text
dmesg
ls /dev
ps
free
```

Success requires `USB CDC: DWC3 gadget started`, `/dev/ttyACM0`, then
`USB CDC: bus reset`, `connected at high speed` and `host set configuration
1` after Windows enumerates the board.  In Windows Device Manager the device
must appear as a COM port without a warning icon.  Test target-to-PC traffic:

```text
echo openvela-rv1103-to-pc > /dev/ttyACM0
```

Send `pc-to-rv1103` from a Windows serial terminal, then read exactly 12 bytes
on the UART NSH console:

```text
dd if=/dev/ttyACM0 of=/data/usb-rx.bin bs=12 count=1
hexdump /data/usb-rx.bin
dmesg
```

The USB data path is deliberately separate from the UART NSH console, so a
CDC disconnect cannot remove the recovery console.  The current character
node returns `EAGAIN` when no receive data is queued; run the `dd` command only
after transmitting from the PC.  Acceptance also requires the existing NAND,
SDMMC, timers, sensors and network devices to remain present with no data abort
or DWC3 endpoint error.

## Stage 7E Fix 5: ISP unity-gain main-path checkpoint

Fix 4 proved that the MI main-path control and Y base address reach their
shadow registers, while ISP input advances one frame.  The missing difference
from Rockchip's `rk_isp_module_init()` was the six AWB gain registers: ISP
control `0x6397` enables this stage, but their reset value is zero.  Fix 5
programs Q8.8 unity gains (`0x01000100`) for all three HDR banks and enables
zero-offset BLS before starting the existing bounded MI test.  It also reports
ISP output-line, MI byte/pixel, crop-shadow and resize-shadow counters.  All
modified registers are saved and restored, so a failed experiment leaves the
validated RKCIF RAW path intact.

Candidate image:
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage7e-isp-mi-fix5-unity/boot.img`
(407040 bytes, SHA-256
`8f63b76060d65f1d9c779babcb0e21c55d2661bbddd9059a994f2b35e4a610f9`).
Replace only the SPI-NAND `boot` partition, then run:

```text
echo micapture > /dev/sc3336
dmesg
ls /dev
```

Success requires `RKISP32 MI capture passed`, non-zero `changed`, `bytes` and
`pixels`, `guard=ok`, and a new `/dev/isp0`.  If it still fails, retain the
entire final diagnostic line; `outln`, `bytes`, `pixels`, `crop` and `rsz`
identify the remaining boundary without another blind register change.

### Fix 6: exact official fast-path order and clean sensor start

Fix 5 reported `outln=0 bytes=0 pixels=0`, while MI control/address shadows,
crop/resize bypass and one ISP input frame were valid.  Fix 6 follows the
active Rockchip RT-Thread `drv_isp3.c` branch more closely: MPFBC `0x502`, full
MI control `0x217a200x`, preliminary ISP control `0x6397`, final
`rk_isp_hw_set_isp_top()` value `0x397`, and debayer enable after the final
top-level update.  It also stops SC3336 before programming the path and starts
it only after every ISP/MI shadow update, matching the official sensor-on
ordering.  The original sensor streaming state is restored after the bounded
test.

Candidate image:
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage7e-isp-mi-fix6-official-sequence/boot.img`
(407040 bytes, SHA-256
`c1472ab8a4be05e46f57c3d972ef88471f6a27ed9abb35c79868c98108205c18`).

### Fix 7: VICAP ID0 ownership and dedicated RAW feeder buffers

Fix 6 produced `frames=0 outln=0 bytes=0 pixels=0`.  Re-reading the complete
official `rk_isp_hw_vicap_init()` exposed the missing ownership step: TOISP0
does not run from the selector alone; MIPI0 ID0 and the VICAP global control
must remain enabled.  The existing RAW checkpoint intentionally disables ID0
after each burst, so earlier ISP checks observed at most one residual frame.

Fix 7 performs a bounded, exclusive transaction.  It stops SC3336, saves the
VICAP/RKCIF state, assigns two private aligned RAW10 ping-pong buffers, enables
the already proven MIPI0 ID0 format plus TOISP0, commits ISP/MI, and only then
restarts the sensor.  It disables ID0 before freeing those buffers and restores
every saved register and the original sensor state afterward.  Retained
`/dev/camera0` and `/dev/video0` buffers are never used as DMA targets.

Candidate image:
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage7e-isp-mi-fix7-vicap-id0/boot.img`
(407552 bytes, SHA-256
`4e8f130b180fe32bc90bad4e3b4a55ed06bc38095cc1109014b74efee068220c`).
The final diagnostic now also includes `cif=...`; bits 8/9 indicate the two
RAW ping-pong frame completions, while bits 16..20 indicate VICAP errors.

## Stage 7F: bounded continuous RKISP32 NV12 and `/dev/video1`

Fix 7 passed on hardware with an exact 4,478,976-byte NV12 frame,
`guard=ok`, MI completion and `/dev/isp0`.  Stage 7F keeps the exclusive
VICAP→ISP transaction active and acknowledges MI frame interrupts until 60
NV12 frames complete or a four-second guard expires.  It records `mi=<frames>`
and `msec=<elapsed>` before restoring the proven RAW path.

The board also registers `/dev/video1` as a V4L2 capture node advertising
2304x1296 `NV12`, while `/dev/video0` remains packed RAW10.  For this bounded
checkpoint, V4L2 stream-on runs the 60-frame transaction and reads expose its
last complete immutable frame.  A later stage will keep the hardware running
behind an interrupt-driven multi-buffer queue instead of ending after the
acceptance burst.

Candidate image:
`cmake_out/luckfox-pico-mini_nsh_build/rv1103-spinand-stage7f-isp-video1-burst/boot.img`
(408576 bytes, SHA-256
`ba91f77002666ea411e54010542408fc482868906eb1bbb3bb213903015c3d8c`).
Replace only `boot`, then run:

```text
ls /dev
echo micapture > /dev/sc3336
dmesg
hexdump /dev/video1 count=64
time "dd if=/dev/video1 of=/dev/null bs=65536 count=69"
echo capture > /dev/sc3336
time "dd if=/dev/video0 of=/dev/null bs=65536 count=61"
free
```

Acceptance requires `MI capture passed`, `mi=60`, approximately two seconds
at the default 30 fps, `/dev/video1`, a non-constant NV12 hexdump, and a
successful post-test RAW capture.  `guard` must remain `ok`, and `cif` bits
16..20 must remain clear.
