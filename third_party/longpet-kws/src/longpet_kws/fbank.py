"""Small NumPy implementation of the Kaldi FBank used by WeKWS.

This implements the fixed frontend settings needed by the FSMN-CTC model.  It
keeps PyTorch and torchaudio off the LoongArch64 inference target.
"""

from __future__ import annotations

import numpy as np

SAMPLE_RATE = 16_000
FRAME_LENGTH = 400
FRAME_SHIFT = 160
FFT_SIZE = 512
MEL_BINS = 80


def _make_window() -> np.ndarray:
    # torch.hann_window(400, periodic=False).pow(0.85), Kaldi's Povey window.
    index = np.arange(FRAME_LENGTH, dtype=np.float32)
    window = 0.5 - 0.5 * np.cos(np.float32(2.0 * np.pi) * index / np.float32(FRAME_LENGTH - 1))
    return np.power(window, np.float32(0.85)).astype(np.float32)


def _mel_scale(frequency: np.ndarray | float) -> np.ndarray:
    return np.float32(1127.0) * np.log1p(np.asarray(frequency, dtype=np.float32) / np.float32(700.0))


def _make_mel_banks() -> np.ndarray:
    low_mel = _mel_scale(20.0)
    high_mel = _mel_scale(SAMPLE_RATE / 2)
    delta = (high_mel - low_mel) / np.float32(MEL_BINS + 1)
    bins = np.arange(MEL_BINS, dtype=np.float32)[:, None]
    left = low_mel + bins * delta
    center = left + delta
    right = center + delta

    # Kaldi builds 256 bins and appends a zero weight for the Nyquist bin.
    frequencies = np.arange(FFT_SIZE // 2, dtype=np.float32) * np.float32(SAMPLE_RATE / FFT_SIZE)
    mel = _mel_scale(frequencies)[None, :]
    up = (mel - left) / (center - left)
    down = (right - mel) / (right - center)
    banks = np.maximum(np.float32(0.0), np.minimum(up, down)).astype(np.float32)
    return np.pad(banks, ((0, 0), (0, 1))).astype(np.float32)


POVEY_WINDOW = _make_window()
MEL_BANKS = _make_mel_banks()


def kaldi_fbank(waveform: np.ndarray) -> np.ndarray:
    """Return 80-bin log FBank features for mono int16-scale float samples."""
    wave = np.ascontiguousarray(waveform, dtype=np.float32)
    if wave.size < FRAME_LENGTH:
        return np.empty((0, MEL_BINS), dtype=np.float32)

    frame_count = 1 + (wave.size - FRAME_LENGTH) // FRAME_SHIFT
    shape = (frame_count, FRAME_LENGTH)
    strides = (wave.strides[0] * FRAME_SHIFT, wave.strides[0])
    frames = np.lib.stride_tricks.as_strided(wave, shape=shape, strides=strides).copy()
    frames -= frames.mean(axis=1, keepdims=True, dtype=np.float32)

    previous = np.concatenate((frames[:, :1], frames[:, :-1]), axis=1)
    frames -= np.float32(0.97) * previous
    frames *= POVEY_WINDOW[None, :]

    spectrum = np.fft.rfft(frames, n=FFT_SIZE, axis=1)
    power = np.square(np.abs(spectrum)).astype(np.float32)
    energies = power @ MEL_BANKS.T
    np.maximum(energies, np.finfo(np.float32).eps, out=energies)
    np.log(energies, out=energies)
    return energies.astype(np.float32, copy=False)
