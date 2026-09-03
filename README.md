# Enhanced Differential Brake Steering Plugin for X-Plane 12
Copyright 2026 Kyle D. Ross  
See LICENSE for details.

An X-Plane 12 plugin that adds enhanced differential braking to aircraft with free-castering
steering gear (such as the Cirrus SR22, Cirrus Vision Jet SF50, Lancair
Evolution, RV-10, etc.). While the aircraft is on the ground and rolling
slowly, rudder/yaw input from your joystick is mapped onto the left and right brakes so you can steer the 
castering wheel the same way you would in the real airplane. The plugin is inert for steerable
aircraft.

If this software enhances your experience, please consider supporting the project:  
https://buymeacoffee.com/kyledross

## Requirements

- CMake 3.16+
- A C++20 compiler (GCC, Clang, or MSVC)
- The X-Plane 12 SDK headers (already vendored under `SDK/`)

## Building

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --config Release
```

This produces a shared library named `DiffBrakePlugin.xpl`:

- Linux: `DiffBrakePlugin.xpl` (ELF shared object)
- macOS: `DiffBrakePlugin.xpl` (Mach-O dylib, built with
  `-undefined dynamic_lookup` since X-Plane resolves SDK symbols at load
  time)
- Windows: `DiffBrakePlugin.xpl` (PE DLL)

## Building and deploying automatically

The `build_and_deploy.sh` script automates the steps below: it builds the
plugin, finds your X-Plane 12 installation by reading
`~/.x-plane/x-plane_install_12.txt` (created by X-Plane's installer), creates
the plugin's directory under `Resources/plugins` if needed, and copies the
freshly built `.xpl` into place.

```bash
./build_and_deploy.sh
```

## Installing into X-Plane

X-Plane expects plugins to live in a per-platform subfolder under a folder
named after the plugin, inside `Resources/plugins`:

```
<X-Plane 12>/Resources/plugins/DiffBrakePlugin/
├── lin_x64/DiffBrakePlugin.xpl   (Linux)
├── mac_x64/DiffBrakePlugin.xpl   (macOS)
└── win_x64/DiffBrakePlugin.xpl   (Windows)
```

Steps:

1. Build the plugin as described above.
2. Create the folder
   `<X-Plane 12 install>/Resources/plugins/DiffBrakePlugin/`.
3. Inside it, create the platform-specific subfolder that matches your OS
   (`lin_x64`, `mac_x64`, or `win_x64`) and copy the built `.xpl` file into
   it.
4. Start (or restart) X-Plane 12. Load the Cirrus SR22 or the Cirrus Vision
   Jet, and check `Log.txt` (in the X-Plane root folder) for lines prefixed
   `DiffBrakePlugin: ` to confirm the plugin loaded and detected the
   aircraft.

## How it works

- A single flight loop callback (`XPLMRegisterFlightLoopCallback`) runs once
  per frame. It only performs a handful of dataref reads and, at most, two
  writes, so it has no measurable impact on sim performance.
- The plugin automatically detects whether the loaded aircraft has a
  free-castering steering by checking whether the active aircraft (`.acf`) file
  configures free-castoring gear (`_gear_castors 1`, as in the Cirrus SF50 and
  Cirrus SR22), or by inspecting the steering deflection limits
  (`sim/aircraft/gear/acf_nw_steerdeg1` and `sim/aircraft/gear/acf_nw_steerdeg2`
  $\le 0.05^\circ$). When castering gear is detected, the plugin enables
  differential braking. This re-evaluates upon aircraft load
  (`XPLM_MSG_PLANE_LOADED`) without requiring an aircraft whitelist.
- Ground contact and speed are read from
  `sim/flightmodel/failures/onground_any` and
  `sim/flightmodel/position/groundspeed`. If groundspeed is ever unavailable,
  the plugin falls back to gating on throttle position
  (`sim/cockpit2/engine/actuators/throttle_ratio_all`), only braking
  differentially below 50% throttle.
- Rudder/yaw input is read from `sim/joystick/yoke_heading_ratio` and mapped
  onto `sim/flightmodel/controls/l_brake_add` and
  `sim/flightmodel/controls/r_brake_add`, capped at 75% brake force even at
  full rudder deflection to avoid accidental maximum braking. These "add"
  datarefs apply additional brake force on top of whatever the pilot's brake
  pedals/axis are already commanding, so the plugin never has to take
  ownership of (i.e. override) the normal brake controls.
- The rudder-to-brake mapping is deliberately gentle, not linear: inputs
  within a small deadzone around center apply no brake at all (absorbing
  joystick centering noise), inputs beyond that ramp in along a quadratic
  curve (so a slight rudder movement only applies a small brake nudge, not
  a large fraction of the max force), and the resulting brake force is
  rate-limited so it can only change by a bounded amount per second. Together
  these prevent the sudden "wheel locks up" jerk that a naive linear mapping
  can produce from small/noisy rudder inputs.
- The left/right brake force is derived from a single rate-limited, signed
  "brake balance" value (negative = left brake, positive = right brake)
  rather than two independently rate-limited left/right values. This
  guarantees the two sides can never both be braking at once: reversing the
  rudder smoothly slides the balance through zero (briefly releasing both
  brakes) instead of ramping the old side down while simultaneously ramping
  the new side up. The latter used to add up to more total stopping force
  right at the moment of direction change, which is what caused the
  aircraft to abruptly come to a stop before turning the other way.
- While differential braking is active, the plugin also zeroes
  `sim/flightmodel/controls/rudder_ratio` every frame so rudder pedal input
  no longer deflects the aerodynamic rudder surface at the same time it is
  applying brakes. This prevents "dual-steering" from both the rudder and
  the brakes fighting each other; the rudder is returned to normal joystick
  control as soon as differential braking becomes inactive.
- Whenever a castering gear aircraft is loaded, the plugin sets
  `sim/operation/override/override_gearbrake` to `1`, which tells X-Plane's
  physics engine to bypass its own built-in step-function auto-toe-brake
  helper logic entirely (the crude native behavior this plugin is meant to
  replace) without having to modify the aircraft's `.acf` file in
  Plane-Maker. Debug logging confirmed this works: `actual_left_brake`/
  `actual_right_brake` always matched exactly what the plugin requested,
  even across rudder direction reversals, so no extra native braking was
  ever being added.
- The plugin does **not** set `sim/operation/override/override_wheel_steer`.
  It was tried in an earlier revision on the theory that X-Plane's native
  nosewheel-steering model was the remaining cause of an abrupt "stop"
  feeling when reversing rudder direction. However, per X-Plane's own
  `DataRefs.txt`, that override only lets a plugin drive
  `sim/flightmodel2/gear/tire_steer_command_deg` directly - it does not make
  the nosewheel free-caster by itself. Since this plugin never writes a
  steer command, enabling it actually froze the nosewheel at whatever angle
  it last had instead of letting it free-caster, which caused binding and
  produced the very "stop" symptom it was meant to fix. Leaving this
  override alone lets X-Plane's normal free-castering nosewheel physics run
  (which is how the real Cirrus's nosewheel behaves), while
  `override_gearbrake` alone remains enabled and is reverted to `0` (giving
  control back to X-Plane) as soon as the aircraft is switched away from a
  castering gear aircraft or the plugin is disabled.
- All log entries are written to `Log.txt` and prefixed with
  `DiffBrakePlugin: ` for easy searching. The plugin logs once when a
  castering aircraft is detected, once on each ground/air transition, once on each
  activation/deactivation of differential braking, and optionally (when
  debug logging is enabled and differential braking is actively applied)
  throttled to once per quarter second with the rudder input and resulting brake
  forces for debugging.
  This trace also includes the actual brake ratio X-Plane reports it is
  using per wheel (`sim/cockpit2/controls/left_brake_ratio` /
  `right_brake_ratio`); if those values are noticeably higher than what the
  plugin requested, that is evidence X-Plane's native auto-toe-brake logic
  is still contributing braking on top of ours despite the override above.

## Tuning

The following constants at the top of `src/DiffBrakePlugin.cpp` can be
adjusted if needed:

- `kMaxGroundspeedForBrakingKnots` – speed below which differential braking
  is engaged (default 15 kts).
- `kMaxBrakeForce` – maximum additive brake force applied per side, in the
  0.0–1.0 range (default 0.05, i.e. 5%). Kept deliberately low: steering
  only needs a gentle retarding force on one wheel to create a turning
  moment — heavier braking slows the aircraft dramatically and causes a
  "jerk to a stop" feel when reversing direction.
- `kMaxThrottleForBraking` – fallback throttle threshold (0.0–1.0) used to
  gate differential braking when groundspeed is unavailable (default 0.5,
  i.e. 50%).
- `kYawInputDeadzone` – rudder input magnitude (0.0–1.0) below which no
  brake is applied at all (default 0.05, i.e. 5%).
- `kBrakeResponseCurveExponent` – exponent of the easing curve applied to
  rudder input beyond the deadzone before scaling to brake force; higher
  values make small rudder movements apply proportionally less brake
  (default 2.0, i.e. quadratic).
- `kMaxBrakeChangePerSecond` – maximum rate (in brake-force units per
  second, 0.0–1.0 scale) at which the applied brake force is allowed to
  change, smoothing out sudden input spikes (default 0.6, i.e. 0 to full
  in ~0.5 s at the 0.05 max brake force).
- `kEnableDiffBrakingLog` – boolean flag to turn on/off continuous differential
  braking debug log messages while braking is applied (default `false`).
- `kDiffBrakeLogInterval` – minimum interval (in seconds) between differential
  braking debug log entries when enabled (default 0.25 s, i.e. once a quarter
  second).
