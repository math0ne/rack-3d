#include "plugin.hpp"


namespace rack {
namespace core {


/** Rack 3D's audio-reactive flower. The input is split into six bands, each followed by an envelope; in the 3D view
six petals (rack3d::Element3D) around a floating hub open from a closed bud as their band gets louder. Audio passes
through untouched, and each band's level comes out as a CV.
*/
struct Bloom : Module {
	enum ParamIds {
		SENSE_PARAM,
		RELEASE_PARAM,
		OPEN_PARAM,
		HUE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,
		IN_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		ENUMS(BAND_OUTPUT, 6),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr int BANDS = 6;
	/** Band centre frequencies, Hz: bass to air, about 1.3 octaves apart. */
	static constexpr float CENTERS[BANDS] = {90.f, 220.f, 520.f, 1250.f, 3000.f, 7200.f};
	/** Levels below this (dB re 5 V, after sensitivity) leave a petal closed; 0 dB opens it fully. 30 dB keeps a full mix
	from opening every petal at once. */
	static constexpr float FLOOR_DB = -30.f;

	dsp::TBiquadFilter<float> filters[BANDS];
	float env[BANDS] = {};
	float sampleRate = 0.f;
	float attack = 0.f;
	float release = 0.f;
	dsp::ClockDivider releaseDivider;

	/** Each band's level 0..1 and how open its petal should be 0..1; written by the engine, read by the UI. */
	float level[BANDS] = {};
	float open[BANDS] = {};
	/** Loudest band's level, for the hub. */
	float peak = 0.f;

	Bloom() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SENSE_PARAM, 0.f, 1.f, 0.45f, "Sensitivity", " dB", 0.f, 42.f, -12.f);
		configParam(RELEASE_PARAM, 0.f, 1.f, 0.4f, "Release", " ms", 30.f, 50.f);
		configParam(OPEN_PARAM, 0.f, 1.f, 0.f, "Opening at rest", "%", 0.f, 100.f);
		configParam(HUE_PARAM, 0.f, 1.f, 0.9f, "Hue", "°", 0.f, 360.f);
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		const char* names[BANDS] = {"Bass", "Low", "Low mid", "Mid", "Presence", "Air"};
		for (int i = 0; i < BANDS; i++)
			configOutput(BAND_OUTPUT + i, string::f("%s band (%g Hz) level", names[i], CENTERS[i]));
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		releaseDivider.setDivision(32);
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != sampleRate) {
			sampleRate = args.sampleRate;
			for (int i = 0; i < BANDS; i++)
				filters[i].setParameters(dsp::TBiquadFilter<float>::BANDPASS, std::min(CENTERS[i] / sampleRate, 0.45f), 1.4f, 1.f);
			attack = 1.f - std::exp(-1.f / (0.004f * sampleRate));
			releaseDivider.reset();
		}
		if (releaseDivider.process() || release == 0.f) {
			float seconds = 0.05f * std::pow(30.f, params[RELEASE_PARAM].getValue());
			release = 1.f - std::exp(-1.f / (seconds * sampleRate));
		}

		// Pass the audio through, polyphonic; right follows left when unpatched
		int channelsL = inputs[IN_L_INPUT].getChannels();
		outputs[OUT_L_OUTPUT].setChannels(channelsL);
		outputs[OUT_L_OUTPUT].writeVoltages(inputs[IN_L_INPUT].getVoltages());
		Input& right = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT] : inputs[IN_L_INPUT];
		outputs[OUT_R_OUTPUT].setChannels(right.getChannels());
		outputs[OUT_R_OUTPUT].writeVoltages(right.getVoltages());

		// Analyse the mono sum
		float gain = std::pow(10.f, (-12.f + 42.f * params[SENSE_PARAM].getValue()) / 20.f);
		float x = 0.5f * (inputs[IN_L_INPUT].getVoltageSum() + right.getVoltageSum()) * gain;
		float rest = params[OPEN_PARAM].getValue();
		float loudest = 0.f;
		for (int i = 0; i < BANDS; i++) {
			float y = std::fabs(filters[i].process(x));
			env[i] += (y - env[i]) * (y > env[i] ? attack : release);
			float db = 20.f * std::log10(env[i] / 5.f + 1e-9f);
			level[i] = math::clamp(1.f - db / FLOOR_DB, 0.f, 1.f);
			open[i] = rest + (1.f - rest) * level[i];
			outputs[BAND_OUTPUT + i].setVoltage(10.f * level[i]);
			loudest = std::max(loudest, level[i]);
		}
		peak = loudest;
	}
};


constexpr float Bloom::CENTERS[];


/** Colour of a band: the hue knob sets the first petal, the others follow around the wheel. */
static NVGcolor bandColor(float hue, int band, float lightness, float alpha = 1.f) {
	float h = hue + band * 0.11f;
	h -= std::floor(h);
	NVGcolor c = nvgHSL(h, 0.85f, lightness);
	c.a = alpha;
	return c;
}


/** The petal outline in a w x h box, base at the bottom middle, tip at the top middle. */
static void petalPath(NVGcontext* vg, float w, float h) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, w / 2, h);
	nvgBezierTo(vg, w * 0.05f, h * 0.85f, -w * 0.05f, h * 0.35f, w / 2, 0);
	nvgBezierTo(vg, w * 1.05f, h * 0.35f, w * 0.95f, h * 0.85f, w / 2, h);
	nvgClosePath(vg);
}


/** One petal: closed it leans in over the hub with the others, a bud; open it lies back toward the panel. Brighter as
it opens, so loud bands glow.
*/
struct BloomPetal : rack3d::Element3D {
	Bloom* module = NULL;
	int band = 0;
	/** Shown openness, eased toward the module's. */
	float shown = 0.f;

	static constexpr float CLOSED_TILT = 150.f * M_PI / 180.f;
	static constexpr float OPEN_TILT = 10.f * M_PI / 180.f;

	BloomPetal() {
		hinge = HINGE_BOTTOM;
		flatIn2D = false;
		resolution = 3.f;
		lift = 1.f;
	}

	void step() override {
		// In the module browser (no module), show it half open
		float target = module ? module->open[band] : 0.55f;
		float dt = APP->window ? (float) APP->window->getLastFrameDuration() : 0.f;
		shown += (target - shown) * math::clamp(dt * 14.f, 0.f, 1.f);
		tilt = CLOSED_TILT + (OPEN_TILT - CLOSED_TILT) * shown;
		Element3D::step();
	}

	void drawContent(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float w = box.size.x, h = box.size.y;
		float hue = module ? module->params[Bloom::HUE_PARAM].getValue() : 0.9f;
		float glow = 0.28f + 0.36f * shown;
		petalPath(vg, w, h);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, h, 0, 0, bandColor(hue, band, glow * 0.55f), bandColor(hue, band, glow)));
		nvgFill(vg);
		nvgStrokeColor(vg, bandColor(hue, band, 0.15f));
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);
		// Midrib and veins
		nvgBeginPath(vg);
		nvgMoveTo(vg, w / 2, h - 2);
		nvgLineTo(vg, w / 2, h * 0.12f);
		for (int k = 1; k <= 3; k++) {
			float y = h * (0.85f - 0.2f * k);
			nvgMoveTo(vg, w / 2, y + 6);
			nvgLineTo(vg, w * 0.22f, y);
			nvgMoveTo(vg, w / 2, y + 6);
			nvgLineTo(vg, w * 0.78f, y);
		}
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x50));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);
	}
};


/** The hub: a disc floating above the panel's centre that glows with the loudest band. */
struct BloomHub : rack3d::Element3D {
	Bloom* module = NULL;

	BloomHub() {
		tilt = 0.f;
		lift = 9.f;
		resolution = 4.f;
	}

	void drawContent(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float r = box.size.x / 2;
		float peak = module ? module->peak : 0.5f;
		float hue = module ? module->params[Bloom::HUE_PARAM].getValue() : 0.9f;
		nvgBeginPath(vg);
		nvgCircle(vg, r, r, r - 0.5f);
		nvgFillColor(vg, nvgRGB(0x1e, 0x1f, 0x22));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, r, r, r * (0.35f + 0.45f * peak));
		NVGcolor core = bandColor(hue + 0.08f, 0, 0.3f + 0.35f * peak);
		nvgFillPaint(vg, nvgRadialGradient(vg, r, r, 0, r * 0.8f, bandColor(hue + 0.08f, 0, 0.75f), core));
		nvgFill(vg);
	}
};


/** The 2D rack's stand-in for the petals: a flat flower seen from above whose petals grow as they open. Left out of
the 3D view's texture, where the real petals are.
*/
struct BloomFlower2D : Widget {
	BloomPetal* petals[Bloom::BANDS] = {};
	math::Vec center;
	float hubRadius = 0;

	void draw(const DrawArgs& args) override {
		if (rack3d::isDrawingRackTextures())
			return;
		NVGcontext* vg = args.vg;
		for (int i = 0; i < Bloom::BANDS; i++) {
			BloomPetal* p = petals[i];
			if (!p)
				continue;
			float w = p->box.size.x, h = p->box.size.y;
			float length = 0.35f + 0.65f * p->shown;
			nvgSave(vg);
			nvgTranslate(vg, center.x, center.y);
			nvgRotate(vg, p->angle);
			nvgTranslate(vg, -w / 2, -hubRadius - h * length);
			nvgScale(vg, 1.f, length);
			float hue = p->module ? p->module->params[Bloom::HUE_PARAM].getValue() : 0.9f;
			petalPath(vg, w, h);
			nvgFillColor(vg, bandColor(hue, i, 0.3f + 0.3f * p->shown));
			nvgFill(vg);
			nvgRestore(vg);
		}
	}
};


struct BloomPanel : Widget {
	Widget* panelBorder;
	math::Vec center;
	float dishRadius = 0;

	BloomPanel() {
		panelBorder = new PanelBorder;
		addChild(panelBorder);
	}

	void step() override {
		panelBorder->box.size = box.size;
		Widget::step();
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(vg, nvgRGB(0xec, 0xeb, 0xe8));
		nvgFill(vg);

		// The dish the petals open onto
		nvgBeginPath(vg);
		nvgCircle(vg, center.x, center.y, dishRadius);
		nvgFillPaint(vg, nvgRadialGradient(vg, center.x, center.y, dishRadius * 0.2f, dishRadius, nvgRGB(0x2c, 0x2d, 0x31), nvgRGB(0x3c, 0x3d, 0x42)));
		nvgFill(vg);

		if (APP->window && APP->window->uiFont) {
			nvgFontFaceId(vg, APP->window->uiFont->handle);
			nvgFillColor(vg, nvgRGB(0x22, 0x22, 0x22));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			nvgFontSize(vg, 14);
			nvgText(vg, box.size.x / 2, 24, "BLOOM", NULL);
			nvgFontSize(vg, 8);
			nvgText(vg, box.size.x / 2, 35, "SPECTRAL FLOWER", NULL);
			const char* knobs[4] = {"SENSE", "RELEASE", "REST", "HUE"};
			for (int i = 0; i < 4; i++)
				nvgText(vg, 30 + 40 * i, 214, knobs[i], NULL);
			nvgText(vg, box.size.x / 2, 256, "BAND LEVELS", NULL);
			const char* io[4] = {"IN L", "IN R", "OUT L", "OUT R"};
			for (int i = 0; i < 4; i++)
				nvgText(vg, 30 + 40 * i, 318, io[i], NULL);
		}
		// Output section backing
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 92, 322, 78, 30, 4);
		nvgFillColor(vg, nvgRGB(0x2a, 0x2b, 0x2e));
		nvgFill(vg);
		Widget::draw(args);
	}
};


struct BloomWidget : ModuleWidget {
	BloomWidget(Bloom* module) {
		setModule(module);
		box.size = Vec(RACK_GRID_WIDTH * 12, RACK_GRID_HEIGHT);
		const Vec center(box.size.x / 2, 128);
		const float hubRadius = 12.f;
		const Vec petalSize(28, 48);

		BloomPanel* panel = new BloomPanel;
		panel->box.size = box.size;
		panel->center = center;
		panel->dishRadius = hubRadius + petalSize.y + 6;
		setPanel(panel);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Petals around the hub: each box's bottom middle (its hinge) on the hub's rim, turned to point outward
		BloomFlower2D* flower = new BloomFlower2D;
		flower->box.size = box.size;
		flower->center = center;
		flower->hubRadius = hubRadius;
		addChild(flower);
		for (int i = 0; i < Bloom::BANDS; i++) {
			float a = 2 * M_PI * i / Bloom::BANDS;
			Vec rim = center.plus(Vec(std::sin(a), -std::cos(a)).mult(hubRadius));
			BloomPetal* petal = new BloomPetal;
			petal->module = module;
			petal->band = i;
			petal->angle = a;
			petal->box.pos = rim.minus(Vec(petalSize.x / 2, petalSize.y));
			petal->box.size = petalSize;
			addChild(petal);
			flower->petals[i] = petal;
		}
		BloomHub* hub = new BloomHub;
		hub->module = module;
		hub->box.size = Vec(2 * hubRadius + 4, 2 * hubRadius + 4);
		hub->box.pos = center.minus(hub->box.size.div(2));
		addChild(hub);

		for (int i = 0; i < 4; i++)
			addParam(createParamCentered<RoundSmallBlackKnob>(Vec(30 + 40 * i, 230), module, Bloom::SENSE_PARAM + i));
		for (int i = 0; i < Bloom::BANDS; i++)
			addOutput(createOutputCentered<PJ301MPort>(Vec(22.5f + 27 * i, 280), module, Bloom::BAND_OUTPUT + i));
		addInput(createInputCentered<PJ301MPort>(Vec(30, 337), module, Bloom::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(70, 337), module, Bloom::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(110, 337), module, Bloom::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(150, 337), module, Bloom::OUT_R_OUTPUT));
	}
};


Model* modelBloom = createModel<Bloom, BloomWidget>("Bloom");


} // namespace core
} // namespace rack
