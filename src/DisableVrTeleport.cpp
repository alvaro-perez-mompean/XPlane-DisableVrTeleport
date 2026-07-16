// DisableVrTeleport -- disables X-Plane's VR teleport-locomotion gesture on demand,
// so accidentally brushing the VR controller's thumbstick can't yank you out of the
// cockpit. No window/UI: controlled by holding the VR controller's menu button for
// 1.5 seconds, or by binding the disablevrteleport/toggle command to a key/button.
//
// Mechanism (see PLAN.md for how this was found): teleport is driven by X-Plane's
// joystick axis-assignment system, not a command -- sim/joystick/joystick_axis_assignments
// is an int[500] dataref where each entry says what function a physical controller axis
// currently serves. Values 49/50 ("VR Touchpad X/Y") are what let the touchpad/thumbstick
// arm and aim a teleport. Disabling teleport means scanning for every axis holding 49/50,
// remembering its original value, and zeroing it; re-enabling restores those values.
//
// The toggle itself is triggered by holding sim/VR/reserved/menu (an undocumented,
// "reserved" command exposing the VR controller's menu button) for 1.5s. The trigger
// (sim/VR/reserved/select) was deliberately not used for this, since it's the
// general-purpose cockpit interaction button (switches, knobs, yoke) and tying the
// toggle to it risked accidental flips during normal flying.
//
// While disabled, a ~1 Hz flight-loop callback (ReassertionCallback) keeps re-scanning
// the array and re-zeroing any axis that newly shows 49/50 -- a safety net in case
// X-Plane or a headset reconnect silently reassigns a different physical axis to the
// touchpad function mid-session. No persistence yet: state always resets to
// "enabled" on restart (see PLAN.md milestone 3).

#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

XPLMDataRef g_axis_assignments_ref = nullptr;
XPLMCommandRef g_toggle_command = nullptr;
XPLMMenuID g_menu = nullptr;
int g_toggle_item_index = -1;

// sim/VR/reserved/menu turned out to be a *command* (confirmed via DataRefTool
// showing it as type "com"), not a dataref -- it has no persistent "value". We
// register a pass-through observer (inBefore=1, always return 1) so a normal quick
// press still opens/closes the radial menu exactly as X-Plane intends; only a
// sustained hold triggers our own toggle logic.
XPLMCommandRef g_vr_menu_command = nullptr;
bool g_vr_menu_observer_registered = false;

bool g_teleport_disabled = false;

struct SavedAxis {
    int index;
    int original_value;
};
std::vector<SavedAxis> g_saved_axes;

// Hold-to-toggle: press and hold the VR controller's menu button for 1.5 seconds to
// flip the touchpad axes on/off, with a spoken confirmation (audible in VR same as
// 2D -- XPLMSpeakString needs no window/UI).
constexpr float kLongHoldSeconds = 1.5f;
float g_menu_press_start_time = -1.0f;
bool g_menu_long_hold_fired = false;

void SetTeleportDisabled(bool disabled);

int VrMenuCommandObserver(XPLMCommandRef /*command*/, XPLMCommandPhase phase, void* /*refcon*/) {
    if (phase == xplm_CommandBegin) {
        g_menu_press_start_time = XPLMGetElapsedTime();
        g_menu_long_hold_fired = false;
    } else if (phase == xplm_CommandContinue) {
        if (!g_menu_long_hold_fired && g_menu_press_start_time >= 0.0f &&
            (XPLMGetElapsedTime() - g_menu_press_start_time) >= kLongHoldSeconds) {
            g_menu_long_hold_fired = true;
            SetTeleportDisabled(!g_teleport_disabled);
        }
    } else if (phase == xplm_CommandEnd) {
        g_menu_press_start_time = -1.0f;
    }

    return 1;  // never block the underlying menu behavior -- a normal quick press
               // still opens/closes the radial menu exactly as before
}

// sim/VR/reserved/menu isn't registered by X-Plane's VR subsystem until VR is
// actually active, so a single XPLMFindCommand at plugin startup could miss it --
// retry (see RetryRegisterObserverCallback below) until it resolves.
void EnsureVrMenuObserverRegistered() {
    if (g_vr_menu_observer_registered) {
        return;
    }
    g_vr_menu_command = XPLMFindCommand("sim/VR/reserved/menu");
    if (g_vr_menu_command) {
        XPLMRegisterCommandHandler(g_vr_menu_command, VrMenuCommandObserver, 1, nullptr);
        g_vr_menu_observer_registered = true;
        XPLMDebugString("DisableVrTeleport: registered observer on sim/VR/reserved/menu\n");
    }
}

// Retries registration on its own, once a second, with no user action needed. Stops
// itself (returns 0) once registered.
float RetryRegisterObserverCallback(float /*elapsed_since_last_call*/,
                                     float /*elapsed_since_last_flight_loop*/,
                                     int /*counter*/, void* /*refcon*/) {
    EnsureVrMenuObserverRegistered();
    return g_vr_menu_observer_registered ? 0 : 1.0f;
}

void UpdateMenuCheckmark() {
    if (g_menu != nullptr && g_toggle_item_index >= 0) {
        XPLMCheckMenuItem(g_menu, g_toggle_item_index,
                           g_teleport_disabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
}

// Re-scans the array every time rather than trusting indices from a previous call --
// the VR runtime may re-lay-out axis indices across headset reconnects (see PLAN.md
// risk #2), so a stale cached index list could zero/restore the wrong axis.
void SetTeleportDisabled(bool disabled) {
    if (!g_axis_assignments_ref) {
        XPLMDebugString("DisableVrTeleport: axis assignments dataref not found, cannot toggle\n");
        return;
    }
    if (disabled == g_teleport_disabled) {
        return;
    }

    char line[128];

    if (disabled) {
        int total = XPLMGetDatavi(g_axis_assignments_ref, nullptr, 0, 0);
        if (total <= 0) {
            XPLMDebugString("DisableVrTeleport: axis array empty, nothing to disable\n");
            return;
        }
        std::vector<int> values(static_cast<size_t>(total));
        XPLMGetDatavi(g_axis_assignments_ref, values.data(), 0, total);

        g_saved_axes.clear();
        for (int i = 0; i < total; ++i) {
            if (values[i] == 49 || values[i] == 50) {
                g_saved_axes.push_back({i, values[i]});
            }
        }

        for (const auto& saved : g_saved_axes) {
            int zero = 0;
            XPLMSetDatavi(g_axis_assignments_ref, &zero, saved.index, 1);
        }

        std::snprintf(line, sizeof(line), "DisableVrTeleport: toggle -- disabled, zeroed %zu axis entries\n",
                      g_saved_axes.size());
        XPLMDebugString(line);
    } else {
        for (const auto& saved : g_saved_axes) {
            int original = saved.original_value;
            XPLMSetDatavi(g_axis_assignments_ref, &original, saved.index, 1);
        }
        std::snprintf(line, sizeof(line),
                      "DisableVrTeleport: toggle -- re-enabled, restored %zu axis entries\n",
                      g_saved_axes.size());
        XPLMDebugString(line);
        g_saved_axes.clear();
    }

    g_teleport_disabled = disabled;
    UpdateMenuCheckmark();

    // Audible status confirmation -- works identically in VR and on the 2D monitor,
    // no window/UI needed.
    XPLMSpeakString(g_teleport_disabled ? "VR sticks disabled" : "VR sticks enabled");
}

// While disabled, periodically re-scans for any axis that newly shows 49/50 (e.g. a
// headset reconnect re-assigning a different physical axis to the touchpad function)
// and zeroes it too, adding it to g_saved_axes so it gets restored correctly on
// re-enable. A no-op while enabled. Runs for the plugin's whole lifetime at ~1 Hz --
// cheap (one 500-int array read) and avoids flight-loop register/unregister churn on
// every toggle. Already-zeroed axes read back as 0, so a nonzero 49/50 found here
// always means something reasserted it since the last disable/catch.
float ReassertionCallback(float /*elapsed_since_last_call*/, float /*elapsed_since_last_flight_loop*/,
                          int /*counter*/, void* /*refcon*/) {
    if (!g_teleport_disabled || !g_axis_assignments_ref) {
        return 1.0f;
    }

    int total = XPLMGetDatavi(g_axis_assignments_ref, nullptr, 0, 0);
    if (total <= 0) {
        return 1.0f;
    }
    std::vector<int> values(static_cast<size_t>(total));
    XPLMGetDatavi(g_axis_assignments_ref, values.data(), 0, total);

    size_t newly_caught = 0;
    for (int i = 0; i < total; ++i) {
        if (values[i] == 49 || values[i] == 50) {
            g_saved_axes.push_back({i, values[i]});
            int zero = 0;
            XPLMSetDatavi(g_axis_assignments_ref, &zero, i, 1);
            ++newly_caught;
        }
    }

    if (newly_caught > 0) {
        char line[128];
        std::snprintf(line, sizeof(line),
                      "DisableVrTeleport: reassertion loop caught and re-zeroed %zu axis entries\n",
                      newly_caught);
        XPLMDebugString(line);
    }

    return 1.0f;  // keep checking every second for the plugin's whole lifetime
}

int ToggleCommandHandler(XPLMCommandRef /*command*/, XPLMCommandPhase phase, void* /*refcon*/) {
    if (phase == xplm_CommandBegin) {
        SetTeleportDisabled(!g_teleport_disabled);
    }
    return 1;
}

void MenuHandler(void* /*menu_ref*/, void* /*item_ref*/) {
    SetTeleportDisabled(!g_teleport_disabled);
}

}  // namespace

PLUGIN_API int XPluginStart(char* out_name, char* out_sig, char* out_desc) {
    std::strcpy(out_name, "DisableVrTeleport");
    std::strcpy(out_sig, "com.apmomp.disablevrteleport");
    std::strcpy(out_desc,
                "Hold the VR menu button for 1.5s (or bind disablevrteleport/toggle) to "
                "disable/enable VR touchpad teleport");

    g_axis_assignments_ref = XPLMFindDataRef("sim/joystick/joystick_axis_assignments");

    g_toggle_command = XPLMCreateCommand("disablevrteleport/toggle",
                                          "Toggle VR touchpad teleport on/off");

    int menu_container_item =
        XPLMAppendMenuItem(XPLMFindPluginsMenu(), "DisableVrTeleport", nullptr, 0);
    g_menu = XPLMCreateMenu("DisableVrTeleport", XPLMFindPluginsMenu(), menu_container_item,
                            MenuHandler, nullptr);
    g_toggle_item_index =
        XPLMAppendMenuItem(g_menu, "Disable VR teleport", reinterpret_cast<void*>(0), 0);
    UpdateMenuCheckmark();

    return 1;
}

PLUGIN_API void XPluginStop() {
    // XPluginDisable() (always called before Stop) already tears down every
    // handler/callback registered by Enable; this is just a final safety net
    // in case Stop is ever reached with teleport still disabled.
    if (g_teleport_disabled) {
        SetTeleportDisabled(false);
    }
    g_toggle_command = nullptr;
}

// Enable/Disable can cycle independently of Start/Stop (e.g. toggling the
// plugin off/on in Plugin Admin without a reload), so every handler and
// callback registered here must be unregistered in XPluginDisable below --
// otherwise a Disable->Enable cycle re-registers on top of a registration
// that was never removed, and X-Plane starts calling it multiple times per
// tick.
PLUGIN_API int XPluginEnable() {
    XPLMRegisterCommandHandler(g_toggle_command, ToggleCommandHandler, 1, nullptr);
    // Keep retrying registration on its own -- sim/VR/reserved/menu may not exist
    // until the VR subsystem actually initializes, so no manual action is needed.
    XPLMRegisterFlightLoopCallback(RetryRegisterObserverCallback, 1.0f, nullptr);
    // ~1 Hz safety net -- see ReassertionCallback's comment.
    XPLMRegisterFlightLoopCallback(ReassertionCallback, 1.0f, nullptr);
    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (g_teleport_disabled) {
        SetTeleportDisabled(false);
    }
    XPLMUnregisterCommandHandler(g_toggle_command, ToggleCommandHandler, 1, nullptr);
    if (g_vr_menu_observer_registered) {
        XPLMUnregisterCommandHandler(g_vr_menu_command, VrMenuCommandObserver, 1, nullptr);
        g_vr_menu_observer_registered = false;
    }
    g_menu_press_start_time = -1.0f;
    g_menu_long_hold_fired = false;
    XPLMUnregisterFlightLoopCallback(RetryRegisterObserverCallback, nullptr);
    XPLMUnregisterFlightLoopCallback(ReassertionCallback, nullptr);
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*from*/, int /*message*/, void* /*param*/) {}
