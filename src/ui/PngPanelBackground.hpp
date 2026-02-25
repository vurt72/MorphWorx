#pragma once

#include <rack.hpp>

#include <string>

namespace bem {

// Draws a PNG as a panel background using NanoVG, avoiding Rack's limited SVG <image> support.
// Default fit is "Cover" (preserve aspect ratio, center-crop).
struct PngPanelBackground : rack::Widget {
	enum class FitMode {
		Stretch,
		Cover,
		Contain,
	};

	std::string imagePath;
	FitMode fitMode = FitMode::Cover;
	// Alignment anchor within the available extra space (Contain) or cropped overflow (Cover).
	// 0 = left/top, 0.5 = center, 1 = right/bottom.
	float alignX = 0.5f;
	float alignY = 0.5f;
	float alpha = 1.f;
	float angleRadians = 0.f;

	int imageHandle = -1;

	explicit PngPanelBackground(std::string path) : imagePath(std::move(path)) {}

	void draw(const DrawArgs& args) override {
		if (box.size.x <= 0.f || box.size.y <= 0.f)
			return;

		if (imageHandle < 0) {
			imageHandle = nvgCreateImage(args.vg, imagePath.c_str(), NVG_IMAGE_GENERATE_MIPMAPS);
		}
		if (imageHandle < 0) {
			Widget::draw(args);
			return;
		}

		int imageW = 0;
		int imageH = 0;
		nvgImageSize(args.vg, imageHandle, &imageW, &imageH);
		if (imageW <= 0 || imageH <= 0) {
			Widget::draw(args);
			return;
		}

		float targetW = box.size.x;
		float targetH = box.size.y;

		float drawX = 0.f;
		float drawY = 0.f;
		float drawW = targetW;
		float drawH = targetH;

		if (fitMode != FitMode::Stretch) {
			float scaleX = targetW / (float)imageW;
			float scaleY = targetH / (float)imageH;
			float scale = (fitMode == FitMode::Cover) ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
			drawW = (float)imageW * scale;
			drawH = (float)imageH * scale;
			drawX = (targetW - drawW) * alignX;
			drawY = (targetH - drawH) * alignY;
		}

		nvgSave(args.vg);
		nvgScissor(args.vg, 0, 0, targetW, targetH);
		NVGpaint paint = nvgImagePattern(args.vg, drawX, drawY, drawW, drawH, angleRadians, imageHandle, alpha);
		nvgBeginPath(args.vg);
		// For Contain, draw only the image rect to avoid NanoVG pattern tiling.
		if (fitMode == FitMode::Contain) {
			nvgRect(args.vg, drawX, drawY, drawW, drawH);
		}
		else {
			// Stretch and Cover fill the panel bounds.
			nvgRect(args.vg, 0, 0, targetW, targetH);
		}
		nvgFillPaint(args.vg, paint);
		nvgFill(args.vg);
		nvgRestore(args.vg);

		Widget::draw(args);
	}
};

} // namespace bem
