from __future__ import annotations

import math
from collections import deque

import cv2
import numpy as np


class MotionVisionDetector:
    """Constant-memory motion and trajectory detector for a fixed camera.

    This detector intentionally emits fall *candidates*, not confirmed falls.
    Monocular foreground geometry cannot reliably distinguish falling from
    sitting, lying down, or bending over. Confirmation belongs to a future
    second-stage model and is never fabricated here.
    """

    def __init__(
        self,
        width: int = 320,
        height: int = 240,
        *,
        fall_enabled: bool = False,
        wave_enabled: bool = True,
    ) -> None:
        self.width = width
        self.height = height
        self.area = width * height
        self.fall_enabled = fall_enabled
        self.wave_enabled = wave_enabled
        self.background = cv2.createBackgroundSubtractorMOG2(
            history=160, varThreshold=28, detectShadows=False
        )
        self.open_kernel = np.ones((3, 3), np.uint8)
        self.close_kernel = np.ones((7, 7), np.uint8)
        self.frames = 0

        self.upright_baseline_y: float | None = None
        self.upright_baseline_time = -1e9
        self.fall_evidence: deque[tuple[float, float, float]] = deque()
        self.last_fall_candidate = -1e9

        self.wave_points: deque[tuple[float, float, float]] = deque()
        self.last_wave_seen = -1e9
        self.last_wave_event = -1e9

    def process(self, frame: np.ndarray, now: float) -> list[dict]:
        self.frames += 1
        if frame.shape[1] != self.width or frame.shape[0] != self.height:
            frame = cv2.resize(
                frame, (self.width, self.height), interpolation=cv2.INTER_AREA
            )
        blurred = cv2.GaussianBlur(frame, (5, 5), 0)
        learning_rate = 0.08 if self.frames < 12 else 0.012
        mask = self.background.apply(blurred, learningRate=learning_rate)
        mask = cv2.morphologyEx(
            mask, cv2.MORPH_OPEN, self.open_kernel, iterations=1
        )
        mask = cv2.morphologyEx(
            mask, cv2.MORPH_CLOSE, self.close_kernel, iterations=1
        )
        if self.frames <= 12:
            return []

        contours, _ = cv2.findContours(
            mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
        )
        components: list[tuple[float, int, int, int, int]] = []
        for contour in contours:
            area = cv2.contourArea(contour)
            if area < 40:
                continue
            x, y, width, height = cv2.boundingRect(contour)
            components.append((area, x, y, width, height))

        events: list[dict] = []
        if self.fall_enabled:
            fall = self._detect_fall_candidate(components, now)
            if fall:
                events.append(fall)
        if self.wave_enabled:
            wave = self._detect_wave(components, now)
            if wave:
                events.append(wave)
        return events

    def _detect_fall_candidate(
        self, components: list[tuple[float, int, int, int, int]], now: float
    ) -> dict | None:
        large = [
            item
            for item in components
            if item[0] >= self.area * 0.004
            and item[3] * item[4] <= self.area * 0.62
        ]
        if not large:
            while self.fall_evidence and now - self.fall_evidence[0][0] > 1.2:
                self.fall_evidence.popleft()
            return None

        _, _x, y, width, height = max(large, key=lambda item: item[0])
        bottom = y + height
        centroid_y = y + height / 2.0
        ratio = width / max(1.0, height)
        box_fraction = width * height / self.area
        upright = height / max(1.0, width) >= 1.25 and box_fraction >= 0.055
        if upright:
            if self.upright_baseline_y is None:
                self.upright_baseline_y = centroid_y
            elif abs(centroid_y - self.upright_baseline_y) <= self.height * 0.08:
                self.upright_baseline_y = (
                    0.90 * self.upright_baseline_y + 0.10 * centroid_y
                )
            self.upright_baseline_time = now

        if self.upright_baseline_y is None:
            return None
        drop = centroid_y - self.upright_baseline_y
        if (
            bottom >= self.height * 0.90
            and drop >= self.height * 0.28
            and now - self.upright_baseline_time <= 3.0
            and box_fraction >= 0.03
        ):
            self.fall_evidence.append((now, drop, ratio))
        while self.fall_evidence and now - self.fall_evidence[0][0] > 1.2:
            self.fall_evidence.popleft()
        if len(self.fall_evidence) < 2 or now - self.last_fall_candidate < 30.0:
            return None

        strongest_drop = max(item[1] for item in self.fall_evidence)
        strongest_ratio = max(item[2] for item in self.fall_evidence)
        self.last_fall_candidate = now
        self.fall_evidence.clear()
        confidence = min(
            0.92,
            0.48
            + min(strongest_drop / self.height, 0.32)
            + min(strongest_ratio / 14.0, 0.10),
        )
        return {
            "type": "fall_candidate",
            "confidence": round(confidence, 3),
            "metrics": {
                "ratio": round(strongest_ratio, 3),
                "drop": round(strongest_drop, 2),
                "safety_level": "candidate_only",
            },
        }

    def _detect_wave(
        self, components: list[tuple[float, int, int, int, int]], now: float
    ) -> dict | None:
        candidates: list[tuple[float, float, float]] = []
        for area, x, y, width, height in components:
            centroid_y = y + height / 2.0
            if not self.area * 0.0006 <= area <= self.area * 0.045:
                continue
            if (
                centroid_y >= self.height * 0.68
                or width > self.width * 0.24
                or height > self.height * 0.32
            ):
                continue
            candidates.append((area, x + width / 2.0, centroid_y))

        if candidates:
            if self.wave_points:
                previous_x, previous_y = self.wave_points[-1][1:]
                _, centroid_x, centroid_y = min(
                    candidates,
                    key=lambda item: (item[1] - previous_x) ** 2
                    + 1.8 * (item[2] - previous_y) ** 2,
                )
                if math.hypot(
                    centroid_x - previous_x, centroid_y - previous_y
                ) > self.width * 0.20:
                    self.wave_points.clear()
            else:
                _, centroid_x, centroid_y = max(candidates)
            self.wave_points.append((now, centroid_x, centroid_y))
            self.last_wave_seen = now
        elif now - self.last_wave_seen > 0.38:
            self.wave_points.clear()

        while self.wave_points and now - self.wave_points[0][0] > 2.4:
            self.wave_points.popleft()
        if len(self.wave_points) < 6 or now - self.last_wave_event < 5.0:
            return None

        points = list(self.wave_points)
        xs = np.asarray([point[1] for point in points])
        ys = np.asarray([point[2] for point in points])
        delta_x = np.diff(xs)
        delta_y = np.diff(ys)
        significant = delta_x[np.abs(delta_x) >= self.width * 0.018]
        directions = np.sign(significant)
        reversals = (
            int(np.sum(directions[1:] * directions[:-1] < 0))
            if len(directions) >= 2
            else 0
        )
        x_span = float(xs.max() - xs.min())
        y_span = float(ys.max() - ys.min())
        horizontal_path = float(np.abs(delta_x).sum())
        vertical_path = float(np.abs(delta_y).sum())
        maximum_step = float(np.abs(delta_x).max()) if len(delta_x) else 0.0
        duration = points[-1][0] - points[0][0]
        if (
            3 <= reversals <= 5
            and self.width * 0.12 <= x_span <= self.width * 0.45
            and horizontal_path >= self.width * 0.52
            and horizontal_path >= max(1.0, vertical_path * 0.75)
            and vertical_path >= horizontal_path * 0.70
            and maximum_step <= self.width * 0.20
            and 0.45 <= duration <= 2.5
        ):
            self.last_wave_event = now
            self.wave_points.clear()
            confidence = min(
                0.97, 0.55 + reversals * 0.06 + x_span / self.width * 0.30
            )
            return {
                "type": "wave",
                "confidence": round(confidence, 3),
                "metrics": {
                    "reversals": reversals,
                    "x_span": round(x_span, 2),
                    "y_span": round(y_span, 2),
                    "horizontal_path": round(horizontal_path, 2),
                    "vertical_path": round(vertical_path, 2),
                    "duration_ms": round(duration * 1000.0),
                },
            }
        return None
