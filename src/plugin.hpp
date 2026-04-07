#pragma once
#include <rack.hpp>

#include "version.h"

#ifndef METAMODULE
#include <componentlibrary.hpp>
#include <cmath>
#include <string>
#endif

using namespace rack;

// Declare the Plugin instance
extern Plugin* pluginInstance;

// Declare each Model
extern Model* modelTrigonomicon;
extern Model* modelSlideWyrm;
extern Model* modelSeptagon;
extern Model* modelMinimalith;
extern Model* modelAmenolith;
extern Model* modelPhaseon1;
extern Model* modelXenostasis;
extern Model* modelFerroklast;
extern Model* modelFerroklastMM;

#ifndef METAMODULE
struct MVXPort : app::PortWidget {
	std::string imagePath;
	int imageHandle = -1;

	MVXPort() : imagePath(asset::plugin(pluginInstance, "res/ports/MVXport.png")) {
        static Vec portSize = []() {
            componentlibrary::PJ301MPort port;
            return port.box.size;
        }();
        box.size = portSize;
    }

	void draw(const DrawArgs& args) override {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return;

		if (imageHandle < 0) {
			imageHandle = nvgCreateImage(args.vg, imagePath.c_str(), NVG_IMAGE_GENERATE_MIPMAPS);
		}

		if (imageHandle >= 0) {
			nvgSave(args.vg);
			nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
			NVGpaint paint = nvgImagePattern(args.vg, 0, 0, box.size.x, box.size.y, 0.f, imageHandle, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
			nvgRestore(args.vg);
		}

		PortWidget::draw(args);
	}
};

// 62.5%-scale visual variant of the stock Rack Trimpot, used for dense layouts
// like the OP volume row. Hit area stays full-size; only the art is scaled.
struct MiniTrimpot : componentlibrary::Trimpot {
	MiniTrimpot() {
		// Use the base Trimpot's default box size and SVG; no changes needed here.
	}

    void draw(const DrawArgs& args) override {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return;

		constexpr float kScale = 0.72f;
		float cx = box.size.x * 0.5f;
		float cy = box.size.y * 0.5f;

		nvgSave(args.vg);
		nvgTranslate(args.vg, cx, cy);
		nvgScale(args.vg, kScale, kScale);
		nvgTranslate(args.vg, -cx, -cy);
		componentlibrary::Trimpot::draw(args);
		nvgRestore(args.vg);
	}
};

// 75%-scale visual variant used for LFO rate/phase/deform/amp trims.
struct SmallTrimpot : componentlibrary::Trimpot {
	SmallTrimpot() {
	}

	void draw(const DrawArgs& args) override {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return;

		constexpr float kScale = 0.75f;
		float cx = box.size.x * 0.5f;
		float cy = box.size.y * 0.5f;

		nvgSave(args.vg);
		nvgTranslate(args.vg, cx, cy);
		nvgScale(args.vg, kScale, kScale);
		nvgTranslate(args.vg, -cx, -cy);
		componentlibrary::Trimpot::draw(args);
		nvgRestore(args.vg);
	}
};

// Custom knob used by the VCV Rack plugin UI.
// Renders a static background PNG plus a rotating knob PNG.
struct MVXKnob : app::Knob {
	std::string bgPath;
	std::string fgPath;
	int bgHandle = -1;
	int fgHandle = -1;

	MVXKnob()
		: bgPath(asset::plugin(pluginInstance, "res/knobs/MVXKnob_BG.png"))
		, fgPath(asset::plugin(pluginInstance, "res/knobs/MVXKnob.png")) {
		// Size this knob relative to the stock Rack small knob.
		// The art can be high-res; we scale it into the widget bounds.
		static Vec fallbackSize = []() {
			componentlibrary::RoundSmallBlackKnob ref;
			return ref.box.size;
		}();
		constexpr float kScale = 1.4f;
		box.size = Vec(fallbackSize.x * kScale, fallbackSize.y * kScale);

		// Stock-ish travel.
		minAngle = -0.83f * (float)M_PI;
		maxAngle = 0.83f * (float)M_PI;
	}

	float getAngleRadians() {
		auto* q = getParamQuantity();
		if (!q)
			return 0.f;
		return math::rescale(q->getValue(), q->getMinValue(), q->getMaxValue(), minAngle, maxAngle);
	}

	void draw(const DrawArgs& args) override {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return;

		if (bgHandle < 0) {
			bgHandle = nvgCreateImage(args.vg, bgPath.c_str(), 0);
		}
		if (fgHandle < 0) {
			fgHandle = nvgCreateImage(args.vg, fgPath.c_str(), 0);
		}

		// Background
		if (bgHandle >= 0) {
			nvgSave(args.vg);
			nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
			NVGpaint paint = nvgImagePattern(args.vg, 0, 0, box.size.x, box.size.y, 0.f, bgHandle, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
			nvgRestore(args.vg);
		}

		// Foreground (rotating)
		if (fgHandle >= 0) {
			nvgSave(args.vg);
			nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
			const float cx = box.size.x * 0.5f;
			const float cy = box.size.y * 0.5f;
			nvgTranslate(args.vg, cx, cy);
			nvgRotate(args.vg, getAngleRadians());
			nvgTranslate(args.vg, -cx, -cy);
			NVGpaint paint = nvgImagePattern(args.vg, 0, 0, box.size.x, box.size.y, 0.f, fgHandle, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
			nvgRestore(args.vg);
		}

		Widget::draw(args);
	}
};

// Red-accent variant of MVXKnob, sized to match RoundSmallBlackKnob exactly.
// Uses MVXKnob_red.png (turning part) and MVXKnob_red_BG.png (background).
struct MVXKnob_red : MVXKnob {
	MVXKnob_red() {
		bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_red_BG.png");
		fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_red.png");
		// Invalidate cached handles so the new paths are loaded.
		bgHandle = -1;
		fgHandle = -1;
		// Match RoundSmallBlackKnob size exactly.
		componentlibrary::RoundSmallBlackKnob ref;
		box.size = ref.box.size;
	}
};

// Grey variant of MVXKnob — used for the TEAR knob.
// Uses MVXKnob_c.png (turning part) and MVXKnob_c_bg.png (background).
struct MVXKnob_c : MVXKnob {
	MVXKnob_c() {
		bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_c_bg.png");
		fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_c.png");
		// Invalidate cached handles so the new paths are loaded.
		bgHandle = -1;
		fgHandle = -1;
		// 20% larger than RoundSmallBlackKnob.
		componentlibrary::RoundSmallBlackKnob ref;
		box.size = ref.box.size.mult(1.2f);
	}
};

// Blue variant of MVXKnob — used for reverb parameters.
// Uses MVXKnob_blue.png (turning part) and MVXKnob_blue_BG.png (background).
struct MVXKnob_blue : MVXKnob {
	MVXKnob_blue() {
		bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_blue_BG.png");
		fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_blue.png");
		bgHandle = -1;
		fgHandle = -1;
		componentlibrary::RoundSmallBlackKnob ref;
		box.size = ref.box.size.mult(1.44f);
	}
};

// White variant of MVXKnob — used for snap/body/material/noise/chaos parameters.
// Uses MVXKnob_wh.png (turning part) and MVXKnob_wh_BG.png (background).
struct MVXKnob_wh : MVXKnob {
	MVXKnob_wh() {
		bgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh_BG.png");
		fgPath = asset::plugin(pluginInstance, "res/knobs/MVXKnob_wh.png");
		bgHandle = -1;
		fgHandle = -1;
		componentlibrary::RoundSmallBlackKnob ref;
		box.size = ref.box.size.mult(1.44f);
	}
};

// Silver output port variant.
struct MVXport_silver : MVXPort {
	MVXport_silver() {
		imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver.png");
		imageHandle = -1;
	}
};

// Silver+red output port variant.
struct MVXport_silver_red : MVXPort {
	MVXport_silver_red() {
		imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_silver_red.png");
		imageHandle = -1;
	}
};

// Red input port variant (s1 style).
struct MVXport_s1_red : MVXPort {
	MVXport_s1_red() {
		imagePath = asset::plugin(pluginInstance, "res/ports/MVXport_s1_red.png");
		imageHandle = -1;
	}
};

#else
using MVXPort         = componentlibrary::PJ301MPort;
// MetaModule doesn't render custom PNG knobs; fall back to standard knob widgets.
using MVXKnob         = componentlibrary::RoundSmallBlackKnob;
using MVXKnob_red     = componentlibrary::RoundSmallBlackKnob;
using MVXKnob_c       = componentlibrary::RoundSmallBlackKnob;
using MVXKnob_blue    = componentlibrary::RoundSmallBlackKnob;
using MVXKnob_wh      = componentlibrary::RoundSmallBlackKnob;
using MVXport_silver      = componentlibrary::PJ301MPort;
using MVXport_silver_red  = componentlibrary::PJ301MPort;
using MVXport_s1_red      = componentlibrary::PJ301MPort;
#endif
