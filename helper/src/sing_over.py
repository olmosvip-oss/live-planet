"""Sing lyrics over an instrumental with ACE-Step 1.5 (XL base when downloaded, else base; 'lego' vocals). Local only.
usage: vendor/ACE-Step-1.5/.venv/bin/python sing_over.py <instrumental.wav> <lyrics.txt> <out_dir> --bpm N --key "C minor" [--style "..."] [--seed N] [--steps 50] [--model auto]
"""
import argparse, os, sys, time
from pathlib import Path
ap = argparse.ArgumentParser()
ap.add_argument("instrumental"); ap.add_argument("lyrics"); ap.add_argument("out_dir")
ap.add_argument("--bpm", type=int, required=True); ap.add_argument("--key", required=True)
ap.add_argument("--style", default="dry intimate R&B lead vocal, melodic, emotional, clear diction")
ap.add_argument("--seed", type=int, default=1); ap.add_argument("--takes", type=int, default=1); ap.add_argument("--steps", type=int, default=50)
ap.add_argument("--track", default="vocals", help="which part to add: vocals, backing_vocals, drums, bass, guitar, keyboard, percussion, strings, synth, brass, woodwinds, fx")
ap.add_argument("--model", default="auto", help="auto (XL base when it's downloaded, else base), acestep-v15-xl-base or acestep-v15-base")
ap.add_argument("--lowmem", default="auto", choices=["auto", "on", "bf16", "off"],
                help="XL on a Mac: on = one copy of the model (no MLX copy), about 20 GB instead of 38; bf16 = also half precision "
                     "(crashes on Apple GPUs today); auto = on for XL")
a = ap.parse_args()
inst, lyr, out = (Path(p).expanduser().resolve() for p in (a.instrumental, a.lyrics, a.out_dir))
ROOT = Path(__file__).resolve().parent / "vendor" / "ACE-Step-1.5"
sys.path.insert(0, str(ROOT)); os.chdir(ROOT)
import soundfile as sf
from acestep.handler import AceStepHandler
from acestep.inference import GenerationConfig, GenerationParams, generate_music
dur = sf.info(str(inst)).duration
model = a.model
if model == "auto":  # the bigger XL model when it's there (better melodies and words), else the base one
    xl = ROOT / "checkpoints" / "acestep-v15-xl-base"
    model = "acestep-v15-xl-base" if xl.is_dir() and any(xl.glob("*.safetensors")) else "acestep-v15-base"
print("model:", model, flush=True)
lowmem = a.lowmem in ("on", "bf16") or (a.lowmem == "auto" and "xl" in model)
h = AceStepHandler(); t = time.time()
print("init:", str(h.initialize_service(project_root=str(ROOT), config_path=model, device="auto", use_mlx_dit=not lowmem))[:300])
if a.lowmem == "bf16" and str(getattr(h, "device", "")) == "mps":
    import torch
    h.model = h.model.to(torch.bfloat16); h.dtype = torch.bfloat16  # (half the memory; Apple GPUs run bfloat16)
    torch.mps.empty_cache()
    print("lowmem: bfloat16", flush=True)
elif lowmem:
    print("lowmem: one copy of the model", flush=True)
out.mkdir(parents=True, exist_ok=True)
for seed in range(a.seed, a.seed + a.takes):
    t1 = time.time()
    sung = a.track in ("vocals", "backing_vocals")
    p = GenerationParams(task_type="lego", src_audio=str(inst), instruction=f"Generate the {a.track.upper()} track based on the audio context:",
                         caption=a.style, lyrics=lyr.read_text() if sung else "[Instrumental]", vocal_language="en", bpm=a.bpm, keyscale=a.key,
                         timesignature="4", duration=dur, inference_steps=a.steps, seed=seed, thinking=False,
                         repainting_start=0.0, repainting_end=-1)
    r = generate_music(h, None, p, GenerationConfig(batch_size=1, audio_format="wav", use_random_seed=False, seeds=[seed]), save_dir=str(out))
    for x in r.audios:
        os.replace(x["path"], out / f"take_{seed}.wav")
    print("TAKE", seed, "ok" if r.success else r.error, f"{time.time()-t1:.0f}s for {dur:.1f}s", flush=True)
