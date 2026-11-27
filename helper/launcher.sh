#!/bin/bash
# Loopwright Voice.app's main program: checks this Mac can run it, sets it up the first time (or after an update),
# then starts the voice helper in the background. Loopwright finds it by itself (voice-helper.json).
# Opening the app again while the helper runs offers to stop it.
set -u
APP_RES="$(cd "$(dirname "$0")/../Resources" && pwd)"
ROOT="$HOME/Library/Application Support/Loopwright/Voice"
CONN="$HOME/Library/Application Support/Loopwright/voice-helper.json"
LOGDIR="$HOME/Library/Logs"
UV="$APP_RES/uv"
export UV_PYTHON_INSTALL_DIR="$ROOT/.python"
export UV_CACHE_DIR="$ROOT/.uv-cache"
export UV_NO_CONFIG=1

alert() { # title, message
  /usr/bin/osascript -e 'on run argv' -e 'display alert (item 1 of argv) message (item 2 of argv) as critical buttons {"OK"} default button "OK"' -e 'end run' "$1" "$2" >/dev/null 2>&1
}
ask() { # title, message, other button, another button -> prints the button chosen
  /usr/bin/osascript -e 'on run argv' -e 'button returned of (display dialog (item 2 of argv) with title (item 1 of argv) buttons {(item 3 of argv), (item 4 of argv), "OK"} default button "OK")' -e 'end run' "$1" "$2" "$3" "$4" 2>/dev/null
}
ask_code() { # message -> prints the code typed, or nothing when cancelled
  /usr/bin/osascript -e 'on run argv' -e 'text returned of (display dialog (item 1 of argv) with title "Loopwright Voice" default answer "" buttons {"Cancel", "Activate"} default button "Activate")' -e 'end run' "$1" 2>/dev/null
}
PY() { "$UV" run --no-project --quiet --python 3.11 "$@"; }
LICENSE="$APP_RES/payload/src/app/license.py"

# ---- this Mac: Apple Silicon only (also true when this runs under Rosetta), macOS 14+, 16 GB
if [ "$(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null)" != "1" ]; then
  alert "Loopwright Voice needs Apple Silicon" "Loopwright Voice runs on Macs with Apple Silicon (M1 or later) only. This Mac has an Intel chip, so it can't run the voice helper. Everything else in Loopwright works as usual."
  exit 1
fi
MACOS_MAJOR=$(/usr/bin/sw_vers -productVersion | cut -d. -f1)
if [ "${MACOS_MAJOR:-0}" -lt 14 ]; then
  alert "Loopwright Voice needs macOS 14 or later" "Update macOS (System Settings > General > Software Update), then open Loopwright Voice again."
  exit 1
fi
MEM_GB=$(( $(/usr/sbin/sysctl -n hw.memsize) / 1073741824 ))
if [ "$MEM_GB" -lt 16 ]; then
  alert "Loopwright Voice needs 16 GB of memory" "This Mac has ${MEM_GB} GB. The singing models need at least 16 GB to run."
  exit 1
fi

# ---- already running? offer to stop it
if [ -f "$CONN" ]; then
  PID=$(/usr/bin/plutil -extract pid raw -o - "$CONN" 2>/dev/null || true)
  if [ -n "$PID" ] && /bin/kill -0 "$PID" 2>/dev/null && /bin/ps -p "$PID" -o command= | grep -q "app/server.py"; then
    CHOICE=$(ask "Loopwright Voice" "Loopwright Voice is running. Loopwright's Vocals tab can use it now." "Deactivate this Mac" "Stop it")
    if [ "$CHOICE" = "Stop it" ] || [ "$CHOICE" = "Deactivate this Mac" ]; then
      /bin/kill "$PID" 2>/dev/null
      rm -f "$CONN"
    fi
    if [ "$CHOICE" = "Deactivate this Mac" ]; then
      if MSG=$(PY "$LICENSE" deactivate); then
        alert "This Mac is deactivated" "Your Loopwright Voice code is free for another Mac. Open Loopwright Voice again to use it here."
      else
        alert "Couldn't deactivate" "$MSG"
      fi
    fi
    exit 0
  fi
fi

# ---- Python (for the license check and the setup)
mkdir -p "$ROOT" "$LOGDIR"
# (--no-bin: never touch ~/.local/bin, where the person may have their own python3.11)
"$UV" python install 3.11 --no-bin >>"$LOGDIR/Loopwright Voice setup.log" 2>&1 || {
  alert "Loopwright Voice couldn't set up" "It couldn't download Python. Check your internet connection, then open Loopwright Voice again. (Details: ~/Library/Logs/Loopwright Voice setup.log)"
  exit 1
}

# ---- the license: a code from loop-wright.com, activated on 2 Macs at most
if ! PY "$LICENSE" check >/dev/null 2>&1; then
  PROMPT="Paste your Loopwright Voice code (it's in your purchase email and starts with LW1-). It works on 2 Macs. Don't have one? Get it at loop-wright.com/voices"
  while true; do
    CODE=$(ask_code "$PROMPT") || exit 0
    [ -z "$CODE" ] && exit 0
    if MSG=$(PY "$LICENSE" activate "$CODE"); then
      break
    fi
    PROMPT="$MSG"
  done
fi

# ---- set up (first time, after an update, or to finish a stopped download)
VERSION=$(cat "$APP_RES/payload/VERSION")
if [ "$(cat "$ROOT/.ready" 2>/dev/null)" != "$VERSION" ]; then
  "$UV" run --no-project --python 3.11 "$APP_RES/setup.py" --root "$ROOT" --payload "$APP_RES/payload" --uv "$UV" \
      >>"$LOGDIR/Loopwright Voice setup.log" 2>&1 || {
    alert "Loopwright Voice setup stopped" "Open Loopwright Voice again to carry on where it stopped. (Details: ~/Library/Logs/Loopwright Voice setup.log)"
    exit 1
  }
  echo "$VERSION" > "$ROOT/.ready"
fi

# ---- start the helper in the background (it writes voice-helper.json for Loopwright)
cd "$ROOT" || exit 1
PYTORCH_ENABLE_MPS_FALLBACK=1 HF_HUB_DISABLE_TELEMETRY=1 HF_HUB_OFFLINE=1 \
  /usr/bin/nohup "$ROOT/.venv-soulx/bin/python" app/server.py >>"$LOGDIR/Loopwright Voice.log" 2>&1 &
sleep 2
if ! /bin/kill -0 $! 2>/dev/null; then
  alert "Loopwright Voice didn't start" "Details are in ~/Library/Logs/Loopwright Voice.log"
  exit 1
fi
/usr/bin/osascript -e 'display notification "Running. Open Loopwright and go to the Vocals tab." with title "Loopwright Voice"' >/dev/null 2>&1
exit 0
