# Explicit microphone/echo qualification only. Never run automatically at boot.
# Copy to /data/models/v182-voice-test.nsh, then: sh /data/models/v182-voice-test.nsh
echo v182 voice transport qualification - NOT complete ASR/TTS
npu info
npu selftest
aipet audio output i2s
aipet audio volume 25
aipet audio gain 5
echo Keep quiet during the next 1000 ms
sleep 2
aipet audio stream-features 1000
echo Speak or clap during the next 3000 ms; playback follows capture
sleep 2
aipet audio stream-echo 3000
echo Speak briefly then stop; diagnostic endpoint max 8000 ms
sleep 2
aipet audio stream-endpoint 8000 400
aipet audio local-status
free
echo End - return complete serial output and describe echo sound
