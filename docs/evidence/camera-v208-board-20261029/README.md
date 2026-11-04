# v208-r2 user short test, 2026-10-29

serial.txt is the byte-exact user transcript, SHA256
dabfb6f5cfb7ce24bdadc1ba8eb1024ce76fc34bed5008ba8b7d04b664cd2b2a.
Cold/warm six-head CRC and detection equality against legacy passed.
Serial stream: 193 frames/inferences, warm 6.455 FPS, overall 6.121.
Two-slot pipe: 231 frames/inferences, warm 7.733 FPS, overall 7.324.
Both complete with error/cleanup/fallback zero. Pipe produced232/consumed231,
one unfinished slot discarded at stop; no runtime skip is inferred.

Important: requested CPU6/7 read back CPU1/A55, affinity-error=19 and
crc=portable. A76 placement and hardware CRC did NOT qualify in this test.
Timer 24MHz is not CPU frequency. No 30FPS, long soak, physical frequency
or independent boot-image hash is proved by this transcript.
