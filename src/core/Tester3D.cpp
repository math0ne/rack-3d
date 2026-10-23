#include "plugin.hpp"


namespace rack {
namespace core {


/** Rack 3D's test module for rack3d::Element3D: a display fin hinged to the panel that stands out of it in the 3D
view. Its knobs tilt the fin and change the wave it shows, so it exercises an element that moves and redraws live.
*/
struct Tester3D : Module {
	enum ParamIds {
		TILT_PARAM,
		CYCLES_PARAM,
		HUE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** The wave's scroll position, 0..1, advanced by the engine. */
	float phase = 0.f;

	Tester3D() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TILT_PARAM, 0.f, 1.f, 0.5f, "Fin angle", "°", 0.f, 85.f);
		configParam(CYCLES_PARAM, 0.f, 1.f, 0.3f, "Wave cycles", "", 0.f, 3.8f, 0.2f);
		configParam(HUE_PARAM, 0.f, 1.f, 0.55f, "Hue", "°", 0.f, 360.f);
	}

	void process(const ProcessArgs& args) override {
		phase += 0.25f * args.sampleTime;
		phase -= std::floor(phase);
	}
};


/** The fin: a small scope screen showing a scrolling sine wave, tilted out of the panel by the angle knob. */
struct Tester3DFin : rack3d::Element3D {
	Tester3D* module = NULL;

	Tester3DFin() {
		hinge = HINGE_BOTTOM;
		lift = 1.f;
		resolution = 3.f;
	}

	float param(int id, float def) {
		return module ? module->params[id].getValue() : def;
	}

	void step() override {
		// Ease toward the knob's angle, so turning the knob swings the fin smoothly
		float target = param(Tester3D::TILT_PARAM, 0.5f) * 85.f * M_PI / 180.f;
		float dt = APP->window ? (float) APP->window->getLastFrameDuration() : 0.f;
		tilt += (target - tilt) * math::clamp(dt * 12.f, 0.f, 1.f);
		Element3D::step();
	}

	void drawContent(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float w = box.size.x, h = box.size.y;
		float hue = param(Tester3D::HUE_PARAM, 0.55f);
		float cycles = 0.2f + 3.8f * param(Tester3D::CYCLES_PARAM, 0.3f);
		float phase = module ? module->phase : 0.f;
		NVGcolor accent = nvgHSL(hue, 0.85f, 0.6f);

		// Screen
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 4);
		nvgFillColor(vg, nvgRGB(0x12, 0x15, 0x1c));
		nvgFill(vg);
		nvgStrokeColor(vg, accent);
		nvgStrokeWidth(vg, 1.5);
		nvgStroke(vg);

		// Grid
		nvgBeginPath(vg);
		for (int i = 1; i < 4; i++) {
			nvgMoveTo(vg, w * i / 4, 4);
			nvgLineTo(vg, w * i / 4, h - 4);
		}
		nvgMoveTo(vg, 4, h / 2);
		nvgLineTo(vg, w - 4, h / 2);
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x28));
		nvgStrokeWidth(vg, 0.75);
		nvgStroke(vg);

		// Wave
		nvgBeginPath(vg);
		const int N = 96;
		for (int i = 0; i <= N; i++) {
			float x = 6 + (w - 12) * i / N;
			float y = h / 2 - (h / 2 - 12) * std::sin(2 * M_PI * (cycles * i / N - phase));
			if (i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgStrokeColor(vg, accent);
		nvgStrokeWidth(vg, 2.0);
		nvgLineCap(vg, NVG_ROUND);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);

		// Readout
		if (APP->window && APP->window->uiFont) {
			nvgFontFaceId(vg, APP->window->uiFont->handle);
			nvgFontSize(vg, 9);
			nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xc0));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			std::string angle = string::f("%.0f°", param(Tester3D::TILT_PARAM, 0.5f) * 85.f);
			nvgText(vg, 6, 4, angle.c_str(), NULL);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			std::string label = string::f("%.1f cyc", cycles);
			nvgText(vg, w - 6, 4, label.c_str(), NULL);
		}
		Element3D::drawContent(args);
	}
};


/** A plain light panel drawn with NanoVG: title, the bay the fin swings out of, and knob labels. */
struct Tester3DPanel : Widget {
	Widget* panelBorder;
	math::Rect bay;

	Tester3DPanel() {
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

		// The bay behind the fin: a dark recess, seen when the fin swings out in 3D
		nvgBeginPath(vg);
		nvgRoundedRect(vg, bay.pos.x - 2, bay.pos.y - 2, bay.size.x + 4, bay.size.y + 4, 5);
		nvgFillColor(vg, nvgRGB(0x2a, 0x2b, 0x2e));
		nvgFill(vg);

		if (APP->window && APP->window->uiFont) {
			nvgFontFaceId(vg, APP->window->uiFont->handle);
			nvgFillColor(vg, nvgRGB(0x22, 0x22, 0x22));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			nvgFontSize(vg, 13);
			nvgText(vg, box.size.x / 2, 26, "3D TESTER", NULL);
			nvgFontSize(vg, 8);
			nvgText(vg, box.size.x / 2, 38, "RACK 3D ELEMENT", NULL);
			nvgFontSize(vg, 9);
			const char* labels[3] = {"ANGLE", "CYCLES", "HUE"};
			for (int i = 0; i < 3; i++)
				nvgText(vg, box.size.x / 2, 180 + 62 * i, labels[i], NULL);
		}
		Widget::draw(args);
	}
};


struct Tester3DWidget : ModuleWidget {
	Tester3DWidget(Tester3D* module) {
		setModule(module);
		box.size = Vec(RACK_GRID_WIDTH * 8, RACK_GRID_HEIGHT);

		Tester3DPanel* panel = new Tester3DPanel;
		panel->box.size = box.size;
		panel->bay = math::Rect(10, 52, box.size.x - 20, 96);
		setPanel(panel);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		Tester3DFin* fin = new Tester3DFin;
		fin->module = module;
		fin->box = panel->bay;
		addChild(fin);

		for (int i = 0; i < 3; i++)
			addParam(createParamCentered<RoundBlackKnob>(Vec(box.size.x / 2, 205 + 62 * i), module, Tester3D::TILT_PARAM + i));
	}
};


Model* modelTester3D = createModel<Tester3D, Tester3DWidget>("Tester3D");


} // namespace core
} // namespace rack
