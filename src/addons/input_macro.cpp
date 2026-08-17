#include "addons/input_macro.h"
#include "storagemanager.h"
#include "GamepadState.h"

#include "hardware/gpio.h"

// ---------------------------------------------------------------------------
// Level-sensed macro chain
//
// Shorting CHAIN_ENABLE_PIN to GND runs, forever:
//     [ main macro x CHAIN_REPEAT_COUNT, cleanup macro x1 ] x GAME_RESET_EVERY_GROUPS
//     reset macro x1
// One "group" is counted when the cleanup macro finishes. After the reset macro
// both counters are cleared and the cycle starts over at the main macro.
// Opening the connection stops immediately and clears both counters, so the next
// time it is shorted the chain always restarts at the first main macro.
//
// The macros themselves are never copied: every run reads the current contents
// of macroList[] straight out of storage, so editing macro 2 / 4 / 5 in the
// Web Config takes effect on the next repetition with no firmware change.
//
// CHAIN_ENABLE_PIN must be left unassigned in the Web Config Pin Mapping. If it
// is claimed by a button, a macro trigger, an addon or reserved hardware, the
// chain disables itself (fail-safe) rather than double-driving the pin.
// ---------------------------------------------------------------------------
namespace {
constexpr int CHAIN_MAIN_MACRO_INDEX = 1;       // Web Config "Macro 2"
constexpr int CHAIN_CLEANUP_MACRO_INDEX = 3;    // Web Config "Macro 4"
constexpr int GAME_RESET_MACRO_INDEX = 5;       // Web Config "Macro 6"

constexpr uint32_t CHAIN_REPEAT_COUNT = 10;     // main macro runs per cleanup macro
constexpr uint32_t GAME_RESET_EVERY_GROUPS = 20; // groups per game reset macro
constexpr int CHAIN_ENABLE_PIN = 21;            // GP21, active-low (GP21 <-> GND)

static_assert(CHAIN_REPEAT_COUNT > 0,
        "CHAIN_REPEAT_COUNT must be at least 1");
static_assert(GAME_RESET_EVERY_GROUPS > 0,
        "GAME_RESET_EVERY_GROUPS must be at least 1");
static_assert(CHAIN_MAIN_MACRO_INDEX >= 0 && CHAIN_MAIN_MACRO_INDEX < MAX_MACRO_LIMIT,
        "CHAIN_MAIN_MACRO_INDEX out of range");
static_assert(CHAIN_CLEANUP_MACRO_INDEX >= 0 && CHAIN_CLEANUP_MACRO_INDEX < MAX_MACRO_LIMIT,
        "CHAIN_CLEANUP_MACRO_INDEX out of range");
static_assert(GAME_RESET_MACRO_INDEX >= 0 && GAME_RESET_MACRO_INDEX < MAX_MACRO_LIMIT,
        "GAME_RESET_MACRO_INDEX out of range");
}

bool InputMacro::available() {
    // Macro Button initialized by void Gamepad::setup()
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
            case GpioAction::BUTTON_PRESS_MACRO_1:
            case GpioAction::BUTTON_PRESS_MACRO_2:
            case GpioAction::BUTTON_PRESS_MACRO_3:
            case GpioAction::BUTTON_PRESS_MACRO_4:
            case GpioAction::BUTTON_PRESS_MACRO_5:
            case GpioAction::BUTTON_PRESS_MACRO_6:
                return true;
            default:
                break;
        }
    }

    // A usable chain-enable pin is enough on its own to want this addon loaded,
    // even when no macro trigger pin is mapped at all.
    if (isValidPin(CHAIN_ENABLE_PIN) &&
            pinMappings[CHAIN_ENABLE_PIN].action == GpioAction::NONE &&
            Storage::getInstance().getAddonOptions().macroOptions.enabled) {
        return true;
    }

    return false;
}

void InputMacro::setup() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    macroButtonMask = 0;
    memset(macroPinMasks, 0, sizeof(macroPinMasks));
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
                macroButtonMask = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_1:
                macroPinMasks[0] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_2:
                macroPinMasks[1] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_3:
                macroPinMasks[2] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_4:
                macroPinMasks[3] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_5:
                macroPinMasks[4] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroPinMasks[5] = 1 << pin;
                break;
            default:
                break;
        }
    }

    inputMacroOptions = &Storage::getInstance().getAddonOptions().macroOptions;
    if (inputMacroOptions->macroBoardLedEnabled && isValidPin(BOARD_LED_PIN)) {
        gpio_init(BOARD_LED_PIN);
        gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);
        boardLedEnabled = true;
    } else {
        boardLedEnabled = false;
    }
    boardLedEnabled = false;
    prevMacroInputPressed = false;
    setupChainPin();
    stopChain();
}

void InputMacro::setupChainPin() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    // Only drive the pin if it is a real GPIO that nothing else in this profile
    // claims, otherwise shorting it to GND would also fire a real input.
    chainPinAvailable = isValidPin(CHAIN_ENABLE_PIN) &&
            pinMappings[CHAIN_ENABLE_PIN].action == GpioAction::NONE;
    if (!chainPinAvailable)
        return;

    gpio_init(CHAIN_ENABLE_PIN);
    gpio_set_dir(CHAIN_ENABLE_PIN, GPIO_IN);
    gpio_pull_up(CHAIN_ENABLE_PIN);
}

bool InputMacro::isChainEnabledByPin() const {
    // active-low: shorted to GND = enabled, open (internal pull-up) = disabled
    return chainPinAvailable && gpio_get(CHAIN_ENABLE_PIN) == 0;
}

void InputMacro::startChainMacro(int macroIndex) {
    if (macroIndex < 0 || macroIndex >= MAX_MACRO_LIMIT) {
        stopChain();
        return;
    }

    Macro& macro = inputMacroOptions->macroList[macroIndex];
    if (!macro.enabled || macro.macroInputs_count == 0) {
        stopChain(); // fail-safe: never index into an empty/disabled macro
        return;
    }

    chainMacroIndex = macroIndex;
    macroPosition = macroIndex;
    pressedMacro = -1;
    macroInputPosition = 0;
    isMacroRunning = true;
    // The chain owns this macro's lifetime, there is no physical trigger held.
    isMacroTriggerHeld = true;
    currentMicros = getMicro();
    macroStartTime = currentMicros;

    MacroInput& firstInput = macro.macroInputs[macroInputPosition];
    uint32_t firstInputDuration = firstInput.duration + firstInput.waitDuration;
    macroInputHoldTime = firstInputDuration <= 0 ? INPUT_HOLD_US : firstInputDuration;
}

void InputMacro::stopChain() {
    chainModeActive = false;
    chainMacroIndex = -1;
    chainMainCompletedCount = 0;
    chainGroupCompletedCount = 0;
    reset();
}

void InputMacro::handleChainMacroFinished() {
    if (!chainModeActive)
        return;

    if (macroPosition == CHAIN_MAIN_MACRO_INDEX) {
        ++chainMainCompletedCount;
        if (chainMainCompletedCount >= CHAIN_REPEAT_COUNT) {
            startChainMacro(CHAIN_CLEANUP_MACRO_INDEX);
        } else {
            startChainMacro(CHAIN_MAIN_MACRO_INDEX);
        }
        return;
    }

    // The cleanup macro closes a group; every GAME_RESET_EVERY_GROUPS of them the
    // game gets put back to a known state before the next group starts.
    if (macroPosition == CHAIN_CLEANUP_MACRO_INDEX) {
        chainMainCompletedCount = 0;
        ++chainGroupCompletedCount;
        if (chainGroupCompletedCount >= GAME_RESET_EVERY_GROUPS) {
            startChainMacro(GAME_RESET_MACRO_INDEX);
        } else {
            startChainMacro(CHAIN_MAIN_MACRO_INDEX);
        }
        return;
    }

    if (macroPosition == GAME_RESET_MACRO_INDEX) {
        chainMainCompletedCount = 0;
        chainGroupCompletedCount = 0;
        startChainMacro(CHAIN_MAIN_MACRO_INDEX);
        return;
    }

    stopChain(); // should not happen: the chain only ever runs the macros above
}

void InputMacro::reset() {
    macroPosition = -1;
    pressedMacro = -1;
    isMacroRunning = false;
    macroStartTime = 0;
    macroInputPosition = 0;
    isMacroTriggerHeld = false;
    macroInputHoldTime = INPUT_HOLD_US;
    if (boardLedEnabled) {
        gpio_put(BOARD_LED_PIN, 0);
    }
}

void InputMacro::restart(Macro& macro) {
    macroStartTime = currentMicros;
    macroInputPosition = 0;
    MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
    uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
    macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
}

void InputMacro::checkMacroPress() {
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    Mask_t allPins = gamepad->debouncedGpio;

    // Go through our macro list
    pressedMacro = -1;
    for(int i = 0; i < MAX_MACRO_LIMIT; i++) {
        if ( inputMacroOptions->macroList[i].enabled == false ) // Skip disabled macros
            continue;
        Macro * macro = &inputMacroOptions->macroList[i];
        if ( macro->useMacroTriggerButton ) {
            // Use Gamepad Button for Macro Trigger
            if ((allPins & macroButtonMask) &&
                ((gamepad->state.buttons & macro->macroTriggerButton) ||
                    (gamepad->state.dpad & (macro->macroTriggerButton >> 16))) ) {
                pressedMacro = i;
                break;
            }
        } else if ( allPins & macroPinMasks[i] ) {
            // Use Pin Manager for Macro Trigger
            pressedMacro = i;
            break;
        }
    }
}

void InputMacro::checkMacroAction() {
    bool macroInputPressed = (pressedMacro != -1); // Was any macro input pressed?

    // Is our pressed macro button different from our current macro AND no macro is running?
    if ( pressedMacro != macroPosition && !isMacroRunning ) {
        macroPosition = pressedMacro; // move our position to that macro
    }

    bool newPress = macroInputPressed && (prevMacroInputPressed ^ macroInputPressed);

    // Check to see if we should change the current macro (or turn off based on input)
    if ( inputMacroOptions->macroList[macroPosition].macroType == ON_PRESS ) {
        // START Macro: On Press or On Hold Repeat
        if (!isMacroRunning ) {
            isMacroTriggerHeld = newPress;
        }
    } else if ( inputMacroOptions->macroList[macroPosition].macroType == ON_HOLD_REPEAT ) {
        isMacroTriggerHeld = macroInputPressed;
    } else if ( inputMacroOptions->macroList[macroPosition].macroType == ON_TOGGLE ) {
        //isMacroTriggerHeld = macroInputPressed;
        if (!isMacroRunning ) {
            isMacroTriggerHeld = newPress;
        } else if (isMacroRunning && newPress) {
            // STOP Macro: Toggle on new press
            reset(); // Stop Macro: Toggle
            prevMacroInputPressed = macroInputPressed;
            return;
        }
    }

    prevMacroInputPressed = macroInputPressed;
    if (!isMacroRunning && isMacroTriggerHeld) {
        // New Macro to run
        macroPosition = pressedMacro; // Set current macro
        Macro& macro = inputMacroOptions->macroList[macroPosition];
        MacroInput& macroInput = macro.macroInputs[macroInputPosition];
        uint32_t macroInputDuration = macroInput.duration + macroInput.waitDuration;
        macroInputHoldTime = macroInputDuration <= 0 ? INPUT_HOLD_US : macroInputDuration;
        isMacroRunning = true;
        macroStartTime = getMicro(); // current time
    }
}

void InputMacro::runCurrentMacro() {
    // Do nothing if macro is not currently running
    if (!isMacroRunning ||
            macroPosition == -1)
        return;

    Macro& macro = inputMacroOptions->macroList[macroPosition];

    // Stop Macro if released (ON PRESS & ON HOLD REPEAT)
    // In chain mode the enable pin owns the lifetime, not a physical trigger.
    if (!chainModeActive &&
            inputMacroOptions->macroList[macroPosition].macroType == ON_HOLD_REPEAT &&
            !isMacroTriggerHeld ) {
        reset();
        return;
    }

    MacroInput& macroInput = macro.macroInputs[macroInputPosition];
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    currentMicros = getMicro();

    if (!macro.interruptible && macro.exclusive) {
        // Prevent any other inputs from modifying our input (Exclusive)
        gamepad->state.dpad = 0;
        gamepad->state.buttons = 0;
    } else {
        if (macro.useMacroTriggerButton) {
            // Remove the trigger button from the input state
            gamepad->state.dpad &= ~(macro.macroTriggerButton >> 16);
            gamepad->state.buttons &= ~macro.macroTriggerButton;
        }
        if (macro.interruptible &&
            (gamepad->state.buttons != 0 || gamepad->state.dpad != 0)) {
            // Macro is interruptible and a user pressed something
            reset();
            return;
        }
    }

    // Have we elapsed the input hold time?
    if ((currentMicros - macroStartTime) >= macroInputHoldTime) {
        macroStartTime = currentMicros;
        macroInputPosition++;
        
        if (macroInputPosition >= (macro.macroInputs_count)) {
            if (chainModeActive) {
                // Hand over to the chain scheduler and bail out immediately:
                // macro/macroInput above may now refer to the previous macro.
                handleChainMacroFinished();
                return;
            }
            if ( macro.macroType == ON_PRESS ) {
                reset(); // On press = no more macro
            } else {
                restart(macro); // On Hold-Repeat or On Toggle = start macro again
            }
        } else {
            MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
            uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
            macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
        }
    }

    // Check if we should still hold this macro input based on duration
    if ((currentMicros - macroStartTime) <= macroInput.duration) {
        uint32_t buttonMask = macroInput.buttonMask;
        if (buttonMask & GAMEPAD_MASK_DU) {
            gamepad->state.dpad |= GAMEPAD_MASK_UP;
        }
        if (buttonMask & GAMEPAD_MASK_DD) {
            gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
        }
        if (buttonMask & GAMEPAD_MASK_DL) {
            gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
        }
        if (buttonMask & GAMEPAD_MASK_DR) {
            gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
        }
        gamepad->state.buttons |= buttonMask;

        // Macro LED is on if we're currently running and inputs are doing something (wait-timers turn it off)
        if (boardLedEnabled) {
            gpio_put(BOARD_LED_PIN, (gamepad->state.dpad || gamepad->state.buttons) ? 1 : 0);
        }
    }
}

void InputMacro::preprocess()
{
    FocusModeOptions * focusModeOptions = &Storage::getInstance().getAddonOptions().focusModeOptions;
    if (focusModeOptions->enabled && focusModeOptions->macroLockEnabled) {
        Gamepad * gamepad = Storage::getInstance().GetGamepad();
        // Override Toggle Pressed OR focus mode pin is set
        if (focusModeOptions->overrideEnabled ||
            (gamepad->mapFocusMode->pinMask && (gamepad->debouncedGpio & gamepad->mapFocusMode->pinMask))) {
            // Focus mode locks out macros, and that includes the chain.
            if (chainModeActive) {
                stopChain();
            }
            return;
        }
    }

    if (!isChainEnabledByPin()) {
        // Enable pin is open (or unusable): stop any chain and behave exactly
        // like stock GP2040-CE, so manually triggered macros are untouched.
        if (chainModeActive) {
            stopChain();
        }

        checkMacroPress();
        checkMacroAction();
        runCurrentMacro();
        return;
    }

    if (!chainModeActive) {
        // Pin just went low: take control away from any manually running macro
        // and always (re)start the chain at the first main macro, counts zeroed.
        reset();
        chainModeActive = true;
        chainMainCompletedCount = 0;
        chainGroupCompletedCount = 0;
        startChainMacro(CHAIN_MAIN_MACRO_INDEX);
    } else if (!isMacroRunning) {
        // Current step was aborted from underneath us (an interruptible macro
        // seeing user input); resume the chain at the same step.
        startChainMacro(chainMacroIndex);
    }

    // No checkMacroPress()/checkMacroAction() while chaining: physical macro
    // triggers must not disturb the scheduler.
    runCurrentMacro();
}

void InputMacro::reinit() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    macroButtonMask = 0;
    memset(macroPinMasks, 0, sizeof(macroPinMasks));
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
                macroButtonMask = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_1:
                macroPinMasks[0] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_2:
                macroPinMasks[1] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_3:
                macroPinMasks[2] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_4:
                macroPinMasks[3] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_5:
                macroPinMasks[4] = 1 << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroPinMasks[5] = 1 << pin;
                break;
            default:
                break;
        }
    }

    // Profile switch: re-check the chain pin against the new mapping and never
    // carry chain state across.
    setupChainPin();
    stopChain();
}
