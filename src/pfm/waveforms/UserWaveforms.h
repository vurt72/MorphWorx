#pragma once

#include <string>

namespace pfm {

// PreenFM2 supports 6 user waveforms (User1..User6)
static constexpr int USER_WAVE_COUNT = 6;
static constexpr int USER_WAVE_SAMPLES = 1024; // per hardware/forum + 4KB BIN

// Load user waveforms from a directory.
// Looks for USR1.BIN..USR6.BIN (preferred) and falls back to usr1.txt..usr6.txt.
// Returns true if at least one waveform was loaded.
bool loadUserWaveformsFromDir(const std::string& dirPath);

// Returns whether user waveform N (1..6) is loaded.
bool isUserWaveformLoaded(int userWaveNumber);

// For diagnostics/logging.
const std::string& getUserWaveformsLastError();

} // namespace pfm
