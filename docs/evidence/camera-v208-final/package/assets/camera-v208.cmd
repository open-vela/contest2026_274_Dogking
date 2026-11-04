# v208 exact-Image RAM handoff; NOT a PMIC voltage write or camera stream
echo camera-handoff-v208: loading exact Image and reading PMIC
if ext4load mmc 0:3 40080000 /boot/openvela/a733/Image &&
   itest.l ${filesize} == 6d2268 &&
   crc32 40080000 6d2268 40747f4c &&
   if itest.l *40747f4c == 61e1f07c || itest.l *40747f4c == 7cf0e161; then true; else false; fi &&
   itest.l *40747f40 == 41375057 &&
   itest.l *40747f44 == 19201 &&
   mw.l 40747f48 0 1 &&
   itest.l *40747c80 == 41374d44 &&
   itest.l *40747c84 == 1 &&
   ext4load mmc 0:3 40747c90 /boot/openvela/a733/imx214-1080p.a7cm 294 &&
   itest.l ${filesize} == 294 &&
   mw.l 40747c88 294 1 &&
   mw.l 40747c8c 434d4f31 1 &&
   i2c dev 0 &&
   i2c read 36 0e.1 1 40747f50 &&
   i2c read 36 0f.1 1 40747f51 &&
   i2c read 36 10.1 1 40747f52 &&
   i2c read 36 12.1 1 40747f53 &&
   i2c read 36 20.1 1 40747f54 &&
   i2c read 36 2a.1 1 40747f55 &&
   itest.b *40747f50 == 3 &&
   itest.b *40747f51 == 0 &&
   ext4load mmc 0:3 4fa00000 /boot/dtb/sun60i-a733-cubie-a7z.dtb &&
   echo 'swout1-v208: checking DCDC enable10/voltage12' &&
   md.b 40747f52 2 &&
   itest.b *40747f52 == ff &&
   itest.b *40747f53 == 17 &&
   i2c read 36 11.1 1 40747f4c &&
   echo 'swout1-v208: control11 before (only 17 or1f)' &&
   md.b 40747f4c 1 &&
   if itest.b *40747f4c == 17; then sunxi_axp pmu write 11 1f; elif itest.b *40747f4c == 1f; then true; else false; fi &&
   sleep 1 &&
   i2c dev 0 &&
   i2c read 36 11.1 1 40747f4d &&
   echo 'swout1-v208: control11 readback (requires1f)' &&
   md.b 40747f4d 1 &&
   itest.b *40747f4d == 1f &&
   i2c read 36 12.1 1 40747f4e &&
   echo 'swout1-v208: voltage12 readback (requires17)' &&
   md.b 40747f4e 1 &&
   itest.b *40747f4e == 17 &&
   mw.l 40747f48 434d5031 1; then
  echo 'swout1-v208: VERIFIED; control-before/control-after/DCDC1-config'
  md.b 40747f4c 3 &&
  md.b 40747f50 6 &&
  echo 'camera-handoff-v208: committed; SWOUT1 readback passed; runtime retention still ASSUMED' &&
  booti 40080000 - 4fa00000
else
  echo 'camera-handoff-v208: FAILED; kernel NOT entered by this script; no voltage/media writes; SWOUT1 may remain on'
  false
fi
