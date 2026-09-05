/*
*   Copyright 2026 Kyle D. Ross
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
    // Prefix required by design.md for all log entries.
    constexpr const char* kLogPrefix = "DiffBrakePlugin: ";

    // Maximum steering deflection limit (in degrees) to consider the gear
    // free-castering (no active mechanical steering linkage).
    constexpr float kMaxSteerDegForCastering = 0.05f;

    // Below this groundspeed (in knots) differential braking is active.
    // Above it, aerodynamic control surfaces should dominate steering, so we
    // stay out of the way.
    constexpr float kMaxGroundspeedForBrakingKnots = 45.0f;

    // Groundspeed dataref is in meters/second; convert knots -> m/s.
    constexpr float kKnotsToMetersPerSecond = 0.514444f;
    constexpr float kMaxGroundspeedForBrakingMs =
        kMaxGroundspeedForBrakingKnots * kKnotsToMetersPerSecond;

    // Maximum brake force we are willing to add (0.0 - 1.0 range, same as
    // the underlying brake datarefs). Kept deliberately low: differential
    // braking for ground steering only needs a gentle retarding force on
    // one wheel to create a turning moment — locking or heavily loading a
    // wheel slows the aircraft dramatically and causes the "jerk to a stop"
    // feel.
    constexpr float kMaxBrakeForce = 0.085f;

    // Small rudder inputs near center are ignored entirely. This absorbs
    // joystick centering noise / hardware jitter on the yaw axis so that
    // resting your feet on the pedals, or the slightest unintentional
    // twist, never triggers any braking at all.
    constexpr float kYawInputDeadzone = 0.05f;

    // Above the deadzone, the response curve uses a 1.5 power rather than a
    // linear response, so small rudder deflections produce only a gentle brake
    // nudge, while the full 0-8.5% range is still reachable at full rudder
    // deflection. This is what actually fixes "the slightest movement
    // applies way too much brake": a linear mapping means even a tiny
    // deflection commands a proportionally large fraction of the max brake
    // force, which is what was causing the sudden jerk/lockup feel.
    constexpr float kBrakeResponseCurveExponent = 1.5f;

    // Maximum amount the applied brake force is allowed to change per
    // second when braking is increasing (balance moving away from zero).
    // This rate-limits how quickly brakes can go from 0 to full,
    // smoothing out any remaining input noise/spikes and preventing a
    // sudden wheel lock-up/jerk even if the rudder input itself jumps
    // abruptly from one frame to the next. With kMaxBrakeForce=0.085, a
    // value of 0.25 reaches full braking force in about 0.34 seconds.
    constexpr float kMaxBrakeChangePerSecond = 0.25f;

    // Rate at which the brake balance is allowed to move back toward zero
    // (i.e. brake pressure is being released). A higher value than
    // kMaxBrakeChangePerSecond means brake pressure decreases slightly
    // faster than it ramps up, giving a more natural release feel.
    constexpr float kMaxBrakeReleasePerSecond = kMaxBrakeChangePerSecond * 1.5f;

    // If groundspeed is unavailable for some reason, fall back to gating
    // differential braking on throttle position instead: below this
    // fraction (0.0 - 1.0) of throttle, we assume the aircraft is taxiing
    // rather than attempting to take off or fly.
    constexpr float kMaxThrottleForBraking = 0.5f;

    // Whether to log differential braking force trace messages while differential
    // braking is actively being applied. Defaulted to off and used only for debugging.
    constexpr bool kEnableDiffBrakingLog = false;

    // Minimum interval (in seconds) between differential braking debug log entries
    // when diff braking logging is enabled (once every quarter second).
    constexpr float kDiffBrakeLogInterval = 0.25f;

    // How often we run our flight loop callback. Doing this once per frame
    // is cheap (a handful of dataref reads and, at most, two writes), so we
    // do not need to throttle it further; X-Plane recommends against doing
    // expensive work in a flight loop, and this callback does none.
    constexpr float kFlightLoopInterval = -1.0f; // negative == every frame

    // --- Dataref handles, resolved lazily in XPluginEnable. ---
    XPLMDataRef gSteerDeg1DataRef = nullptr;
    XPLMDataRef gSteerDeg2DataRef = nullptr;
    XPLMDataRef gGearOnGroundDataRef = nullptr;
    XPLMDataRef gGroundSpeedDataRef = nullptr;
    XPLMDataRef gThrottleDataRef = nullptr;
    XPLMDataRef gYawInputDataRef = nullptr;
    XPLMDataRef gLeftBrakeAddDataRef = nullptr;
    XPLMDataRef gRightBrakeAddDataRef = nullptr;
    XPLMDataRef gRudderRatioDataRef = nullptr;
    XPLMDataRef gOverrideGearBrakeDataRef = nullptr;
    // Read-only; used purely for debug logging so we can tell whether
    // X-Plane's native auto-toe-brake logic is still contributing braking
    // on top of what we command via the "add" datarefs.
    XPLMDataRef gLeftBrakeRatioDataRef = nullptr;
    XPLMDataRef gRightBrakeRatioDataRef = nullptr;

    // --- State tracked across flight loop callbacks so that we only log
    // transitions, not every single frame. ---
    bool gIsCasteringAircraft = false;
    bool gHasLoggedAircraftDetected = false;
    bool gIsDifferentialBrakingActive = false;
    bool gHasLoggedGroundState = false; // whether the last ground/air state was logged
    bool gLastLoggedOnGround = false;
    float gTimeSinceLastDiffBrakeLog = kDiffBrakeLogInterval;

    // Last applied brake balance, used to rate-limit how quickly the
    // brakes can change from frame to frame. This is a single signed value
    // (negative == left brake, positive == right brake) rather than two
    // independent left/right values so that reversing rudder direction
    // smoothly slides the brake balance through zero, instead of ramping
    // the old side down and the new side up at the same time. The latter
    // would apply both brakes simultaneously during the transition, which
    // adds up to more total stopping force than either side alone and is
    // what caused the aircraft to abruptly come to a stop before turning
    // the other way.
    float gLastAppliedBrakeBalance = 0.0f;

    void LogMessage(const std::string& message)
    {
        std::string line = kLogPrefix;
        line += message;
        line += '\n';
        XPLMDebugString(line.c_str());
    }

    // Resolves all the datarefs we need. Safe to call more than once.
    void FindDataRefsIfNeeded()
    {
        if (gSteerDeg1DataRef == nullptr)
        {
            gSteerDeg1DataRef = XPLMFindDataRef("sim/aircraft/gear/acf_nw_steerdeg1");
        }
        if (gSteerDeg2DataRef == nullptr)
        {
            gSteerDeg2DataRef = XPLMFindDataRef("sim/aircraft/gear/acf_nw_steerdeg2");
        }
        if (gGearOnGroundDataRef == nullptr)
        {
            // "sim/flightmodel2/gear/on_ground_act" is a per-gear array, and
            // reading only index 0 is not reliable across aircraft (index 0
            // is not guaranteed to be the nose gear, and can briefly read
            // false while the aircraft/gear model finishes initializing).
            // "sim/flightmodel/failures/onground_any" is a single boolean
            // dataref that is true whenever any part of the aircraft is
            // touching the ground, which is what we actually want here.
            gGearOnGroundDataRef = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
        }
        if (gGroundSpeedDataRef == nullptr)
        {
            gGroundSpeedDataRef = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
        }
        if (gThrottleDataRef == nullptr)
        {
            // Used as a fallback when groundspeed is unavailable.
            gThrottleDataRef = XPLMFindDataRef("sim/cockpit2/engine/actuators/throttle_ratio_all");
        }
        if (gYawInputDataRef == nullptr)
        {
            gYawInputDataRef = XPLMFindDataRef("sim/joystick/yoke_heading_ratio");
        }
        if (gLeftBrakeAddDataRef == nullptr)
        {
            gLeftBrakeAddDataRef = XPLMFindDataRef("sim/flightmodel/controls/l_brake_add");
        }
        if (gRightBrakeAddDataRef == nullptr)
        {
            gRightBrakeAddDataRef = XPLMFindDataRef("sim/flightmodel/controls/r_brake_add");
        }
        if (gRudderRatioDataRef == nullptr)
        {
            // Written to zero while differential braking is active so that
            // rudder pedal input steers only via the brakes, not also via
            // the aerodynamic rudder surface (which would otherwise cause
            // dual-steering).
            gRudderRatioDataRef = XPLMFindDataRef("sim/flightmodel/controls/rudder_ratio");
        }
        if (gOverrideGearBrakeDataRef == nullptr)
        {
            // Set to 1 for every aircraft while the plugin is enabled. X-Plane's
            // auto-toe-brake helper can apply abrupt braking at steering extremes;
            // bypassing it leaves castering differential braking to this plugin and
            // normal braking to the simulator.
            gOverrideGearBrakeDataRef = XPLMFindDataRef("sim/operation/override/override_gearbrake");
        }
        if (gLeftBrakeRatioDataRef == nullptr)
        {
            gLeftBrakeRatioDataRef = XPLMFindDataRef("sim/cockpit2/controls/left_brake_ratio");
        }
        if (gRightBrakeRatioDataRef == nullptr)
        {
            gRightBrakeRatioDataRef = XPLMFindDataRef("sim/cockpit2/controls/right_brake_ratio");
        }
    }

    // Inspects an .acf aircraft file to determine if any landing gear is configured
    // with free-castering enabled (e.g. "_gear_castors 1").
    bool CheckAcfFileForCasteringGear(const char* acfPath)
    {
        if (acfPath == nullptr || acfPath[0] == '\0')
        {
            return false;
        }

        std::ifstream acfFile(acfPath);
        if (!acfFile.is_open())
        {
            return false;
        }

        std::string line;
        while (std::getline(acfFile, line))
        {
            // Match gear castor property, e.g.:
            // "P _gear/0/_gear_castors 1"
            if (line.rfind("P _gear/", 0) == 0 && line.find("_gear_castors") != std::string::npos)
            {
                const auto lastSpace = line.find_last_of(" \t");
                if (lastSpace != std::string::npos && lastSpace + 1 < line.size())
                {
                    if (line[lastSpace + 1] == '1')
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    // Determines whether the currently loaded user aircraft has a castering wheel
    // (no mechanical steering linkage), by inspecting the aircraft's gear definition
    // and nosewheel steering deflection limits.
    bool IsCasteringAircraftLoaded()
    {
        FindDataRefsIfNeeded();

        // 1. Check if the active .acf model file defines any gear as free-castoring.
        char fileName[256];
        char path[512];
        std::memset(fileName, 0, sizeof(fileName));
        std::memset(path, 0, sizeof(path));
        XPLMGetNthAircraftModel(0, fileName, path);

        if (CheckAcfFileForCasteringGear(path))
        {
            return true;
        }

        // If path was an object/relative file and did not directly open as an .acf file,
        // try looking up the .acf in the same directory using fileName if available.
        std::string pathStr(path);
        if (!pathStr.empty() && !pathStr.ends_with(".acf"))
        {
            const auto lastSlash = pathStr.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                std::string dir = pathStr.substr(0, lastSlash + 1);
                std::string altPath = dir + fileName;
                if (CheckAcfFileForCasteringGear(altPath.c_str()))
                {
                    return true;
                }
            }
        }

        // 2. Query the nosewheel steering deflection limits.
        // Aircraft with no mechanical steering linkage have 0 degrees deflection defined in Plane-Maker.
        if (gSteerDeg1DataRef != nullptr && gSteerDeg2DataRef != nullptr)
        {
            const float steer1 = XPLMGetDataf(gSteerDeg1DataRef);
            const float steer2 = XPLMGetDataf(gSteerDeg2DataRef);

            return std::abs(steer1) <= kMaxSteerDegForCastering &&
                   std::abs(steer2) <= kMaxSteerDegForCastering;
        }

        return false;
    }

    // Enables or disables X-Plane's native auto-toe-brake helper logic for
    // every aircraft while the plugin is enabled.
    //
    // NOTE: we deliberately do NOT also set
    // sim/operation/override/override_wheel_steer here. Per X-Plane's own
    // DataRefs.txt, that override only lets a plugin drive
    // sim/flightmodel2/gear/tire_steer_command_deg directly - it does NOT
    // make the nosewheel free-caster on its own. Since this plugin never
    // writes a steer command, enabling that override was actually freezing
    // the nosewheel at whatever angle it last had instead of letting it
    // free-caster, which caused binding/resistance (and the reported abrupt
    // "stop") exactly when reversing rudder direction. Leaving
    // override_wheel_steer alone lets X-Plane's normal free-castering
    // nosewheel physics run.
    void SetSimSteeringOverrides(bool overrideEnabled)
    {
        if (gOverrideGearBrakeDataRef != nullptr)
        {
            XPLMSetDatai(gOverrideGearBrakeDataRef, overrideEnabled ? 1 : 0);
        }
    }
    // Removes any additive brake force and resets state that carries between
    // flight loop callbacks.
    void ReleaseDifferentialBraking()
    {
        gIsDifferentialBrakingActive = false;
        gLastAppliedBrakeBalance = 0.0f;
        gTimeSinceLastDiffBrakeLog = kDiffBrakeLogInterval;

        if (gLeftBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gLeftBrakeAddDataRef, 0.0f);
        }
        if (gRightBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gRightBrakeAddDataRef, 0.0f);
        }
    }

    // Evaluates the aircraft steering type, logs changes, and updates plugin state and overrides.
    void UpdateAircraftSteeringType()
    {
        const bool wasCasteringAircraft = gIsCasteringAircraft;
        gIsCasteringAircraft = IsCasteringAircraftLoaded();

        if (gIsCasteringAircraft && (!wasCasteringAircraft || !gHasLoggedAircraftDetected))
        {
            LogMessage("Detected castering gear aircraft loaded; differential braking feature enabled.");
            gHasLoggedAircraftDetected = true;
        }
        else if (!gIsCasteringAircraft && wasCasteringAircraft)
        {
            LogMessage("Detected steerable gear aircraft loaded; differential braking feature disabled.");
            gHasLoggedAircraftDetected = false;
        }

        // Bypass X-Plane's auto-toe-brake helper on every aircraft because its
        // braking at steering extremes causes an abrupt jerk.
        SetSimSteeringOverrides(true);

        if (!gIsCasteringAircraft)
        {
            // Reset state so we don't leave brakes engaged if the user
            // switches to a different aircraft.
            gHasLoggedGroundState = false;
            ReleaseDifferentialBraking();
        }
    }

    // Reads whether any part of the aircraft is touching the ground.
    bool IsOnGround()
    {
        if (gGearOnGroundDataRef == nullptr)
        {
            return false;
        }

        return XPLMGetDatai(gGearOnGroundDataRef) != 0;
    }

    // Maps a raw one-sided rudder deflection magnitude (0.0 - 1.0) onto a
    // brake force (0.0 - kMaxBrakeForce), applying the deadzone and the
    // easing curve described above kYawInputDeadzone/kBrakeResponseCurveExponent.
    float MapYawMagnitudeToBrakeForce(float yawMagnitude)
    {
        if (yawMagnitude <= kYawInputDeadzone)
        {
            return 0.0f;
        }

        const float normalized =
            (yawMagnitude - kYawInputDeadzone) / (1.0f - kYawInputDeadzone);
        const float curved = std::pow(normalized, kBrakeResponseCurveExponent);
        return curved * kMaxBrakeForce;
    }

    // Moves `current` towards `target` by at most `maxDelta`, preventing the
    // brakes from jumping instantly and causing a jerky lock-up feel.
    float RateLimit(float current, float target, float maxDelta)
    {
        const float delta = std::clamp(target - current, -maxDelta, maxDelta);
        return current + delta;
    }

    // The main flight loop callback. Runs every frame but does only cheap
    // dataref reads/writes, so it should not cause any stutter.
    float FlightLoopCallback(float inElapsedSinceLastCall,
                              float /*inElapsedTimeSinceLastFlightLoop*/,
                              int /*inCounter*/,
                              void* /*inRefcon*/)
    {
        if (!gIsCasteringAircraft)
        {
            return kFlightLoopInterval;
        }

        const bool onGround = IsOnGround();

        // Prefer gating on groundspeed. If that dataref is unavailable for
        // some reason, fall back to gating on throttle position instead, so
        // we still avoid engaging differential braking during takeoff/flight.
        bool slowEnoughForBraking;
        if (gGroundSpeedDataRef != nullptr)
        {
            const float groundSpeedMs = XPLMGetDataf(gGroundSpeedDataRef);
            slowEnoughForBraking = groundSpeedMs <= kMaxGroundspeedForBrakingMs;
        }
        else if (gThrottleDataRef != nullptr)
        {
            const float throttle = XPLMGetDataf(gThrottleDataRef);
            slowEnoughForBraking = throttle <= kMaxThrottleForBraking;
        }
        else
        {
            slowEnoughForBraking = false;
        }

        const bool shouldUseDifferentialBraking = onGround && slowEnoughForBraking;

        // Log ground/air transitions once, as required by design.md.
        if (!gHasLoggedGroundState || onGround != gLastLoggedOnGround)
        {
            LogMessage(onGround ? "Aircraft is on the ground."
                                 : "Aircraft is in the air.");
            gHasLoggedGroundState = true;
            gLastLoggedOnGround = onGround;
        }

        // Log the switch into/out of differential braking mode once per
        // transition.
        if (shouldUseDifferentialBraking != gIsDifferentialBrakingActive)
        {
            gIsDifferentialBrakingActive = shouldUseDifferentialBraking;
            LogMessage(gIsDifferentialBrakingActive
                           ? "On ground and below braking speed threshold; "
                             "differential braking is now ACTIVE."
                           : "Differential braking is now INACTIVE.");
        }

        if (!gIsDifferentialBrakingActive)
        {
            // Make sure we are not leaving any stale added braking force
            // applied once we stop managing the brakes.
            ReleaseDifferentialBraking();
            return kFlightLoopInterval;
        }

        const float yawInput = gYawInputDataRef != nullptr
                                    ? XPLMGetDataf(gYawInputDataRef)
                                    : 0.0f;

        // yawInput is in the range [-1, 1]. Negative == left twist/pedal,
        // positive == right. Map the magnitude through the deadzone/curve
        // above onto a single signed brake balance (negative == left brake,
        // positive == right brake) so the two sides can never both be
        // non-zero at the same time.
        float targetBrakeBalance = 0.0f;
        if (yawInput < 0.0f)
        {
            targetBrakeBalance = -MapYawMagnitudeToBrakeForce(-yawInput);
        }
        else if (yawInput > 0.0f)
        {
            targetBrakeBalance = MapYawMagnitudeToBrakeForce(yawInput);
        }

        // Rate-limit how quickly the applied brake balance can change, so a
        // sudden/noisy input spike cannot cause the wheel to lock up
        // instantly; instead the brake ramps in smoothly. Because this is a
        // single signed value rather than two independent left/right
        // values, reversing rudder direction slides smoothly through zero
        // (briefly releasing both brakes) rather than ramping the old side
        // down while simultaneously ramping the new side up, which would
        // apply both brakes at once and cause an abrupt stop mid-turn.
        // When the balance is moving toward zero (brake pressure releasing),
        // use the faster release rate so brake pressure decreases slightly
        // quicker than it ramps up.
        const bool isReleasing = std::abs(targetBrakeBalance) < std::abs(gLastAppliedBrakeBalance);
        const float ratePerSecond = isReleasing ? kMaxBrakeReleasePerSecond : kMaxBrakeChangePerSecond;
        const float maxBrakeDelta = ratePerSecond * inElapsedSinceLastCall;
        const float brakeBalance = RateLimit(gLastAppliedBrakeBalance, targetBrakeBalance, maxBrakeDelta);
        gLastAppliedBrakeBalance = brakeBalance;

        const float leftBrake = brakeBalance < 0.0f ? -brakeBalance : 0.0f;
        const float rightBrake = brakeBalance > 0.0f ? brakeBalance : 0.0f;

        if (gLeftBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gLeftBrakeAddDataRef, leftBrake);
        }
        if (gRightBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gRightBrakeAddDataRef, rightBrake);
        }

        // Suppress the aerodynamic rudder surface while differential braking
        // is active so that rudder pedal input only steers the aircraft via
        // the brakes, not also via the rudder, preventing dual-steering.
        if (gRudderRatioDataRef != nullptr)
        {
            XPLMSetDataf(gRudderRatioDataRef, 0.0f);
        }

        // Debug-level trace of the forces applied, useful for tuning, and
        // including the actual net brake ratio X-Plane reports it is using
        // on each wheel. If those values are noticeably higher than what we
        // requested above, that is evidence the sim's native auto-toe-brake
        // logic is still contributing braking on top of ours.
        // Only log when differential braking is actually being applied and
        // throttled to once per quarter second (when debug logging is enabled).
        // ReSharper disable once CppIfCanBeReplacedByConstexprIf
        if (kEnableDiffBrakingLog && (leftBrake > 0.0f || rightBrake > 0.0f))
        {
            gTimeSinceLastDiffBrakeLog += inElapsedSinceLastCall;
            if (gTimeSinceLastDiffBrakeLog >= kDiffBrakeLogInterval)
            {
                gTimeSinceLastDiffBrakeLog = 0.0f;
                const float actualLeftBrakeRatio =
                    gLeftBrakeRatioDataRef != nullptr ? XPLMGetDataf(gLeftBrakeRatioDataRef) : -1.0f;
                const float actualRightBrakeRatio =
                    gRightBrakeRatioDataRef != nullptr ? XPLMGetDataf(gRightBrakeRatioDataRef) : -1.0f;
                char buffer[220];
                std::snprintf(buffer, sizeof(buffer),
                              "Applying differential braking: rudder=%.2f left_brake_add=%.2f right_brake_add=%.2f "
                              "actual_left_brake=%.2f actual_right_brake=%.2f",
                              yawInput, leftBrake, rightBrake, actualLeftBrakeRatio, actualRightBrakeRatio);
                LogMessage(buffer);
            }
        }
        else
        {
            gTimeSinceLastDiffBrakeLog = kDiffBrakeLogInterval;
        }

        return kFlightLoopInterval;
    }
} // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    std::snprintf(outName, 256, "Enhanced Differential Brake Steering %s",
                  DIFF_BRAKE_PLUGIN_VERSION);
    std::strcpy(outSig, "com.kyleross.diffbrakeplugin");
    std::strcpy(outDesc, "Provides smooth differential braking via rudder control.");

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, kFlightLoopInterval, nullptr);

    return 1;
}

PLUGIN_API void XPluginStop()
{
    SetSimSteeringOverrides(false);
    ReleaseDifferentialBraking();
    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
}

PLUGIN_API int XPluginEnable()
{
    FindDataRefsIfNeeded();

    // The aircraft is typically already loaded by the time our plugin is
    // enabled, so check right away rather than waiting for a
    // "plane loaded" message that may never arrive for the initial aircraft.
    UpdateAircraftSteeringType();

    return 1;
}

PLUGIN_API void XPluginDisable()
{
    // Return control to X-Plane and remove any additive braking applied
    // during the last flight loop.
    SetSimSteeringOverrides(false);
    ReleaseDifferentialBraking();
    gIsCasteringAircraft = false;
    gHasLoggedAircraftDetected = false;
    gHasLoggedGroundState = false;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*inFromWho*/, int inMessage, void* inParam)
{
    if (inMessage == XPLM_MSG_PLANE_LOADED)
    {
        // inParam contains the index number of the plane being loaded;
        // 0 (XPLM_USER_AIRCRAFT) indicates the user's plane.
        if (reinterpret_cast<intptr_t>(inParam) == XPLM_USER_AIRCRAFT)
        {
            UpdateAircraftSteeringType();
        }
    }
}
