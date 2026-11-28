"""Sing a written melody (from Loopwright) in a voice, with SoulX-Singer's score mode. Runs locally; uploads nothing.

usage: .venv-soulx/bin/python sing_score.py <score.json> <voice_reference.wav> <out.wav> [--device auto|mps|cpu] [--steps 32]
   or: .venv-soulx/bin/python sing_score.py --serve      (stays loaded: one JSON request a line on stdin,
       {"score": ..., "voice": ..., "out": ..., "steps": 24}; answers "DONE <out>" or "FAIL <why>" a line on stdout)

score.json, written by Loopwright: {"segments": [{"start": ms, "notes": [{"word": "hold", "pitch": 62, "dur": 0.43, "slur": false}, ...]}]}
  - one segment per lyric line, its start in ms from the start of the section
  - a note sings one word (a long word can sit on one note); "slur": true holds the word before onto a new note;
    "word": "<SP>" is a rest
The voice clip is read once (its words by Whisper, its notes by RMVPE, its sounds by g2p_en) and remembered next to it
in a hidden .prompts folder next to it. The melody is moved by whole octaves (never in between: it stays in
key) to sit in the voice's range. An existing output file is never overwritten.
"""
import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
ap = argparse.ArgumentParser()
ap.add_argument("score", nargs="?")
ap.add_argument("voice", nargs="?")
ap.add_argument("out", nargs="?")
ap.add_argument("--device", default="auto", choices=["auto", "mps", "cpu"])
ap.add_argument("--steps", type=int, default=32)
ap.add_argument("--serve", action="store_true", help="stay loaded and sing request after request (much quicker after the first)")
a = ap.parse_args()
if not a.serve:
    if not (a.score and a.voice and a.out):
        sys.exit("usage: sing_score.py <score.json> <voice.wav> <out.wav>   (or --serve)")
    if Path(a.out).expanduser().exists():
        sys.exit(f"{a.out} already exists; choose another name")

ROOT = Path(__file__).resolve().parent / "vendor" / "SoulX-Singer"
sys.path.insert(0, str(ROOT))
os.chdir(ROOT)
import librosa  # noqa: E402
import numpy as np  # noqa: E402
import soundfile as sf  # noqa: E402
import torch  # noqa: E402
try:
    from g2p_en import G2p  # noqa: E402
except ImportError:
    sys.exit("g2p_en is missing: run  .venv-soulx/bin/pip install g2p_en  in the helper folder, once")
from cli.inference import build_model  # noqa: E402  (the singing model, not the voice changer)
from preprocess.tools.f0_extraction import F0Extractor  # noqa: E402
from soulxsinger.utils.data_processor import DataProcessor  # noqa: E402
from soulxsinger.utils.file_utils import load_config  # noqa: E402

cfg = load_config("soulxsinger/config/soulxsinger.yaml")
sr = cfg.audio.sample_rate
MODEL = Path("pretrained_models/SoulX-Singer/model.pt")
PHONESET = "soulxsinger/utils/phoneme/phone_set.json"
if not MODEL.is_file():
    sys.exit("The singing model is missing: run  hf download Soul-AILab/SoulX-Singer --local-dir vendor/SoulX-Singer/pretrained_models/SoulX-Singer  "
             "in the helper folder, once")
g2p = G2p()
phoneset = set(json.load(open(PHONESET)))


def phonemes(word):
    """'hold' -> 'en_HH-OW1-L-D' (only sounds the model knows)"""
    w = "".join(c for c in word.lower() if c.isalpha() or c == "'")
    ph = [p for p in g2p(w) if p.strip() and ("en_" + p) in phoneset] if w else []
    return "en_" + "-".join(ph) if ph else None


def meta_of(notes, start_ms=0):
    """notes: [(phoneme or '<SP>', seconds, midi, type)] -> one SoulX segment"""
    durs = [max(0.02, d) for _, d, _, _ in notes]
    return dict(index=f"seg_{start_ms}", language="English", time=[start_ms, start_ms + int(round(sum(durs) * 1000))],
                duration=" ".join(f"{d:.3f}" for d in durs), text=" ".join(p for p, _, _, _ in notes),
                phoneme=" ".join(p for p, _, _, _ in notes), note_pitch=" ".join(str(m) for _, _, m, _ in notes),
                note_type=" ".join(str(t) for _, _, _, t in notes))


def prepare_voice(voice, device):
    """the voice clip, read once: what it sings (words, notes), as the model's prompt"""
    cache = voice.parent / ".prompts"  # (hidden: not offered as a voice)
    cache.mkdir(exist_ok=True)
    pj, pw = cache / (voice.stem + ".prompt.json"), cache / (voice.stem + ".prompt.wav")
    if pj.is_file() and pw.is_file() and pj.stat().st_mtime >= voice.stat().st_mtime:
        return json.loads(pj.read_text()), str(pw)
    from transformers import pipeline
    y, _ = librosa.load(str(voice), sr=sr, mono=True, duration=20)
    asr = pipeline("automatic-speech-recognition", model="openai/whisper-small", device="cpu")
    words = asr({"raw": librosa.resample(y, orig_sr=sr, target_sr=16000), "sampling_rate": 16000},
                return_timestamps="word", generate_kwargs={"language": "en", "task": "transcribe"}).get("chunks", [])
    with tempfile.TemporaryDirectory() as tmp:
        sf.write(f"{tmp}/v.wav", y, sr)
        f0 = np.asarray(F0Extractor("pretrained_models/SoulX-Singer-Preprocess/rmvpe/rmvpe.pt", device=device).process(f"{tmp}/v.wav"),
                        dtype=np.float32).ravel()
    hop = cfg.audio.hop_size / sr
    notes, t, last = [], 0.0, 60
    for w in words:
        s, e = w.get("timestamp", (None, None))
        if s is None or e is None or e <= s or e > 12.0:  # (the first 12 seconds are plenty for the voice)
            if e is not None and e > 12.0:
                break
            continue
        ph = phonemes(w["text"])
        if ph is None:
            continue
        if s > t + 0.02:
            notes.append(("<SP>", s - t, 0, 1))
        seg = f0[int(s / hop):max(int(s / hop) + 1, int(e / hop))]
        seg = seg[seg > 40]
        last = int(round(float(np.median(librosa.hz_to_midi(seg))))) if len(seg) else last
        notes.append((ph, e - s, last, 2))
        t = e
    if len([n for n in notes if n[3] == 2]) < 3:
        sys.exit("Couldn't hear enough words in the voice clip: use 10 to 20 seconds of clear singing or rapping, no music")
    notes.append(("<SP>", 0.2, 0, 1))
    m = meta_of(notes)
    sf.write(str(pw), y[:int((t + 0.2) * sr)], sr)
    pj.write_text(json.dumps(m))
    print(f"voice read: {sum(1 for n in notes if n[3] == 2)} words, {t:.1f}s")
    return m, str(pw)


LOADED = {}   # device -> (DataProcessor, model): loaded once, kept while serving
PROMPTS = {}  # (voice, mtime, device) -> (prompt meta, processed prompt)


def loaded(device):
    if device not in LOADED:
        dp = DataProcessor(hop_size=cfg.audio.hop_size, sample_rate=sr, phoneset_path=PHONESET, device=device)
        LOADED[device] = (dp, build_model(str(MODEL), cfg, device))
    return LOADED[device]


def run(device, score_path, voice, out, steps):
    t0 = time.time()
    dp, model = loaded(device)
    key = (str(voice), voice.stat().st_mtime, device)
    if key not in PROMPTS:
        prompt_meta, prompt_wav = prepare_voice(voice, device)
        PROMPTS[key] = (prompt_meta, dp.process(dict(prompt_meta), prompt_wav))
    prompt_meta, prompt = PROMPTS[key]
    score = json.loads(Path(score_path).read_text())
    segs = score.get("segments", [])
    if not segs:
        raise RuntimeError("the melody is empty")
    # whole octaves to the voice's range (stays in key). A phrase sung again on its own is told the whole melody's
    # middle ("reference_pitch"), so it lands in the same octave as the rest of the take.
    pv = [int(x) for x in prompt_meta["note_pitch"].split() if int(x) > 0]
    tv = [int(n["pitch"]) for s_ in segs for n in s_["notes"] if n.get("word") != "<SP>" and int(n.get("pitch", 0)) > 0]
    mid = float(score["reference_pitch"]) if score.get("reference_pitch") else (float(np.median(tv)) if tv else 0.0)
    shift = min((-24, -12, 0, 12, 24), key=lambda k: abs(mid + k - np.median(pv))) if pv and tv else 0
    print(f"octave: voice around {librosa.midi_to_note(int(np.median(pv)))}, melody around {librosa.midi_to_note(int(mid))} -> shift {shift:+d}", flush=True)
    end_ms = max(int(s_["start"]) + int(round(sum(float(n["dur"]) for n in s_["notes"]) * 1000)) for s_ in segs)
    merged = np.zeros(int((end_ms / 1000 + 1.0) * sr), dtype=np.float32)
    words_sung = 0
    for s_ in segs:
        notes, prev = [], None
        for n in s_["notes"]:
            d = float(n["dur"])
            if n.get("word") == "<SP>" or int(n.get("pitch", 0)) <= 0:
                notes.append(("<SP>", d, 0, 1))
                prev = None
            elif n.get("slur") and prev is not None:
                notes.append((prev, d, int(n["pitch"]) + shift, 3))
            else:
                ph = phonemes(n["word"])
                if ph is None:  # (a word with no sounds we know: a rest instead)
                    notes.append(("<SP>", d, 0, 1))
                    prev = None
                    continue
                notes.append((ph, d, int(n["pitch"]) + shift, 2))
                prev = ph
                words_sung += 1
        notes = [("<SP>", 0.1, 0, 1)] + notes + [("<SP>", 0.15, 0, 1)]  # (a breath either side)
        start = max(0, int(s_["start"]) - 100)
        target = dp.process(meta_of(notes, start), None)
        with torch.no_grad():
            audio = model.infer({"prompt": prompt, "target": target}, auto_shift=False, pitch_shift=0,
                                n_steps=steps, cfg=cfg.infer.cfg, control="score", use_fp16=False)
        y = audio.squeeze().float().cpu().numpy()
        i0 = int(start / 1000 * sr)
        k = min(len(y), len(merged) - i0)
        merged[i0:i0 + k] += y[:k]
    if not np.isfinite(merged).all() or np.abs(merged).max() < 1e-3:
        raise RuntimeError("output is silent or invalid")
    merged *= min(1.0, 0.95 / np.abs(merged).max())  # (only ever turned down, so phrases match the rest of the take)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(out), merged, sr)
    print(f"OK device={device} {len(segs)} lines, {words_sung} words, {time.time() - t0:.0f}s -> {out}", flush=True)


def sing(score_path, voice, out, steps):
    devices = ["mps", "cpu"] if a.device == "auto" else [a.device]
    err = ""
    for d in devices:
        try:
            run(d, score_path, voice, out, steps)
            return
        except SystemExit:
            raise
        except Exception as e:
            err = f"{d} failed: {type(e).__name__}: {str(e)[:300]}"
            print(err, flush=True)
    raise RuntimeError(err or "singing failed on every device")


if a.serve:
    print("READY", flush=True)
    for line in sys.stdin:
        try:
            q = json.loads(line)
            sing(Path(q["score"]), Path(q["voice"]).expanduser().resolve(), Path(q["out"]), int(q.get("steps", a.steps)))
            print("DONE " + str(q["out"]), flush=True)
        except SystemExit as e:
            print("FAIL " + str(e), flush=True)
        except Exception as e:
            print("FAIL " + f"{type(e).__name__}: {str(e)[:300]}", flush=True)
else:
    try:
        sing(Path(a.score).expanduser().resolve(), Path(a.voice).expanduser().resolve(), Path(a.out).expanduser().resolve(), a.steps)
    except RuntimeError as e:
        sys.exit(str(e))
