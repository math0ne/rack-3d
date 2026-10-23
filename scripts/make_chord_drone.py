"""Builds Rack/examples/chord-drone.vcv: a six-voice drone that glides slowly through a chord progression, spread over the
six faces of the Rack 3D cube, using white-panel modules (Mutable Instruments, Fundamental, Valley Plateau, Core).

Port and parameter ids come from build-tools/models.json (bash Rack/scripts/rack3d-tool.sh dump ...), looked up by name, and
every value is range-checked, so a typo fails here instead of making a silently wrong patch.
"""
import json
import math
import os
import random

RACK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# models.json is generated beside the checkout, from the plugins installed there
MODELS = json.load(open(os.path.join(os.path.dirname(RACK), "build-tools", "models.json"), encoding="utf-8"))
OUT = os.path.join(RACK, "examples", "chord-drone.vcv")

# The default Rack 3D cube: 6 racks of 2 rows x 50 HP, stacked with one gutter row
RACK_HP, RACK_ROWS = 50, 2
FACES = ["front", "right", "back", "left", "top", "bottom"]

modules = []
cables = []


def model_info(plugin, model):
	return MODELS[plugin][model]


def port_id(info, kind, name):
	for p in info[kind]:
		if p["name"] == name:
			return p["id"]
	raise KeyError(f"no {kind[:-1]} named {name!r}; have {[p['name'] for p in info[kind]]}")


class Module:
	def __init__(self, plugin, model, params=None, data=None):
		self.plugin, self.model = plugin, model
		self.info = model_info(plugin, model)
		self.id = len(modules) + 1
		self.params = {}
		for name, value in (params or {}).items():
			p = next((q for q in self.info["params"] if q["name"] == name), None)
			if p is None:
				raise KeyError(f"{model}: no param {name!r}")
			if not (p["min"] - 1e-6 <= value <= p["max"] + 1e-6):
				raise ValueError(f"{model}: {name} = {value} outside {p['min']}..{p['max']}")
			self.params[p["id"]] = value
		self.data = data
		self.pos = None
		modules.append(self)

	@property
	def hp(self):
		# Blank's width comes from its data, not its default size
		if self.model == "Blank" and self.data:
			return self.data["width"]
		return self.info["hp"]

	def out(self, name):
		return (self, port_id(self.info, "outputs", name))

	def inp(self, name):
		return (self, port_id(self.info, "inputs", name))

	def json(self):
		j = {
			"id": self.id,
			"plugin": self.plugin,
			"model": self.model,
			"version": MODELS[self.plugin]["version"],
			"params": [{"id": i, "value": v} for i, v in sorted(self.params.items())],
			"pos": list(self.pos),
		}
		if self.data is not None:
			j["data"] = self.data
		return j


def cable(out, inp, color=None):
	"""Connects `out` to `inp`. With no color, the cable gets a random shade of white or grey."""
	(om, oid), (im, iid) = out, inp
	cables.append({"id": len(cables) + 1, "outputModuleId": om.id, "outputId": oid, "inputModuleId": im.id, "inputId": iid, "color": color or shade()})


def place(face, row, *ms):
	"""Places modules side by side on a face's row, centred."""
	width = sum(m.hp for m in ms)
	assert width <= RACK_HP, f"{FACES[face]} row {row}: {width} HP > {RACK_HP}"
	x = (RACK_HP - width) // 2
	y = face * (RACK_ROWS + 1) + row
	for m in ms:
		m.pos = (x, y)
		x += m.hp


# --- Chords: one note per voice, low to high, as semitones from C4 (Plaits and Braids tune from C4 at 0 V) ---
CHORDS = [
	("Cmaj9", [-24, -17, -8, -1, 2, 7]),   # C2 G2 E3 B3 D4 G4
	("Am9", [-27, -20, -5, -1, 0, 4]),     # A1 E2 G3 B3 C4 E4
	("Fmaj9", [-19, -12, -8, -3, 4, 7]),   # F2 C3 E3 A3 E4 G4
	("G6/9", [-17, -10, -5, 2, 4, 9]),     # G2 D3 G3 D4 E4 A4
]
CHORD_SECONDS = 14
# Pitch glide between chords, in ms per volt (an octave): a few semitones take a few hundred ms
GLIDE_MS_PER_V = 1500


# Two SEQ3s hold the progression: SEQ3 A steps through voices 1-3, SEQ3 B voices 4-6, both clocked by one slow LFO
def seq3(voices):
	params = {"Steps": len(CHORDS)}
	for row, v in enumerate(voices):
		for step, (_, notes) in enumerate(CHORDS):
			params[f"CV {row + 1} step {step + 1}"] = notes[v] / 12
	return Module("Fundamental", "SEQ3", params)


seq_a = seq3([0, 1, 2])
seq_b = seq3([3, 4, 5])
# Square LFO as the chord clock; unipolar so each rising edge is a clean trigger
clock = Module("Fundamental", "LFO", {"Offset": 1, "Frequency": math.log2(1 / CHORD_SECONDS)})
# Merge the six pitches into one polyphonic cable, glide them together, split them back out
merge = Module("Fundamental", "Merge")
glide = Module("Fundamental", "Process", {"Slew": math.log2(GLIDE_MS_PER_V / 1000)})
split = Module("Fundamental", "Split")


BRAIDS_MODELS = 46


def braids_model(index):
	return index / BRAIDS_MODELS


# --- Voices (pitch comes from the sequencers, so the frequency knobs sit at C4) ---
# Plaits engines (saved as data "model"): 4 harmonic (additive), 5 wavetable.
# With nothing in TRIGGER, Plaits sounds continuously.
v1 = Module("AudibleInstruments", "Plaits", {
	"Frequency": 0, "Harmonics": 0.3, "Timbre": 0.35, "Morph": 0.3, "Timbre CV": 0.3,
}, data={"lowCpu": False, "model": 4})
v2 = Module("AudibleInstruments", "Plaits", {
	"Frequency": 0, "Harmonics": 0.2, "Timbre": 0.4, "Morph": 0.5, "Morph CV": 0.3,
}, data={"lowCpu": False, "model": 5})
v3 = Module("AudibleInstruments", "Plaits", {
	"Frequency": 0, "Harmonics": 0.5, "Timbre": 0.35, "Morph": 0.4, "Timbre CV": 0.25,
}, data={"lowCpu": False, "model": 4})


# Braids: coarse is octaves from C4; shapes 24 HARMONICS, 23 VOWEL_FOF, 3 SINE_TRIANGLE (all continuous).
# Braids' model param is shape index / 46.
def braids(shape, timbre):
	return Module("AudibleInstruments", "Braids", {
		"Coarse frequency": 0.0, "Fine frequency": 0.0,
		"Model": braids_model(shape), "Timbre": timbre, "Color": 0.5,
	})


v4 = braids(24, 0.4)
v5 = braids(23, 0.45)
v6 = braids(3, 0.3)
voices = [v1, v2, v3, v4, v5, v6]

# --- Slow timbre movement: two bipolar LFOs (frequency is log2 Hz), scaled down by Shades for Braids ---
lfo_a = Module("Fundamental", "LFO", {"Offset": 0, "Frequency": math.log2(0.05)})
lfo_b = Module("Fundamental", "LFO", {"Offset": 0, "Frequency": math.log2(0.031)})
# Shades in attenuverter mode: 0.5 is zero, so 0.62 passes about a quarter of the LFO
shades = Module("AudibleInstruments", "Shades", {
	"Channel 1 gain": 0.62, "Channel 2 gain": 0.6, "Channel 3 gain": 0.62,
	"Channel 1 mode": 1, "Channel 2 mode": 1, "Channel 3 mode": 1,
})

# --- Mix: Veils levels (linear response) -> Fundamental Mixer. Bass a little louder, top voices softer. ---
veils_a = Module("AudibleInstruments", "Veils", {
	"Channel 1 gain": 0.55, "Channel 2 gain": 0.5, "Channel 3 gain": 0.45, "Channel 4 gain": 0.32,
	"Channel 1 response": 1, "Channel 2 response": 1, "Channel 3 response": 1, "Channel 4 response": 1,
})
veils_b = Module("AudibleInstruments", "Veils", {
	"Channel 1 gain": 0.25, "Channel 2 gain": 0.3,
	"Channel 1 response": 1, "Channel 2 response": 1,
})
mixer = Module("Fundamental", "Mixer", {"Level": 1.0})

# --- Effects: delay -> Clouds (grain shimmer) -> Plateau (large reverb) -> audio out ---
# Delay time shows as 0.001 * 10000^value seconds; 0.75 s:
delay = Module("Fundamental", "Delay", {
	"Time": math.log(750) / math.log(10000), "Feedback": 0.45, "Tone": 0.4, "Mix": 0.25,
})
clouds = Module("AudibleInstruments", "Clouds", {
	"Grain position": 0.4, "Grain size": 0.85, "Grain pitch": 0.0, "Audio input gain": 1.0,
	"Grain density": 0.6, "Grain texture": 0.8, "Dry/wet": 0.3, "Stereo spread": 0.8,
	"Feedback amount": 0.3, "Reverb amount": 0.3,
}, data={"blendMode": 0, "playback": 0, "quality": 0})
# Plateau: a long, soft hall with slow modulation; panelStyle 1 is its light panel
plateau = Module("Valley", "Plateau", {
	"Dry level": 0.65, "Wet level": 0.8, "Pre-delay": 0.04, "Size": 0.9, "Diffusion": 10.0, "Decay": 0.93,
	"Reverb high cut": 7.0, "Modulation rate": 0.15, "Modulation shape": 0.5, "Modulation depth": 3.0,
}, data={"diffuseInput": 1, "freezeToggle": False, "frozen": False, "inputSensitivity": 0, "outputSaturation": 0,
	"panelStyle": 1, "preDelayCVSens": 0, "tuned": 0})
# DirectSound's "Primary Sound Driver" follows the Windows default output, so the patch sounds without picking a
# device (an Audio module saved with no device stays silent). Pick another in the module's menu if you like.
# About -4 dB: Plateau peaks near 5 V (-6 dBFS) and its long tail keeps building
AUDIO_LEVEL = 0.8
audio = Module("Core", "AudioInterface2", {"Level": AUDIO_LEVEL}, data={
	"audio": {"driver": 8, "deviceName": "Primary Sound Driver", "sampleRate": 48000.0, "blockSize": 512,
		"inputOffset": 0, "outputOffset": 0},
	"dcFilter": True,
})

# --- Layout: one stage of the signal per face, so cables wrap around the cube ---
place(0, 0, v1, v2, v3)                  # front: Plaits voices
place(0, 1, seq_a, clock)                #        chord sequencer A + its clock
place(1, 0, v4, v5, v6)                  # right: Braids voices
place(1, 1, seq_b, lfo_b, shades)        #        chord sequencer B + timbre LFO
place(2, 0, veils_a, veils_b, mixer)     # back: mix
place(2, 1, merge, glide, split)         #       pitch glide
place(3, 0, delay, lfo_a)                # left: delay + timbre LFO
place(4, 0, clouds, plateau)             # top: Clouds + Plateau
notes = Module("Core", "Notes", data={"text": "Chord drone\n\nCmaj9 - Am9 - Fmaj9 - G6/9, about 14 s each.\n"
	"Front/right: voices + chord sequencers\nBack: mix + pitch glide\nLeft: delay\nTop: Clouds -> Plateau\n"
	"Bottom: audio out\n\nEverything unpatched is decoration."})
place(5, 0, audio, notes)                # bottom: out


# --- Decoration: fill every row's free space with random unpatched modules, and Blanks where only a few HP remain ---
# Audio and MIDI interfaces are left out so nothing grabs a device.
FILL_SEED = 7
FILL_POOL = [("Fundamental", k) for k in MODELS["Fundamental"] if k != "version"] + \
	[("AudibleInstruments", k) for k in MODELS["AudibleInstruments"] if k != "version"] + [("Core", "Notes")]
# Below this many HP, a gap gets a Blank instead of another module
BLANK_BELOW = 6
BLANK_MIN = 3


def fill_gap(rng, x, y, width):
	while width > 0:
		if width < BLANK_BELOW:
			m = Module("Core", "Blank", data={"width": width})
		else:
			# A module may leave nothing, or at least BLANK_MIN HP for a Blank
			fits = [pm for pm in FILL_POOL if MODELS[pm[0]][pm[1]]["hp"] == width or
				width - MODELS[pm[0]][pm[1]]["hp"] >= BLANK_MIN]
			m = Module(*rng.choice(fits))
		m.pos = (x, y)
		x += m.hp
		width -= m.hp


def fill_rows():
	rng = random.Random(FILL_SEED)
	for face in range(len(FACES)):
		for row in range(RACK_ROWS):
			y = face * (RACK_ROWS + 1) + row
			taken = sorted((m.pos[0], m.pos[0] + m.hp) for m in modules if m.pos and m.pos[1] == y)
			x = 0
			for start, end in taken + [(RACK_HP, RACK_HP)]:
				fill_gap(rng, x, y, start - x)
				x = end
			assert sum(m.hp for m in modules if m.pos[1] == y) == RACK_HP, f"{FACES[face]} row {row} not full"


fill_rows()

# --- Cables: whites and greys, each cable its own shade (seeded, so the patch is the same every time) ---
CABLE_SHADES = ["#ffffff", "#f2f0ec", "#e3e3e3", "#d0d0d0", "#bdbdbd", "#a8a8a8", "#949494", "#7d7d7d"]
cable_rng = random.Random(11)


def shade():
	return cable_rng.choice(CABLE_SHADES)


AUDIO = MOD = OUT_C = PITCH = None
cable(clock.out("Square"), seq_a.inp("Clock"), PITCH)
cable(clock.out("Square"), seq_b.inp("Clock"), PITCH)
for i, (seq, row) in enumerate([(seq_a, 1), (seq_a, 2), (seq_a, 3), (seq_b, 1), (seq_b, 2), (seq_b, 3)]):
	cable(seq.out(f"CV {row}"), merge.inp(f"Channel {i + 1}"), PITCH)
cable(merge.out("Polyphonic"), glide.inp("Voltage"), PITCH)
cable(glide.out("Slew"), split.inp("Polyphonic"), PITCH)
for i, voice in enumerate(voices):
	cable(split.out(f"Channel {i + 1}"), voice.inp("Pitch (1V/oct)"), PITCH)

for voice, ch in [(v1, 1), (v2, 2), (v3, 3), (v4, 4)]:
	cable(voice.out("Main" if voice.model == "Plaits" else "Audio"), veils_a.inp(f"Channel {ch}"), AUDIO)
cable(v5.out("Audio"), veils_b.inp("Channel 1"), AUDIO)
cable(v6.out("Audio"), veils_b.inp("Channel 2"), AUDIO)
for i, ch in enumerate([1, 2, 3, 4]):
	cable(veils_a.out(f"Channel {ch}"), mixer.inp(f"Channel {i + 1}"), AUDIO)
cable(veils_b.out("Channel 1"), mixer.inp("Channel 5"), AUDIO)
cable(veils_b.out("Channel 2"), mixer.inp("Channel 6"), AUDIO)

cable(mixer.out("Mix"), delay.inp("Audio"), OUT_C)
cable(delay.out("Mix"), clouds.inp("Left"), OUT_C)
cable(clouds.out("Left"), plateau.inp("Left"), OUT_C)
cable(clouds.out("Right"), plateau.inp("Right"), OUT_C)
cable(plateau.out("Left"), audio.inp('To "device output 1"'), OUT_C)
cable(plateau.out("Right"), audio.inp('To "device output 2"'), OUT_C)

cable(lfo_a.out("Triangle"), v1.inp("Timbre"), MOD)
cable(lfo_b.out("Sine"), v2.inp("Morph"), MOD)
cable(lfo_b.out("Triangle"), v3.inp("Timbre"), MOD)
cable(lfo_a.out("Sine"), shades.inp("Channel 1"), MOD)
cable(lfo_b.out("Triangle"), shades.inp("Channel 2"), MOD)
cable(lfo_b.out("Sine"), shades.inp("Channel 3"), MOD)
cable(shades.out("Channel 1"), v4.inp("Timbre"), MOD)
cable(shades.out("Channel 2"), v5.inp("Color"), MOD)
cable(shades.out("Channel 3"), v6.inp("Timbre"), MOD)

patch = {
	"version": "2.6.6",
	"rack3d": {
		"version": 1,
		"placements": [{"id": f, "grid": [0, f * (RACK_ROWS + 1), RACK_HP, RACK_ROWS]} for f in range(6)],
	},
	"modules": [m.json() for m in modules],
	"cables": cables,
}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
	json.dump(patch, f, indent=1)

print(f"wrote {OUT}: {len(modules)} modules, {len(cables)} cables")
for name, notes in CHORDS:
	print(f"  {name:6} " + " ".join(f"{n:+d}" for n in notes))
for m in modules:
	print(f"  {FACES[m.pos[1] // (RACK_ROWS + 1)]:6} row {m.pos[1] % (RACK_ROWS + 1)} x {m.pos[0]:2}: {m.plugin}/{m.model} ({m.hp} HP)")
