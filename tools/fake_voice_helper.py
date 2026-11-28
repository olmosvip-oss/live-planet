"""A stand-in for the Loopwright Voice helper, for testing the plugin's Voice page without the singing models.

Same API as the real helper (GET /api/options, /api/jobs, /audio/<id>/vocal_N.wav; POST /api/upload,
/api/create, /api/cancel; X-Token on every POST; Host must be 127.0.0.1:<port>). Instead of singing, each
stage takes a moment and the "vocal" is a soft sung-like tone over the track's length.

  python3 tools/fake_voice_helper.py --dir /tmp/fake-voice --conn /tmp/fake-voice/voice-helper.json [--port 8791] [--stage-seconds 1.5]
  (then run Loopwright with LP_VOICE_HELPER_FILE=/tmp/fake-voice/voice-helper.json)

Only standard Python; binds to 127.0.0.1 only. For tests, never for customers."""
import argparse, json, math, os, re, secrets, struct, threading, time, uuid, wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

ap = argparse.ArgumentParser()
ap.add_argument("--dir", required=True)
ap.add_argument("--conn", required=True)
ap.add_argument("--port", type=int, default=8791)
ap.add_argument("--stage-seconds", type=float, default=1.5)
ap.add_argument("--fail", action="store_true", help="every job ends in an error (to test that path)")
A = ap.parse_args()

ROOT = Path(A.dir)
JOBS, INST, VOICES, GUIDES = ROOT / "jobs", ROOT / "instrumentals", ROOT / "voices", ROOT / "guides"
for d in (JOBS, INST, VOICES, GUIDES):
    d.mkdir(parents=True, exist_ok=True)
if not any(VOICES.glob("*.wav")):  # a demo voice, like the real one ships with
    with wave.open(str(VOICES / "demo voice.wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(b"\0\0" * 16000)
PORT, TOKEN = A.port, secrets.token_urlsafe(24)
NOTES = "C C# D D# E F F# G G# A A# B".split()
STAGES = [("sing", "Writing melodies"), ("score", "Checking words and key"), ("clean", "Removing background noise"),
          ("voice", "Singing in your voice"), ("mix", "Finishing and mixing")]
jobs = {}
lock = threading.Lock()


def tone_wav(path, seconds, key, bpm):
    """a soft 'sung' line: one note per beat on the minor key's notes, with vibrato, at 44.1 kHz mono"""
    sr, root = 44100, 57 + NOTES.index(key)  # A3-ish
    steps = [0, 3, 7, 5, 3, 0, 7, 10]
    beat = 60.0 / bpm
    frames = bytearray()
    phase = 0.0
    for i in range(int(seconds * sr)):
        t = i / sr
        b = int(t / beat)
        f = 440.0 * 2 ** ((root + steps[b % len(steps)] - 69) / 12) * (1 + 0.006 * math.sin(2 * math.pi * 5.5 * t))
        phase += 2 * math.pi * f / sr
        x = (t % beat) / beat
        env = min(1.0, x / 0.08) * min(1.0, (1 - x) / 0.2)
        v = 0.25 * env * (math.sin(phase) + 0.4 * math.sin(2 * phase) + 0.2 * math.sin(3 * phase))
        frames += struct.pack("<h", int(max(-1, min(1, v)) * 32000))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(bytes(frames))


def melody_wav(path, melody, seconds):
    """a written melody, as tones: each note at its pitch and length (the real helper sings the words on them)"""
    sr = 44100
    buf = [0.0] * int(seconds * sr)
    for seg in melody.get("segments", []):
        t = seg["start"] / 1000.0
        for n in seg["notes"]:
            dur = float(n["dur"])
            if n.get("word") != "<SP>" and int(n.get("pitch", 0)) > 0:
                f, phase = 440.0 * 2 ** ((int(n["pitch"]) - 69) / 12), 0.0
                i0 = int(t * sr)
                for i in range(int(dur * sr)):
                    if i0 + i >= len(buf):
                        break
                    x = i / (dur * sr)
                    phase += 2 * math.pi * f / sr
                    env = min(1.0, x / 0.05) * min(1.0, (1 - x) / 0.15)
                    buf[i0 + i] += 0.25 * env * (math.sin(phase) + 0.4 * math.sin(2 * phase))
            t += dur
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 32000)) for v in buf))


def work(job):
    d, o = Path(job["dir"]), job["opts"]
    try:
        job["state"] = "running"
        for key, _ in STAGES:
            if key == "clean" and not o["clean"]:
                continue
            if o.get("guide") and (key == "sing" or (key == "score" and not o.get("has_lyrics"))):
                continue  # their own take: no melody to write
            if o.get("written") and key in ("clean", "voice"):
                continue  # the written melody is sung in the voice straight away
            if o.get("layer") and key in ("score", "clean", "voice"):
                continue  # an instrument: played, then mixed
            for n in range(1, (o["results"] if key in ("clean", "voice", "mix") or (key == "sing" and o.get("written")) else 1) + 1):
                if job.get("cancelled"):
                    raise InterruptedError
                note = f"version {n} of {o['results']}" if key in ("clean", "voice", "mix") else (
                    (f"melody {n} of {o['results']}" if o.get("written") else f"{o['takes']} takes") if key == "sing" else "")
                job.update(stage=key, note=note)
                time.sleep(A.stage_seconds)
                if A.fail and key == "score":
                    raise RuntimeError("No takes kept enough of the words (fake helper, --fail)")
                if key == "mix" and o.get("guide"):
                    (d / f"vocal_{n}.wav").write_bytes(Path(o["guide"]).read_bytes())  # (their take, as it came)
                elif key == "mix" and o.get("layer"):
                    with wave.open(o["inst"], "rb") as w:
                        secs = w.getnframes() / w.getframerate()
                    tone_wav(d / f"vocal_{n}.wav", min(secs, 60), o["key"], o["bpm"])
                elif key == "mix" and o.get("written"):
                    with wave.open(o["inst"], "rb") as w:
                        secs = w.getnframes() / w.getframerate()
                    melody_wav(d / f"vocal_{n}.wav", json.loads((d / f"melody_{n}.json").read_text()), min(secs, 60))
                elif key == "mix":
                    with wave.open(o["inst"], "rb") as w:
                        secs = w.getnframes() / w.getframerate()
                    tone_wav(d / f"vocal_{n}.wav", min(secs, 60), o["key"], o["bpm"])
                if key == "mix":
                    job["results"].append(dict(n=n, vocal=f"/audio/{job['id']}/vocal_{n}.wav", mix=f"/audio/{job['id']}/mix_{n}.wav",
                                               score=0.9, words=90 - 3 * n, key=95 - n, take=f"take_{n:02d}", heard=job["title"].lower()))
        job["state"] = "done"
    except InterruptedError:
        job["state"] = "cancelled"
    except Exception as e:
        job.update(state="error", error=str(e))


def num(x, lo, hi, kind=int):
    x = kind(x)
    if not lo <= x <= hi:
        raise ValueError(f"value must be {lo}-{hi}")
    return x


class H(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def out(self, code, body, ctype="application/json"):
        b = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def host_ok(self):
        return self.headers.get("Host") in (f"127.0.0.1:{PORT}", f"localhost:{PORT}")

    def do_GET(self):
        if not self.host_ok():
            return self.out(403, {"error": "forbidden"})
        u = urlparse(self.path)
        if u.path == "/api/options":
            return self.out(200, dict(voices=sorted(p.stem for p in VOICES.glob("*.wav")), tracks=sorted(p.name for p in INST.glob("*.wav")), stages=STAGES))
        if u.path == "/api/jobs":
            with lock:
                return self.out(200, sorted(jobs.values(), key=lambda j: -j["created"])[:20])
        m = re.fullmatch(r"/audio/([0-9a-f]{12})/(vocal_\d+\.wav)", u.path)
        if m and (JOBS / m[1] / m[2]).is_file():
            return self.out(200, (JOBS / m[1] / m[2]).read_bytes(), "audio/wav")
        self.out(404, {"error": "not found"})

    def do_POST(self):
        if not self.host_ok() or self.headers.get("X-Token") != TOKEN:
            return self.out(403, {"error": "forbidden"})
        u = urlparse(self.path)
        try:
            n = int(self.headers.get("Content-Length", 0))
            if u.path == "/api/upload":
                q = parse_qs(u.query)
                kind = q["kind"][0]
                if kind not in ("track", "voice", "guide") or not 44 < n < 300_000_000:
                    raise ValueError("Use a WAV under 300 MB")
                body = self.rfile.read(n)
                if body[:4] != b"RIFF" or body[8:12] != b"WAVE":
                    raise ValueError("Please add a WAV file")
                stem = re.sub(r"[^A-Za-z0-9 _-]", "", Path(q["name"][0]).stem)[:50] or "audio"
                dst = (INST if kind == "track" else VOICES if kind == "voice" else GUIDES) / f"{stem}.wav"
                if dst.exists():
                    dst = dst.with_name(f"{stem}-{uuid.uuid4().hex[:4]}.wav")
                dst.write_bytes(body)
                return self.out(200, dict(name=dst.stem if kind == "voice" else dst.name))
            q = json.loads(self.rfile.read(n) or b"{}")
            if u.path == "/api/cancel":
                jobs[q["id"]]["cancelled"] = True
                return self.out(200, {"ok": True})
            if u.path == "/api/create":
                lines = [x for x in q["lyrics"].strip().splitlines() if x.strip() and not x.strip().startswith("[")]
                guide = GUIDES / Path(q["guide"]).name if q.get("guide") else None
                if guide is not None and not guide.is_file():
                    raise ValueError("Your take didn't arrive: record or add it again")
                layer = q.get("layer") or None
                if layer is None and ((guide is None and not lines) or len(q["lyrics"]) > 4000):
                    raise ValueError("Type some lyrics first")
                if layer is None and not q.get("consent"):
                    raise ValueError("Confirm you have permission to use this voice")
                if layer is not None and not q.get("voice"):
                    q["voice"] = next(iter(sorted(p.stem for p in VOICES.glob("*.wav"))), "")
                inst, voice = INST / Path(q["track"]).name, VOICES / (Path(q["voice"]).name + ".wav")
                if not inst.is_file() or not voice.is_file():
                    raise ValueError("Choose a track and a voice")
                if q["key"] not in NOTES:
                    raise ValueError("Choose a key")
                i = uuid.uuid4().hex[:12]
                d = JOBS / i
                d.mkdir()
                o = dict(inst=str(inst), voice=str(voice), key=q["key"], bpm=num(q["bpm"], 50, 220), takes=num(q["takes"], 1, 8),
                         results=num(q["results"], 1, 3), tune=num(q["tune"], 0, 1, float), runs=num(q["runs"], 0, 8), clean=bool(q["clean"]),
                         style=str(q["style"])[:300])
                o["results"] = min(o["results"], o["takes"])
                if guide is not None:
                    o.update(guide=str(guide), has_lyrics=bool(lines), takes=1, results=1)
                melodies = q.get("melodies") or []
                if not isinstance(melodies, list) or len(melodies) > 3 or not all(isinstance(m, dict) and m.get("segments") for m in melodies):
                    raise ValueError("The written melody didn't come through: press Create again")
                if layer is not None:
                    o.update(layer=layer, takes=o["results"])
                if melodies:
                    o.update(written=True, has_lyrics=bool(lines), takes=len(melodies), results=len(melodies))
                    for k, m in enumerate(melodies, 1):
                        (d / f"melody_{k}.json").write_text(json.dumps(m))
                with lock:
                    jobs[i] = dict(id=i, dir=str(d), created=time.time(), state="queued", stage=None, note="", opts=o, scores=[], results=[],
                                   title=(("Layer: " + layer) if layer else lines[0] if lines else "Your take")[:60])
                print(f"CREATE {i} guide={'yes' if guide else 'no'} written={len(melodies)} layer={layer} style={o['style']!r} key={o['key']} bpm={o['bpm']} track={inst.name} voice={voice.stem} takes={o['takes']} results={o['results']}", flush=True)
                threading.Thread(target=work, args=(jobs[i],), daemon=True).start()
                return self.out(200, {"id": i})
            self.out(404, {"error": "not found"})
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as e:
            self.out(400, {"error": str(e)})


if __name__ == "__main__":
    cf = Path(A.conn)
    cf.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(cf, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as fh:
        json.dump(dict(port=PORT, token=TOKEN, pid=os.getpid()), fh)
    print(f"fake voice helper on 127.0.0.1:{PORT}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
