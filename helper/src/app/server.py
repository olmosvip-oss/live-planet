"""Loopwright Voice helper: local-only job server + UI. Run: python3 app/server.py  -> http://127.0.0.1:8791
Binds to 127.0.0.1 only. Writes need a same-origin page plus a per-run token. Never overwrites; each job gets its own folder."""
import json, os, re, secrets, shutil, subprocess, threading, time, uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parent.parent
JOBS, INST, VOICES = ROOT / "songs" / "app", ROOT / "songs" / "instrumentals", ROOT / "my_voice" / "clean"
# "Removing background noise" needs two separation models (becruily's karaoke, anvuew's dereverb) whose licenses
# aren't cleared yet, so the installer leaves them out: without them the step is skipped and the option reported off.
_PRE = ROOT / "vendor/SoulX-Singer/pretrained_models/SoulX-Singer-Preprocess"
CLEAN_OK = all(any((_PRE / d).glob("*.ckpt")) for d in ("mel-band-roformer-karaoke", "dereverb_mel_band_roformer"))
GUIDES = ROOT / "songs" / "guides"  # takes people sang or rapped themselves (Loopwright's "Sing it yourself")
PORT, TOKEN = 8791, secrets.token_urlsafe(24)
ACE, SX = str(ROOT / "vendor/ACE-Step-1.5/.venv/bin/python"), str(ROOT / ".venv-soulx/bin/python")
NOTES = "C C# D D# E F F# G G# A A# B".split()
LAYERS = ["drums", "bass", "guitar", "keyboard", "percussion", "strings", "synth", "brass", "woodwinds", "fx", "backing_vocals"]
STAGES = [("sing", "Writing melodies"), ("score", "Checking words and key"), ("clean", "Removing background noise"),
          ("voice", "Singing in your voice"), ("mix", "Finishing and mixing")]
jobs, procs, gpu = {}, {}, threading.Lock()
for d in (JOBS, INST, GUIDES):
    d.mkdir(parents=True, exist_ok=True)


def run(job, cmd, on=None):
    p = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    procs[job["id"]] = p
    tail = []
    for line in p.stdout:
        tail = (tail + [line.strip()])[-12:]
        if on:
            on(line.strip())
    if p.wait() != 0:
        if job.get("cancelled"):
            raise InterruptedError
        raise RuntimeError(" | ".join(tail)[-600:])


# the singer for written melodies, kept loaded between jobs (loading it is most of a take's wait); let go after
# 10 quiet minutes so the memory comes back
singer = {"proc": None, "used": 0.0}


def singer_proc():
    p = singer["proc"]
    if p is None or p.poll() is not None:
        p = subprocess.Popen([SX, "sing_score.py", "--serve"], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1)
        singer["proc"] = p
        for line in p.stdout:  # (until it's loaded)
            if line.startswith("READY"):
                break
            print("singer:", line.rstrip(), flush=True)
    singer["used"] = time.time()
    return p


def sing_score(job, score, voice, out, steps=32):
    p = singer_proc()
    procs[job["id"]] = p  # (Stop ends it; it's started again next time)
    p.stdin.write(json.dumps(dict(score=str(score), voice=str(voice), out=str(out), steps=steps)) + "\n")
    p.stdin.flush()
    tail = []
    for line in p.stdout:
        line = line.rstrip()
        if line.startswith("DONE "):
            singer["used"] = time.time()
            return
        if line.startswith("FAIL "):
            raise RuntimeError(line[5:])
        tail = (tail + [line])[-8:]
        print("singer:", line, flush=True)
    if job.get("cancelled"):
        raise InterruptedError
    raise RuntimeError("the singer stopped: " + " | ".join(tail)[-400:])


def singer_reaper():
    while True:
        time.sleep(60)
        p = singer["proc"]
        if p is not None and p.poll() is None and time.time() - singer["used"] > 600 and not gpu.locked():
            p.terminate()
            singer["proc"] = None


threading.Thread(target=singer_reaper, daemon=True).start()


def work(job):
    with gpu:
        d, o = Path(job["dir"]), job["opts"]

        def stage(k, note=""):
            if job.get("cancelled"):
                raise InterruptedError
            job.update(stage=k, note=note, stage_at=time.time())
        try:
            job["state"] = "running"
            lyr = d / "lyrics.txt"
            if o.get("layer"):
                # Loopwright's "generate a layer": one instrument played over the song section, by ACE-Step (no voice)
                for n in range(1, o["results"] + 1):
                    stage("sing", f"{o['layer'].replace('_', ' ')}, version {n} of {o['results']}")
                    run(job, [ACE, "sing_over.py", o["inst"], str(lyr), str(d / "takes"), "--bpm", str(o["bpm"]), "--key", f"{o['key']} minor",
                              "--seed", str(o["seed"] + n), "--takes", "1", "--style", o["style"], "--track", o["layer"]])
                    take = d / "takes" / f"take_{o['seed'] + n}.wav"
                    if not take.is_file():
                        raise RuntimeError("The layer wasn't made: " + o["layer"])
                    stage("mix", f"version {n} of {o['results']}")
                    shutil.copy(take, d / f"vocal_{n}.wav")
                    run(job, [SX, "app/mix.py", o["inst"], str(d / f"vocal_{n}.wav"), str(d / f"mix_{n}.wav")])
                    job["results"].append(dict(n=n, vocal=f"/audio/{job['id']}/vocal_{n}.wav", mix=f"/audio/{job['id']}/mix_{n}.wav"))
                job["state"] = "done"
                return
            if o.get("written"):
                # Loopwright wrote the melodies (notes, words, timing): sung straight in the chosen voice, no takes
                scores = sorted(d.glob("melody_*.json"))
                raws = []
                for n, sc in enumerate(scores, 1):
                    stage("sing", f"melody {n} of {len(scores)}")
                    raw = d / f"sung_{n}.wav"
                    sing_score(job, sc, o["voice"], raw, 24 if o.get("quick") else 32)
                    raws.append(str(raw))
                if o.get("quick"):
                    # Draw & sing: the words are known, the mix is Loopwright's: the take straight back, as sung
                    for n, raw in enumerate(raws, 1):
                        shutil.copy(raw, d / f"vocal_{n}.wav")
                        job["results"].append(dict(n=n, vocal=f"/audio/{job['id']}/vocal_{n}.wav", mix=f"/audio/{job['id']}/vocal_{n}.wav"))
                    job["state"] = "done"
                    return
                if o.get("has_lyrics"):
                    stage("score")

                    def scored_sung(line):
                        m = re.match(r"SCORE (\S+) words (\d+)% key (\d+)% median \S+ (.+?)\.wav \| (.*)", line)
                        if m:
                            job["scores"].append(dict(score=float(m[1]), words=int(m[2]), key=int(m[3]), take=m[4], heard=m[5]))
                    run(job, [SX, "score_takes.py", str(lyr), str(NOTES.index(o["key"]))] + raws, scored_sung)
                for n, raw in enumerate(raws, 1):
                    stage("mix", f"version {n} of {len(raws)}")
                    vox = d / f"vocal_{n}.wav"
                    run(job, [SX, "finish_vocal.py", raw, str(vox)])
                    run(job, [SX, "app/mix.py", o["inst"], str(vox), str(d / f"mix_{n}.wav")])
                    s_ = next((x for x in job["scores"] if x["take"] == Path(raw).stem), {})
                    job["results"].append(dict(n=n, vocal=f"/audio/{job['id']}/vocal_{n}.wav", mix=f"/audio/{job['id']}/mix_{n}.wav", **s_))
                job["state"] = "done"
                return
            if o.get("guide"):
                # their own take: no melody to write; check its words (if there are lyrics), then the same clean,
                # voice and mix as an AI take
                rank = [o["guide"]]
                if o.get("has_lyrics"):
                    stage("score", "your take")

                    def scored_guide(line):
                        m = re.match(r"SCORE (\S+) words (\d+)% key (\d+)% median \S+ (.+?)\.wav \| (.*)", line)
                        if m:
                            job["scores"].append(dict(score=float(m[1]), words=int(m[2]), key=int(m[3]), take=m[4], heard=m[5]))
                    run(job, [SX, "score_takes.py", str(lyr), str(NOTES.index(o["key"])), o["guide"]], scored_guide)
                finish(job, d, o, rank, stage)
                job["state"] = "done"
                return
            # takes until enough of them keep the words: a take that loses them never reaches you (up to 3 rounds)
            WORDS_OK, ROUNDS = 75, 3
            for rnd in range(ROUNDS):
                more = f"round {rnd + 1}: " if rnd else ""
                stage("sing", f"{more}0 of {o['takes']} takes")
                job["done_takes"] = 0

                def seen(line):
                    if line.startswith("TAKE"):
                        job["done_takes"] = job.get("done_takes", 0) + 1
                        job["note"] = f"{more}{job['done_takes']} of {o['takes']} takes"
                run(job, [ACE, "sing_over.py", o["inst"], str(lyr), str(d / "takes"), "--bpm", str(o["bpm"]), "--key", f"{o['key']} minor",
                          "--seed", str(o["seed"] + rnd * 100), "--takes", str(o["takes"]), "--style", o["style"]], seen)
                stage("score", more.rstrip(": "))
                takes = sorted(str(x) for x in (d / "takes").glob("take_*.wav"))
                if not takes:
                    raise RuntimeError("No takes were generated")
                job["scores"] = []

                def scored(line):
                    m = re.match(r"SCORE (\S+) words (\d+)% key (\d+)% .*?(take_\d+)\.wav \| (.*)", line)
                    if m:
                        job["scores"].append(dict(score=float(m[1]), words=int(m[2]), key=int(m[3]), take=m[4], heard=m[5]))
                run(job, [SX, "score_takes.py", str(lyr), str(NOTES.index(o["key"]))] + takes, scored)
                good = [x for x in job["scores"] if x["words"] >= WORDS_OK]
                print(f"JOB {job['id']} round {rnd + 1}: {len(good)} of {len(takes)} takes keep {WORDS_OK}%+ of the words", flush=True)
                if len(good) >= o["results"]:
                    break
            rank = json.loads(Path(str(lyr) + ".rank.json").read_text())[:o["results"]]
            finish(job, d, o, rank, stage)
            job["state"] = "done"
        except InterruptedError:
            job["state"] = "cancelled"
        except Exception as e:
            job.update(state="error", error=str(e))
        finally:
            job["ended"] = time.time()
            procs.pop(job["id"], None)
            (d / "job.json").write_text(json.dumps({k: v for k, v in job.items() if k != "cancelled"}, indent=1))


def finish(job, d, o, rank, stage):
    """each chosen take: cleaned (optional), sung in the chosen voice, finished and mixed"""
    for n, t in enumerate(rank, 1):
        src, of = t, f"version {n} of {len(rank)}"
        if o["clean"]:
            stage("clean", of)
            c = d / f"clean_{n}.wav"
            run(job, [SX, "clean_vocal.py", t, str(c), "--device", "cpu"])
            src = str(c)
        stage("voice", of)
        raw = d / f"raw_{n}.wav"
        run(job, [SX, "convert_voice.py", src, o["voice"], str(raw), "--steps", "32", "--tune", str(NOTES.index(o["key"])),
                  "--tune-amount", str(o["tune"]), "--runs", str(o["runs"]), "--shift", "auto"])
        stage("mix", of)
        vox = d / f"vocal_{n}.wav"
        run(job, [SX, "finish_vocal.py", str(raw), str(vox)])
        run(job, [SX, "app/mix.py", o["inst"], str(vox), str(d / f"mix_{n}.wav")])
        s = next((x for x in job["scores"] if x["take"] == Path(t).stem), {})
        job["results"].append(dict(n=n, vocal=f"/audio/{job['id']}/vocal_{n}.wav", mix=f"/audio/{job['id']}/mix_{n}.wav", **s))


def num(x, lo, hi, kind=int):
    x = kind(x)
    if not lo <= x <= hi:
        raise ValueError(f"value must be {lo}-{hi}")
    return x


class H(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def out(self, code, body, ctype="application/json"):
        b = body if isinstance(body, bytes) else (body if isinstance(body, str) else json.dumps(body)).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(b)

    def host_ok(self):  # blocks DNS-rebinding pages
        return self.headers.get("Host") in (f"127.0.0.1:{PORT}", f"localhost:{PORT}")

    def do_GET(self):
        if not self.host_ok():
            return self.out(403, {"error": "forbidden"})
        u = urlparse(self.path)
        if u.path == "/":
            return self.out(200, (ROOT / "app/index.html").read_text().replace("__TOKEN__", TOKEN), "text/html; charset=utf-8")
        if u.path == "/api/options":
            return self.out(200, dict(voices=sorted(p.stem for p in VOICES.glob("*.wav")), tracks=sorted(p.name for p in INST.glob("*.wav")), stages=[st for st in STAGES if CLEAN_OK or st[0] != "clean"], clean=CLEAN_OK))
        if u.path == "/api/jobs":
            return self.out(200, sorted(jobs.values(), key=lambda j: -j["created"])[:20])
        m = re.fullmatch(r"/audio/([0-9a-f]{12})/((?:vocal|mix)_\d+\.wav)", u.path)
        if m and (JOBS / m[1] / m[2]).is_file():
            b, r = (JOBS / m[1] / m[2]).read_bytes(), re.match(r"bytes=(\d+)-(\d*)", self.headers.get("Range", ""))
            s, e = (int(r[1]), min(int(r[2] or len(b) - 1), len(b) - 1)) if r else (0, len(b) - 1)
            if s > e:
                return self.out(416, {"error": "range"})
            self.send_response(206 if r else 200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(e - s + 1))
            if r:
                self.send_header("Content-Range", f"bytes {s}-{e}/{len(b)}")
            self.end_headers()
            try:
                self.wfile.write(b[s:e + 1])
            except BrokenPipeError:
                pass
            return
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
            if n > 1_000_000:
                raise ValueError("too large")
            q = json.loads(self.rfile.read(n) or b"{}")
            if u.path == "/api/cancel":
                j = jobs[q["id"]]
                j["cancelled"] = True
                if j["id"] in procs:
                    procs[j["id"]].terminate()
                return self.out(200, {"ok": True})
            if u.path == "/api/create":
                lines = [x for x in q["lyrics"].strip().splitlines() if x.strip() and not x.strip().startswith("[")]
                guide = GUIDES / Path(q["guide"]).name if q.get("guide") else None
                if guide is not None and not guide.is_file():
                    raise ValueError("Your take didn't arrive: record or add it again")
                melodies = q.get("melodies") or []
                if not isinstance(melodies, list) or len(melodies) > 3 or not all(isinstance(m, dict) and m.get("segments") for m in melodies):
                    raise ValueError("The written melody didn't come through: press Create again")
                layer = q.get("layer") or None
                if layer is not None and layer not in LAYERS:
                    raise ValueError("Choose an instrument for the layer")
                if layer is None and ((guide is None and not lines) or len(q["lyrics"]) > 4000):
                    raise ValueError("Type some lyrics first")
                if layer is None and not q.get("consent"):
                    raise ValueError("Confirm you have permission to use this voice")
                inst, voice = INST / Path(q["track"]).name, VOICES / (Path(q.get("voice") or "x").name + ".wav")
                if layer is not None and not voice.is_file():
                    voice = next(iter(sorted(VOICES.glob("*.wav"))), voice)  # (a layer sings nothing: any voice will do)
                if not inst.is_file() or (layer is None and not voice.is_file()):
                    raise ValueError("Choose a track and a voice")
                if q["key"] not in NOTES:
                    raise ValueError("Choose a key")
                i = uuid.uuid4().hex[:12]
                d = JOBS / i
                d.mkdir()
                (d / "lyrics.txt").write_text((q["lyrics"].strip() or "(your take)") + "\n")
                o = dict(inst=str(inst), voice=str(voice), key=q["key"], bpm=num(q["bpm"], 50, 220), takes=num(q["takes"], 1, 8),
                         results=num(q["results"], 1, 3), tune=num(q["tune"], 0, 1, float), runs=num(q["runs"], 0, 8), clean=bool(q["clean"]) and CLEAN_OK,
                         style=(str(q["style"]).strip()[:300] or "melodic R&B lead vocal") + ", clear diction, dry, close mic", seed=secrets.randbelow(10**6))
                o["results"] = min(o["results"], o["takes"])
                if guide is not None:
                    o.update(guide=str(guide), has_lyrics=bool(lines), takes=1, results=1)
                if layer is not None:
                    o.update(layer=layer, style=str(q["style"]).strip()[:300] or layer, takes=o["results"])
                if melodies:
                    o.update(written=True, has_lyrics=bool(lines), takes=len(melodies), results=len(melodies), quick=bool(q.get("quick")))
                    for n, m in enumerate(melodies, 1):
                        (d / f"melody_{n}.json").write_text(json.dumps(m))
                jobs[i] = dict(id=i, dir=str(d), created=time.time(), state="queued", stage=None, note="", opts=o, scores=[], results=[],
                               title=(("Layer: " + layer.replace("_", " ")) if layer else lines[0] if lines else "Your take")[:60])
                threading.Thread(target=work, args=(jobs[i],), daemon=True).start()
                return self.out(200, {"id": i})
            self.out(404, {"error": "not found"})
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as e:
            self.out(400, {"error": str(e)})


if __name__ == "__main__":
    cf = Path.home() / "Library/Application Support/Loopwright/voice-helper.json"  # how the plugin finds the helper (owner-only)
    cf.parent.mkdir(parents=True, exist_ok=True)
    for p in (JOBS, INST, VOICES, GUIDES):
        p.mkdir(parents=True, exist_ok=True)
    # take the port first: if another helper already has it, stop here and leave its connection file alone
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    except OSError:
        raise SystemExit(f"Loopwright Voice is already running (port {PORT} is taken).")
    fd = os.open(cf, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "w") as fh:
        json.dump(dict(port=PORT, token=TOKEN, pid=os.getpid()), fh)
    print(f"Loopwright Voice: http://127.0.0.1:{PORT}", flush=True)
    srv.serve_forever()
