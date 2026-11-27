"""Isolate + de-reverb a vocal. usage: clean_vocal.py <in> <out.wav> [--device mps|cpu]"""
import os,sys,argparse,time
os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK","1"); from pathlib import Path
ap=argparse.ArgumentParser(); ap.add_argument("src"); ap.add_argument("out"); ap.add_argument("--device",default="mps"); a=ap.parse_args()
src,out=(Path(p).expanduser().resolve() for p in (a.src,a.out))
if out.exists(): sys.exit(f"{out} exists")
R=Path(__file__).resolve().parent/"vendor"/"SoulX-Singer"; sys.path.insert(0,str(R)); os.chdir(R)
import numpy as np,soundfile as sf,torch
import preprocess.tools.vocal_separation.model as M
P="pretrained_models/SoulX-Singer-Preprocess/"
torch.nn.Module.half=lambda self:self  # full precision: half() is for CUDA
m=M.VocalSeparator(P+"mel-band-roformer-karaoke/mel_band_roformer_karaoke_becruily.ckpt",P+"mel-band-roformer-karaoke/config_karaoke_becruily.yaml",P+"dereverb_mel_band_roformer/dereverb_mel_band_roformer_anvuew_sdr_19.1729.ckpt",P+"dereverb_mel_band_roformer/dereverb_mel_band_roformer_anvuew.yaml",use_der=True,device=a.device,verbose=False)
t=time.time(); r=m.process(str(src)); y=np.asarray(r.vocals_dereverbed if r.vocals_dereverbed is not None else r.vocals); y=y.mean(0) if y.ndim==2 and y.shape[0]<5 else y.squeeze()
assert np.isfinite(y).all() and abs(y).max()>1e-3,"bad output"
sf.write(out,y,r.sample_rate); print(f"OK cleaned {len(y)/r.sample_rate:.1f}s in {time.time()-t:.0f}s")
