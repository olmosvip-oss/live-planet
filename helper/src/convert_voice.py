"""Convert a sung guide into the reference singer's voice (SoulX-Singer-SVC). Runs locally; uploads nothing.

usage: .venv-soulx/bin/python convert_voice.py <guide_audio> <voice_reference> <out.wav> [--device auto|mps|cpu]
                                               [--steps 32] [--seconds N] [--shift 0]
--seconds converts only the first N seconds (for speed tests). An existing output file is never overwritten.
"""
import argparse
import os
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
ap = argparse.ArgumentParser()
ap.add_argument("guide")
ap.add_argument("voice")
ap.add_argument("out")
ap.add_argument("--device", default="auto", choices=["auto", "mps", "cpu"])
ap.add_argument("--steps", type=int, default=32)
ap.add_argument("--seconds", type=float)
ap.add_argument("--tune", type=int, help="tonic 0-11 of a minor key: tune notes to it")
ap.add_argument("--tune-amount", type=float, default=0.85)
ap.add_argument("--runs", type=int, default=0, help="max number of runs to add on long notes (needs --tune)")
ap.add_argument("--raw-pitch", action="store_true", help="skip pitch repair")
ap.add_argument("--shift", default="0", help="semitones (keep to 0 or +/-12 to stay in key), or 'auto': the whole octave that puts "
                                          "the guide nearest the voice's own range (a high AI take sung by a low voice won't sound like a chipmunk)")
a = ap.parse_args()
guide, voice, out = (Path(p).expanduser().resolve() for p in (a.guide, a.voice, a.out))
if out.exists():
    sys.exit(f"{out} already exists; choose another name")

ROOT = Path(__file__).resolve().parent / "vendor" / "SoulX-Singer"
sys.path.insert(0, str(ROOT))
os.chdir(ROOT)
import librosa  # noqa: E402
import numpy as np  # noqa: E402
import soundfile as sf  # noqa: E402
import torch  # noqa: E402
from cli.inference_svc import build_model  # noqa: E402
from preprocess.tools.f0_extraction import F0Extractor  # noqa: E402
from soulxsinger.utils.audio_utils import load_wav  # noqa: E402
from soulxsinger.utils.file_utils import load_config  # noqa: E402

cfg = load_config("soulxsinger/config/soulxsinger.yaml")
sr = cfg.audio.sample_rate


def _runs(v):
    i = 0
    while i < len(v):
        e = i
        while e < len(v) and v[e] == v[i]:
            e += 1
        yield i, e, bool(v[i])
        i = e


def repair(f0, hop=0.02):
    """Fold short octave flips, drop blips, smooth lightly WITHOUT blending neighbouring notes."""
    from scipy.ndimage import median_filter
    m = np.where(f0 > 40, librosa.hz_to_midi(np.maximum(f0, 1)), np.nan)
    seg = []
    for i, e, on in _runs(np.isfinite(m)):
        if on:
            cut = [i] + [k for k in range(i + 1, e) if abs(m[k] - m[k - 1]) > 5] + [e]
            seg += list(zip(cut, cut[1:]))
    flips = 0
    for s0, e0 in seg:
        near = np.r_[m[max(0, s0 - 25):s0], m[e0:e0 + 25]]
        near = near[np.isfinite(near)]
        if len(near) >= 5 and (e0 - s0) * hop <= 0.6:
            d = np.median(m[s0:e0]) - np.median(near)
            if 10.5 < abs(d) < 13.5:
                m[s0:e0] -= 12 * np.sign(d)
                flips += 1
    for i, e, on in list(_runs(np.isfinite(m))):
        if on and (e - i) * hop < 0.06:
            m[i:e] = np.nan
        elif on and e - i > 4:
            m[i:e] = median_filter(m[i:e], 3, mode="nearest")
    print(f"pitch repair: {flips} octave flip(s) folded, range {librosa.midi_to_note(int(np.nanmin(m)))}-{librosa.midi_to_note(int(np.nanmax(m)))}")
    return np.where(np.isfinite(m), librosa.midi_to_hz(np.nan_to_num(m)), 0).astype(np.float32)


def tune_and_run(f0, hop=0.02):
    """Viterbi-segment into scale notes, rebuild the line on those notes (short glides, reduced drift), add runs."""
    from scipy.ndimage import gaussian_filter1d
    m = np.where(f0 > 40, librosa.hz_to_midi(np.maximum(f0, 1)), np.nan)
    sc = np.array([n for n in range(36, 85) if (n - a.tune) % 12 in (0, 2, 3, 5, 7, 8, 10)])
    notes = []
    for i, e, on in list(_runs(np.isfinite(m))):
        if not on:
            continue
        x = m[i:e]
        cost = np.minimum(np.abs(x[:, None] - sc[None, :]), 3.0)
        D, back = cost[0].copy(), np.zeros((len(x), len(sc)), int)
        for k in range(1, len(x)):
            j = int(D.argmin())
            stay, jump = D, D[j] + 4.0  # switching must be earned (~80 ms+ of better fit)
            back[k] = np.where(stay <= jump, np.arange(len(sc)), j)
            D = np.minimum(stay, jump) + cost[k]
        path = [int(D.argmin())]
        for k in range(len(x) - 1, 0, -1):
            path.append(int(back[k, path[-1]]))
        tgt = sc[np.array(path[::-1])].astype(float)
        dev = np.clip(x - tgt, -1.0, 1.0)
        dev -= gaussian_filter1d(dev, 5, mode="nearest")  # keep vibrato, remove being flat/sharp
        m[i:e] = gaussian_filter1d(tgt, 1.2, mode="nearest") + (1 - a.tune_amount) * (x - tgt).clip(-.6, .6) + 0.5 * dev.clip(-.4, .4)
        k = 0
        while k < len(tgt):
            q = k
            while q < len(tgt) and tgt[q] == tgt[k]:
                q += 1
            notes.append((i + k, i + q, int(tgt[k]), q == len(tgt)))
            k = q
    print("tuned", len(notes), "notes")
    made = []
    for s0, e0, n, last in sorted(notes, key=lambda z: z[0] - z[1]):
        if len(made) >= a.runs or (e0 - s0) * hop < 0.45 or any(abs(s0 - q) * hop < 2.0 for q in made):
            continue
        k = int(np.where(sc == n)[0][0])
        long = [sc[k], sc[k + 1], sc[k], sc[k - 1], sc[k - 2], sc[k - 1], sc[k]]
        steps, L = (long, 5) if (e0 - s0) * hop > 0.95 else ([sc[k], sc[k + 1], sc[k], sc[k - 1], sc[k]], 4)
        a0 = e0 - L * len(steps)
        m[a0:e0] = gaussian_filter1d(np.repeat(steps, L).astype(float), 0.9, mode="nearest")
        made.append(s0)
        print(f"run at {a0 * hop:.2f}s:", "-".join(librosa.midi_to_note(int(q)) for q in steps))
    return np.where(np.isfinite(m), librosa.midi_to_hz(np.nan_to_num(m)), 0).astype(np.float32)


def run(device, tmp):
    t0 = time.time()
    pe = F0Extractor("pretrained_models/SoulX-Singer-Preprocess/rmvpe/rmvpe.pt", device=device)
    wavs, f0s = [], []
    for k, src in enumerate((voice, guide)):
        y, _ = librosa.load(src, sr=sr, mono=True, duration=a.seconds if k else 30)
        p = f"{tmp}/{k}.wav"
        sf.write(p, y, sr)
        f0 = np.asarray(pe.process(p), dtype=np.float32).ravel()
        if k and not a.raw_pitch:
            f0 = repair(f0)
            if a.tune is not None:
                f0 = tune_and_run(f0)
        f0s.append(torch.from_numpy(f0).unsqueeze(0).to(device))
        wavs.append(load_wav(p, sr).to(device))
    shift = 0
    if a.shift == "auto":
        # whole octaves only (stays in key): the guide's middle note moved as near as it can get to the voice's
        def mid(f):
            v = f.squeeze().float().cpu().numpy()
            v = v[v > 40]
            return float(np.median(librosa.hz_to_midi(v))) if len(v) else None
        mv, mg = mid(f0s[0]), mid(f0s[1])
        if mv is not None and mg is not None:
            shift = min((-24, -12, 0, 12, 24), key=lambda s_: abs(mg + s_ - mv))
            print(f"octave: voice around {librosa.midi_to_note(int(round(mv)))}, guide around {librosa.midi_to_note(int(round(mg)))} -> shift {shift:+d}")
    else:
        shift = int(a.shift)
    model = build_model("pretrained_models/SoulX-Singer/model-svc.pt", cfg, device)
    t1 = time.time()
    with torch.no_grad():
        audio, shift = model.infer(pt_wav=wavs[0], gt_wav=wavs[1], pt_f0=f0s[0], gt_f0=f0s[1],
                                   auto_shift=False, pitch_shift=shift, n_steps=a.steps, cfg=3.0,
                                   use_fp16=False)
    y = audio.squeeze().float().cpu().numpy()
    if not np.isfinite(y).all() or np.abs(y).max() < 1e-3:
        raise RuntimeError("output is silent or invalid")
    y = y * min(1.0, 0.95 / np.abs(y).max())  # never clip
    out.parent.mkdir(parents=True, exist_ok=True)
    sf.write(out, y, sr)
    dur = len(y) / sr
    print(f"OK device={device} setup={t1 - t0:.0f}s convert={time.time() - t1:.0f}s audio={dur:.1f}s "
          f"speed={(time.time() - t1) / dur:.1f}x-realtime pitch_shift={shift} peak={np.abs(y).max():.2f} -> {out}")


devices = ["mps", "cpu"] if a.device == "auto" else [a.device]
for d in devices:
    try:
        with tempfile.TemporaryDirectory() as tmp:
            run(d, tmp)
        break
    except Exception as e:
        print(f"{d} failed: {type(e).__name__}: {str(e)[:300]}")
else:
    sys.exit("conversion failed on every device")
