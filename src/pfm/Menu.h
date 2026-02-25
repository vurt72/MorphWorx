// PreenFM2 VCV Port - Minimal Menu.h shim
// Provides FullState struct and MIDICONFIG enums needed by the engine
#pragma once

#include "Storage.h"

// We always define CVIN for CV input support
#ifndef CVIN
#define CVIN
#endif

enum {
    MIDICONFIG_CVIN1_2 = 0,
    MIDICONFIG_CVIN_A2,
    MIDICONFIG_CVIN_A6,
    MIDICONFIG_CV_GATE,
    MIDICONFIG_USB,
    MIDICONFIG_CHANNEL1,
    MIDICONFIG_CHANNEL2,
    MIDICONFIG_CHANNEL3,
    MIDICONFIG_CHANNEL4,
    MIDICONFIG_CURRENT_INSTRUMENT,
    MIDICONFIG_GLOBAL,
    MIDICONFIG_THROUGH,
    MIDICONFIG_RECEIVES,
    MIDICONFIG_SENDS,
    MIDICONFIG_GLOBAL_TUNING,
    MIDICONFIG_PROGRAM_CHANGE,
    MIDICONFIG_BOOT_START,
    MIDICONFIG_OP_OPTION,
    MIDICONFIG_ENCODER,
    MIDICONFIG_TEST_NOTE,
    MIDICONFIG_TEST_VELOCITY,
    MIDICONFIG_LED_CLOCK,
    MIDICONFIG_ARPEGGIATOR_IN_PRESET,
    MIDICONFIG_OLED_SAVER,
    MIDICONFIG_UNLINKED_EDITING,
    MIDICONFIG_BOOT_SOUND,
    MIDICONFIG_SYSEX,
    MIDICONFIG_SIZE
};

enum SynthEditMode {
    SYNTH_MODE_EDIT = 0,
    SYNTH_MODE_MENU
};

enum MenuState {
    MAIN_MENU = 0,
    MENU_LOAD,
    MENU_SAVE_SELECT_BANK,
    MENU_SAVE_SELECT_BANK_PRESET,
    MENU_SAVE_SELECT_COMBO,
    MENU_SAVE_SELECT_COMBO_PRESET,
    MENU_LOAD_SELECT_BANK,
    MENU_LOAD_SELECT_BANK_PRESET,
    MENU_LOAD_SELECT_DX7_BANK,
    MENU_LOAD_RANDOMIZER,
    MENU_LOAD_SELECT_DX7_PRESET,
    MENU_LOAD_SELECT_COMBO,
    MENU_LOAD_SELECT_COMBO_PRESET,
    MENU_SAVE,
    MENU_SAVE_ENTER_PRESET_NAME,
    MENU_SAVE_ENTER_COMBO_NAME,
    MENU_SAVE_SYSEX_PATCH,
    MENU_CONFIG_SETTINGS,
    MENU_CONFIG_SETTINGS_SAVE,
    MENU_MIDI_BANK,
    MENU_MIDI_BANK_GET,
    MENU_MIDI_PATCH,
    MENU_MIDI_PATCH_GET,
    MENU_DONE,
    MENU_CANCEL,
    MENU_IN_PROGRESS,
    MENU_ERROR,
    MENU_TOOLS,
    MENU_CREATE,
    MENU_CREATE_BANK,
    MENU_CREATE_COMBO,
    MENU_DEFAULT_COMBO,
    MENU_DEFAULT_COMBO_SAVE,
    MENU_DEFAULT_COMBO_RESET,
    MENU_RENAME,
    MENU_RENAME_PATCH,
    MENU_RENAME_SELECT_BANK,
    MENU_RENAME_BANK,
    MENU_RENAME_SELECT_COMBO,
    MENU_RENAME_COMBO,
    MENU_SCALA,
    MENU_SCALA_ENABLE,
    MENU_SCALA_FILENAME,
    MENU_SCALA_FREQUENCY,
    MENU_SCALA_MAPPING,
    LAST_MENU
};

struct MenuItem {
    MenuState menuState;
    const char* name;
    bool hasSubMenu;
    short maxValue;
    MenuState subMenu[4];
};

struct Randomizer {
    char Oper;
    char EnvT;
    char IM;
    char Modl;
};

struct PFM2File;
struct ScalaScaleConfig;

struct FullState {
    SynthEditMode synthMode;
    int menuSelect;
    unsigned char previousMenuSelect;
    const MenuItem* currentMenuItem;
    char name[13];
    unsigned char firstMenu;
    unsigned char loadWhat;
    unsigned char saveWhat;
    unsigned char toolsWhat;
    unsigned char scalaWhat;
    unsigned char menuPosition[5];
    short midiConfigValue[MIDICONFIG_SIZE + 1];
    float globalTuning;
    unsigned char preenFMBankNumber;
    unsigned char preenFMPresetNumber;
    const struct PFM2File* preenFMBank;
    unsigned char preenFMComboNumber;
    unsigned char preenFMComboPresetNumber;
    const struct PFM2File* preenFMCombo;
    short dx7BankNumber;
    unsigned char dx7PresetNumber;
    const struct PFM2File* dx7Bank;
    // Scala not used in minimal port, but keep struct compatible
    // struct ScalaScaleConfig scalaScaleConfig;
    // Randomizer not used
    // struct Randomizer randomizer;
};

// Stub midiConfig array
struct MidiConfig {
    const char** valueName;
};
extern struct MidiConfig midiConfig[];
