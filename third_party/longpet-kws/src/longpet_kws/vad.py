"""Dependency-free adaptive energy voice activity detector."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

import numpy as np


@dataclass
class VadChunk:
    audio: np.ndarray
    speech_started: bool = False
    speech_ended: bool = False


class EnergyVad:
    """Gate audio using frame RMS, an adaptive noise floor and hangover."""

    def __init__(
        self,
        sample_rate: int = 16_000,
        frame_ms: int = 20,
        threshold_db: float = -50.0,
        noise_ratio: float = 2.5,
        trigger_ms: int = 60,
        pre_roll_ms: int = 300,
        hangover_ms: int = 500,
    ):
        self.frame_samples = sample_rate * frame_ms // 1000
        self.absolute_threshold = float(10.0 ** (threshold_db / 20.0))
        self.noise_ratio = noise_ratio
        self.trigger_frames = max(1, trigger_ms // frame_ms)
        self.hangover_frames = max(1, hangover_ms // frame_ms)
        pre_roll_frames = max(self.trigger_frames, pre_roll_ms // frame_ms)

        self.noise_floor = self.absolute_threshold / self.noise_ratio
        self._sample_buffer = np.empty(0, dtype=np.float32)
        self._pre_roll: deque[np.ndarray] = deque(maxlen=pre_roll_frames)
        self._voiced_frames = 0
        self._silent_frames = 0
        self._active = False

    @property
    def active(self) -> bool:
        return self._active

    def _is_voiced(self, frame: np.ndarray) -> bool:
        centered = frame - np.mean(frame, dtype=np.float32)
        rms = float(np.sqrt(np.mean(centered * centered, dtype=np.float32)))
        threshold = max(self.absolute_threshold, self.noise_floor * self.noise_ratio)
        voiced = rms >= threshold
        if not self._active and not voiced:
            # Learn stationary background slowly, but never let one loud frame
            # immediately redefine the noise floor.
            limited = min(rms, threshold)
            self.noise_floor = 0.98 * self.noise_floor + 0.02 * limited
        return voiced

    def accept(self, audio: np.ndarray) -> list[VadChunk]:
        samples = np.concatenate((self._sample_buffer, np.asarray(audio, dtype=np.float32)))
        frame_count = len(samples) // self.frame_samples
        self._sample_buffer = samples[frame_count * self.frame_samples :]
        if frame_count == 0:
            return []

        output: list[VadChunk] = []
        emitted: list[np.ndarray] = []
        speech_started = False
        for offset in range(0, frame_count * self.frame_samples, self.frame_samples):
            frame = samples[offset : offset + self.frame_samples]
            voiced = self._is_voiced(frame)

            if not self._active:
                self._pre_roll.append(frame.copy())
                self._voiced_frames = self._voiced_frames + 1 if voiced else 0
                if self._voiced_frames < self.trigger_frames:
                    continue
                self._active = True
                self._silent_frames = 0
                speech_started = True
                emitted.extend(self._pre_roll)
                self._pre_roll.clear()
                continue

            emitted.append(frame.copy())
            self._silent_frames = 0 if voiced else self._silent_frames + 1
            if self._silent_frames < self.hangover_frames:
                continue

            output.append(VadChunk(np.concatenate(emitted), speech_started, True))
            emitted = []
            speech_started = False
            self._active = False
            self._voiced_frames = 0
            self._silent_frames = 0
            self._pre_roll.clear()

        if emitted:
            output.append(VadChunk(np.concatenate(emitted), speech_started, False))
        return output
