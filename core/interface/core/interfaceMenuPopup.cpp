#include "interfaceMenuPopup.h"
#include "core/interface.h"
#include "gibgob/GameMenuControl.h"

InterfaceMenuPopup mtMenuPopup;

const uint8_t kMenuModulesCount = 11;
const char* const kModuleNamesList[kMenuModulesCount] = { "Sequencer",
                                                          "Instrument Parameters",
                                                          "Sample Playback",
                                                          "Master",
                                                          "Song",
                                                          "Sample Loader",
                                                          "File",
                                                          "Config",
                                                          "CHIMP OUT",
                                                          "Sample Editor",
                                                          "Sample Recorder" };

#ifdef TRACKER_MINI

// GibGob OS: what the monkey says about each (same order as kModuleNamesList)
static const char* const kModuleDescriptions[kMenuModulesCount] = {
    "this is where you make your beats and melodies.",
    "this is where you set the sound of your synths.",
    "play samples and set their start, end and loop.",
    "the whole mix: volume, reverb, delay and the master FX.",
    "chain your patterns into a song.",
    "load samples into your instruments.",
    "open, save and start projects.",
    "the Mini's own settings.",
    "go bananas: hold buttons for live effects.",
    "cut, fade and shape your samples.",
    "record from the mic, line in or USB.",
};
static gibgob::GameMenuView s_gameMenu;

uint32_t instrumentParamOptions[2] = { 1, 2 };
InterfaceMenuPopup::ModuleModes instrumentParamModes = { 2, instrumentParamOptions, { 2, 0 } };
uint32_t masterOptions[4] = { (1 << 8), (2 << 8), (3 << 8), (4 << 8) }; // 4: GibGob OS Master FX
InterfaceMenuPopup::ModuleModes masterModes = { 4, masterOptions, { 4, 0 } };
uint32_t performOptions[2] = { 1, 2 };
InterfaceMenuPopup::ModuleModes performModes = { 2, performOptions, { 2, 0 } };

const InterfaceMenuPopup::ModuleRef InterfaceMenuPopup::modules[kMenuModulesCount] = {
    { mtInterface.modules[13], interfaceButtonPattern, 0, nullptr }, // GibGob OS main sequencer
    { mtInterface.modules[5], interfaceButtonParams, 0, &instrumentParamModes },
    { mtInterface.modules[3], interfaceButtonSamplePlay, 0, nullptr },
    { mtInterface.modules[12], interfaceButtonMaster, 0, &masterModes },
    { mtInterface.modules[4], interfaceButtonSong, 0, nullptr },
    { mtInterface.modules[1], interfaceButtonSampleLoad, 0, nullptr },
    { mtInterface.modules[0], interfaceButtonFile, 0, nullptr },
    { mtInterface.modules[7], interfaceButtonConfig, 0, nullptr },
    { mtInterface.modules[9], interfaceButtonPerformance, 0, nullptr }, // GibGob OS Perform page
    { mtInterface.modules[6], interfaceButtonSampleEdit, 0, nullptr },
    { mtInterface.modules[8], interfaceButtonSampleRec, 0, nullptr },

};

static void functLeftPopup();
static void functRightPopup();
static void functUpPopup();
static void functDownPopup();
static void functMiddlePopup();

static void functEncoderPopup(int16_t value);

void InterfaceMenuPopup::initPopupsDisplayControls()
{
    boxMenuData.columnsCount = kMenuColumnsCount;
    boxMenuData.rowsCount = kMenuRowsCount;
    boxMenuData.textList = (char**)modulesMenuTexts;
    boxMenuData.modes = modulesMenuModes;

    // GibGob OS: the video game style menu over the whole screen
    strControlProperties prop;
    prop.x = 0;
    prop.y = 0;
    prop.w = 800;
    prop.h = 480;
    prop.style = 0;
    prop.data = &s_gameMenu;
    prop.text = nullptr;
    prop.colors = display.getTheme().miniBoxMenuColors;
    boxMenu = display.createControl<gibgob::GameMenuControl>(&prop);
}

void InterfaceMenuPopup::refreshColors()
{
    display.setControlColors(boxMenu, display.getTheme().miniBoxMenuColors);
}

// GibGob OS: Menu pressed. Closed: open it (the release right after keeps it
// open). Open: go to the highlighted item.
void InterfaceMenuPopup::showModulePopup(ModuleBase* sourceModule,
                                         void (*switchModuleCallback)(uint8_t))
{
    if (isVisible)
    {
        ignoreNextRelease = false;
        closePopup(sourceModule);
        return;
    }
    ignoreNextRelease = true;
    openPopup(sourceModule, switchModuleCallback);
}

// Menu let go: nothing (the menu stays until Menu is pressed again)
void InterfaceMenuPopup::hideModulePopup(ModuleBase* modulePtr)
{
    (void)modulePtr;
    ignoreNextRelease = false;
}

void InterfaceMenuPopup::openPopup(ModuleBase* sourceModule,
                                   void (*switchModuleCallback)(uint8_t))
{
    constexpr int boxesCount = kMenuRowsCount * kMenuColumnsCount;

    InterfaceMenuPopup::switchModuleCallback = switchModuleCallback;

    for (int box = 0; box < boxesCount; box++)
    {
        if (box >= kMenuModulesCount)
        {
            menuFunctions[box] = 0;
            modulesMenuTexts[box] = nullptr;
            modulesMenuModes[box] = nullptr;
            continue;
        }

        if (box < 8)
        {
            menuFunctions[box] = mtConfig.interface.functionKeysBinding[box];
            if (menuFunctions[box] >= kMenuModulesCount)
            {
                menuFunctions[box] = 0;
            }
        }
        else
        {
            bool assigned;
            for (uint8_t moduleIdx = 0; moduleIdx < kMenuModulesCount; moduleIdx++)
            {
                assigned = false;
                for (uint8_t assignedIdx = 0; assignedIdx < box; assignedIdx++)
                {
                    if (menuFunctions[assignedIdx] == moduleIdx)
                    {
                        assigned = true;
                    }
                }

                if (!assigned)
                {
                    menuFunctions[box] = moduleIdx;
                    break;
                }
            }
        }

        modulesMenuTexts[box] = kModuleNamesList[menuFunctions[box]];
        if (modules[menuFunctions[box]].modes != nullptr)
        {
            modules[menuFunctions[box]].modes->modes.current =
              modules[menuFunctions[box]].module->getCurrentModeIdx();
            modulesMenuModes[box] = &modules[menuFunctions[box]].modes->modes;
        }
        else
        {
            modulesMenuModes[box] = nullptr;
        }
    }

    selectedBox = 0;

    for (int moduleIdx = 0; moduleIdx < kMenuModulesCount; moduleIdx++)
    {
        if (modules[moduleIdx].module == sourceModule)
        {
            for (uint8_t box = 0; box < boxesCount; box++)
            {
                if (menuFunctions[box] == moduleIdx)
                {
                    selectedModuleOnShow = moduleIdx;
                    selectedBox = box;
                    break;
                }
            }
        }
    }

    this->sourceModule = sourceModule;
    isVisible = true;
    updateGameMenu();

    setModulePopupFunct();

    sourceModule->hideControlsUnderMiniMenu();

    gibgob::ensureMenuArt(); // the beach and the monkey into the display chip (once)
    display.setControlValue(boxMenu, selectedBox);
    display.setControlShow(boxMenu);
    display.exclusiveControl = boxMenu; // the full-screen menu alone (see display.h)
    display.refreshControl(boxMenu);

    display.synchronizeRefresh();
}

void InterfaceMenuPopup::closePopup(ModuleBase* modulePtr)
{
    display.exclusiveControl = -1;
    display.setControlHide(boxMenu);

    mtInterface.ip->clearButton(interfaceButtonLeft);
    mtInterface.ip->clearButton(interfaceButtonRight);
    mtInterface.ip->clearButton(interfaceButtonUp);
    mtInterface.ip->clearButton(interfaceButtonDown);
    mtInterface.ip->clearButton(interfaceButtonMiniMiddle);

    mtInterface.ip->clearAllPots();

    display.refreshControl(boxMenu);

    display.synchronizeRefresh();

    isVisible = false;

    uint8_t moduleToSwitch = modules[menuFunctions[selectedBox]].button;
    uint32_t startOptions = modules[menuFunctions[selectedBox]].defaultOption;

    if (modules[selectedModuleOnShow].button != modules[menuFunctions[selectedBox]].button)
    {
        if (switchModuleCallback != nullptr)
        {
            switchModuleCallback(moduleToSwitch);
            switchModuleCallback = nullptr;
        }
        else
        {
            modulePtr->eventFunct(eventSwitchModule, modulePtr, &moduleToSwitch, &startOptions);
        }
    }
    else
    {
        if (switchModuleCallback != nullptr)
        {
            switchModuleCallback(moduleToSwitch);
            switchModuleCallback = nullptr;
        }
        else
        {
            modulePtr->start(modules[menuFunctions[selectedBox]].defaultOption);
        }
    }
}

void InterfaceMenuPopup::setModulePopupFunct()
{
    mtInterface.ip->clearButtonsRange(interfaceButton0, interfaceButton32);

    mtInterface.ip->setPotObj(interfacePot0, functEncoderPopup);

    mtInterface.ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeftPopup);
    mtInterface.ip->setButtonObj(interfaceButtonRight, buttonPress, functRightPopup);
    mtInterface.ip->setButtonObj(interfaceButtonUp, buttonPress, functUpPopup);
    mtInterface.ip->setButtonObj(interfaceButtonDown, buttonPress, functDownPopup);

    mtInterface.ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functMiddlePopup);
}

void InterfaceMenuPopup::handleFailedSwitching()
{
    // source module handles functions assignment
    display.exclusiveControl = -1;
    display.setControlHide(boxMenu);
    display.refreshControl(boxMenu);
    display.synchronizeRefresh();

    isVisible = false;
}

void InterfaceMenuPopup::changeModulePopupValue(int16_t value, uint8_t dir)
{
    if (dir > 0)
    {
        switch (dir)
        {
            case 1:
                selectedBox = selectedBox < kMenuColumnsCount
                                ? ((kMenuRowsCount - 1) * kMenuColumnsCount) + selectedBox
                                : selectedBox - kMenuColumnsCount;
                break;
            case 2:
                selectedBox = selectedBox % kMenuColumnsCount >= kMenuColumnsCount - 1
                                ? selectedBox - (kMenuColumnsCount - 1)
                                : selectedBox + 1;
                break;
            case 3:
                selectedBox = selectedBox >= (kMenuRowsCount - 1) * kMenuColumnsCount
                                ? selectedBox - ((kMenuRowsCount - 1) * kMenuColumnsCount)
                                : selectedBox + kMenuColumnsCount;
                break;
            case 4:
                selectedBox = selectedBox % kMenuColumnsCount == 0
                                ? selectedBox + (kMenuColumnsCount - 1)
                                : selectedBox - 1;
                break;
            default: selectedBox = 0; break;
        }
        if (selectedBox >= kMenuModulesCount)
        {
            selectedBox = kMenuModulesCount - 1;
        }
    }
    else
    {
        // +/-: one item up / down, going round
        const int step = value > 0 ? 1 : (value < 0 ? -1 : 0);
        selectedBox = (uint8_t)(((int)selectedBox + step + kMenuModulesCount) % kMenuModulesCount);
    }
    updateGameMenu();
    display.setControlValue(boxMenu, selectedBox);
    display.refreshControl(boxMenu);
}

// the game menu's view: every item in box order, the selected one, its modes
void InterfaceMenuPopup::updateGameMenu()
{
    s_gameMenu.count = kMenuModulesCount;
    for (int box = 0; box < kMenuModulesCount && box < gibgob::GameMenuView::kMaxItems; box++)
    {
        s_gameMenu.items[box] = kModuleNamesList[menuFunctions[box]];
        s_gameMenu.descriptions[box] = kModuleDescriptions[menuFunctions[box]];
    }
    s_gameMenu.selected = selectedBox < kMenuModulesCount ? selectedBox : 0;
    const strBoxMenuData::Modes* m = modulesMenuModes[s_gameMenu.selected];
    s_gameMenu.modeCount = m != nullptr ? m->count : 0;
    s_gameMenu.modeCurrent = m != nullptr ? m->current : 0;
}

void InterfaceMenuPopup::setModulePopupValue(int16_t value)
{
    selectedBox = value;
    updateGameMenu();

    display.setControlValue(boxMenu, value);
    display.refreshControl(boxMenu);
}

void InterfaceMenuPopup::changeModuleMode()
{
    ModuleBase* destModule = sourceModule;
    uint32_t options = 0;
    bool srcIsDst = modules[menuFunctions[selectedBox]].module == sourceModule;
    bool destHasModes = modules[menuFunctions[selectedBox]].modes != nullptr;

    if (srcIsDst)
    {
        if (destHasModes)
        {
            int optionsIdx = modules[menuFunctions[selectedBox]].modes->modes.current + 1;
            if (optionsIdx >= modules[menuFunctions[selectedBox]].modes->modes.count)
            {
                optionsIdx = 0;
            }
            options = modules[menuFunctions[selectedBox]].modes->options[optionsIdx];
        }
        else
        {
            if (switchModuleCallback != nullptr)
            {
                uint8_t moduleToSwitch = modules[menuFunctions[selectedBox]].button;
                switchModuleCallback(moduleToSwitch);
                switchModuleCallback = nullptr;
            }
            else
            {
                sourceModule->start(modules[menuFunctions[selectedBox]].defaultOption);
            }
        }

        if (switchModuleCallback != nullptr)
        {
            uint8_t moduleToSwitch = modules[menuFunctions[selectedBox]].button;
            switchModuleCallback(moduleToSwitch);
            switchModuleCallback = nullptr;
        }
        else
        {
            sourceModule->start(options);
        }
    }
    else
    {
        if (destHasModes)
        {
            int optionsIdx = modules[menuFunctions[selectedBox]].modes->modes.current;
            options = modules[menuFunctions[selectedBox]].modes->options[optionsIdx];
        }
        else
        {
            options = modules[menuFunctions[selectedBox]].defaultOption;
        }
        destModule = modules[menuFunctions[selectedBox]].module;
        uint8_t moduleToSwitchButton = modules[menuFunctions[selectedBox]].button;

        if (switchModuleCallback != nullptr)
        {
            switchModuleCallback(moduleToSwitchButton);
            switchModuleCallback = nullptr;
        }
        else
        {
            sourceModule->eventFunct(
              eventSwitchModule, sourceModule, &moduleToSwitchButton, &options);
        }
    }
    if (sourceModule->wasLastSwitchingSuccessfull())
    {
        isVisible = false; // the menu goes on over the new screen
        openPopup(destModule, nullptr);
    }
    else
    {
        handleFailedSwitching();
    }
}

//******************************************************************************************************
// GibGob OS: one column of items, Up / Down scroll it (going round)
static void functLeftPopup()
{
}

static void functRightPopup()
{
}

static void functUpPopup()
{
    mtMenuPopup.changeModulePopupValue(-1);
}

static void functDownPopup()
{
    mtMenuPopup.changeModulePopupValue(1);
}

static void functEncoderPopup(int16_t value)
{
    mtMenuPopup.changeModulePopupValue(value);
}

static void functMiddlePopup()
{
    mtMenuPopup.changeModuleMode();
}

#endif
