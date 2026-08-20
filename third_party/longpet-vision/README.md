# LongPet local vision runtime

This directory contains LongPet-owned glue and lightweight algorithms. It does
not vendor code or model weights from the two research repositories because
neither inspected repository declares a redistribution license.

## Product behavior

- Wave recognition uses 320x240 MOG2 foreground components plus a bounded
  2.4-second trajectory. Accepted waves wake the Home page unless an emergency
  or reminder alert currently owns the screen.
- Fall recognition is deliberately a `fall_candidate` experiment. It is off by
  default and never raises an emergency. Dataset holdout testing showed that
  monocular foreground geometry confuses several normal actions with falls.
- The worker processes one frame at a time, owns no unbounded queue, reports a
  heartbeat, and exits if Linux `MemAvailable` falls below its safety floor.

## Runtime configuration

The C++ `VisionAdapter` starts `src/vision_worker.py`. Important environment
variables are:

- `LONGPET_VISION_ENABLED=0|1`
- `LONGPET_VISION_CAMERA=0`
- `LONGPET_VISION_WIDTH=320`, `LONGPET_VISION_HEIGHT=240`
- `LONGPET_VISION_FPS=5` (board default; raise to 8 for experiments)
- `LONGPET_VISION_WAVE_ENABLED=0|1`
- `LONGPET_VISION_FALL_ENABLED=0|1` (experimental, default off)
- `LONGPET_VISION_PYTHON=/usr/bin/python3`
- `LONGPET_VISION_ROOT=/path/to/longpet-vision`

Run the constant-memory synthetic check with:

```sh
python3 src/vision_worker.py --self-test
```

OpenCV and NumPy must already be provisioned by the device image. LongPet does
not invoke pip or download dependencies at runtime.
