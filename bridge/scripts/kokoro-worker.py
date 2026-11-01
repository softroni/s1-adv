#!/usr/bin/env python3
"""Keep Kokoro loaded so spoken replies don't pay its startup cost.

Reads one JSON request per line on stdin and writes one binary frame per request
on stdout: little-endian uint32 byte length, uint32 sample rate, then mono PCM16.
A zero length reports a failed request. Exits when stdin closes, so it can't
outlive the bridge.
"""
import json
import os
import struct
import sys

import numpy as np
from kokoro_onnx import EspeakConfig, Kokoro

PEAK = 0.95


def main():
    settings = json.loads(sys.argv[1])
    # Libraries can print, and stray text on stdout would corrupt the frames.
    frames = os.fdopen(os.dup(1), "wb")
    os.dup2(2, 1)
    espeak = None
    if settings.get("espeakLib") or settings.get("espeakData"):
        espeak = EspeakConfig(lib_path=settings.get("espeakLib"), data_path=settings.get("espeakData"))
    kokoro = Kokoro(settings["model"], settings["voices"], espeak_config=espeak)
    for line in sys.stdin:
        try:
            samples, rate = kokoro.create(json.loads(line)["text"], voice=settings["voice"],
                                          speed=settings["speed"], lang="en-us")
            peak = float(np.abs(samples).max())
            if peak > 0:
                samples = samples * (PEAK / peak)
            pcm = (np.clip(samples, -1, 1) * 32767).astype("<i2").tobytes()
            frames.write(struct.pack("<II", len(pcm), rate) + pcm)
        except Exception as error:
            print(f"kokoro request failed: {type(error).__name__}", file=sys.stderr)
            frames.write(struct.pack("<II", 0, 0))
        frames.flush()


if __name__ == "__main__":
    main()
