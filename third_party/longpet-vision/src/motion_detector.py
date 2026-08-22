from __future__ import annotations

import math
from collections import deque

import cv2
import numpy as np
import mediapipe as mp  # NEW: 导入 MediaPipe


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
        pose_enabled: bool = True,           # NEW: 新增姿态估计开关
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

        # NEW: 姿态估计器初始化
        self.pose_enabled = pose_enabled
        if pose_enabled:
            self.mp_pose = mp.solutions.pose
            self.pose = self.mp_pose.Pose(
                static_image_mode=False,
                model_complexity=1,
                min_detection_confidence=0.5,
                min_tracking_confidence=0.5
            )
        else:
            self.pose = None

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

        # NEW: 调用姿态估计，将结果附加到事件或单独生成 pose 事件
        if self.pose_enabled and self.pose is not None:
            pose_data = self._extract_pose(frame)
            if pose_data:
                if events:
                    events[0]["pose"] = pose_data
                else:
                    events.append({
                        "type": "pose",
                        "confidence": pose_data.get("confidence", 1.0),
                        "timestamp": now,
                        "source": "mediapipe_pose",
                        "pose": pose_data,
                    })

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

    # NEW: 新增姿态提取方法（整个方法都是新增的）
    def _extract_pose(self, frame: np.ndarray) -> dict | None:
        """提取两髋关键点，返回距离、中点偏移、图像尺寸（算法处理分辨率）"""
        h, w, _ = frame.shape
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self.pose.process(rgb)
        if not results.pose_landmarks:
            return None
        landmarks = results.pose_landmarks.landmark
        left_hip = landmarks[23]
        right_hip = landmarks[24]
        lx = int(left_hip.x * self.width)
        ly = int(left_hip.y * self.height)
        rx = int(right_hip.x * self.width)
        ry = int(right_hip.y * self.height)
        distance = math.hypot(rx - lx, ry - ly)
        cx = (lx + rx) / 2.0
        cy = (ly + ry) / 2.0
        center_x = self.width / 2.0
        center_y = self.height / 2.0
        return {
            "distance": round(distance, 2),
            "dx": round(cx - center_x, 2),
            "dy": round(cy - center_y, 2),
            "width": self.width,
            "height": self.height,
            "confidence": round((left_hip.visibility + right_hip.visibility) / 2.0, 3),
        }