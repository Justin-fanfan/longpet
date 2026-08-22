#!/bin/sh

echo "=== CPU ==="
nproc
cat /proc/cpuinfo

echo "=== CPU FREQ ==="
for x in \
    /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq \
    /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq \
    /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
do
    echo "$x"
    cat "$x" 2>/dev/null || true
done

echo "=== PYTHON ==="
python3 --version

echo "=== PACKAGES ==="
python3 -m pip show sounddevice sherpa-onnx numpy 2>/dev/null || true

echo "=== ALSA ==="
arecord -l

echo "=== PORTAUDIO ==="
python3 -m sounddevice
