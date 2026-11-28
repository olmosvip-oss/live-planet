"""Score sung takes against lyrics + minor key. usage: score_takes.py <lyrics> <tonic 0-11> <wav...>"""
import sys,re,json,warnings; warnings.filterwarnings("ignore")
import numpy as np,librosa
from difflib import SequenceMatcher
from transformers import pipeline
W=lambda t: re.findall(r"[a-z']+",re.sub(r"\[.*?\]","",t).lower())
L=W(open(sys.argv[1]).read()); ton=int(sys.argv[2])
asr=pipeline("automatic-speech-recognition",model="openai/whisper-small",device="cpu",chunk_length_s=30); out=[]
for f in sys.argv[3:]:
    y,_=librosa.load(f,sr=16000); t=asr({"raw":y,"sampling_rate":16000},generate_kwargs={"language":"en","task":"transcribe"})["text"]
    ok=sum(b.size for b in SequenceMatcher(None,L,W(t),autojunk=False).get_matching_blocks())/len(L)
    f0,v,_=librosa.pyin(y,fmin=80,fmax=900,sr=16000); m=np.round(librosa.hz_to_midi(f0[v])).astype(int)
    key=float(np.isin((m-ton)%12,[0,2,3,5,7,8,10]).mean()) if len(m) else 0
    out.append((round(.75*ok+.25*key,3),f)); print(f"SCORE {out[-1][0]} words {ok:.0%} key {key:.0%} median {librosa.midi_to_note(int(np.median(m)))} {f.split('/')[-1]} | {t[:230]}",flush=True)
json.dump([f for _,f in sorted(out,reverse=True)],open("/tmp/song_maker_rank.json" if len(sys.argv)<3 else sys.argv[1]+".rank.json","w"))
