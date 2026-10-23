#include <cmath>

#include <rack3d/SettingsMenu.hpp>
#include <rack3d/settings.hpp>
#include <ui/Slider.hpp>
#include <ui/MenuSeparator.hpp>
#include <helpers.hpp>
#include <string.hpp>


namespace rack {
namespace rack3d {


/** A slider over a float (or integer) setting. */
struct SettingQuantity : Quantity {
	float* value = NULL;
	int* intValue = NULL;
	std::string label;
	std::string unit;
	float minValue = 0;
	float maxValue = 1;
	float defaultValue = 0;
	int precision = 2;

	void setValue(float v) override {
		v = math::clamp(v, minValue, maxValue);
		if (intValue)
			*intValue = (int) std::round(v);
		else
			*value = v;
	}
	float getValue() override {
		return intValue ? (float) *intValue : *value;
	}
	float getMinValue() override {
		return minValue;
	}
	float getMaxValue() override {
		return maxValue;
	}
	float getDefaultValue() override {
		return defaultValue;
	}
	int getDisplayPrecision() override {
		return precision;
	}
	std::string getDisplayValueString() override {
		if (intValue)
			return string::f("%d", *intValue);
		return string::f("%.*f", precision, *value);
	}
	std::string getLabel() override {
		return label;
	}
	std::string getUnit() override {
		return unit;
	}
};


struct SettingSlider : ui::Slider {
	SettingSlider(SettingQuantity* q) {
		quantity = q;
		box.size.x = 250.0;
	}
	~SettingSlider() {
		delete quantity;
	}
};


static SettingSlider* createSlider(float* value, std::string label, float min, float max, float def, int precision, std::string unit = "") {
	SettingQuantity* q = new SettingQuantity;
	q->value = value;
	q->label = label;
	q->minValue = min;
	q->maxValue = max;
	q->defaultValue = def;
	q->precision = precision;
	q->unit = unit;
	return new SettingSlider(q);
}


static SettingSlider* createIntSlider(int* value, std::string label, int min, int max, int def) {
	SettingQuantity* q = new SettingQuantity;
	q->intValue = value;
	q->label = label;
	q->minValue = min;
	q->maxValue = max;
	q->defaultValue = def;
	q->precision = 0;
	return new SettingSlider(q);
}


static void appendCableMenu(ui::Menu* menu) {
	RopeParams& p = settings::cableParams;
	const RopeParams d = settings::defaultCableParams();

	menu->addChild(createMenuLabel("Shape"));
	menu->addChild(createIntSlider(&p.segments, "Segments", 8, 80, d.segments));
	menu->addChild(createSlider(&p.slack, "Slack", 1.f, 2.f, d.slack, 2));
	menu->addChild(createSlider(&p.radius, "Thickness", 0.004f, 0.04f, d.radius, 3));

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("Stiffness"));
	menu->addChild(createSlider(&p.stiffness, "Stretch stiffness", 0.05f, 1.f, d.stiffness, 2));
	menu->addChild(createSlider(&p.bend, "Bend stiffness", 0.f, 1.f, d.bend, 2));
	menu->addChild(createIntSlider(&p.iterations, "Solver iterations", 1, 60, d.iterations));

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("Motion"));
	menu->addChild(createSlider(&p.damping, "Air damping", 0.98f, 1.f, d.damping, 4));
	menu->addChild(createSlider(&p.internalDamping, "Internal damping", 0.f, 1.f, d.internalDamping, 2));
	menu->addChild(createSlider(&p.gravity, "Gravity", 0.f, 30.f, d.gravity, 1));
	menu->addChild(createSlider(&p.friction, "Surface friction", 0.f, 1.f, d.friction, 2));
	menu->addChild(createSlider(&p.bounce, "Bounce off model", 0.f, 1.f, d.bounce, 2));

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("3D view"));
	menu->addChild(createSlider(&settings::spinDamping, "Spin damping", 0.f, 6.f, settings::DEFAULT_SPIN_DAMPING, 1));

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuItem("Reset to defaults", "", []() {
		settings::resetCableSettings();
	}));
}


void appendViewMenu(ui::Menu* menu) {
	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("Rack 3D"));
	menu->addChild(createBoolPtrMenuItem("Rack placements (experimental)", "", &settings::placementsEnabled));
	menu->addChild(createSubmenuItem("Cable physics", "", [](ui::Menu* menu) {
		appendCableMenu(menu);
	}));
	menu->addChild(createSubmenuItem("Scene", "", [](ui::Menu* menu) {
		menu->addChild(createBoolPtrMenuItem("Concrete plaza, sun and fog", "", &settings::environment));
		menu->addChild(createSlider(&settings::fog, "Fog", 0.f, 3.f, 1.f, 2));
		menu->addChild(createSlider(&settings::cableGlow, "Cable glow", 0.f, 1.f, 0.35f, 2));
		menu->addChild(createSlider(&settings::brightness, "Brightness", 0.2f, 1.5f, 0.55f, 2));
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Auto-orbit", "F8", &settings::autoOrbit));
		menu->addChild(createSlider(&settings::orbitSpeed, "Orbit speed", 0.1f, 4.f, 1.f, 2));
	}));
}


} // namespace rack3d
} // namespace rack
