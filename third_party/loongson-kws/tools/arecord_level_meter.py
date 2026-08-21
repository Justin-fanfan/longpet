import subprocess
import numpy as np

RATE = 44100
CHANNELS = 2
FRAMES = 8820
BYTES = FRAMES * CHANNELS * 2


def read_exact(stream, size):
    buf = bytearray()

    while len(buf) < size:
        data = stream.read(size - len(buf))

        if not data:
            return None

        buf.extend(data)

    return bytes(buf)


cmd = [
    "arecord",
    "-M",
    "-D", "hw:0,1",
    "-t", "raw",
    "-f", "S16_LE",
    "-c", "2",
    "-r", "44100",
    "-q",
]

print("Recording... Press Ctrl+C to stop.")

p = subprocess.Popen(
    cmd,
    stdout=subprocess.PIPE,
    bufsize=0,
)

try:
    while True:
        raw = read_exact(p.stdout, BYTES)

        if raw is None:
            print("arecord stopped")
            break

        pcm = np.frombuffer(raw, dtype="<i2")
        pcm = pcm.reshape(-1, 2).astype(np.float32) / 32768.0

        left = pcm[:, 0]
        right = pcm[:, 1]

        l_peak = np.max(np.abs(left))
        r_peak = np.max(np.abs(right))

        l_rms = np.sqrt(np.mean(left * left))
        r_rms = np.sqrt(np.mean(right * right))

        print(
            f"L peak={l_peak:.4f} rms={l_rms:.4f} | "
            f"R peak={r_peak:.4f} rms={r_rms:.4f}",
            flush=True,
        )

except KeyboardInterrupt:
    print("\nStopped")

finally:
    if p.poll() is None:
        p.terminate()
        p.wait()