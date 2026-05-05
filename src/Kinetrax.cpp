#include "plugin.hpp"

#include <algorithm>
#include <cmath>

struct Kinetrax : Module {
	static constexpr int kNumZones = 8;
	static constexpr int kMaxPolyChannels = 16;
	static constexpr float kBaseFullRangeVolts = 8.f;
	static constexpr float kMinSpace = 1e-4f;
	static constexpr float kMaxSpace = 1.f;
	static constexpr float kHysteresisNormalized = 0.01f;
	static constexpr int kNoZone = -1;
	static constexpr float kCvMaxVoltage = 10.f;

	enum CvMode {
		CV_MODE_INDEX,
		CV_MODE_SLEW,
		CV_MODE_FLUX,
		CV_MODE_COUNT
	};

	enum ParamId {
		// Row 1: SPAN | ATTENUVERT | SPACE
		SPAN_PARAM,
		SPAN_ATTENUVERT_PARAM,
		SPACE_PARAM,
		// Row 2: WARP
		WARP_PARAM,
		CV_MODE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		SPAN_INPUT,
		SPAN2_INPUT,
		SPAN_CV_INPUT,
		ATTN_CV_INPUT,
		WARP_CV_INPUT,
		CLOCK_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ZONE_1_OUTPUT,
		ZONE_2_OUTPUT,
		ZONE_3_OUTPUT,
		ZONE_4_OUTPUT,
		ZONE_5_OUTPUT,
		ZONE_6_OUTPUT,
		ZONE_7_OUTPUT,
		ZONE_8_OUTPUT,
		ODD_OUTPUT,
		EVEN_OUTPUT,
		TRANSITION_OUTPUT,
		CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	struct ChannelState {
		dsp::SchmittTrigger clockTrigger;
		float latchedSpan = 0.f;
		float latchedSpan2 = 0.f;
		int activeZone = kNoZone;
		int targetZone = kNoZone;
		int gtePulseSamplesRemaining = 0;
		int stepHoldSamplesRemaining = 0;
		float slewVoltage = 0.f;
		float fluxVoltage = 0.f;
		bool initialized = false;
	};

	ChannelState channelStates[kMaxPolyChannels];
	int gtePulseWidthSamples = 48;
	int fddStepSamples = 240;
	float slewCoeff = 0.f;
	float fluxDecayCoeff = 1.f;

	Kinetrax() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(SPAN_PARAM, 0.f, kBaseFullRangeVolts, 0.f, "Span 1", " V", 0.f, 1.f, 0.f);
		getParamQuantity(SPAN_PARAM)->description = "Manual base offset for the primary channel-selection gesture";
		configParam(SPAN_ATTENUVERT_PARAM, -1.f, 1.f, 1.f, "Span 1 attenuverter", "%", 0.f, 100.f);
		getParamQuantity(SPAN_ATTENUVERT_PARAM)->description = "Bipolar amount applied to the Span 1 input";
		configParam(SPACE_PARAM, kMinSpace, kMaxSpace, 1.f, "Space");
		getParamQuantity(SPACE_PARAM)->description = "Sets the total voltage range needed to traverse all eight channels";
		configParam(WARP_PARAM, 0.f, 1.f, 0.35f, "Warp", "%", 0.f, 100.f);
		getParamQuantity(WARP_PARAM)->description = "Amount of Span 2 boundary warping applied to the primary gesture";
		configSwitch(CV_MODE_PARAM, 0.f, static_cast<float>(CV_MODE_COUNT - 1), 0.f,
			"CV mode", {"Index", "Slew", "Flux"});
		getParamQuantity(CV_MODE_PARAM)->description = "Selects the CV extractor: stepped channel index, smoothed index, or transition-density flux envelope";

		configInput(SPAN_INPUT, "Span 1 input");
		configInput(SPAN2_INPUT, "Span 2 input");
		configInput(SPAN_CV_INPUT, "Span CV input");
		configInput(ATTN_CV_INPUT, "Attenuverter CV input");
		configInput(WARP_CV_INPUT, "Warp CV input");
		configInput(CLOCK_INPUT, "Clock input");
		inputInfos[SPAN_INPUT]->description = "Primary CV or audio gesture used for channel selection";
		inputInfos[SPAN2_INPUT]->description = "Secondary CV or audio source that warps boundary crossings for rhythmic variation";
		inputInfos[SPAN_CV_INPUT]->description = "Control-voltage modulation for the Span knob offset";
		inputInfos[ATTN_CV_INPUT]->description = "Control-voltage modulation for the Span attenuverter amount";
		inputInfos[WARP_CV_INPUT]->description = "Control-voltage modulation for the Warp amount";
		inputInfos[CLOCK_INPUT]->description = "Rising-edge clock or trigger that samples channel changes";

		for (int zone = 0; zone < kNumZones; ++zone) {
			configOutput(ZONE_1_OUTPUT + zone, string::f("Zone %d gate", zone + 1));
			outputInfos[ZONE_1_OUTPUT + zone]->description = string::f("Gate high while channel %d is active", zone + 1);
		}
		configOutput(ODD_OUTPUT, "Odd gate");
		configOutput(EVEN_OUTPUT, "Even gate");
		configOutput(TRANSITION_OUTPUT, "Transition pulse");
		configOutput(CV_OUTPUT, "CV output");
		outputInfos[ODD_OUTPUT]->description = "Gate high when the active channel is 1, 3, 5, or 7";
		outputInfos[EVEN_OUTPUT]->description = "Gate high when the active channel is 2, 4, 6, or 8";
		outputInfos[TRANSITION_OUTPUT]->description = "Short pulse each time a new channel is entered";
		outputInfos[CV_OUTPUT]->description = "Derived control voltage from the current gate state, according to the selected CV mode";

		onSampleRateChange();
		resetStates();
	}

	void onReset() override {
		resetStates();
	}

	void onSampleRateChange() override {
		float sampleRate = APP ? APP->engine->getSampleRate() : 48000.f;
		gtePulseWidthSamples = std::max(1, static_cast<int>(std::round(sampleRate * 0.001f)));
		fddStepSamples = std::max(1, static_cast<int>(std::round(sampleRate * 0.005f)));
		slewCoeff = 1.f - std::exp(-1.f / std::max(sampleRate * 0.03f, 1.f));
		fluxDecayCoeff = std::exp(-1.f / std::max(sampleRate * 0.28f, 1.f));
	}

	void process(const ProcessArgs& args) override {
		const int activeChannels = getActiveChannels();

		for (int outputId = 0; outputId < OUTPUTS_LEN; ++outputId) {
			outputs[outputId].setChannels(activeChannels);
		}

		const bool clockConnected = inputs[CLOCK_INPUT].isConnected();

		const CvMode cvMode = getCvMode();

		for (int channel = 0; channel < activeChannels; ++channel) {
			ChannelState& state = channelStates[channel];
			const float spanValue = resolveSpan(channel);
			const float span2Value = resolveSpan2(channel);
			const float spaceValue = resolveSpace(channel);
			const float warpAmount = resolveWarpAmount(channel);
			bool shouldUpdateZone = !clockConnected;

			if (!state.initialized) {
				state.latchedSpan = spanValue;
				state.latchedSpan2 = span2Value;
				shouldUpdateZone = true;
			}

			if (clockConnected) {
				const float clockVoltage = getPolyInputVoltage(CLOCK_INPUT, channel);
				if (state.clockTrigger.process(clockVoltage, 0.1f, 2.f)) {
					state.latchedSpan = spanValue;
					state.latchedSpan2 = span2Value;
					shouldUpdateZone = true;
				}
			}
			else {
				state.latchedSpan = spanValue;
				state.latchedSpan2 = span2Value;
			}

			if (shouldUpdateZone) {
				const float normalized = applySpan2Warp(
					normalizeSpan(state.latchedSpan, spaceValue),
					state.latchedSpan2,
					spaceValue,
					warpAmount);
				const int nextZone = classifyZone(normalized, state.targetZone, state.initialized);
				if (!state.initialized) {
					state.activeZone = nextZone;
					state.targetZone = nextZone;
					state.initialized = true;
				}
				else if (nextZone != state.targetZone) {
					updateTargetZone(state, nextZone);
				}
			}

			TransitionState transitionState = advanceStateMachine(state);
			const float indexVoltage = computeIndexVoltage(state.activeZone);
			updateDerivedCvState(state, indexVoltage, transitionState.triggered);
			writeOutputs(channel, state.activeZone, transitionState.high, selectCvVoltage(state, indexVoltage, cvMode));
		}
	}

	void resetStates() {
		for (int channel = 0; channel < kMaxPolyChannels; ++channel) {
			channelStates[channel].clockTrigger.reset();
			channelStates[channel].latchedSpan = 0.f;
			channelStates[channel].latchedSpan2 = 0.f;
			channelStates[channel].activeZone = kNoZone;
			channelStates[channel].targetZone = kNoZone;
			channelStates[channel].gtePulseSamplesRemaining = 0;
			channelStates[channel].stepHoldSamplesRemaining = 0;
			channelStates[channel].slewVoltage = 0.f;
			channelStates[channel].fluxVoltage = 0.f;
			channelStates[channel].initialized = false;
		}
	}

	CvMode getCvMode() {
		return static_cast<CvMode>(clamp(static_cast<int>(std::round(params[CV_MODE_PARAM].getValue())), 0, CV_MODE_COUNT - 1));
	}

	int getActiveChannels() {
		int channels = 1;
		for (int inputId = 0; inputId < INPUTS_LEN; ++inputId) {
			if (inputs[inputId].isConnected()) {
				channels = std::max(channels, inputs[inputId].getChannels());
			}
		}
		return clamp(channels, 1, kMaxPolyChannels);
	}

	float getPolyInputVoltage(int inputId, int channel) {
		if (!inputs[inputId].isConnected()) {
			return 0.f;
		}
		const int inputChannels = inputs[inputId].getChannels();
		if (inputChannels <= 0) {
			return 0.f;
		}
		const int useChannel = std::min(channel, inputChannels - 1);
		return inputs[inputId].getVoltage(useChannel);
	}

	float resolveSpan(int channel) {
		float spanValue = params[SPAN_PARAM].getValue() + getPolyInputVoltage(SPAN_CV_INPUT, channel);
		if (inputs[SPAN_INPUT].isConnected()) {
			spanValue += resolveSpanAttenuverter(channel) * getPolyInputVoltage(SPAN_INPUT, channel);
		}
		return spanValue;
	}

	float resolveSpanAttenuverter(int channel) {
		const float attenuverterCv = getPolyInputVoltage(ATTN_CV_INPUT, channel) * 0.1f;
		return clamp(params[SPAN_ATTENUVERT_PARAM].getValue() + attenuverterCv, -1.f, 1.f);
	}

	float resolveSpan2(int channel) {
		return getPolyInputVoltage(SPAN2_INPUT, channel);
	}

	float resolveSpace(int channel) {
		(void) channel;
		return clamp(params[SPACE_PARAM].getValue(), kMinSpace, kMaxSpace);
	}

	float resolveWarpAmount(int channel) {
		const float warpCv = getPolyInputVoltage(WARP_CV_INPUT, channel) * 0.1f;
		return clamp(params[WARP_PARAM].getValue() + warpCv, 0.f, 1.f);
	}

	float normalizeSpan(float spanValue, float spaceValue) const {
		const float spaceSquared = std::max(spaceValue * spaceValue, kMinSpace);
		const float effectiveRange = kBaseFullRangeVolts * spaceSquared;
		return spanValue / effectiveRange;
	}

	float applySpan2Warp(float primaryNormalized, float span2Value, float spaceValue, float warpAmount) const {
		if (warpAmount <= 0.f || std::fabs(span2Value) <= 1e-6f) {
			return primaryNormalized;
		}

		const float effectiveRange = kBaseFullRangeVolts * std::max(spaceValue * spaceValue, kMinSpace);
		const float span2Normalized = clamp(span2Value / std::max(effectiveRange, 1e-4f), -1.f, 1.f);
		const float channelPosition = primaryNormalized * static_cast<float>(kNumZones);
		const float cellPhase = channelPosition - std::floor(channelPosition);
		const float boundaryWeight = clamp(std::fabs(cellPhase - 0.5f) * 2.f, 0.f, 1.f);
		const float warpOffset = span2Normalized * warpAmount * boundaryWeight * 0.35f;
		return primaryNormalized + warpOffset;
	}

	int classifyZone(float normalized, int previousZone, bool initialized) const {
		const float clampedNormalized = clamp(normalized, 0.f, 1.f);
		const int quantized = clamp(static_cast<int>(std::floor(clampedNormalized * kNumZones)), 0, kNumZones - 1);
		if (!initialized) {
			return (normalized < 0.f || normalized > 1.f) ? kNoZone : quantized;
		}

		if (previousZone == kNoZone) {
			return (normalized < 0.f || normalized > 1.f) ? kNoZone : quantized;
		}

		const float zoneWidth = 1.f / static_cast<float>(kNumZones);
		const float lowerBoundary = previousZone * zoneWidth;
		const float upperBoundary = (previousZone + 1) * zoneWidth;

		if (normalized < -kHysteresisNormalized || normalized > 1.f + kHysteresisNormalized) {
			return kNoZone;
		}

		if (normalized >= upperBoundary + kHysteresisNormalized) {
			const float adjusted = clamp(normalized - kHysteresisNormalized, 0.f, 1.f);
			return std::max(previousZone + 1,
				clamp(static_cast<int>(std::floor(adjusted * kNumZones)), 0, kNumZones - 1));
		}

		if (normalized < lowerBoundary - kHysteresisNormalized) {
			const float adjusted = clamp(normalized + kHysteresisNormalized, 0.f, 1.f);
			return std::min(previousZone - 1,
				clamp(static_cast<int>(std::floor(adjusted * kNumZones)), 0, kNumZones - 1));
		}

		return previousZone;
	}

	void updateTargetZone(ChannelState& state, int nextZone) {
		state.targetZone = nextZone;

		if (nextZone == kNoZone) {
			state.activeZone = kNoZone;
			state.stepHoldSamplesRemaining = 0;
			return;
		}

		if (state.activeZone == kNoZone) {
			state.activeZone = nextZone;
			state.stepHoldSamplesRemaining = 0;
			triggerGtePulse(state);
		}
	}

	void triggerGtePulse(ChannelState& state) {
		state.gtePulseSamplesRemaining = gtePulseWidthSamples;
	}

	struct TransitionState {
		bool high = false;
		bool triggered = false;
	};

	TransitionState advanceStateMachine(ChannelState& state) {
		TransitionState result;
		if (state.gtePulseSamplesRemaining > 0) {
			result.high = true;
			--state.gtePulseSamplesRemaining;
		}

		if (!state.initialized || state.activeZone == kNoZone || state.targetZone == kNoZone || state.activeZone == state.targetZone) {
			return result;
		}

		if (state.stepHoldSamplesRemaining > 0) {
			--state.stepHoldSamplesRemaining;
			return result;
		}

		state.activeZone += (state.targetZone > state.activeZone) ? 1 : -1;
		triggerGtePulse(state);
		result.high = true;
		result.triggered = true;
		--state.gtePulseSamplesRemaining;

		if (state.activeZone != state.targetZone) {
			state.stepHoldSamplesRemaining = fddStepSamples;
		}
		else {
			state.stepHoldSamplesRemaining = 0;
		}

		return result;
	}

	float computeIndexVoltage(int zoneIndex) const {
		if (zoneIndex == kNoZone) {
			return 0.f;
		}
		return rescale(static_cast<float>(zoneIndex), 0.f, static_cast<float>(kNumZones - 1), 0.f, kCvMaxVoltage);
	}

	void updateDerivedCvState(ChannelState& state, float indexVoltage, bool transitionTriggered) {
		state.slewVoltage += (indexVoltage - state.slewVoltage) * slewCoeff;
		if (transitionTriggered) {
			state.fluxVoltage = std::min(kCvMaxVoltage, state.fluxVoltage + 2.5f);
		}
		state.fluxVoltage *= fluxDecayCoeff;
	}

	float selectCvVoltage(const ChannelState& state, float indexVoltage, CvMode cvMode) const {
		switch (cvMode) {
		case CV_MODE_INDEX:
			return indexVoltage;
		case CV_MODE_SLEW:
			return state.slewVoltage;
		case CV_MODE_FLUX:
			return state.fluxVoltage;
		default:
			return indexVoltage;
		}
	}

	void writeOutputs(int channel, int zoneIndex, bool transitionHigh, float cvVoltage) {
		for (int zone = 0; zone < kNumZones; ++zone) {
			outputs[ZONE_1_OUTPUT + zone].setVoltage(zoneIndex != kNoZone && zone == zoneIndex ? 10.f : 0.f, channel);
		}

		if (zoneIndex == kNoZone) {
			outputs[ODD_OUTPUT].setVoltage(0.f, channel);
			outputs[EVEN_OUTPUT].setVoltage(0.f, channel);
		}
		else {
			const bool evenZone = (zoneIndex % 2) == 0;
			outputs[ODD_OUTPUT].setVoltage(evenZone ? 0.f : 10.f, channel);
			outputs[EVEN_OUTPUT].setVoltage(evenZone ? 10.f : 0.f, channel);
		}
		outputs[TRANSITION_OUTPUT].setVoltage(transitionHigh ? 10.f : 0.f, channel);
		outputs[CV_OUTPUT].setVoltage(cvVoltage, channel);
	}
};

#ifndef METAMODULE

struct KxPanelLabel : TransparentWidget {
	std::string text;
	float fontSize;
	NVGcolor color;

	KxPanelLabel(Vec pos, const char* labelText, float labelFontSize, NVGcolor labelColor)
		: text(labelText), fontSize(labelFontSize), color(labelColor) {
		box.pos = pos;
		box.size = Vec(90.f, labelFontSize + 4.f);
	}

	void draw(const DrawArgs& args) override {
		std::string fontPath = asset::plugin(pluginInstance, "res/Rajdhani-Bold.ttf");
		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
		if (!font) {
			return;
		}

		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSize);
		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		float y = fontSize * 0.5f;
		nvgText(args.vg, 0.f, y, text.c_str(), NULL);
	}
};

static KxPanelLabel* kxLabel(Vec mmPos, const char* text, float fontSize, NVGcolor color) {
	return new KxPanelLabel(mm2px(mmPos), text, fontSize, color);
}

#endif

struct KinetraxWidget : ModuleWidget {
	KinetraxWidget(Kinetrax* module) {
		setModule(module);
		box.size = Vec(RACK_GRID_WIDTH * 14, RACK_GRID_HEIGHT);
	#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kinetrax.png")));
	#else
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kinetrax.svg")));
	#endif

		constexpr float kKnobXs[4] = {13.24f, 28.12f, 43.00f, 57.88f};
		constexpr float kKnobRow = 29.4f;
		constexpr float kInputXs[3] = {15.24f, 35.56f, 55.88f};
		constexpr float kInputRow1 = 47.8f;
		constexpr float kInputRow2Xs[3] = {20.44f, 35.56f, 50.68f};
		constexpr float kInputRow2 = 62.8f;
		constexpr float kCvModeSwitchX = 62.2f;
		constexpr float kCvModeSwitchRow = 62.8f;
		constexpr float kZoneXs[4] = {12.8f, 27.04f, 44.08f, 58.32f};
		constexpr float kZoneRow1 = 80.0f;
		constexpr float kZoneRow2 = 94.0f;
		constexpr float kLogicXs[4] = {12.8f, 27.04f, 44.08f, 58.32f};
		constexpr float kLogicRow = 114.5f;

		addParam(createParamCentered<MVXKnob_wh>(mm2px(Vec(kKnobXs[0], kKnobRow)), module, Kinetrax::SPAN_PARAM));
		addParam(createParamCentered<MVXKnob_wh>(mm2px(Vec(kKnobXs[1], kKnobRow)), module, Kinetrax::SPAN_ATTENUVERT_PARAM));
		addParam(createParamCentered<MVXKnob_wh>(mm2px(Vec(kKnobXs[2], kKnobRow)), module, Kinetrax::SPACE_PARAM));
		addParam(createParamCentered<MVXKnob_wh>(mm2px(Vec(kKnobXs[3], kKnobRow)), module, Kinetrax::WARP_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(kCvModeSwitchX, kCvModeSwitchRow)), module, Kinetrax::CV_MODE_PARAM));

		addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kInputXs[0], kInputRow1)), module, Kinetrax::SPAN_INPUT));
		addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kInputXs[1], kInputRow1)), module, Kinetrax::SPAN2_INPUT));
		addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kInputXs[2], kInputRow1)), module, Kinetrax::CLOCK_INPUT));
		addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kInputRow2Xs[0], kInputRow2)), module, Kinetrax::SPAN_CV_INPUT));
		addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kInputRow2Xs[1], kInputRow2)), module, Kinetrax::ATTN_CV_INPUT));
		addInput(createInputCentered<MVXport_s1>(mm2px(Vec(kInputRow2Xs[2], kInputRow2)), module, Kinetrax::WARP_CV_INPUT));

		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[0], kZoneRow1)), module, Kinetrax::ZONE_1_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[1], kZoneRow1)), module, Kinetrax::ZONE_2_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[2], kZoneRow1)), module, Kinetrax::ZONE_3_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[3], kZoneRow1)), module, Kinetrax::ZONE_4_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[0], kZoneRow2)), module, Kinetrax::ZONE_5_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[1], kZoneRow2)), module, Kinetrax::ZONE_6_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[2], kZoneRow2)), module, Kinetrax::ZONE_7_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kZoneXs[3], kZoneRow2)), module, Kinetrax::ZONE_8_OUTPUT));

		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kLogicXs[0], kLogicRow)), module, Kinetrax::ODD_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kLogicXs[1], kLogicRow)), module, Kinetrax::EVEN_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kLogicXs[2], kLogicRow)), module, Kinetrax::TRANSITION_OUTPUT));
		addOutput(createOutputCentered<MVXport_s1_red>(mm2px(Vec(kLogicXs[3], kLogicRow)), module, Kinetrax::CV_OUTPUT));

#ifndef METAMODULE
		constexpr float kLogicLabelRow = 106.2f;
		constexpr float kKnobLabelRow = 19.810f;
		NVGcolor white = nvgRGB(0xff, 0xff, 0xff);
		addChild(kxLabel(Vec(35.56f, 6.8f), "K I N E T R A X", 10.8f, white));

		addChild(kxLabel(Vec(kKnobXs[0], kKnobLabelRow), "SPAN", 6.8f, white));
		addChild(kxLabel(Vec(kKnobXs[1], kKnobLabelRow), "ATTN", 6.8f, white));
		addChild(kxLabel(Vec(kKnobXs[2], kKnobLabelRow), "SPACE", 6.8f, white));
		addChild(kxLabel(Vec(kKnobXs[3], kKnobLabelRow), "WARP", 6.8f, white));

		addChild(kxLabel(Vec(kInputXs[0], 41.139f), "SPAN IN", 5.1f, white));
		addChild(kxLabel(Vec(kInputXs[1], 41.139f), "SPAN 2", 5.1f, white));
		addChild(kxLabel(Vec(kInputXs[2], 41.139f), "CLOCK IN", 5.1f, white));
		addChild(kxLabel(Vec(kInputRow2Xs[0], 55.939f), "SPAN CV", 4.4f, white));
		addChild(kxLabel(Vec(kInputRow2Xs[1], 55.939f), "ATTN CV", 4.4f, white));
		addChild(kxLabel(Vec(kInputRow2Xs[2], 55.939f), "WARP CV", 4.4f, white));
		addChild(kxLabel(Vec(kCvModeSwitchX, 55.939f), "CV MODE", 4.0f, white));

		addChild(kxLabel(Vec(kZoneXs[0], 72.878f), "ZONE 1", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[1], 72.878f), "ZONE 2", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[2], 72.878f), "ZONE 3", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[3], 72.878f), "ZONE 4", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[0], 86.878f), "ZONE 5", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[1], 86.878f), "ZONE 6", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[2], 86.878f), "ZONE 7", 4.4f, white));
		addChild(kxLabel(Vec(kZoneXs[3], 86.878f), "ZONE 8", 4.4f, white));

		addChild(kxLabel(Vec(kLogicXs[0], 107.216f), "ODD", 5.0f, white));
		addChild(kxLabel(Vec(kLogicXs[1], 107.216f), "EVEN", 5.0f, white));
		addChild(kxLabel(Vec(kLogicXs[2], 107.216f), "TRANS", 5.0f, white));
		addChild(kxLabel(Vec(kLogicXs[3], 107.216f), "CV OUT", 4.8f, white));
#endif
	}
};

Model* modelKinetrax = createModel<Kinetrax, KinetraxWidget>("Kinetrax");