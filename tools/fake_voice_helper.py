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
JOBS, INST, VOICES = ROOT / "jobs", ROOT / "instrumentals", ROOT / "voices"
for d in (JOBS, INST, VOICES):
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


def work(job):
    d, o = Path(job["dir"]), job["opts"]
    try:
        job["state"] = "running"
        for key, _ in STAGES:
            if key == "clean" and not o["clean"]:
                continue
            for n in range(1, (o["results"] if key in ("clean", "voice", "mix") else 1) + 1):
                if job.get("cancelled"):
                    raise InterruptedError
                note = f"version {n} of {o['results']}" if key in ("clean", "voice", "mix") else (f"{o['takes']} takes" if key == "sing" else "")
                job.update(stage=key, note=note)
                time.sleep(A.stage_seconds)
                if A.fail and key == "score":
                    raise RuntimeError("No takes kept enough of the words (fake helper, --fail)")
                if key == "mix":
                    with wave.open(o["inst"], "rb") as w:
                        secs = w.getnframes() / w.getframerate()
                    tone_wav(d / f"vocal_{n}.wav", min(secs, 60), o["key"], o["bpm"])
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
                if kind not in ("track", "voice") or not 44 < n < 300_000_000:
                    raise ValueError("Use a WAV under 300 MB")
                body = self.rfile.read(n)
                if body[:4] != b"RIFF" or body[8:12] != b"WAVE":
                    raise ValueError("Please add a WAV file")
                stem = re.sub(r"[^A-Za-z0-9 _-]", "", Path(q["name"][0]).stem)[:50] or "audio"
                dst = (INST if kind == "track" else VOICES) / f"{stem}.wav"
                if dst.exists():
                    dst = dst.with_name(f"{stem}-{uuid.uuid4().hex[:4]}.wav")
                dst.write_bytes(body)
                return self.out(200, dict(name=dst.name if kind == "track" else dst.stem))
            q = json.loads(self.rfile.read(n) or b"{}")
            if u.path == "/api/cancel":
                jobs[q["id"]]["cancelled"] = True
                return self.out(200, {"ok": True})
            if u.path == "/api/create":
                lines = [x for x in q["lyrics"].strip().splitlines() if x.strip() and not x.strip().startswith("[")]
                if not lines or len(q["lyrics"]) > 4000:
                    raise ValueError("Type some lyrics first")
                if not q.get("consent"):
                    raise ValueError("Confirm you have permission to use this voice")
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
                with lock:
                    jobs[i] = dict(id=i, dir=str(d), created=time.time(), state="queued", stage=None, note="", opts=o, scores=[], results=[], title=lines[0][:60])
                print(f"CREATE {i} key={o['key']} bpm={o['bpm']} track={inst.name} voice={voice.stem} takes={o['takes']} results={o['results']}", flush=True)
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
