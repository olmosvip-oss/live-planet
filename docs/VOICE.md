# Voice (experimental)

A fifth page, VOICE, that sings your lyrics over a section of your song in a voice you have permission to use.
The singing models are NOT in the plugin: a separate local app, the voice helper, does it. The plugin only talks
to it, on a background thread (never the audio thread). With no helper running, the page says so and everything
else in Loopwright works as before.

## Switching it on
SONGS menu (bottom of Settings) > "Voice page (experimental)". Off by default. (`LP_VOICE=1` also shows it, for tests.)

## How the plugin finds the helper
The helper writes `{port, token, pid}` (mode 0600) to:
- macOS: `~/Library/Application Support/Loopwright/voice-helper.json`
- other: `<app data>/Loopwright/voice-helper.json`
- `LP_VOICE_HELPER_FILE=<path>` overrides it (tests).

The plugin only connects to 127.0.0.1, sends the token as `X-Token` on every POST, caps sizes, checks job ids
(12 hex characters) and audio paths, and treats every answer as data.

## What it does
1. Create: records the chosen section of your song (all tracks, as you hear them, from a bar before it so echoes are
   ringing) to `<song>/voice/<section>.wav`, uploads it (`kind=track`), then `POST /api/create` with your lyrics,
   style, voice, the song's key (a major song: its relative minor, the same notes) and tempo, takes, versions,
   tuning, runs, noise removal and your permission.
2. While it sings: polls `/api/jobs` about every 1.5 s, only while the page is open or a job is running.
3. Each finished version is fetched to `<song>/voice/<job>/vocal_N.wav`, then written again starting at bar 1
   (silent until its section) as `<title> vN (<section>).wav`: that's what you drag, what plays along with the
   song, and what "add as track" adds (a vocal track with its Vocal chain and Auto-tune).
4. `<song>/voice/jobs.json` remembers where each job's section starts.

## The Vocals tab and the Studio

- **Your vocals**: every vocal in the song with its Vocal chain, Chop pads or Auto-tune, and **Stack it...**: a double,
  a harmony a third above or a fifth below (it follows the vocal note by note, in the song's scale), or a six-voice choir.
  Stacks are made inside Loopwright (`Source/song/Stack.h`), no helper needed.
- **Draw & sing** (`Source/plugin/DrawSing.cpp`): a piano roll over one section. Draw notes, double-click one for its
  word (or "Fill in my words", or "Start from a melody" for one written for your lyrics), "hear it" plays them as tones
  with the song. "Sing it in my voice" sends them to the helper as a written melody; the result lands as a new vocal track.
- **+ AI layer** (Studio, with sections picked): an instrument (`"layer"` in `/api/create`: drums, bass, guitar, keyboard,
  percussion, strings, synth, brass, woodwinds, fx, backing_vocals) played by ACE-Step over those sections
  (`sing_over.py --track`). It lands as a new track by itself (`lp::ai::AutoAdd`).

## "Write melody" (the default)

Loopwright writes the melody itself (`Source/song/LyricMelody.h`), over the chords the song's tracks really play:
each line gets its share of the section's bars, each word a note (longer words longer notes), a rap flow when the
style says hip hop, rap, trap, drill or grime, otherwise a sung line that arches and holds its last word. Lines share
a shape, and every second line comes home to the key's note. One melody per version, each different.

The melodies go to the helper as `"melodies"` in `/api/create` (up to 3, each
`{"segments": [{"start": ms, "notes": [{"word", "pitch" (MIDI), "dur" (s), "slur"}]}]}`, `"<SP>"` a rest). The
helper sings each note for note in the chosen voice with SoulX-Singer's score mode (`sing_score.py`), checks the
words, then finishes and mixes, so there are no takes, no clean-up and no separate voice step. A line needs at
least a bar.

## "I sing it" (your own take)
The switch at the top of the Lyrics card. Instead of the AI writing a melody, you rap or sing the lines over the
section (in the Loopwright app: Record my take, with a one-bar count-in; in a DAW: record there from the section's
first bar, or bounce from bar 1, and drop the file on the page). It keeps your exact words and timing; the helper
cleans, tunes and turns it into the chosen voice. Lyrics are optional (only used to check the words).

This needs the helper to accept a take: `POST /api/upload?kind=guide&name=...` (the WAV), then `POST /api/create`
with `"guide": "<name it returned>"`. It then skips writing a melody (and scores the take only if there are lyrics),
and does the same clean, voice and mix with one version. Jobs made this way have `opts.guide` set.

## Style words
The singing model goes by the style most of all. A style that names no voice is sent with one added:
"rap vocals, clear lyrics" for hip hop, rap, trap, drill or grime; otherwise "lead vocals, clear lyrics".

## Testing without the real helper
`tools/fake_voice_helper.py` answers the same API: each stage takes a moment and the "vocal" is a tone in the key.

    python3 tools/fake_voice_helper.py --dir /tmp/fake-voice --conn /tmp/fake-voice/voice-helper.json --stage-seconds 2
    LP_VOICE=1 LP_VOICE_HELPER_FILE=/tmp/fake-voice/voice-helper.json <Loopwright app>

It also takes a guide (your take) like the real one. `--fail` makes every job end in an error. Kill it mid-job to check the page copes with the helper going away.

## Not done (before customers)
Everything in the helper brief's "Before customers get it" list: a signed helper installer and model downloads,
license and legal review, consent wording and takedown policy, privacy text, Windows, hardware checks. Minor keys only.
