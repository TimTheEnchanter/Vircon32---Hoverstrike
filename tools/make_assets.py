#!/usr/bin/env python3
"""Generates HOVERSTRIKE's assets: the vector line texture and all sound effects.
Everything here is original. usage: make_assets.py <assets_dir>"""
import math, os, struct, sys, wave
import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "assets"
os.makedirs(OUT, exist_ok=True)

# vector.h wants: x = 0, y = 0..3 solid white; x = 2, y = 0..31 a gaussian strip
img = Image.new("RGBA", (8, 32), (0, 0, 0, 0))
for y in range(4):
    img.putpixel((0, y), (255, 255, 255, 255))
for y in range(32):
    d = (y - 15.5) / 6.0
    img.putpixel((2, y), (255, 255, 255, int(255 * math.exp(-d * d))))
img.save(os.path.join(OUT, "vector_lines.png"))

RATE = 44100
SOUNDS = []   # order = SND_ numbers in main.c
gen = np.random.default_rng(7)

def save(name, x):
    x = np.clip(np.asarray(x, dtype=np.float64), -1, 1)
    pcm = (x * 26000).astype(np.int16)
    st = np.repeat(pcm[:, None], 2, axis=1)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(st.tobytes())
    with open(os.path.join(OUT, name + ".vsnd"), "wb") as f:     # for local testing only
        f.write(b"V32-VSND"); f.write(struct.pack("<I", len(pcm))); f.write(st.tobytes())
    SOUNDS.append(name)

def t(d): return np.arange(int(d * RATE)) / RATE
def ph(freqs): return np.cumsum(freqs) / RATE
def sq(p): return np.where((p % 1.0) < 0.5, 1.0, -1.0)
def tri(p): return 4 * np.abs((p % 1.0) - 0.5) - 1
def env(n, r=0.5):
    e = np.ones(n); a = min(n, 120); e[:a] = np.linspace(0, 1, a)
    k = int(n * r); e[n - k:] *= np.linspace(1, 0, k) ** 1.5
    return e
def crunch(d, hold, vol):
    n = int(d * RATE); holds = np.linspace(hold, hold * 7, n).astype(int); x = np.zeros(n); v = 0
    for i in range(n):
        if i % max(1, holds[i]) == 0: v = gen.uniform(-1, 1)
        x[i] = v
    return vol * x * env(n, 0.95)
def loop(x):
    x = np.asarray(x, dtype=np.float64); x[-1] = x[0]; return x

# laser: a falling buzz, about as long as the beam's sweep
tt = t(0.22); save("laser", 0.24 * sq(ph(2200 - 6000 * tt)) * (0.6 + 0.4 * tri(ph(np.full(len(tt), 60.0)))) * env(len(tt), 0.6))
save("tank_boom", crunch(0.7, 3, 0.55) + 0.18 * tri(ph(np.linspace(220, 50, int(0.7 * RATE)))) * env(int(0.7 * RATE), 0.9))
tt = t(0.35); save("missile", 0.16 * tri(ph(500 + 1500 * tt)) * env(len(tt), 0.4))
tt = t(0.5); save("hit", 0.30 * sq(ph(880 + 300 * sq(ph(np.full(len(tt), 14.0))))) * env(len(tt), 0.5))
save("crash", crunch(1.6, 5, 0.6) + 0.22 * tri(ph(np.linspace(300, 30, int(1.6 * RATE)))) * env(int(1.6 * RATE), 0.9))
jing = np.concatenate([0.20 * sq(ph(np.full(int(0.08 * RATE), f))) * env(int(0.08 * RATE), 0.3) for f in [784, 988, 1175, 1568, 1319, 1568]])
save("rack_clear", jing)
tt = t(0.5); save("hum", loop(0.10 * tri(ph(70 + 6 * np.sin(2 * np.pi * 4 * tt))) + 0.04 * sq(ph(np.full(len(tt), 140.0)))))
save("blip", 0.2 * sq(ph(np.full(int(0.05 * RATE), 880.0))) * env(int(0.05 * RATE)))

with open(os.path.join(OUT, "sound_order.txt"), "w") as f:
    f.write("\n".join(SOUNDS) + "\n")
print("assets ->", OUT, "| sounds:", " ".join(SOUNDS))
