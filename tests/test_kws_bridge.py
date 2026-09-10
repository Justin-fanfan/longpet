"""Bridge lifecycle tests; fake capture is NOT acoustic recognition validation."""
import importlib.util
import io
import json
import queue
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "components/longpet-kws/src"))
from longpet_kws.vad import EnergyVad

spec = importlib.util.spec_from_file_location("bridge", ROOT / "deploy/kws/longpet_kws_bridge.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class Detector:
    loads = 0

    def __init__(self, model, tokens, thresholds):
        Detector.loads += 1
        self.thresholds = thresholds

    def reset_stream(self):
        pass

    def accept(self, audio):
        return []


class FakeCapture:
    starts = 0
    stops = 0

    def __init__(self, *args):
        self.process = types.SimpleNamespace(wait=lambda timeout: 0, stdout=io.BytesIO())
        self.thread = types.SimpleNamespace(join=lambda timeout: None)

    def start(self):
        FakeCapture.starts += 1

    def stop(self):
        FakeCapture.stops += 1


class ClosedQueue(queue.Queue):
    created = 0
    closed = 0

    def __init__(self, maxsize):
        super().__init__(maxsize)
        ClosedQueue.created += 1

    def close(self):
        ClosedQueue.closed += 1

    def cancel_join_thread(self):
        pass


class Process:
    dead = False

    def __init__(self, *args, **kwargs):
        self.alive = False

    def start(self):
        self.alive = not Process.dead

    def is_alive(self):
        return self.alive

    def join(self, timeout):
        self.alive = False

    def close(self):
        pass


class BridgeTests(unittest.TestCase):
    def setUp(self):
        Detector.loads = FakeCapture.starts = FakeCapture.stops = 0
        ClosedQueue.created = ClosedQueue.closed = 0
        Process.dead = False
        self.events = []
        self.runtime = types.ModuleType("longpet_kws.cli")
        self.runtime.FsmnKws = Detector
        self.runtime.ArecordCapture = FakeCapture
        self.runtime.resample_block = lambda x, rate: x
        self.runtime.sounddevice_capture_worker = lambda *args: None
        self.runtime.KEYWORD_ALIASES = {
            "小龙小龙": ["小龙小龙"], "你好": ["你好"],
            "陪我说话": ["陪我说话"], "救命": ["救命"],
        }

    def run_bridge(self, commands=(), backend="arecord", check=False):
        args = types.SimpleNamespace(
            kws_root=str(ROOT / "components/longpet-kws"), model="model", tokens="tokens",
            capture_backend=backend, alsa_device="test-card", device=None,
            input_samplerate=48000, wake_threshold=.15, nihao_threshold=.1,
            peiwoshuohua_threshold=.05, jiuming_threshold=.05, command_threshold=.05,
            vad_threshold_db=-60, vad_noise_ratio=2.5, start_paused=True, check_model=check)
        package = types.ModuleType("longpet_kws")
        package.cli = self.runtime
        vad = types.ModuleType("longpet_kws.vad")
        vad.EnergyVad = lambda **kw: None
        modules = {"longpet_kws": package, "longpet_kws.cli": self.runtime,
                   "longpet_kws.vad": vad}
        context = types.SimpleNamespace(Queue=ClosedQueue, Process=Process,
                                        Event=lambda: types.SimpleNamespace(set=lambda: None))

        def reader(target):
            for item in commands:
                target.put(item)

        def thread(*, target, args, **kwargs):
            return types.SimpleNamespace(start=lambda: target(*args))

        with patch.dict(sys.modules, modules), patch.object(bridge, "parse_args", return_value=args), \
             patch.object(bridge, "emit", side_effect=lambda event, **kw: self.events.append((event, kw))), \
             patch.object(bridge, "command_reader", side_effect=reader), \
             patch.object(bridge.threading, "Thread", side_effect=thread), \
             patch.object(bridge.mp, "get_context", return_value=context):
            bridge.main()

    def test_command_protocol_ids_and_eof(self):
        command = {"protocol": "longpet-kws", "version": 1, "command": "pause", "command_id": 7}
        target = queue.Queue()
        with patch.object(sys, "stdin", io.StringIO(json.dumps(command) + "\ninvalid\n")), \
             patch.object(bridge, "emit") as output:
            bridge.command_reader(target)
        self.assertEqual(target.get_nowait(), ("pause", 7))
        self.assertEqual(target.get_nowait(), ("stop", 0))
        self.assertEqual(output.call_count, 1)

    def test_vocabulary_is_supported_by_token_table(self):
        self.run_bridge(check=True)
        tokens = {line.rsplit(maxsplit=1)[0] for line in
                  (ROOT / "components/longpet-kws/assets/fsmn/tokens.txt").read_text(encoding="utf-8").splitlines()}
        missing = {char for aliases in self.runtime.KEYWORD_ALIASES.values()
                   for alias in aliases for char in alias if char not in tokens}
        self.assertEqual(missing, set())
        self.assertEqual(self.runtime.KEYWORD_ALIASES["返回主页"], ["反回主页"])
        self.assertEqual(len(self.runtime.KEYWORDS), 11)
        self.assertEqual(FakeCapture.starts, 0)
        self.assertFalse(self.events[0][1]["acoustic_accuracy_verified"])

    def test_start_paused_never_opens_capture(self):
        self.run_bridge([("pause", 1), ("stop", 2)])
        self.assertEqual(FakeCapture.starts, 0)
        self.assertTrue(self.events[0][1]["paused"])
        self.assertEqual(self.events[1], ("paused", {"command_id": 1}))

    def test_arecord_repeated_pause_resume_keeps_model(self):
        commands = [(command, n * 2 + offset) for n in range(30)
                    for command, offset in [("resume", 1), ("pause", 2)]] + [("stop", 61)]
        self.run_bridge(commands)
        self.assertEqual(Detector.loads, 1)
        self.assertEqual(FakeCapture.starts, 30)
        self.assertEqual(FakeCapture.stops, 30)
        self.assertEqual(sum(event == "paused" for event, _ in self.events), 30)

    def test_sounddevice_multiprocessing_queues_closed(self):
        commands = [(command, n * 2 + offset) for n in range(30)
                    for command, offset in [("resume", 1), ("pause", 2)]] + [("stop", 61)]
        self.run_bridge(commands, backend="sounddevice")
        self.assertEqual(ClosedQueue.created, 60)
        self.assertEqual(ClosedQueue.closed, 60)
        self.assertEqual(Detector.loads, 1)

    def test_repeated_resume_acknowledges_latest_command(self):
        self.run_bridge([("resume", 1), ("resume", 2), ("pause", 3), ("stop", 4)])
        self.assertEqual([data["command_id"] for event, data in self.events
                          if event == "resumed"], [1, 2])
        self.assertEqual(FakeCapture.starts, 1)

    def test_dead_capture_fails_and_cleans_queues(self):
        Process.dead = True
        with self.assertRaisesRegex(RuntimeError, "child exited"):
            self.run_bridge([("resume", 1)], backend="sounddevice")
        self.assertEqual(ClosedQueue.created, ClosedQueue.closed)

    def test_adaptive_vad_gates_stationary_noise_and_releases_after_speech(self):
        vad = EnergyVad(threshold_db=-60.0, noise_ratio=2.5,
                        trigger_ms=60, hangover_ms=500)
        ambient = np.tile(np.asarray([0.004, -0.004], dtype=np.float32), 160)
        speech = np.tile(np.asarray([0.04, -0.04], dtype=np.float32), 160)
        emitted = []
        for _ in range(100):
            emitted.extend(vad.accept(ambient))
        self.assertEqual(emitted, [])
        self.assertFalse(vad.active)
        self.assertGreater(vad.effective_threshold_db, -42.0)

        for _ in range(5):
            emitted.extend(vad.accept(speech))
        self.assertTrue(vad.active)
        self.assertTrue(any(chunk.speech_started for chunk in emitted))
        for _ in range(25):
            emitted.extend(vad.accept(ambient))
        self.assertFalse(vad.active)
        self.assertTrue(any(chunk.speech_ended for chunk in emitted))


if __name__ == "__main__":
    unittest.main()
