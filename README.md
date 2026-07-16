# DisableVrTeleport

An X-Plane plugin that lets you disable VR touchpad/thumbstick teleport-locomotion
on demand, so accidentally brushing the controller's thumbstick can't yank you out
of the cockpit mid-flight.

There's no window or UI. It's controlled entirely through a hold-gesture and an
optional key/button binding.

## Usage

- **Hold the VR controller's menu button for 1.5 seconds** to toggle teleport
  disabled/enabled. A quick tap still opens/closes X-Plane's radial menu exactly
  as normal — only a sustained hold triggers the toggle, so nothing about the
  menu button's usual behavior changes.
- **Bind `disablevrteleport/toggle`** to any key or controller button, via
  X-Plane's Joystick/keyboard settings, for a one-press toggle instead of the hold
  gesture.
- **Plugins menu → DisableVrTeleport** also shows the toggle, with a checkmark
  reflecting the current state.

Each toggle is confirmed with a spoken status message ("VR sticks disabled" /
"VR sticks enabled"), audible identically in VR and on the 2D monitor.

## How it works

Teleport is driven by X-Plane's joystick axis-assignment system, not a command.
`sim/joystick/joystick_axis_assignments` is a 500-entry array where each entry
says what function a given physical controller axis currently serves; values 49
and 50 ("VR Touchpad X/Y") are what let the touchpad/thumbstick arm and aim a
teleport. Disabling teleport means scanning that array for every axis holding
49/50, remembering its original value, and zeroing it. Re-enabling restores the
saved values.

While disabled, a ~1 Hz background check keeps re-scanning the array and
re-zeroing any axis that newly shows 49/50 — a safety net in case X-Plane or a
headset reconnect silently reassigns a different physical axis to the touchpad
function mid-session.

The toggle gesture itself listens on `sim/VR/reserved/menu` (the VR controller's
menu button) rather than `sim/VR/reserved/select` (the general-purpose cockpit
interaction button), specifically to avoid accidental toggles during normal
switch/knob/yoke interaction.

State is not persisted — teleport always comes back enabled on restart. See
[PLAN.md](PLAN.md) for the fuller design history and rationale.

## Building

Requires CMake 3.16+ and a C++17 compiler. The vendored X-Plane SDK
(`third_party/SDK`) is used for headers and, on Windows/Mac, link libraries.

```sh
cmake -B build
cmake --build build --config RelWithDebInfo
```

The build produces a platform-specific `.xpl` plugin binary:

| Platform | Output |
|---|---|
| Windows | `build/win_x64/DisableVrTeleport.xpl` |
| macOS   | `build/mac_x64/DisableVrTeleport.xpl` |
| Linux   | `build/lin_x64/DisableVrTeleport.xpl` |

## Installing

Copy the platform's output folder (e.g. `win_x64/DisableVrTeleport.xpl`) into
`X-Plane 12/Resources/plugins/DisableVrTeleport/<platform>/`.
