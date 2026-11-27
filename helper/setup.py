"""Loopwright Voice: first-launch setup (and updates). Standard library only: it runs on the Python that uv installs.

    python setup.py --root <install folder> --payload <app Resources/payload> --uv <path to uv>

Shows its progress on http://127.0.0.1:8792 (opened in the browser), then exits 0 when the helper is ready to start.
Each step is skipped when it's already done for this version, so a second launch (or a retry after a failed
download) picks up where it stopped. Nothing here deletes the person's own voices or songs.
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, threading, time, traceback, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--root", required=True)
ap.add_argument("--payload", required=True)
ap.add_argument("--uv", required=True)
ap.add_argument("--port", type=int, default=8792)
ap.add_argument("--no-browser", action="store_true")
a = ap.parse_args()
ROOT, PAY, UV = Path(a.root), Path(a.payload), a.uv
LOG = Path.home() / "Library/Logs/Loopwright Voice setup.log"
STATE = ROOT / ".setup.json"
PY = "3.11"

# what the models need on disk, roughly (for the progress bar), and where they come from (Hugging Face)
MODELS = [
    # (label, repo, folder under ROOT, allow patterns or None for the whole repo, about how many GB)
    ("Song model: main parts", "ACE-Step/Ace-Step1.5", "vendor/ACE-Step-1.5/checkpoints", None, 9.4),
    ("Song model: base", "ACE-Step/acestep-v15-base", "vendor/ACE-Step-1.5/checkpoints/acestep-v15-base", None, 4.5),
    ("Singer", "Soul-AILab/SoulX-Singer", "vendor/SoulX-Singer/pretrained_models/SoulX-Singer", ["model.pt", "model-svc.pt", "*.json", "*.yaml"], 5.2),
    # only the pitch tracker: the two separation models in this repo are left out (licenses not cleared)
    ("Pitch tracker", "Soul-AILab/SoulX-Singer-Preprocess", "vendor/SoulX-Singer/pretrained_models/SoulX-Singer-Preprocess", ["rmvpe/*"], 0.2),
    ("Word checker", "openai/whisper-small", None, ["*.json", "*.txt", "model.safetensors"], 1.0),
]

status = {"step": "Starting", "detail": "", "steps": [], "done": False, "error": "", "progress": None}
lock = threading.Lock()


def say(step=None, detail=None, progress=None):
    with lock:
        if step is not None and step != status["step"]:
            status["steps"].append(status["step"])
            status["step"] = step
        if detail is not None:
            status["detail"] = detail
        status["progress"] = progress
    log(f"{step or ''} {detail or ''}".strip())


def log(msg):
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a") as f:
        f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + msg + "\n")


def state():
    try:
        return json.loads(STATE.read_text())
    except Exception:
        return {}


def mark(key, value):
    s = state()
    s[key] = value
    STATE.write_text(json.dumps(s, indent=1))


def digest(*files):
    h = hashlib.sha256()
    for f in files:
        h.update(Path(f).read_bytes())
    return h.hexdigest()[:16]


def run(cmd, **kw):
    log("$ " + " ".join(map(str, cmd)))
    with LOG.open("a") as f:
        r = subprocess.run(list(map(str, cmd)), stdout=f, stderr=subprocess.STDOUT, **kw)
    if r.returncode != 0:
        raise RuntimeError(f"{Path(str(cmd[0])).name} failed (exit {r.returncode}). The details are in {LOG}")


def folder_gb(p):
    total = 0
    for dp, _, fs in os.walk(p):
        for f in fs:
            try:
                total += os.path.getsize(os.path.join(dp, f))
            except OSError:
                pass
    return total / 1e9


def copy_payload():
    """the helper's code and the two model projects' code (no models in the app); people's own files are kept"""
    say("Copying Loopwright Voice", "")
    version = (PAY / "VERSION").read_text().strip()
    if state().get("payload") == version and (ROOT / "app/server.py").exists():
        return
    ROOT.mkdir(parents=True, exist_ok=True)
    for item in ["app", *[p.name for p in (PAY / "src").glob("*.py")]]:
        src = PAY / "src" / item
        dst = ROOT / item
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dst)
    for proj in ("ACE-Step-1.5", "SoulX-Singer"):
        # the code is replaced; checkpoints / pretrained_models / .venv (not in the payload) are left alone
        shutil.copytree(PAY / "vendor" / proj, ROOT / "vendor" / proj, dirs_exist_ok=True)
    voices = ROOT / "my_voice/clean"
    voices.mkdir(parents=True, exist_ok=True)
    for v in (PAY / "voices").glob("*.wav"):
        if not (voices / v.name).exists():
            shutil.copy2(v, voices / v.name)
    for f in ("requirements-soulx.txt", "requirements-ace.txt", "VERSION"):
        shutil.copy2(PAY / f, ROOT / f)
    mark("payload", version)


def make_env(name, venv, reqs):
    key = "env:" + name
    want = digest(reqs)
    if state().get(key) == want and (venv / "bin/python").exists():
        return
    say(f"Installing {name}", "Python packages, about 3 GB the first time. This takes a few minutes.")
    if venv.exists():
        shutil.rmtree(venv)
    run([UV, "venv", "--python", PY, str(venv)])
    run([UV, "pip", "install", "--python", str(venv / "bin/python"), "-r", str(reqs)])
    mark(key, want)


def download_models():
    py = ROOT / ".venv-soulx/bin/python"
    total = sum(m[4] for m in MODELS)
    before = 0.0
    for label, repo, sub, allow, gb in MODELS:
        key = "model:" + repo
        if state().get(key):
            before += gb
            continue
        target = ROOT / sub if sub else None
        say(f"Downloading: {label}", f"{repo}, about {gb:.1f} GB")
        code = (
            "import sys, json\n"
            "from huggingface_hub import snapshot_download\n"
            "a = json.loads(sys.argv[1])\n"
            "kw = dict(repo_id=a['repo'], allow_patterns=a['allow'])\n"
            "if a['dir']: kw['local_dir'] = a['dir']\n"
            "for i in range(4):\n"
            "    try:\n"
            "        print(snapshot_download(**kw)); break\n"
            "    except Exception as e:\n"
            "        print('retrying:', e, flush=True)\n"
            "        if i == 3: raise\n"
        )
        arg = json.dumps(dict(repo=repo, allow=allow, dir=str(target) if target else None))
        watch = target or Path.home() / ".cache/huggingface/hub" / ("models--" + repo.replace("/", "--"))
        start = folder_gb(watch) if watch.exists() else 0.0
        stop = threading.Event()

        def tick():
            while not stop.wait(1.0):
                have = (folder_gb(watch) - start) if watch.exists() else 0.0
                with lock:
                    status["progress"] = min(0.99, (before + min(have, gb)) / total)

        t = threading.Thread(target=tick, daemon=True)
        t.start()
        try:
            run([py, "-c", code, arg], env={**os.environ, "HF_HUB_DISABLE_TELEMETRY": "1"})
        finally:
            stop.set()
        before += gb
        mark(key, True)
    with lock:
        status["progress"] = 1.0


def check_disk():
    free = shutil.disk_usage(Path.home()).free / 1e9
    need = 0.0 if state().get("model:ACE-Step/Ace-Step1.5") else 25.0
    if free < need:
        raise RuntimeError(f"Loopwright Voice needs about {need:.0f} GB of free space for its singing models. "
                           f"This Mac has {free:.0f} GB free. Free up some space, then open Loopwright Voice again.")


def work():
    try:
        check_disk()
        copy_payload()
        make_env("the singer", ROOT / ".venv-soulx", PAY / "requirements-soulx.txt")
        make_env("the song model", ROOT / "vendor/ACE-Step-1.5/.venv", PAY / "requirements-ace.txt")
        download_models()
        say("Ready", "Loopwright Voice is set up and running. You can close this page: open Loopwright and go to the Vocals tab.")
        with lock:
            status["done"] = True
    except Exception as e:
        log(traceback.format_exc())
        with lock:
            status["error"] = str(e)


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Loopwright Voice setup</title>
<style>
body{margin:0;background:#0e0d14;color:#ece8ff;font:15px/1.5 -apple-system,Helvetica,Arial,sans-serif;display:flex;
min-height:100vh;align-items:center;justify-content:center}
.c{width:min(560px,90vw);background:#17151f;border:1px solid #2b2740;border-radius:18px;padding:32px}
h1{font-size:22px;margin:0 0 4px}.sub{color:#9a93b8;margin:0 0 24px}
.bar{height:10px;background:#252134;border-radius:6px;overflow:hidden;margin:16px 0}
.bar i{display:block;height:100%;background:linear-gradient(90deg,#7c5cf6,#b69cff);width:0;transition:width .8s}
.step{font-weight:600}.detail{color:#b3acd0;font-size:14px}.done{color:#6f6990;font-size:13px;margin-top:18px}
.err{color:#ff9b9b;margin-top:16px;white-space:pre-wrap}
</style></head><body><div class="c"><h1>Loopwright Voice</h1>
<p class="sub">Setting up the voice helper on this Mac. Keep this page open; it can take a while the first time
(about 20 GB to download).</p>
<div class="step" id="s"></div><div class="detail" id="d"></div><div class="bar" id="b"><i id="p"></i></div>
<div class="done" id="h"></div><div class="err" id="e"></div></div>
<script>
async function poll(){try{const r=await fetch('/status');const s=await r.json();
document.getElementById('s').textContent=s.step;document.getElementById('d').textContent=s.detail;
const b=document.getElementById('b');b.style.display=s.progress==null?'none':'block';
document.getElementById('p').style.width=((s.progress||0)*100)+'%';
document.getElementById('h').textContent=s.steps.length?'Done: '+s.steps.join(' \\u00b7 '):'';
document.getElementById('e').textContent=s.error?s.error+'\\n\\nOpen Loopwright Voice again to retry: it picks up where it stopped.':'';
if(!s.done&&!s.error)setTimeout(poll,1000)}catch(e){setTimeout(poll,2000)}}poll()
</script></body></html>"""


class H(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.headers.get("Host", "").split(":")[0] not in ("127.0.0.1", "localhost"):
            self.send_response(403); self.end_headers(); return
        if self.path == "/status":
            with lock:
                body = json.dumps(status).encode()
            ctype = "application/json"
        else:
            body, ctype = PAGE.encode(), "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    log(f"setup {sys.version.split()[0]} root={ROOT}")
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    if not a.no_browser:
        webbrowser.open(f"http://127.0.0.1:{a.port}/")
    w = threading.Thread(target=work)
    w.start()
    w.join()
    time.sleep(4)  # (the page's last poll shows the result)
    sys.exit(0 if status["done"] else 1)
