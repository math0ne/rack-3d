#include <cstdlib>

#include <rack3d/Tools.hpp>
#include <rack3d/ModelView.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/RackWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/Scene.hpp>
#include <engine/Engine.hpp>
#include <engine/ParamQuantity.hpp>
#include <engine/PortInfo.hpp>
#include <plugin.hpp>
#include <audio.hpp>
#include <patch.hpp>
#include <window/Window.hpp>
#include <context.hpp>
#include <string.hpp>
#include <system.hpp>


namespace rack {
namespace rack3d {


static std::string env(const char* name) {
	const char* v = std::getenv(name);
	return v ? v : "";
}


static int dumpModels() {
	std::string out = env("RACK3D_TOOL_OUT");
	std::vector<std::string> slugs = string::split(env("RACK3D_TOOL_PLUGINS"), ",");
	if (out.empty() || slugs.empty()) {
		WARN("dump: set RACK3D_TOOL_OUT and RACK3D_TOOL_PLUGINS");
		return 1;
	}
	json_t* rootJ = json_object();
	for (const std::string& slug : slugs) {
		plugin::Plugin* plugin = plugin::getPlugin(slug);
		if (!plugin) {
			WARN("dump: plugin %s not found", slug.c_str());
			continue;
		}
		json_t* pluginJ = json_object();
		for (plugin::Model* model : plugin->models) {
			INFO("dump: %s/%s", slug.c_str(), model->slug.c_str());
			engine::Module* module = model->createModule();
			json_t* modelJ = json_object();
			json_object_set_new(modelJ, "name", json_string(model->name.c_str()));

			// Width needs the widget, which owns the module and removes it from the engine when deleted
			APP->engine->addModule(module);
			app::ModuleWidget* mw = model->createModuleWidget(module);
			json_object_set_new(modelJ, "hp", json_integer((int) std::round(mw->box.size.x / app::RACK_GRID_WIDTH)));

			json_t* paramsJ = json_array();
			for (int i = 0; i < (int) module->paramQuantities.size(); i++) {
				engine::ParamQuantity* pq = module->paramQuantities[i];
				json_t* pJ = json_object();
				json_object_set_new(pJ, "id", json_integer(i));
				json_object_set_new(pJ, "name", json_string(pq->getLabel().c_str()));
				json_object_set_new(pJ, "min", json_real(pq->getMinValue()));
				json_object_set_new(pJ, "max", json_real(pq->getMaxValue()));
				json_object_set_new(pJ, "default", json_real(pq->getDefaultValue()));
				json_object_set_new(pJ, "displayBase", json_real(pq->displayBase));
				json_object_set_new(pJ, "displayMultiplier", json_real(pq->displayMultiplier));
				json_object_set_new(pJ, "displayOffset", json_real(pq->displayOffset));
				json_object_set_new(pJ, "unit", json_string(pq->unit.c_str()));
				json_object_set_new(pJ, "snap", json_boolean(pq->snapEnabled));
				if (engine::SwitchQuantity* sq = dynamic_cast<engine::SwitchQuantity*>(pq)) {
					json_t* labelsJ = json_array();
					for (const std::string& l : sq->labels)
						json_array_append_new(labelsJ, json_string(l.c_str()));
					json_object_set_new(pJ, "labels", labelsJ);
				}
				json_array_append_new(paramsJ, pJ);
			}
			json_object_set_new(modelJ, "params", paramsJ);

			auto ports = [](const std::vector<engine::PortInfo*>& infos) {
				json_t* portsJ = json_array();
				for (int i = 0; i < (int) infos.size(); i++) {
					json_t* pJ = json_object();
					json_object_set_new(pJ, "id", json_integer(i));
					json_object_set_new(pJ, "name", json_string(infos[i]->getName().c_str()));
					json_array_append_new(portsJ, pJ);
				}
				return portsJ;
			};
			json_object_set_new(modelJ, "inputs", ports(module->inputInfos));
			json_object_set_new(modelJ, "outputs", ports(module->outputInfos));
			// Default saved state (e.g. Plaits keeps its engine here), to show which keys a patch can set
			json_t* dataJ = module->dataToJson();
			if (dataJ)
				json_object_set_new(modelJ, "data", dataJ);
			json_object_set_new(pluginJ, model->slug.c_str(), modelJ);
			delete mw;
		}
		json_object_set_new(pluginJ, "version", json_string(plugin->version.c_str()));
		json_object_set_new(rootJ, slug.c_str(), pluginJ);
	}
	int err = json_dump_file(rootJ, out.c_str(), JSON_INDENT(1) | JSON_SORT_KEYS);
	json_decref(rootJ);
	INFO("dump: wrote %s", out.c_str());
	return err == 0 ? 0 : 1;
}


static int previewPatch() {
	std::string path = env("RACK3D_TOOL_PATCH");
	std::string out = env("RACK3D_TOOL_OUT");
	if (path.empty() || out.empty()) {
		WARN("preview: set RACK3D_TOOL_PATCH and RACK3D_TOOL_OUT");
		return 1;
	}
	// What the file asks for
	json_error_t error;
	json_t* fileJ = json_load_file(path.c_str(), 0, &error);
	if (!fileJ) {
		WARN("preview: %s: JSON error at line %d: %s", path.c_str(), error.line, error.text);
		return 1;
	}
	size_t wantModules = json_array_size(json_object_get(fileJ, "modules"));
	size_t wantCables = json_array_size(json_object_get(fileJ, "cables"));
	json_decref(fileJ);

	try {
		APP->patch->load(path);
	}
	catch (Exception& e) {
		WARN("preview: %s", e.what());
		return 1;
	}
	app::RackWidget* rack = APP->scene->rack;
	size_t gotModules = rack->getModules().size();
	size_t gotCables = rack->getCables().size();
	int failures = 0;
	if (gotModules != wantModules) {
		WARN("preview: %d of %d modules loaded", (int) gotModules, (int) wantModules);
		failures++;
	}
	if (gotCables != wantCables) {
		WARN("preview: %d of %d cables loaded", (int) gotCables, (int) wantCables);
		failures++;
	}
	int outside = 0;
	for (app::ModuleWidget* mw : rack->getModules()) {
		if (!rack->getLayout()->placementOf(mw->getGridBox())) {
			WARN("preview: %s is outside every rack", mw->model->getFullName().c_str());
			outside++;
		}
	}
	failures += outside;
	INFO("preview: %d modules, %d cables, %d outside racks", (int) gotModules, (int) gotCables, outside);

	// Signal levels: peak of each module's first output over 2 s (the engine runs on its fallback thread), to catch
	// silent voices or clipping without listening
	std::vector<app::ModuleWidget*> mws = rack->getModules();
	std::vector<float> peaks(mws.size(), 0.f);
	system::sleep(0.5);
	for (int t = 0; t < 400; t++) {
		for (size_t i = 0; i < mws.size(); i++) {
			engine::Module* m = mws[i]->module;
			if (!m->outputs.empty() && m->outputs[0].isConnected())
				peaks[i] = std::fmax(peaks[i], std::fabs(m->outputs[0].getVoltage()));
		}
		system::sleep(0.005);
	}
	for (size_t i = 0; i < mws.size(); i++) {
		engine::Module* m = mws[i]->module;
		if (m->outputs.empty() || !m->outputs[0].isConnected())
			continue;
		std::string name = m->outputInfos.empty() ? "" : m->outputInfos[0]->getName();
		INFO("preview: level %-32s %-14s peak %6.2f V", m->model->getFullName().c_str(), name.c_str(), peaks[i]);
	}

	setModelViewShown(true);
	ModelView* view = getModelView();
	using render3d::Quat;
	using render3d::Vec3;
	const Quat angles[4] = {
		Quat::fromAxisAngle(Vec3(1, 0, 0), 0.45f) * Quat::fromAxisAngle(Vec3(0, 1, 0), -0.6f),
		Quat::fromAxisAngle(Vec3(1, 0, 0), 0.45f) * Quat::fromAxisAngle(Vec3(0, 1, 0), -0.6f + M_PI),
		Quat::fromAxisAngle(Vec3(1, 0, 0), -0.6f) * Quat::fromAxisAngle(Vec3(0, 1, 0), 0.8f),
		Quat::fromAxisAngle(Vec3(1, 0, 0), 0.45f) * Quat::fromAxisAngle(Vec3(0, 1, 0), M_PI / 2 + 0.6f),
	};
	float distance = view->renderer.distance;
	for (int i = 0; i < 5; i++) {
		// The fifth shot steps back to show the scene around the model
		view->renderer.rotation = angles[i % 4];
		view->renderer.distance = i < 4 ? distance : ModelView::MAX_DISTANCE;
		view->angularVelocity = Vec3();
		// Let cables settle into the new orientation
		for (int f = 0; f < 90; f++)
			APP->window->step();
		APP->window->screenshot(string::f("%s-%d.png", out.c_str(), i + 1));
	}
	view->renderer.distance = distance;
	return failures == 0 ? 0 : 1;
}


static int listDevices() {
	for (int driverId : audio::getDriverIds()) {
		audio::Driver* driver = audio::getDriver(driverId);
		int defaultId = driver->getDefaultDeviceId();
		INFO("devices: driver %d %s", driverId, driver->getName().c_str());
		for (int deviceId : driver->getDeviceIds()) {
			INFO("devices:   %s%d \"%s\" (%d in, %d out)", deviceId == defaultId ? "*" : " ", deviceId,
				driver->getDeviceName(deviceId).c_str(), driver->getDeviceNumInputs(deviceId), driver->getDeviceNumOutputs(deviceId));
		}
	}
	return 0;
}


int runTool(const std::string& tool) {
	if (tool == "dump")
		return dumpModels();
	if (tool == "preview")
		return previewPatch();
	if (tool == "devices")
		return listDevices();
	WARN("Unknown RACK3D_TOOL %s", tool.c_str());
	return 1;
}


} // namespace rack3d
} // namespace rack
