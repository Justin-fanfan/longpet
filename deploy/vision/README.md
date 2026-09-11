# Vision board artifacts

Vision V2.0 uses the frozen V1.2 domain-finetuned detector from
`D:\LongPet-Vision-Final-Handoff\03_models\V1.2_FINAL` and a lightweight
sparse Lucas-Kanade tracker. Training artifacts remain outside this repository.

Runtime files:

- `/home/longpet/LongPet` — application, built with `LONGPET_ENABLE_VISION=ON`
- `/home/longpet/LongPetVisionBench` — optional standalone benchmark
- `/home/longpet/models/tinyissimo-person-128-longpet-v1.onnx` — frozen V1.2
  static person-only FP32 model (SHA256 `cb3defed...f822f89c`)

The repository service example keeps Vision disabled. Benchmark a candidate on
the target before enabling the drop-in in this directory. `LONGPET_VISION_DETECTOR`
accepts `tinyissimo` or `fastestdet`; both use the same `VisionDetectorPort`,
camera source, latest-frame-only service, and benchmark executable.

Example:

```sh
/home/longpet/LongPetVisionBench \
  --detector tinyissimo \
  --model /home/longpet/models/tinyissimo-person-128-longpet-v1.onnx \
  --image /tmp/person-test.jpg --warmup 10 --iterations 100
```

Detector + tracker camera benchmark:

```sh
/home/longpet/LongPetVisionBench \
  --detector tinyissimo \
  --model /home/longpet/models/tinyissimo-person-128-longpet-v1.onnx \
  --camera /dev/video0 --tracking --warmup 1 --duration 60 \
  --tracker-interval-ms 100 --correction-ms 8000
```

Do not enable both a standalone camera benchmark and a video call at the same
time. The production service pauses Vision during video calls, but the standalone
tool intentionally has no access to that application-level call state.
