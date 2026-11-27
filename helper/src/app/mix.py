"""usage: mix.py <instrumental> <vocal> <out>  (simple level-matched preview mix)"""
import os, sys
import librosa, numpy as np, soundfile as sf
if os.path.exists(sys.argv[3]):
    sys.exit("exists")
g = np.atleast_2d(librosa.load(sys.argv[1], sr=48000, mono=False)[0])
v = librosa.load(sys.argv[2], sr=48000)[0]
n = g.shape[-1]
v = np.pad(v[:n], (0, max(0, n - len(v))))
act = v[np.abs(v) > .01]
v = v / (np.sqrt((act ** 2).mean()) + 1e-9) * np.sqrt((g ** 2).mean()) * 1.4
m = g * .8 + v
sf.write(sys.argv[3], (m * .89 / max(np.abs(m).max(), 1e-9)).T, 48000)
