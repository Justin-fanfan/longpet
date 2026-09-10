# Vision board artifacts

The current checked-in runtime baseline is Vision V1.1. Vision V1.2 has passed
the documented PC evaluation but remains a candidate pending user board
acceptance; it is not present in `third_party/tinyissimo-yolo/models/` and does
not replace the default path below.

Runtime files:

- `/home/longpet/LongPet` — application, built with `LONGPET_ENABLE_VISION=ON`
- `/home/longpet/LongPetVisionBench` — optional standalone benchmark
- `/home/longpet/models/tinyissimo-yolo-v1-small-person-128.onnx` — static
  person-only FP32 model

The repository service example keeps Vision disabled. Benchmark a candidate on
the target before enabling the drop-in in this directory. `LONGPET_VISION_DETECTOR`
accepts `tinyissimo` or `fastestdet`; both use the same `VisionDetectorPort`,
camera source, latest-frame-only service, and benchmark executable.

Example:

```sh
/home/longpet/LongPetVisionBench \
  --detector tinyissimo \
  --model /home/longpet/models/tinyissimo-yolo-v1-small-person-128.onnx \
  --image /tmp/person-test.jpg --warmup 10 --iterations 100
```

Do not enable both a standalone camera benchmark and a video call at the same
time. The production service pauses Vision during video calls, but the standalone
tool intentionally has no access to that application-level call state.
