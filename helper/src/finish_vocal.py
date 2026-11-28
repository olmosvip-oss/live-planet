"""Gentle high-pass + downward expander so gaps go quiet. usage: finish_vocal.py <in> <out>"""
import sys,os,numpy as np,librosa,soundfile as sf
from scipy.signal import butter,sosfiltfilt
from scipy.ndimage import gaussian_filter1d,maximum_filter1d
if os.path.exists(sys.argv[2]): sys.exit("exists")
y,sr=sf.read(sys.argv[1]); y=sosfiltfilt(butter(2,75,"hp",fs=sr,output="sos"),y); h=sr//100
r=20*np.log10(librosa.feature.rms(y=y,frame_length=4*h,hop_length=h)[0]+1e-7); ref=np.percentile(r,90); o=maximum_filter1d((r>ref-28).astype(float),13)  # 60 ms either side
g=10**(-24*(1-gaussian_filter1d(o,3))/20); y=y*np.interp(np.arange(len(y)),np.arange(len(g))*h,g); sf.write(sys.argv[2],y/max(1,abs(y).max()/.95),sr)
