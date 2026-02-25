"""Apply 8 edits to Xenostasis.cpp: add TABLE_CV input + BB character knob.

This script is intended to be robust to the project folder being renamed/moved.
By default it targets <repo_root>/src/Xenostasis.cpp.
"""

import sys
from pathlib import Path


def resolve_target_path() -> Path:
        if len(sys.argv) >= 2:
                return Path(sys.argv[1]).expanduser().resolve()
        repo_root = Path(__file__).resolve().parents[1]
        return (repo_root / "src" / "Xenostasis.cpp").resolve()


path = resolve_target_path()

with open(path, 'r', encoding='utf-8') as f:
    code = f.read()

changes = 0

# 1. Add BBCHAR_PARAM to enum
old = "        DENSITY_PARAM,\n        TABLE_PARAM,\n        PARAMS_LEN"
new = "        DENSITY_PARAM,\n        TABLE_PARAM,\n        BBCHAR_PARAM,\n        PARAMS_LEN"
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 1: BBCHAR_PARAM enum")
else: print("MISS 1"); sys.exit(1)

# 2. Add TABLE_CV_INPUT to enum
old = "        CROSS_CV_INPUT,\n        VOCT_INPUT,\n        INPUTS_LEN"
new = "        CROSS_CV_INPUT,\n        VOCT_INPUT,\n        TABLE_CV_INPUT,\n        INPUTS_LEN"
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 2: TABLE_CV_INPUT enum")
else: print("MISS 2"); sys.exit(1)

# 3. configParam + configInput
old = '''        configParam(TABLE_PARAM, 0.f, 6.f, 0.f, "Table Select");
        getParamQuantity(TABLE_PARAM)->snapEnabled = true;

        configInput(CLOCK_INPUT, "Clock");
        configInput(CHAOS_CV_INPUT, "Chaos CV (0-10V)");
        configInput(CROSS_CV_INPUT, "Cross CV (0-10V)");
        configInput(VOCT_INPUT, "V/Oct");'''
new = '''        configParam(TABLE_PARAM, 0.f, 6.f, 0.f, "Table Select");
        getParamQuantity(TABLE_PARAM)->snapEnabled = true;
        configParam(BBCHAR_PARAM, 0.f, 1.f, 0.5f, "Bytebeat Character", "%", 0.f, 100.f);

        configInput(CLOCK_INPUT, "Clock");
        configInput(CHAOS_CV_INPUT, "Chaos CV (0-10V)");
        configInput(CROSS_CV_INPUT, "Cross CV (0-10V)");
        configInput(VOCT_INPUT, "V/Oct");
        configInput(TABLE_CV_INPUT, "Table CV (0-10V)");'''
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 3: configParam/configInput")
else: print("MISS 3"); sys.exit(1)

# 4. Table CV reading + bbChar param
old = '''        float densityParam = params[DENSITY_PARAM].getValue();
        int tableIdx = (int)params[TABLE_PARAM].getValue();'''
new = '''        float densityParam = params[DENSITY_PARAM].getValue();
        int tableIdx = (int)params[TABLE_PARAM].getValue();
        // Table CV: 0-10V maps across all 7 tables
        if (inputs[TABLE_CV_INPUT].isConnected()) {
            float tableCv = clamp(inputs[TABLE_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            tableIdx = clamp((int)(tableCv * 6.99f), 0, 6);
        }

        // Bytebeat character knob (0..1): controls shift amounts + volume
        float bbChar = params[BBCHAR_PARAM].getValue();'''
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 4: Table CV + bbChar read")
else: print("MISS 4"); sys.exit(1)

# 5. Replace bytebeat layers with bbChar-controlled shifts
old = '''        // Layer 1: harmonic additive shifts -- creates tonal overtone patterns
        uint32_t a1 = (t >> 4) + (t >> 7);
        uint32_t b1 = (t >> 5) ^ (t >> 9);
        uint32_t raw1 = (a1 + b1) & 0xFF;

        // Layer 2: slower, creates evolving sub-harmonic drone texture
        bbTime2F += (double)(bbSpeed * 0.517f) * (1.0 + (double)bbEpsilon * 1.7);
        uint32_t t2 = (uint32_t)bbTime2F;
        uint32_t a2 = ((t2 >> 6) | (t2 >> 3)) + (t2 >> 10);
        uint32_t b2 = (t2 >> 8) ^ (t2 >> 4);
        uint32_t raw2 = ((a2 * b2) >> 4) & 0xFF;

        // Layer 3: pitch-related harmonic series -- adds metallic / bell overtones
        bbTime3F += (double)(bbSpeed * 1.333f + energy * 0.2);
        uint32_t t3 = (uint32_t)bbTime3F;
        uint32_t raw3 = ((t3 * ((t3 >> 5) | (t3 >> 8))) >> 6) & 0xFF;'''
new = '''        // bbChar modulates shift amounts: at 0 shifts are minimal, at 1 aggressive
        int s1a = 4 + (int)(bbChar * 4.f);   // 4..8
        int s1b = 7 + (int)(bbChar * 5.f);   // 7..12
        int s1c = 5 + (int)(bbChar * 3.f);   // 5..8
        int s1d = 9 + (int)(bbChar * 4.f);   // 9..13

        // Layer 1: harmonic additive shifts -- character-morphed
        uint32_t a1 = (t >> s1a) + (t >> s1b);
        uint32_t b1 = (t >> s1c) ^ (t >> s1d);
        uint32_t raw1 = (a1 + b1) & 0xFF;

        // Layer 2: slower, character affects structure
        int s2a = 6 + (int)(bbChar * 3.f);   // 6..9
        int s2b = 3 + (int)(bbChar * 4.f);   // 3..7
        int s2c = 10 - (int)(bbChar * 3.f);  // 10..7
        int s2d = 8 + (int)(bbChar * 3.f);   // 8..11
        int s2e = 4 + (int)(bbChar * 2.f);   // 4..6
        bbTime2F += (double)(bbSpeed * 0.517f) * (1.0 + (double)bbEpsilon * 1.7);
        uint32_t t2 = (uint32_t)bbTime2F;
        uint32_t a2 = ((t2 >> s2a) | (t2 >> s2b)) + (t2 >> s2c);
        uint32_t b2 = (t2 >> s2d) ^ (t2 >> s2e);
        uint32_t raw2 = ((a2 * b2) >> 4) & 0xFF;

        // Layer 3: pitch-related -- character warps the expression
        int s3a = 5 + (int)(bbChar * 3.f);   // 5..8
        int s3b = 8 - (int)(bbChar * 3.f);   // 8..5
        bbTime3F += (double)(bbSpeed * 1.333f + energy * 0.2);
        uint32_t t3 = (uint32_t)bbTime3F;
        uint32_t raw3 = ((t3 * ((t3 >> s3a) | (t3 >> s3b))) >> 6) & 0xFF;'''
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 5: bbChar bytebeat shifts")
else: print("MISS 5"); sys.exit(1)

# 6. bbChar volume scaling
old = '''        // Stability slightly attenuates BB at high values (calmer sound)
        bbWeight *= (0.6f + (1.f - stability) * 0.4f);

        // Slight stereo offset on BB via layer phase differences'''
new = '''        // Stability slightly attenuates BB at high values (calmer sound)
        bbWeight *= (0.6f + (1.f - stability) * 0.4f);
        // bbChar controls BB volume: 0 = silent, 1.0 = 160% of base level
        bbWeight *= bbChar * 1.6f;

        // Slight stereo offset on BB via layer phase differences'''
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 6: bbChar volume")
else: print("MISS 6"); sys.exit(1)

# 7. Row 4 widget layout (add BB knob)
old = '''        // Row 4: CROSS, DENSITY + lights
        // \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
        float row4Y = 58.f;
        float r4x1 = 16.f, r4x2 = 40.f;
        float lightX1 = 58.f, lightX2 = 68.f;
#ifndef METAMODULE
        addChild(xsCreateLabel(Vec(r4x1, row4Y - 6.f), "CROSS", 8.f, neonGreen));
        addChild(xsCreateLabel(Vec(r4x2, row4Y - 6.f), "DENSITY", 8.f, neonGreen));
        addChild(xsCreateLabel(Vec(lightX1, row4Y - 5.f), "NRG", 7.f, dimGreen));
        addChild(xsCreateLabel(Vec(lightX2, row4Y - 5.f), "STM", 7.f, dimGreen));
#endif
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(r4x1, row4Y)), module, Xenostasis::CROSS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(r4x2, row4Y)), module, Xenostasis::DENSITY_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(lightX1, row4Y)), module, Xenostasis::ENERGY_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(lightX2, row4Y)), module, Xenostasis::STORM_LIGHT));'''
new = '''        // Row 4: CROSS, DENSITY, BB_CHAR + lights
        // \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
        float row4Y = 58.f;
        float r4x1 = 12.f, r4x2 = 30.f, r4x3 = 48.f;
        float lightX1 = 62.f, lightX2 = 72.f;
#ifndef METAMODULE
        addChild(xsCreateLabel(Vec(r4x1, row4Y - 6.f), "CROSS", 7.5f, neonGreen));
        addChild(xsCreateLabel(Vec(r4x2, row4Y - 6.f), "DENSITY", 7.5f, neonGreen));
        addChild(xsCreateLabel(Vec(r4x3, row4Y - 6.f), "BB", 7.5f, neonGreen));
        addChild(xsCreateLabel(Vec(lightX1, row4Y - 5.f), "NRG", 7.f, dimGreen));
        addChild(xsCreateLabel(Vec(lightX2, row4Y - 5.f), "STM", 7.f, dimGreen));
#endif
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(r4x1, row4Y)), module, Xenostasis::CROSS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(r4x2, row4Y)), module, Xenostasis::DENSITY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(r4x3, row4Y)), module, Xenostasis::BBCHAR_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(lightX1, row4Y)), module, Xenostasis::ENERGY_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(lightX2, row4Y)), module, Xenostasis::STORM_LIGHT));'''
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 7: Row4 widget layout")
else: print("MISS 7"); sys.exit(1)

# 8. Row 5 widget layout (add TABLE CV jack)
old = '''        // Row 5: CV inputs
        // \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
        float row5Y = 82.f;
        float cv1X = 11.f, cv2X = 27.f, cv3X = 47.f, cv4X = 67.f;
#ifndef METAMODULE
        addChild(xsCreateLabel(Vec(cv1X, row5Y - 5.f), "V/OCT", 7.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv2X, row5Y - 5.f), "CLK", 7.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv3X, row5Y - 5.f), "CHAOS", 7.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv4X, row5Y - 5.f), "CROSS", 7.5f, neonGreen));
#endif
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv1X, row5Y)), module, Xenostasis::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv2X, row5Y)), module, Xenostasis::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv3X, row5Y)), module, Xenostasis::CHAOS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv4X, row5Y)), module, Xenostasis::CROSS_CV_INPUT));'''
new = '''        // Row 5: CV inputs (5 jacks)
        // \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
        float row5Y = 82.f;
        float cv1X = 9.f, cv2X = 23.f, cv3X = 40.64f, cv4X = 58.f, cv5X = 72.f;
#ifndef METAMODULE
        addChild(xsCreateLabel(Vec(cv1X, row5Y - 5.f), "V/OCT", 6.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv2X, row5Y - 5.f), "CLK", 6.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv3X, row5Y - 5.f), "CHAOS", 6.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv4X, row5Y - 5.f), "CROSS", 6.5f, neonGreen));
        addChild(xsCreateLabel(Vec(cv5X, row5Y - 5.f), "TBL", 6.5f, neonGreen));
#endif
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv1X, row5Y)), module, Xenostasis::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv2X, row5Y)), module, Xenostasis::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv3X, row5Y)), module, Xenostasis::CHAOS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv4X, row5Y)), module, Xenostasis::CROSS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cv5X, row5Y)), module, Xenostasis::TABLE_CV_INPUT));'''
if old in code:
    code = code.replace(old, new, 1); changes += 1; print("OK 8: Row5 widget layout")
else: print("MISS 8"); sys.exit(1)

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(code)

print(f"\nAll {changes}/8 changes applied successfully!")
