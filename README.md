# Enhanced Differential Brake Steering Plugin for X-Plane 12
Copyright 2026 Kyle D. Ross  
See LICENSE for details.

Note:
This software is not affiliated with, endorsed by, or supported by Laminar Research. All trademarks are the property of
their respective owners and are used herein for reference only.

This is a native X-Plane plugin that fixes steering quirks and improves ground braking
dynamics for simulated aircraft. Specifically, it eliminates the jerkiness found
on castering aircraft by replacing built-in differential braking with smooth,
predictable steering logic. Additionally, it provides a two-stage progressive braking
system with integrated Anti-Skid (ABS) protection across aircraft to ensure smooth taxiing,
maximum stopping efficiency, and prevention of tire lockup.  This is helpful if you have braking mapped to a 
non-proportional button on a joystick, for example.  In such a scenario, braking is either all or nothing.  With this 
plugin, braking is gradual and can back off braking should there be wheel skidding.

The behavior of the plugin is set with various constants regarding maximum brake force,
ramp rates, ABS thresholds, and so-on.  You may adapt and build this software to suit
your needs.

If this software enhances your experience, please consider supporting the project:  
https://buymeacoffee.com/kyledross

## Requirements

- CMake 3.16+
- A C++20 compiler (GCC, Clang, or MSVC)
- The X-Plane 12 SDK headers (already vendored under `SDK/`)

## Building
### Local build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

This produces a shared library named `DiffBrakePlugin.xpl`:

- Linux: `DiffBrakePlugin.xpl` (ELF shared object)
- macOS: `DiffBrakePlugin.xpl` (Mach-O dylib, built with
  `-undefined dynamic_lookup` since X-Plane resolves SDK symbols at load
  time)
- Windows: `DiffBrakePlugin.xpl` (PE DLL)
### Reproducible Linux build

Use Docker to build and test the release artifact in a clean Ubuntu
environment:

```bash
./docker-build.sh
```

The script writes the tested Linux release payload to `docker-output/`:

- `DiffBrakePlugin.xpl`
- `install.sh`
- `LICENSE`
- `NOTICE`

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

For a GitHub Linux release, extract `DiffBrakePlugin_Linux_Install.tar.gz` and
run the included installer:

```bash
tar -xzf DiffBrakePlugin_Linux_Install.tar.gz
cd DiffBrakePlugin
./install.sh
```

The installer obtains the X-Plane 12 directory from
`~/.x-plane/x-plane_install_12.txt` and installs the plugin in the expected
Linux location. It does not modify the aircraft or any X-Plane settings.

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

## GitHub builds and releases

GitHub Actions builds and tests the Linux plugin for pushes and pull requests
targeting `main`, `master`, or `development`. Successful builds retain the
`docker-output` directory as a workflow artifact.

Push a version tag beginning with `v` to build the same payload and create a
draft GitHub release with `DiffBrakePlugin_Linux_Install.tar.gz` attached:

```bash
git tag v1.0.0
git push origin v1.0.0
```

Review and publish the draft release in GitHub after the workflow completes.

## Features and Controls

### 1. Smooth Differential Brake Steering
For aircraft with free-castering nose or tail gear (such as the Cirrus SR22 and SF50 Vision Jet), rudder inputs on the ground are mapped directly to gentle differential braking. This eliminates the abrupt jerks caused by simulator auto-toe-brake helpers while allowing natural, predictable low-speed ground maneuvering.

### 2. Two-Stage Progressive Braking
The plugin registers a dedicated custom command:
- **Command:** `diffbrakeplugin/braking/hold_brakes_progressive_abs`
- **Description:** `Hold Brakes (Progressive with ABS)`

Pilots can bind this command to a joystick button, throttle paddle, or keyboard key in X-Plane's settings (**Settings > Joystick / Keyboard**).

- **Stage 1 (Controlled Taxi Deceleration):** Holding the command smoothly ramps brake pressure up at 0.10 units/second, capped at 50% force (`0.50`) for the first 10.0 seconds (reaching 50% across 5.0 seconds and holding for another 5.0 seconds). This allows smooth, controlled deceleration during taxiing without jarring stops or excessive tire wear.
- **Stage 2 (Full Stopping Power):** Continuing to hold the command past 10.0 seconds automatically transitions to Stage 2, ramping pressure up to 100% force (`1.0`) across 5.0 seconds for maximum deceleration during short-field landings or emergency stops.
- **Smooth Release:** Releasing the command ramps pressure back down at 0.50 units/second to prevent abrupt weight transfer and nose bounce.
- **Fallback Mode:** If braking enhancements are disabled in the settings dialog, triggering this command automatically forwards directly to X-Plane's native "Hold brakes regular" command (`sim/flight_controls/brakes_regular`) so braking operates normally without progressive/ABS enhancements.

### 3. Universal Anti-Skid / Anti-Lock Braking System (ABS)
Active across all aircraft when groundspeed exceeds 5.0 m/s (~9.7 knots):
- Continuously monitors left and right main gear rotational velocities against true aircraft groundspeed.
- If a wheel enters a skid (wheel slip ratio drops below 0.40, meaning the wheel's linear speed is under 40% of groundspeed), brake pressure to that specific wheel is immediately pulsed off for 0.1 seconds (100 ms) to restore wheel spin.
- Prevents tire skidding, blowouts, and loss of directional stability during high-speed rollout braking.

### 4. Per-Aircraft Settings & Dear ImGui UI
Settings are segregated per aircraft and saved automatically in a `settings.json` file alongside the plugin:
- When a new aircraft is loaded, default settings are applied (differential braking enabled, braking enhancements enabled) and saved.
- Flown aircraft settings persist across simulator sessions.
- Open the settings dialog via the X-Plane menu (**Plugins > DiffBrakePlugin > Settings...**).
- The Dear ImGui dialog displays the active aircraft name and allows toggling:
  - **Enable differential braking**
  - **Enable braking enhancements**
- Changes update the settings file in real-time and apply immediately to the simulation.
- Close the dialog via the title bar or the **OK** button in the lower-right corner.
- View plugin information, licensing, third-party notices, and donation links via (**Plugins > DiffBrakePlugin > About...**).

## How it works

- A single flight loop callback (`XPLMRegisterFlightLoopCallback`) runs once
  per frame. It performs lightweight dataref operations with no measurable impact
  on flight simulator performance.
- The plugin automatically detects whether the loaded aircraft has
  free-castering steering by inspecting the active aircraft (`.acf`) configuration
  (`_gear_castors 1`) or steering deflection limits
  (`sim/aircraft/gear/acf_nw_steerdeg1` and `sim/aircraft/gear/acf_nw_steerdeg2`
  $\le 0.05^\circ$). This re-evaluates upon aircraft load
  (`XPLM_MSG_PLANE_LOADED`) without requiring an aircraft whitelist.
- Ground contact and speed are read from
  `sim/flightmodel/failures/onground_any` and
  `sim/flightmodel/position/groundspeed`. If groundspeed is ever unavailable,
  the plugin falls back to gating on throttle position
  (`sim/cockpit2/engine/actuators/throttle_ratio_all`), only braking
  differentially below 50% throttle.
- **Progressive Braking Accumulator:** When the `hold_brakes_progressive_abs` command
  is held, an internal hold duration timer advances. While under 10 seconds,
  pressure ramps up to a 50% cap; beyond 10 seconds, pressure ramps up to 100%.
  When released, pressure smoothly drains to zero.
- **Differential Steering Mapping:** Rudder/yaw input is read from
  `sim/joystick/yoke_heading_ratio` and mapped onto differential brake targets,
  capped at 8.5% brake force to maintain steady forward motion during turns. A small
  center deadzone absorbs hardware noise/jitter.
- While differential braking is active on castering aircraft, the plugin zeroes
  `sim/flightmodel/controls/rudder_ratio` every frame so rudder pedal input
  does not deflect the aerodynamic rudder surface at the same time, preventing
  opposing control fighting.
- **Force Blending:** The per-wheel differential braking force is added to the current
  progressive brake pressure and clamped to a maximum ratio of 1.0.
- **Anti-Skid (ABS) Monitoring:** When groundspeed is above 5.0 m/s and brakes are applied,
  wheel tangential speeds are calculated from tire rotational velocity
  (`sim/flightmodel2/gear/tire_rotation_speed_rad_sec` or fallback `sim/flightmodel/parts/tire_rot_speed_rad_sec`)
  and gear radius (`sim/flightmodel2/gear/tire_radius_mtrs` or fallbacks `sim/aircraft/parts/acf_gear_tirrad`, `sim/aircraft/parts/acf_gear_radi`).
  If slip drops below 40%, the affected wheel's brake is pulsed off for 0.1 seconds.
- **Dataref Output:** The final calculated forces are written to
  `sim/flightmodel/controls/l_brake_add` and `sim/flightmodel/controls/r_brake_add`.
  These "add" datarefs apply brake force additively on top of hardware toe brake pedals
  without overriding the pilot's physical hardware controls.
- While enabled, the plugin sets
  `sim/operation/override/override_gearbrake` to `1` for every aircraft to bypass
  X-Plane's abrupt built-in auto-toe-brake helper. The plugin does not override
  `sim/operation/override/override_wheel_steer`, allowing native free-castering physics
  to operate naturally without steering binding.
- All log entries are written to `Log.txt` prefixed with `DiffBrakePlugin: ` for
  easy inspection. State transitions (aircraft detection, ground/air changes, and
  differential braking activation) are logged once per event. Continuous brake force
  traces can be enabled for debugging.

## Tuning

The following constants at the top of `src/DiffBrakePlugin.cpp` can be
adjusted if needed:

### Progressive Braking
- `kProgressiveRampUpRate` – rate (pressure units per second) at which progressive
  brake pressure builds up (default `0.10`).
- `kProgressiveRampDownRate` – rate (pressure units per second) at which progressive
  brake pressure releases (default `0.50`).
- `kStage1BrakeCap` – maximum brake force (0.0–1.0) allowed during Stage 1
  application (default `0.50`, i.e. 50%).
- `kStage1Duration` – hold duration (in seconds) before progressing from Stage 1 to
  Stage 2 full braking (default `10.0` s).

### Anti-Skid (ABS)
- `kMinAbsGroundspeedMs` – minimum groundspeed in meters/second (~9.7 knots) required
  for ABS logic to engage (default `5.0` m/s).
- `kSkidSlipRatio` – wheel slip ratio threshold below which a wheel is flagged as
  skidding (default `0.4`, i.e. 40% of groundspeed).
- `kAbsPulseTime` – duration in seconds to pulse release brake pressure when a skid
  is detected (default `0.1` s).

### Differential Braking
- `kMaxGroundspeedForBrakingKnots` – speed below which differential braking
  is engaged (default `45.0` kts).
- `kMaxBrakeForce` – maximum additive differential brake force applied per side, in the
  0.0–1.0 range (default `0.085`, i.e. 8.5%).
- `kMaxThrottleForBraking` – fallback throttle threshold (0.0–1.0) used to
  gate differential braking when groundspeed is unavailable (default `0.5`,
  i.e. 50%).
- `kYawInputDeadzone` – rudder input magnitude (0.0–1.0) below which no
  differential brake is applied (default `0.05`, i.e. 5%).
- `kBrakeResponseCurveExponent` – exponent of the response curve applied to
  rudder input beyond the deadzone (default `1.0`, linear).
- `kEnableDiffBrakingLog` – boolean flag to turn on/off continuous differential
  braking debug log messages while braking is applied (default `false`).
- `kDiffBrakeLogInterval` – minimum interval (in seconds) between differential
  braking debug log entries when enabled (default `0.25` s).
