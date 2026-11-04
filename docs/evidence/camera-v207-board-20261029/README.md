# v207 user board short test, 2026-10-29

`serial.txt` is the unmodified user-supplied transcript. v207 reports cold and
warm same-input equality against legacy for all six output CRCs and detections.
The subsequent 30-second stream completed 154 frames / 154 inferences,
`error=0 cleanup=0 fallback=0`. Overall 4.892 FPS; warm display 5.158 FPS;
loop inference rate 5.117 FPS. Not a 30 FPS or long-soak acceptance.

Approximate per-frame means: preparation 27.0 ms, NPU preparation 33.2 ms
(including package 21.4 ms), hardware+wait 17.1 ms, output/weight verification
55.2 ms, decode 16.8 ms, display 37.9 ms. Consumer capture wait is NOT physical
capture duration. The transcript contains no independent boot image hash.
The prior sealed v207 image/source/checkpoint remain unchanged.
