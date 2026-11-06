# RELAY-16

A sequencer-first firmware for the M-VAVE FM-1 and the M-VAVE SMK-37. RELAY-16 makes no sound of its own: sixteen
tracks play other instruments over USB, Bluetooth MIDI and, on the SMK-37, the TRS jack. Typical targets are another
FM-1 running [LOOM-8](https://github.com/charlesvestal/fm1-loom), a hardware synth, or a DAW.

**Site: <https://davidpryan.github.io/fm1-relay/>** has the web installer for each unit, the
[manual](https://davidpryan.github.io/fm1-relay/manual/) and the firmware in the browser.

**Status: 0.6, untested on hardware.** It builds for both boards and runs in fm1-base's host simulator. Nothing has
been tried on a real FM-1 or SMK-37 yet. Keep M-VAVE's stock firmware and M-UPGRADE at hand before installing.

## What it does

- 16 tracks, each with 8 patterns of up to 64 steps, its own length, note value (1/32 to a bar, triplets),
  direction (forward, reverse, ping-pong, random) and swing. Tracks of different lengths drift against each other.
- A step holds up to 4 notes, velocity, length, micro-timing (±11/24 of a step), chance, a condition (A:B, FILL,
  !FILL, PRE, !PRE, NEI, !NEI, 1ST, !1ST), a ratchet (1-8 hits with a velocity ramp), glide, accent, and 4 CC locks.
- Each track has a destination (MIDI channel and ports) and a device profile that names its lanes and drum notes:
  LOOM-8 synth and kit, JIANT FM synth and DRUM-X, Clipper, GM drums, a generic drum machine, the stock FM-1 and a
  generic synth.
- 4 CC lanes per track (any CC, pitch bend or channel pressure): locked per step, played live from the knobs (and
  the SMK-37's faders), recorded while recording.
- 16 scenes (a pattern and a mute for every track), launched at the bar or at once; a song of 32 slots.
- Perform: mutes, solo, and FILL held.
- Live record (quantised to the nearest step, with note lengths), step record (chords too), and a step held with keys
  played sets its notes.
- Generators: Euclid (steps, pulses, rotate) and Random (density, range, seed), previewed, then printed into the
  pattern. Undo, copy, paste, double, shift.
- Global key and scale: a track with IN KEY snaps its notes to it.
- The conductor, from LOOM-8: one track leads, and the tracks that follow it move with its notes through the key
  (diatonic, transpose, harmony or chord degree), so a chord part or a bass line drives the rest.
- Four project slots (GLOBAL > PROJECTS: load, save here, clear). SAVE while playing saves at the stop.
- Help cards: hold any button alone for a second and a card explains it, in the unit's own names.
- Per-port timing (GLOBAL > TIMING): each port's notes and clock early or late, and which ports get the clock and
  start/stop. Two LOOM-8s, one on USB or TRS and one on Bluetooth, play all 16 tracks in time.
- Bluetooth: AUTO reconnects to the last device at power-on and when it drops; the Ports page lists the devices
  around with their signal, shows a passkey a device asks for, and program changes are sent again on connect.
- Clock: internal, send (MIDI clock, start, stop on every port), or receive (follows a clock coming in).
- MIDI in plays the selected track (thru), from USB, Bluetooth or the FM-1's TRS jack.
- USB host (a synth or another FM-1 through a powered hub), Bluetooth MIDI both ways (a computer or phone connects to
  RELAY-16, and RELAY-16 connects to one Bluetooth MIDI device).

## Playing another FM-1 running LOOM-8

The new project is set up for it: T1 is LOOM-8's kit on channel 1, T2-T8 are LOOM-8's synth tracks on channels 2-8,
and the clock is sent. On the LOOM-8 FM-1, leave CLOCK on *receive* (its default).

- **Over USB:** on RELAY-16, GLOBAL (GLO) page 3 sets USB to HOST. Connect the two through a powered USB hub; the FM-1
  gives its port no power. RELAY-16 then has no computer connection until USB goes back to COMPUTER.
- **Over Bluetooth:** GLOBAL page 3 lists the Bluetooth MIDI devices around. Pick the other FM-1 with KNOB 3, then
  SEL > BT CONNECT.
- **From an SMK-37's TRS jack:** a 3.5 mm TRS cable into the LOOM-8 FM-1's MIDI IN.

LOOM-8's kit: RELAY-16 sends its eleven sounds as the notes of LOOM-8's black keys (F#3 to F#5). That mapping is a
guess from LOOM-8's manual and has not been checked on a unit. If a lane plays the wrong sound, change its note on
TRACK (EDIT) page 2 with KNOB 3 (LANE NOTE: the lane picked on Steps), and tell us.

## The controls

Both boards use the FM-1's names in this list. On the SMK-37 the buttons are printed differently:

| FM-1 | SMK-37 |
| --- | --- |
| SEL | SCALE |
| ENV | CHORD |
| LFO | NOTE REPEAT |
| EDIT | PARA |
| GLO | GLOBE |
| HOME | PATCH |
| SAVE | BT |
| PLAY | SEQ PLAY |
| REC | SEQ REC |
| SELECT | K6 |
| ALGORITHM | K7 |
| PRESETS | K8 |
| KNOB 1-4 | K1-K4 |

| Control | What it does |
| --- | --- |
| SEQ | Steps. On the FM-1, hold it and the keys play the track and set the notes a step press writes |
| EDIT | Track: device, channel, ports, transpose; page 2: type, program change, IN KEY, velocity; page 3: the conductor (follow, lead) |
| ENV | Lanes: page 1 picks each lane's CC, page 2 plays them live (and records while recording) |
| FX (SMK: KNOB BANK) | Perform: the 16 keys or pads mute the tracks; HOME held solos; FILL while black key 1 is held (SMK: FADER BANK held) |
| LFO | Overview: every track's pattern length and playhead |
| ARP | Generate: Euclid and Random; ARP again prints into the pattern |
| GLO | Global: clock and tempo, key and scale, ports (USB host, Bluetooth, TRS out), about |
| HOME | Arrange: scenes on the 16 keys or pads (again: at once); tap HOME again for Song |
| SEL | The page's menu: copy, paste, clear, double, shift, undo, scenes, the song, save, new project, all notes off |
| HOME + SEL | Undo (again: redo) |
| HOME + GLO | All notes off |
| HOME + PRESETS | The next scene |
| PLAY | Start and stop. REC held + PLAY: play and record |
| REC | Playing: recording on or off. Stopped: step record (OCT+ a rest, OCT- back; SMK: > and <) |
| SAVE | Save the project into its slot; while playing, at the stop |
| ALGORITHM | The track |
| PRESETS | The track's pattern, at the next bar (on a drum track's Steps page: its lane) |
| SELECT | The page's sub-page; with a step held, its group |
| OCT- / OCT+ | The bar shown (FM-1); the keys' octave while they play. SMK: the keybed's octave; < > the bar |

**Steps.** The 16 white keys (SMK: pads) are a bar of the pattern. A tap puts the remembered notes on a step, or
clears it. On a drum track a tap adds or removes the selected lane's hit; the black keys (SMK: a key on the keybed)
pick the lane. Hold a step to see everything about it. The knobs edit it in four groups, which the black keys 1-4
pick (SMK: < >, or SELECT on both): NOTE (pitch, velocity, length, glide), TIME (micro, ratchet, ramp, accent), LOGIC
(condition, chance) and LOCKS (the four lanes; turned below 0, no lock). Hold several steps to edit them together.
Hold a step and play keys (FM-1: with SEQ held) to give it those notes.

**The SMK-37.** The 37-key keybed always plays the selected track, with velocity. The pads are the 16 steps (or
scenes, mutes, tracks, page by page), lit in the track's colour. The four faders play lanes 1-4, and the pitch and mod
wheels go to the selected track. The TRS jack is MIDI out: everything USB sends, and tracks set to TRS alone.

## Install

From Chrome or Edge on a computer, with the unit connected by USB:
[FM-1](https://davidpryan.github.io/fm1-relay/install/),
[SMK-37 Pro](https://davidpryan.github.io/fm1-relay/install-smk37/) (and the Starrykey 37 Play),
[SMK-37 Elite](https://davidpryan.github.io/fm1-relay/install-smk37-elite/),
[MKE-P37](https://davidpryan.github.io/fm1-relay/install-smk37-mke/).

From the command line, with a package from the [releases](https://github.com/davidpryan/fm1-relay/releases):

| File | For |
| --- | --- |
| `relay-16-X.Y.fwsc` | the FM-1 |
| `relay-16-smk37-X.Y.fwsc` | the SMK-37 Pro (and the Starrykey 37 Play) |
| `relay-16-smk37-elite-X.Y.fwsc` | the SMK-37 Elite |
| `relay-16-smk37-mke-X.Y.fwsc` | the MKE-P37 |

1. Install `python-rtmidi` once: `python3 -m pip install python-rtmidi`
2. Connect the FM-1 or SMK-37 over USB, switched on, running any firmware.
3. Run `python3 base/tools/fm1_install.py relay-16-X.Y.fwsc` (from this repository)

Both installers check that the package matches the unit: an SMK-37 package is refused on an FM-1, and an FM-1
package on an SMK-37. To go back, install M-VAVE's firmware with M-UPGRADE. If a unit doesn't start, hold OCT- (SMK-37:
OCTAVE-) while switching on for the update mode, then install again.

RELAY-16 keeps four project slots in its own windows of the flash: about 90 KB each on the FM-1 and 56 KB on the
SMK-37. On the SMK-37, fm1-base's board layer never writes the calibration, the Bluetooth pairing or the key areas,
and going back to the stock firmware rebuilds the stock key mapping.

## Build

```sh
./build.sh                  # the four packages into build/relay-16/
./build.sh --release 0.2    # release packages (their identities: FM-1_7000200, SMK-37 Pro_7000200 ...)
./build.sh --host           # the host simulators and the tests (app/tests/)
python3 tools/make_pages.py 0.2 build/site   # the site, after a release build and base/tools/build.py app --web
```

You need JieLi's toolchain (`~/.jieli/toolchain`), the AC79 SDK (`~/.jieli/ac79-sdk`), Docker on macOS, and Python 3
with Pillow. The JieLi toolchain runs on Linux x86-64 only. On macOS, `base/tools/build.py` runs it in a container.

`base/` is fm1-base at keremimo's `feat/smk37-board` (charlesvestal/fm1-base#38, a349743), with felucca-bsp at
hugelton/felucca-bsp#30 (6617ee0) in `base/bsp/`. They are copied here rather than kept as submodules until both
pull requests are merged.

## Licence

MPL-2.0 (`LICENSE`), as fm1-base. fm1-base and felucca-bsp keep their own files' licences (`base/LICENSING.md`).
