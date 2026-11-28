"""Loopwright Voice's license: a code from loop-wright.com, activated on 2 Macs at most (like Loopwright's).

    python license.py check                # exit 0 when this Mac is activated (prints who it's licensed to)
    python license.py activate <code>      # checks the code, asks the site to add this Mac, saves the activation
    python license.py deactivate           # frees this Mac (so the code can go on another one)

Standard library only. The code is "LW1-" + base64("owner\\nsignature in hex"), signed with Loopwright's private
key over sha256("Loopwright Voice helper v1|" + owner); the activation is a signature over
sha256("Loopwright Voice activation v1|" + owner + "|" + this Mac's id). Both are checked here with the public
key only (the same scheme as the plugin's Source/plugin/License.h and the site's license.server.ts).
"""
import base64, hashlib, json, os, platform, subprocess, sys, urllib.error, urllib.request
from pathlib import Path

CODE_PREFIX = "Loopwright Voice helper v1|"
TOKEN_PREFIX = "Loopwright Voice activation v1|"
SITE = os.environ.get("LW_ACTIVATE_URL", "https://loop-wright.com/api/activate")
DIR = Path.home() / "Library/Application Support/Loopwright/Voice"
CODE_FILE, TOKEN_FILE = DIR / "license.txt", DIR / "activation.txt"
KEY_FILE = Path(__file__).resolve().with_name("license-public-key.txt")


def public_key():
    e, n = KEY_FILE.read_text().strip().split(",")
    return int(e, 16), int(n, 16)


def digest(text, prefix):
    h = hashlib.sha256((prefix + text.strip()).encode()).digest()
    return int.from_bytes(h, "little") & ~(1 << 255)  # (JUCE reads the hash little-endian, bit 255 cleared)


def signed(sig_hex, text, prefix):
    try:
        sig = int(sig_hex.strip(), 16)
    except ValueError:
        return False
    if sig <= 0:
        return False
    e, n = public_key()
    return pow(sig, e, n) == digest(text, prefix)


def owner_of(code):
    """who a code was made for, or "" if it isn't a real Loopwright Voice code"""
    t = "".join(code.split())
    if not t.startswith("LW1-"):
        return ""
    try:
        text = base64.b64decode(t[4:], validate=False).decode("utf-8")
    except Exception:
        return ""
    owner, _, sig = text.partition("\n")
    return owner.strip() if owner.strip() and signed(sig, owner, CODE_PREFIX) else ""


def machine_id():
    raw = ""
    try:
        out = subprocess.run(["/usr/sbin/ioreg", "-rd1", "-c", "IOPlatformExpertDevice"], capture_output=True, text=True).stdout
        raw = next((ln.split('"')[-2] for ln in out.splitlines() if "IOPlatformUUID" in ln), "")
    except Exception:
        pass
    raw = raw or platform.node() + os.environ.get("USER", "")
    return hashlib.sha256(("Loopwright Voice machine|" + raw).encode()).hexdigest()[:32]


def machine_name():
    try:
        return subprocess.run(["/usr/sbin/scutil", "--get", "ComputerName"], capture_output=True, text=True).stdout.strip()[:40] or "a Mac"
    except Exception:
        return "a Mac"


def ask_site(code, action):
    body = json.dumps(dict(code=code.strip(), machine=machine_id(), name=machine_name(), action=action, product="helper")).encode()
    req = urllib.request.Request(SITE, data=body, headers={"Content-Type": "application/json", "User-Agent": "LoopwrightVoice"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        try:
            return json.loads(e.read() or b"{}") if e.code < 500 else {"ok": False, "message": "The license server didn't answer. Try again in a minute."}
        except Exception:
            return {"ok": False, "message": f"The license server said no ({e.code})."}
    except Exception:
        return {"ok": False, "message": "Couldn't reach loop-wright.com. Check your internet connection and try again."}


def licensed_to():
    """this Mac's licensed owner, or "" (needs the saved code and this Mac's activation)"""
    try:
        owner = owner_of(CODE_FILE.read_text())
        return owner if owner and signed(TOKEN_FILE.read_text(), owner + "|" + machine_id(), TOKEN_PREFIX) else ""
    except OSError:
        return ""


def activate(code):
    """"" when it worked, else what went wrong (in plain words)"""
    owner = owner_of(code)
    if not owner:
        return "That code didn't work. Copy the whole Loopwright Voice code from your purchase email, starting with LW1-"
    a = ask_site(code, "activate")
    if not a.get("ok"):
        return a.get("message") or "That code didn't work."
    token = str(a.get("token", ""))
    if not signed(token, owner + "|" + machine_id(), TOKEN_PREFIX):
        return "The activation didn't check out. Email support@loop-wright.com."
    DIR.mkdir(parents=True, exist_ok=True)
    CODE_FILE.write_text("".join(code.split()))
    TOKEN_FILE.write_text(token)
    os.chmod(CODE_FILE, 0o600)
    os.chmod(TOKEN_FILE, 0o600)
    return ""


def deactivate():
    try:
        code = CODE_FILE.read_text()
    except OSError:
        return ""
    a = ask_site(code, "deactivate")
    if not a.get("ok"):
        return a.get("message") or "Couldn't reach loop-wright.com to free this Mac. Try again with an internet connection."
    for f in (CODE_FILE, TOKEN_FILE):
        f.unlink(missing_ok=True)
    return ""


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    if cmd == "check":
        who = licensed_to()
        print(who)
        sys.exit(0 if who else 1)
    if cmd == "activate":
        msg = activate(sys.argv[2] if len(sys.argv) > 2 else "")
        print(msg or licensed_to())
        sys.exit(1 if msg else 0)
    if cmd == "deactivate":
        msg = deactivate()
        print(msg)
        sys.exit(1 if msg else 0)
    sys.exit(f"unknown command: {cmd}")
