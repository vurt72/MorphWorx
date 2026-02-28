import os
import re

def update_phaseon_cpp():
    path = 'src/Phaseon.cpp'
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. Change float lfoRate[2] = {0.5f, 0.5f}; to float lfoRate[2][6] = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
    content = re.sub(r'float\s+lfoRate\[2\]\s*=\s*\{0\.5f,\s*0\.5f\};', 
                     r'float lfoRate[2][6]        = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};', content)
    content = re.sub(r'float\s+lfoPhaseOffset\[2\]\s*=\s*\{0\.0f,\s*0\.0f\};', 
                     r'float lfoPhaseOffset[2][6] = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};', content)
    content = re.sub(r'float\s+lfoDeform\[2\]\s*=\s*\{0\.0f,\s*0\.0f\};', 
                     r'float lfoDeform[2][6]      = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};', content)
    content = re.sub(r'float\s+lfoAmp\[2\]\s*=\s*\{1\.0f,\s*1\.0f\};', 
                     r'float lfoAmp[2][6]         = {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};', content)

    # Remove lfoTargetOp
    content = re.sub(r'\s*int\s+lfoTargetOp\[2\]\s*=\s*\{-1,\s*-1\};.*?\n', '\n', content)

    # Update bankLoad
    bank_load_old = r'''                auto readArr = \[\&\]\(const char\* key, float\* dst, float lo, float hi\) \{
                    json_t\* aJ = json_object_get\(slotJ, key\);
                    if \(aJ && json_is_array\(aJ\)\) \{
                        int an = \(int\)json_array_size\(aJ\);
                        for \(int li = 0; li < 2 && li < an; \+\+li\) \{
                            json_t\* vJ = json_array_get\(aJ, li\);
                            if \(vJ && json_is_number\(vJ\)\) \{
                                float v = \(float\)json_number_value\(vJ\);
                                if \(v < lo\) v = lo;
                                if \(v > hi\) v = hi;
                                dst\[li\] = v;
                            \}
                        \}
                    \}
                \};'''
    bank_load_new = r'''                auto readArr = [&](const char* key, float dst[2][6], float lo, float hi) {
                    json_t* aJ = json_object_get(slotJ, key);
                    if (aJ && json_is_array(aJ)) {
                        int an = (int)json_array_size(aJ);
                        for (int li = 0; li < 2 && li < an; ++li) {
                            json_t* lfoJ = json_array_get(aJ, li);
                            if (lfoJ && json_is_array(lfoJ)) {
                                int on = (int)json_array_size(lfoJ);
                                for (int oi = 0; oi < 6 && oi < on; ++oi) {
                                    json_t* vJ = json_array_get(lfoJ, oi);
                                    if (vJ && json_is_number(vJ)) {
                                        float v = (float)json_number_value(vJ);
                                        if (v < lo) v = lo;
                                        if (v > hi) v = hi;
                                        dst[li][oi] = v;
                                    }
                                }
                            } else if (lfoJ && json_is_number(lfoJ)) {
                                float v = (float)json_number_value(lfoJ);
                                if (v < lo) v = lo;
                                if (v > hi) v = hi;
                                for (int oi = 0; oi < 6; ++oi) dst[li][oi] = v;
                            }
                        }
                    }
                };'''
    content = re.sub(bank_load_old, bank_load_new, content)

    # Remove lfoTargetOp from bankLoad
    content = re.sub(r'\s*json_t\*\s*ltJ\s*=\s*json_object_get\(slotJ,\s*"lfoTarget"\);[\s\S]*?\}\s*\}', '', content)

    # Update bankSave
    bank_save_old = r'''            // Per-LFO settings
            \{
                json_t\* lrJ = json_array\(\);
                json_t\* lpJ = json_array\(\);
                json_t\* ldJ = json_array\(\);
                json_t\* laJ = json_array\(\);
                json_t\* ltJ = json_array\(\);
                for \(int li = 0; li < 2; \+\+li\) \{
                    json_array_append_new\(lrJ, json_real\(\(double\)s\.lfoRate\[li\]\)\);
                    json_array_append_new\(lpJ, json_real\(\(double\)s\.lfoPhaseOffset\[li\]\)\);
                    json_array_append_new\(ldJ, json_real\(\(double\)s\.lfoDeform\[li\]\)\);
                    json_array_append_new\(laJ, json_real\(\(double\)s\.lfoAmp\[li\]\)\);
                    json_array_append_new\(ltJ, json_integer\(s\.lfoTargetOp\[li\]\)\);
                \}
                json_object_set_new\(slotJ, "lfoRate", lrJ\);
                json_object_set_new\(slotJ, "lfoPhase", lpJ\);
                json_object_set_new\(slotJ, "lfoDeform", ldJ\);
                json_object_set_new\(slotJ, "lfoAmp", laJ\);
                json_object_set_new\(slotJ, "lfoTarget", ltJ\);
            \}'''
    bank_save_new = r'''            // Per-LFO settings
            {
                json_t* lrJ = json_array();
                json_t* lpJ = json_array();
                json_t* ldJ = json_array();
                json_t* laJ = json_array();
                for (int li = 0; li < 2; ++li) {
                    json_t* lrOpJ = json_array();
                    json_t* lpOpJ = json_array();
                    json_t* ldOpJ = json_array();
                    json_t* laOpJ = json_array();
                    for (int oi = 0; oi < 6; ++oi) {
                        json_array_append_new(lrOpJ, json_real((double)s.lfoRate[li][oi]));
                        json_array_append_new(lpOpJ, json_real((double)s.lfoPhaseOffset[li][oi]));
                        json_array_append_new(ldOpJ, json_real((double)s.lfoDeform[li][oi]));
                        json_array_append_new(laOpJ, json_real((double)s.lfoAmp[li][oi]));
                    }
                    json_array_append_new(lrJ, lrOpJ);
                    json_array_append_new(lpJ, lpOpJ);
                    json_array_append_new(ldJ, ldOpJ);
                    json_array_append_new(laJ, laOpJ);
                }
                json_object_set_new(slotJ, "lfoRate", lrJ);
                json_object_set_new(slotJ, "lfoPhase", lpJ);
                json_object_set_new(slotJ, "lfoDeform", ldJ);
                json_object_set_new(slotJ, "lfoAmp", laJ);
            }'''
    content = re.sub(bank_save_old, bank_save_new, content)

    # Update bankCaptureCurrentToSlot
    capture_old = r'''        // Capture per-LFO settings into preset slot
        for \(int li = 0; li < 2; \+\+li\) \{
            s\.lfoRate\[li\]        = lfoRate\[li\];
            s\.lfoPhaseOffset\[li\] = lfoPhaseOffset\[li\];
            s\.lfoDeform\[li\]      = lfoDeform\[li\];
            s\.lfoAmp\[li\]         = lfoAmp\[li\];
            s\.lfoTargetOp\[li\]    = lfoTargetOp\[li\];
        \}'''
    capture_new = r'''        // Capture per-LFO settings into preset slot
        for (int li = 0; li < 2; ++li) {
            for (int oi = 0; oi < 6; ++oi) {
                s.lfoRate[li][oi]        = lfoRate[li][oi];
                s.lfoPhaseOffset[li][oi] = lfoPhaseOffset[li][oi];
                s.lfoDeform[li][oi]      = lfoDeform[li][oi];
                s.lfoAmp[li][oi]         = lfoAmp[li][oi];
            }
        }'''
    content = re.sub(capture_old, capture_new, content)

    # Update bankApplySlot
    apply_old = r'''            // Restore per-LFO settings from preset
            for \(int li = 0; li < 2; \+\+li\) \{
                lfoRate\[li\]        = s\.lfoRate\[li\];
                lfoPhaseOffset\[li\] = s\.lfoPhaseOffset\[li\];
                lfoDeform\[li\]      = s\.lfoDeform\[li\];
                lfoAmp\[li\]         = s\.lfoAmp\[li\];
                lfoTargetOp\[li\]    = s\.lfoTargetOp\[li\];
            \}'''
    apply_new = r'''            // Restore per-LFO settings from preset
            for (int li = 0; li < 2; ++li) {
                for (int oi = 0; oi < 6; ++oi) {
                    lfoRate[li][oi]        = s.lfoRate[li][oi];
                    lfoPhaseOffset[li][oi] = s.lfoPhaseOffset[li][oi];
                    lfoDeform[li][oi]      = s.lfoDeform[li][oi];
                    lfoAmp[li][oi]         = s.lfoAmp[li][oi];
                }
            }'''
    content = re.sub(apply_old, apply_new, content)

    # Update constructor reset
    reset_old = r'''        // Reset LFO settings to defaults
        for \(int li = 0; li < 2; \+\+li\) \{
            lfoRate\[li\]        = 0\.5f;
            lfoPhaseOffset\[li\] = 0\.0f;
            lfoDeform\[li\]      = 0\.0f;
            lfoAmp\[li\]         = 1\.0f;
            lfoTargetOp\[li\]    = -1;
        \}'''
    reset_new = r'''        // Reset LFO settings to defaults
        for (int li = 0; li < 2; ++li) {
            for (int oi = 0; oi < 6; ++oi) {
                lfoRate[li][oi]        = 0.5f;
                lfoPhaseOffset[li][oi] = 0.0f;
                lfoDeform[li][oi]      = 0.0f;
                lfoAmp[li][oi]         = 1.0f;
            }
        }'''
    content = re.sub(reset_old, reset_new, content)

    # Update clock sync
    clock_sync_old = r'''                // Hard-sync both LFO phases to clock edge \(with respective phase offsets\)
                voice\.lfo1Phase = lfoPhaseOffset\[0\];
                voice\.lfo2Phase = lfoPhaseOffset\[1\];
            \}
            lastClockHigh = clockHigh;

            // When clock is connected and valid, override both LFO rates\.
            // LFO2 rate is ratio-locked to LFO1 based on relative Rate knob settings\.
            if \(clockConnected && clockPeriod > 0\.01f\) \{
                float clockHz = 1\.0f / clockPeriod;
                
                auto getRateMult = \[\]\(float v\) \{
                    if \(v >= 0\.49f && v <= 0\.51f\) return 1\.0f;
                    if \(v > 0\.5f\) \{
                        return 1\.0f \+ \(v - 0\.5f\) \* 2\.0f \* 7\.0f;
                    \} else \{
                        int divs\[\] = \{32, 16, 8, 4, 3, 2\};
                        int idx = \(int\)\(v \* 2\.0f \* 5\.99f\);
                        if \(idx < 0\) idx = 0;
                        if \(idx > 5\) idx = 5;
                        return 1\.0f / \(float\)divs\[idx\];
                    \}
                \};
                
                // LFO1: clock rate \* user rate scaling
                float rate1Scale = getRateMult\(lfoRate\[0\]\);
                voice\.lfo1Phase \+= dt \* clockHz \* rate1Scale;
                if \(voice\.lfo1Phase >= 1\.0f\)
                    voice\.lfo1Phase -= 1\.0f;
                // LFO2: clock rate \* LFO2 rate scaling \(independent ratio\)
                float rate2Scale = getRateMult\(lfoRate\[1\]\);
                voice\.lfo2Phase \+= dt \* clockHz \* rate2Scale;
                if \(voice\.lfo2Phase >= 1\.0f\)
                    voice\.lfo2Phase -= 1\.0f;
            \}'''
    clock_sync_new = r'''                // Hard-sync both LFO phases to clock edge (with respective phase offsets)
                for (int oi = 0; oi < 6; ++oi) {
                    voice.lfo1Phase[oi] = lfoPhaseOffset[0][oi];
                    voice.lfo2Phase[oi] = lfoPhaseOffset[1][oi];
                }
            }
            lastClockHigh = clockHigh;

            // When clock is connected and valid, override both LFO rates.
            // LFO2 rate is ratio-locked to LFO1 based on relative Rate knob settings.
            if (clockConnected && clockPeriod > 0.01f) {
                float clockHz = 1.0f / clockPeriod;
                
                auto getRateMult = [](float v) {
                    if (v >= 0.49f && v <= 0.51f) return 1.0f;
                    if (v > 0.5f) {
                        return 1.0f + (v - 0.5f) * 2.0f * 7.0f;
                    } else {
                        int divs[] = {32, 16, 8, 4, 3, 2};
                        int idx = (int)(v * 2.0f * 5.99f);
                        if (idx < 0) idx = 0;
                        if (idx > 5) idx = 5;
                        return 1.0f / (float)divs[idx];
                    }
                };
                
                for (int oi = 0; oi < 6; ++oi) {
                    float rate1Scale = getRateMult(lfoRate[0][oi]);
                    voice.lfo1Phase[oi] += dt * clockHz * rate1Scale;
                    if (voice.lfo1Phase[oi] >= 1.0f)
                        voice.lfo1Phase[oi] -= 1.0f;
                    float rate2Scale = getRateMult(lfoRate[1][oi]);
                    voice.lfo2Phase[oi] += dt * clockHz * rate2Scale;
                    if (voice.lfo2Phase[oi] >= 1.0f)
                        voice.lfo2Phase[oi] -= 1.0f;
                }
            }'''
    content = re.sub(clock_sync_old, clock_sync_new, content)

    # Update process LFO edit mode
    process_lfo_old = r'''                    // If coming from an LFO mode, persist the last LFO's trimpot values\.
                    if \(lastLfoMode == 0 \|\| lastLfoMode == 1\) \{
                        int lastIdx = \(lastLfoMode < 0\) \? 0 : lastLfoMode;
                        lfoRate\[lastIdx\]        = params\[LFO_RATE_PARAM\]\.getValue\(\);
                        lfoPhaseOffset\[lastIdx\] = params\[LFO_PHASE_PARAM\]\.getValue\(\);
                        lfoDeform\[lastIdx\]      = params\[LFO_DEFORM_PARAM\]\.getValue\(\);
                        lfoAmp\[lastIdx\]         = params\[LFO_AMP_PARAM\]\.getValue\(\);
                    \}'''
    process_lfo_new = r'''                    // If coming from an LFO mode, persist the last LFO's trimpot values.
                    if (lastLfoMode == 0 || lastLfoMode == 1) {
                        int lastIdx = (lastLfoMode < 0) ? 0 : lastLfoMode;
                        if (lfoTgt == 0) {
                            for (int oi = 0; oi < 6; ++oi) {
                                lfoRate[lastIdx][oi]        = params[LFO_RATE_PARAM].getValue();
                                lfoPhaseOffset[lastIdx][oi] = params[LFO_PHASE_PARAM].getValue();
                                lfoDeform[lastIdx][oi]      = params[LFO_DEFORM_PARAM].getValue();
                                lfoAmp[lastIdx][oi]         = params[LFO_AMP_PARAM].getValue();
                            }
                        } else {
                            lfoRate[lastIdx][opSel]        = params[LFO_RATE_PARAM].getValue();
                            lfoPhaseOffset[lastIdx][opSel] = params[LFO_PHASE_PARAM].getValue();
                            lfoDeform[lastIdx][opSel]      = params[LFO_DEFORM_PARAM].getValue();
                            lfoAmp[lastIdx][opSel]         = params[LFO_AMP_PARAM].getValue();
                        }
                    }'''
    content = re.sub(process_lfo_old, process_lfo_new, content)

    process_lfo_edit_old = r'''                else \{
                    // LFO1 / LFO2 edit modes
                    int lfoSel = mode; // 0 or 1
                    if \(lfoSel < 0\) lfoSel = 0;
                    if \(lfoSel > 1\) lfoSel = 1;

                    // If switching between LFOs or coming from ENV, load the selected LFO's values\.
                    if \(lfoSel != lastLfoMode\) \{
                        params\[LFO_RATE_PARAM\]\.setValue\(lfoRate\[lfoSel\]\);
                        params\[LFO_PHASE_PARAM\]\.setValue\(lfoPhaseOffset\[lfoSel\]\);
                        params\[LFO_DEFORM_PARAM\]\.setValue\(lfoDeform\[lfoSel\]\);
                        params\[LFO_AMP_PARAM\]\.setValue\(lfoAmp\[lfoSel\]\);
                    \}

                    // Write current trimpot values back to the selected LFO's storage
                    lfoRate\[lfoSel\]        = params\[LFO_RATE_PARAM\]\.getValue\(\);
                    lfoPhaseOffset\[lfoSel\] = params\[LFO_PHASE_PARAM\]\.getValue\(\);
                    lfoDeform\[lfoSel\]      = params\[LFO_DEFORM_PARAM\]\.getValue\(\);
                    lfoAmp\[lfoSel\]         = params\[LFO_AMP_PARAM\]\.getValue\(\);

                    // Target operator: -1=ALL, 0\.\.5=specific
                    if \(lfoTgt == 0\) \{
                        lfoTargetOp\[lfoSel\] = -1;
                    \} else \{
                        lfoTargetOp\[lfoSel\] = opSel;
                    \}
                \}'''
    process_lfo_edit_new = r'''                else {
                    // LFO1 / LFO2 edit modes
                    int lfoSel = mode; // 0 or 1
                    if (lfoSel < 0) lfoSel = 0;
                    if (lfoSel > 1) lfoSel = 1;

                    if (lfoTgt == 0) {
                        // ALL operators
                        if (lastLfoMode != lfoSel) {
                            params[LFO_RATE_PARAM].setValue(lfoRate[lfoSel][0]);
                            params[LFO_PHASE_PARAM].setValue(lfoPhaseOffset[lfoSel][0]);
                            params[LFO_DEFORM_PARAM].setValue(lfoDeform[lfoSel][0]);
                            params[LFO_AMP_PARAM].setValue(lfoAmp[lfoSel][0]);
                        }

                        float r = params[LFO_RATE_PARAM].getValue();
                        float p = params[LFO_PHASE_PARAM].getValue();
                        float d = params[LFO_DEFORM_PARAM].getValue();
                        float a = params[LFO_AMP_PARAM].getValue();
                        for (int oi = 0; oi < 6; ++oi) {
                            lfoRate[lfoSel][oi]        = r;
                            lfoPhaseOffset[lfoSel][oi] = p;
                            lfoDeform[lfoSel][oi]      = d;
                            lfoAmp[lfoSel][oi]         = a;
                        }
                    } else {
                        // Single-operator edit
                        if (lastLfoMode != lfoSel || opSel != lastEnvEditOp) {
                            if (lastLfoMode == lfoSel) {
                                // Commit shapes for previously edited operator.
                                int prevOp = lastEnvEditOp;
                                if (prevOp < 0) prevOp = 0;
                                if (prevOp > 5) prevOp = 5;
                                lfoRate[lfoSel][prevOp]        = params[LFO_RATE_PARAM].getValue();
                                lfoPhaseOffset[lfoSel][prevOp] = params[LFO_PHASE_PARAM].getValue();
                                lfoDeform[lfoSel][prevOp]      = params[LFO_DEFORM_PARAM].getValue();
                                lfoAmp[lfoSel][prevOp]         = params[LFO_AMP_PARAM].getValue();
                            }

                            // Load current operator's shapes into the trimpots.
                            params[LFO_RATE_PARAM].setValue(lfoRate[lfoSel][opSel]);
                            params[LFO_PHASE_PARAM].setValue(lfoPhaseOffset[lfoSel][opSel]);
                            params[LFO_DEFORM_PARAM].setValue(lfoDeform[lfoSel][opSel]);
                            params[LFO_AMP_PARAM].setValue(lfoAmp[lfoSel][opSel]);
                            lastEnvEditOp = opSel;
                        }

                        // Continuously write trimpots back into the selected operator's shapes.
                        lfoRate[lfoSel][opSel]        = params[LFO_RATE_PARAM].getValue();
                        lfoPhaseOffset[lfoSel][opSel] = params[LFO_PHASE_PARAM].getValue();
                        lfoDeform[lfoSel][opSel]      = params[LFO_DEFORM_PARAM].getValue();
                        lfoAmp[lfoSel][opSel]         = params[LFO_AMP_PARAM].getValue();
                    }
                }'''
    content = re.sub(process_lfo_edit_old, process_lfo_edit_new, content)

    # Update copy to macros
    copy_macros_old = r'''                // Copy all LFO params to macros \(ENV shapes are handled separately below\)
                for \(int li = 0; li < 2; \+\+li\) \{
                    macros\.lfoRate\[li\]        = lfoRate\[li\];
                    macros\.lfoPhaseOffset\[li\] = lfoPhaseOffset\[li\];
                    macros\.lfoDeform\[li\]      = lfoDeform\[li\];
                    macros\.lfoAmp\[li\]         = lfoAmp\[li\];
                    macros\.lfoTargetOp\[li\]    = lfoTargetOp\[li\];
                \}'''
    copy_macros_new = r'''                // Copy all LFO params to macros (ENV shapes are handled separately below)
                for (int li = 0; li < 2; ++li) {
                    for (int oi = 0; oi < 6; ++oi) {
                        macros.lfoRate[li][oi]        = lfoRate[li][oi];
                        macros.lfoPhaseOffset[li][oi] = lfoPhaseOffset[li][oi];
                        macros.lfoDeform[li][oi]      = lfoDeform[li][oi];
                        macros.lfoAmp[li][oi]         = lfoAmp[li][oi];
                    }
                }'''
    content = re.sub(copy_macros_old, copy_macros_new, content)

    # Update dataToJson
    data_to_json_old = r'''        // Per-LFO settings \(patch persistence\)
        \{
            json_t\* lrJ = json_array\(\);
            json_t\* lpJ = json_array\(\);
            json_t\* ldJ = json_array\(\);
            json_t\* laJ = json_array\(\);
            json_t\* ltJ = json_array\(\);
            for \(int li = 0; li < 2; \+\+li\) \{
                json_array_append_new\(lrJ, json_real\(\(double\)lfoRate\[li\]\)\);
                json_array_append_new\(lpJ, json_real\(\(double\)lfoPhaseOffset\[li\]\)\);
                json_array_append_new\(ldJ, json_real\(\(double\)lfoDeform\[li\]\)\);
                json_array_append_new\(laJ, json_real\(\(double\)lfoAmp\[li\]\)\);
                json_array_append_new\(ltJ, json_integer\(lfoTargetOp\[li\]\)\);
            \}
            json_object_set_new\(rootJ, "lfoRate", lrJ\);
            json_object_set_new\(rootJ, "lfoPhase", lpJ\);
            json_object_set_new\(rootJ, "lfoDeform", ldJ\);
            json_object_set_new\(rootJ, "lfoAmp", laJ\);
            json_object_set_new\(rootJ, "lfoTarget", ltJ\);
        \}'''
    data_to_json_new = r'''        // Per-LFO settings (patch persistence)
        {
            json_t* lrJ = json_array();
            json_t* lpJ = json_array();
            json_t* ldJ = json_array();
            json_t* laJ = json_array();
            for (int li = 0; li < 2; ++li) {
                json_t* lrOpJ = json_array();
                json_t* lpOpJ = json_array();
                json_t* ldOpJ = json_array();
                json_t* laOpJ = json_array();
                for (int oi = 0; oi < 6; ++oi) {
                    json_array_append_new(lrOpJ, json_real((double)lfoRate[li][oi]));
                    json_array_append_new(lpOpJ, json_real((double)lfoPhaseOffset[li][oi]));
                    json_array_append_new(ldOpJ, json_real((double)lfoDeform[li][oi]));
                    json_array_append_new(laOpJ, json_real((double)lfoAmp[li][oi]));
                }
                json_array_append_new(lrJ, lrOpJ);
                json_array_append_new(lpJ, lpOpJ);
                json_array_append_new(ldJ, ldOpJ);
                json_array_append_new(laJ, laOpJ);
            }
            json_object_set_new(rootJ, "lfoRate", lrJ);
            json_object_set_new(rootJ, "lfoPhase", lpJ);
            json_object_set_new(rootJ, "lfoDeform", ldJ);
            json_object_set_new(rootJ, "lfoAmp", laJ);
        }'''
    content = re.sub(data_to_json_old, data_to_json_new, content)

    # Update dataFromJson
    data_from_json_old = r'''        // Per-LFO settings
        auto readLfoArr = \[\&\]\(const char\* key, float\* dst, float lo, float hi\) \{
            json_t\* aJ = json_object_get\(rootJ, key\);
            if \(aJ && json_is_array\(aJ\)\) \{
                int n = \(int\)json_array_size\(aJ\);
                for \(int li = 0; li < 2 && li < n; \+\+li\) \{
                    json_t\* vJ = json_array_get\(aJ, li\);
                    if \(vJ && json_is_number\(vJ\)\) \{
                        float v = \(float\)json_number_value\(vJ\);
                        if \(v < lo\) v = lo;
                        if \(v > hi\) v = hi;
                        dst\[li\] = v;
                    \}
                \}
            \}
        \};'''
    data_from_json_new = r'''        // Per-LFO settings
        auto readLfoArr = [&](const char* key, float dst[2][6], float lo, float hi) {
            json_t* aJ = json_object_get(rootJ, key);
            if (aJ && json_is_array(aJ)) {
                int n = (int)json_array_size(aJ);
                for (int li = 0; li < 2 && li < n; ++li) {
                    json_t* lfoJ = json_array_get(aJ, li);
                    if (lfoJ && json_is_array(lfoJ)) {
                        int on = (int)json_array_size(lfoJ);
                        for (int oi = 0; oi < 6 && oi < on; ++oi) {
                            json_t* vJ = json_array_get(lfoJ, oi);
                            if (vJ && json_is_number(vJ)) {
                                float v = (float)json_number_value(vJ);
                                if (v < lo) v = lo;
                                if (v > hi) v = hi;
                                dst[li][oi] = v;
                            }
                        }
                    } else if (lfoJ && json_is_number(lfoJ)) {
                        float v = (float)json_number_value(lfoJ);
                        if (v < lo) v = lo;
                        if (v > hi) v = hi;
                        for (int oi = 0; oi < 6; ++oi) dst[li][oi] = v;
                    }
                }
            }
        };'''
    content = re.sub(data_from_json_old, data_from_json_new, content)

    # Remove lfoTargetOp from dataFromJson
    content = re.sub(r'\s*\{\s*json_t\*\s*ltJ\s*=\s*json_object_get\(rootJ,\s*"lfoTarget"\);[\s\S]*?\}\s*\}', '', content)

    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def update_phaseon_macros():
    path = 'src/phaseon/PhaseonMacros.hpp'
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    content = re.sub(r'float\s+lfoRate\[2\]\s*=\s*\{0\.5f,\s*0\.5f\};', 
                     r'float lfoRate[2][6]        = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};', content)
    content = re.sub(r'float\s+lfoPhaseOffset\[2\]\s*=\s*\{0\.0f,\s*0\.0f\};', 
                     r'float lfoPhaseOffset[2][6] = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};', content)
    content = re.sub(r'float\s+lfoDeform\[2\]\s*=\s*\{0\.0f,\s*0\.0f\};', 
                     r'float lfoDeform[2][6]      = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};', content)
    content = re.sub(r'float\s+lfoAmp\[2\]\s*=\s*\{1\.0f,\s*1\.0f\};', 
                     r'float lfoAmp[2][6]         = {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};', content)

    content = re.sub(r'\s*int\s+lfoTargetOp\[2\]\s*=\s*\{-1,\s*-1\};.*?\n', '\n', content)

    apply_old = r'''    for \(int li = 0; li < 2; \+\+li\) \{
        voice\.lfoRateUser\[li\]   = m\.lfoRate\[li\];
        voice\.lfoPhaseUser\[li\]  = m\.lfoPhaseOffset\[li\];
        voice\.lfoDeformUser\[li\] = m\.lfoDeform\[li\];
        voice\.lfoAmpUser\[li\]    = m\.lfoAmp\[li\];
        voice\.lfoTargetOp\[li\]   = m\.lfoTargetOp\[li\];
    \}'''
    apply_new = r'''    for (int li = 0; li < 2; ++li) {
        for (int oi = 0; oi < 6; ++oi) {
            voice.lfoRateUser[li][oi]   = m.lfoRate[li][oi];
            voice.lfoPhaseUser[li][oi]  = m.lfoPhaseOffset[li][oi];
            voice.lfoDeformUser[li][oi] = m.lfoDeform[li][oi];
            voice.lfoAmpUser[li][oi]    = m.lfoAmp[li][oi];
        }
    }'''
    content = re.sub(apply_old, apply_new, content)

    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def update_phaseon_voice():
    path = 'src/phaseon/PhaseonVoice.hpp'
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    content = re.sub(r'float\s+lfoRateUser\[2\]\s*=\s*\{0\.5f,\s*0\.5f\};', 
                     r'float lfoRateUser[2][6]    = {{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};', content)
    content = re.sub(r'float\s+lfoPhaseUser\[2\]\s*=\s*\{0\.0f,\s*0\.0f\};', 
                     r'float lfoPhaseUser[2][6]   = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};', content)
    content = re.sub(r'float\s+lfoDeformUser\[2\]\s*=\s*\{0\.0f,\s*0\.0f\};', 
                     r'float lfoDeformUser[2][6]  = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};', content)
    content = re.sub(r'float\s+lfoAmpUser\[2\]\s*=\s*\{1\.0f,\s*1\.0f\};', 
                     r'float lfoAmpUser[2][6]     = {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};', content)

    content = re.sub(r'\s*int\s+lfoTargetOp\[2\]\s*=\s*\{-1,\s*-1\};.*?\n', '\n', content)

    content = re.sub(r'float\s+lfo1Phase\s*=\s*0\.0f;', r'float lfo1Phase[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};', content)
    content = re.sub(r'float\s+lfo2Phase\s*=\s*0\.0f;', r'float lfo2Phase[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};', content)

    content = re.sub(r'lfo1Phase\s*=\s*lfo2Phase\s*=\s*0\.0f;', 
                     r'for (int i = 0; i < 6; ++i) { lfo1Phase[i] = lfo2Phase[i] = 0.0f; }', content)

    tick_lfo_old = r'''        float rate1 = 0\.18f \* getRateMult\(lfoRateUser\[0\]\);
        float rate2 = 0\.41f \* getRateMult\(lfoRateUser\[1\]\);
        float rateScale = 0\.25f \+ motion \* 2\.5f;
        lfo1Phase \+= dt \* \(rate1 \* rateScale\);
        lfo2Phase \+= dt \* \(rate2 \* rateScale\);
        if \(lfo1Phase >= 1\.0f\) lfo1Phase -= 1\.0f;
        if \(lfo2Phase >= 1\.0f\) lfo2Phase -= 1\.0f;

        // Apply phase offset \+ deform \+ amplitude
        float lfo1p = lfo1Phase \+ lfoPhaseUser\[0\];
        if \(lfo1p >= 1\.0f\) lfo1p -= 1\.0f;
        float lfo1 = deformLfo\(lfo1p, lfoDeformUser\[0\]\) \* lfoAmpUser\[0\];

        float lfo2p = lfo2Phase \+ lfoPhaseUser\[1\];
        if \(lfo2p >= 1\.0f\) lfo2p -= 1\.0f;
        float lfo2 = deformLfo\(lfo2p, lfoDeformUser\[1\]\) \* lfoAmpUser\[1\];'''
    tick_lfo_new = r'''        float rateScale = 0.25f + motion * 2.5f;
        float lfo1[6];
        float lfo2[6];
        for (int i = 0; i < 6; ++i) {
            float rate1 = 0.18f * getRateMult(lfoRateUser[0][i]);
            float rate2 = 0.41f * getRateMult(lfoRateUser[1][i]);

            lfo1Phase[i] += dt * (rate1 * rateScale);
            lfo2Phase[i] += dt * (rate2 * rateScale);
            if (lfo1Phase[i] >= 1.0f) lfo1Phase[i] -= 1.0f;
            if (lfo2Phase[i] >= 1.0f) lfo2Phase[i] -= 1.0f;

            float lfo1p = lfo1Phase[i] + lfoPhaseUser[0][i];
            if (lfo1p >= 1.0f) lfo1p -= 1.0f;
            lfo1[i] = deformLfo(lfo1p, lfoDeformUser[0][i]) * lfoAmpUser[0][i];

            float lfo2p = lfo2Phase[i] + lfoPhaseUser[1][i];
            if (lfo2p >= 1.0f) lfo2p -= 1.0f;
            lfo2[i] = deformLfo(lfo2p, lfoDeformUser[1][i]) * lfoAmpUser[1][i];
        }'''
    content = re.sub(tick_lfo_old, tick_lfo_new, content)

    # Remove global lfoGrowl calculation
    content = re.sub(r'\s*float\s+macroLfo\s*=\s*clamp01\(macroLfoAmount\);\s*float\s+lfoGrowl\s*=\s*0\.0f;\s*if\s*\(macroLfo\s*>\s*0\.001f\)\s*\{\s*float\s+shape\s*=\s*macroLfo\s*\*\s*macroLfo;\s*lfoGrowl\s*=\s*lfo2\s*\*\s*shape;\s*\}', 
                     r'\n        float macroLfo = clamp01(macroLfoAmount);', content)

    # Update opLfo2
    op_lfo2_old = r'''                // Per-operator LFO targeting: if LFO targets specific op, zero it for others
                float opLfo2 = \(lfoTargetOp\[1\] == -1 \|\| lfoTargetOp\[1\] == i\) \? lfo2 : 0\.0f;
                float wobble = 0\.10f \* opLfo2 \+ 0\.08f \* randLane \+ 0\.02f \* micro;'''
    op_lfo2_new = r'''                float opLfo2 = lfo2[i];
                float wobble = 0.10f * opLfo2 + 0.08f * randLane + 0.02f * micro;'''
    content = re.sub(op_lfo2_old, op_lfo2_new, content)

    # Update lfoGrowl in FM depth
    fm_depth_old = r'''            // Macro LFO: additional FM index wobble \(PM depth\)\. Nonlinear so
            // low settings stay subtle while high settings become extreme\.
            if \(macroLfo > 0\.001f\) \{
                float maxSwing = 0\.30f \+ 1\.50f \* macroLfo; // 0\.3\.\.1\.8
                float delta = 1\.0f \+ lfoGrowl \* maxSwing;
                if \(delta < 0\.10f\) delta = 0\.10f;
                if \(delta > 3\.0f\)  delta = 3\.0f;
                effectiveFmDepth \*= delta;
            \}'''
    fm_depth_new = r'''            // Macro LFO: additional FM index wobble (PM depth). Nonlinear so
            // low settings stay subtle while high settings become extreme.
            if (macroLfo > 0.001f) {
                float shape = macroLfo * macroLfo;
                float opLfoGrowl = lfo2[i] * shape;
                float maxSwing = 0.30f + 1.50f * macroLfo; // 0.3..1.8
                float delta = 1.0f + opLfoGrowl * maxSwing;
                if (delta < 0.10f) delta = 0.10f;
                if (delta > 3.0f)  delta = 3.0f;
                effectiveFmDepth *= delta;
            }'''
    content = re.sub(fm_depth_old, fm_depth_new, content)

    # Update opLfo1
    op_lfo1_old = r'''                // Per-operator LFO targeting
                float opLfo1 = \(lfoTargetOp\[0\] == -1 \|\| lfoTargetOp\[0\] == i\) \? lfo1 : 0\.0f;
                float orbit = 0\.035f \* opLfo1 \+ 0\.020f \* randLane \+ 0\.006f \* micro;'''
    op_lfo1_new = r'''                float opLfo1 = lfo1[i];
                float orbit = 0.035f * opLfo1 + 0.020f * randLane + 0.006f * micro;'''
    content = re.sub(op_lfo1_old, op_lfo1_new, content)

    # Update opLfo1w
    op_lfo1w_old = r'''                float opLfo1w = \(lfoTargetOp\[0\] == -1 \|\| lfoTargetOp\[0\] == i\) \? lfo1 : 0\.0f;
                width = motion \* \(0\.06f \+ 0\.04f \* \(0\.5f \+ 0\.5f \* opLfo1w\)\);'''
    op_lfo1w_new = r'''                float opLfo1w = lfo1[i];
                width = motion * (0.06f + 0.04f * (0.5f + 0.5f * opLfo1w));'''
    content = re.sub(op_lfo1w_old, op_lfo1w_new, content)

    # Update pitch drift
    pitch_drift_old = r'''            // Macro LFO: pitch drift
            if \(macroLfo > 0\.001f\) \{
                float drift = lfoGrowl \* 0\.25f \* macroLfo; // small, signed offset
                op\.ratio \*= powf\(2\.0f, drift\);
            \}'''
    pitch_drift_new = r'''            // Macro LFO: pitch drift
            if (macroLfo > 0.001f) {
                float shape = macroLfo * macroLfo;
                float opLfoGrowl = lfo2[i] * shape;
                float drift = opLfoGrowl * 0.25f * macroLfo; // small, signed offset
                op.ratio *= powf(2.0f, drift);
            }'''
    content = re.sub(pitch_drift_old, pitch_drift_new, content)

    # Update updateFormantCoeffs
    formant_old = r'''        if \(macroLfo > 0\.001f\) \{
            float shape = macroLfo \* macroLfo;
            // We need to calculate lfo2 here since it's not passed in
            float lfo2p = lfo2Phase \+ lfoPhaseUser\[1\];
            if \(lfo2p >= 1\.0f\) lfo2p -= 1\.0f;
            float lfo2 = deformLfo\(lfo2p, lfoDeformUser\[1\]\) \* lfoAmpUser\[1\];
            
            float lfoGrowl = lfo2 \* shape;
            // Modulate vowel position by up to \+/- 0\.5 \(half the vowel space\)
            vp = clamp01\(vp \+ lfoGrowl \* 0\.5f\);
        \}'''
    formant_new = r'''        if (macroLfo > 0.001f) {
            float shape = macroLfo * macroLfo;
            // We need to calculate lfo2 here since it's not passed in
            float lfo2p = lfo2Phase[0] + lfoPhaseUser[1][0];
            if (lfo2p >= 1.0f) lfo2p -= 1.0f;
            float lfo2 = deformLfo(lfo2p, lfoDeformUser[1][0]) * lfoAmpUser[1][0];
            
            float lfoGrowl = lfo2 * shape;
            // Modulate vowel position by up to +/- 0.5 (half the vowel space)
            vp = clamp01(vp + lfoGrowl * 0.5f);
        }'''
    content = re.sub(formant_old, formant_new, content)

    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

if __name__ == '__main__':
    update_phaseon_cpp()
    update_phaseon_macros()
    update_phaseon_voice()
