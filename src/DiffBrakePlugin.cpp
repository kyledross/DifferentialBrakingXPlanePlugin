/*
 * Copyright 2026 Kyle D. Ross
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "AboutUI.h"
#include "SettingsManager.h"
#include "SettingsUI.h"
#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    // Prefix for all log entries.
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

    // Progressive braking ramp rates (pressure units per second).
    constexpr float kProgressiveRampUpRate = 0.10f;
    constexpr float kProgressiveRampDownRate = 0.50f;

    // Two-stage progressive braking configuration:
    // Stage 1 caps force to allow smooth taxi deceleration and prevent excessive tire wear.
    constexpr float kStage1BrakeCap = 0.50f;
    constexpr float kStage1Duration = 10.0f;

    // Minimum groundspeed (m/s) required for Anti-Skid (ABS) activation.
    constexpr float kMinAbsGroundspeedMs = 5.0f;

    // Wheel slip ratio threshold below which a wheel is considered skidding.
    constexpr float kSkidSlipRatio = 0.4f;

    // Duration (in seconds) to release brake pressure when a skid is detected.
    constexpr float kAbsPulseTime = 0.1f;

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

    // Above the deadzone, brake force scales linearly with rudder deflection.
    constexpr float kBrakeResponseCurveExponent = 1.0f;

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
    XPLMDataRef gTireRotSpeedDataRef = nullptr;
    XPLMDataRef gGearRadiiDataRef = nullptr;
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

    // Progressive brake command target, hold duration, and filtered pressure state.
    float gTargetProgressiveBrake = 0.0f;
    float gBrakeHoldDuration = 0.0f;
    float gCurrentProgressiveBrake = 0.0f;

    // ABS pulse timers for left and right wheels.
    float gAbsReleaseTimerLeft = 0.0f;
    float gAbsReleaseTimerRight = 0.0f;

    // Custom command handle for progressive braking with ABS.
    XPLMCommandRef gProgressiveBrakeCmd = nullptr;

    // Handle to X-Plane's native "Hold brakes regular" command.
    XPLMCommandRef gHoldBrakesRegularCmd = nullptr;
    bool gIsHoldingRegularBrakes = false;

    // Settings state
    SettingsManager gSettingsManager;
    AircraftSettings gCurrentAircraftSettings;
    std::string gCurrentAircraftName;

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
            gGearOnGroundDataRef = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
        }
        if (gGroundSpeedDataRef == nullptr)
        {
            gGroundSpeedDataRef = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
        }
        if (gThrottleDataRef == nullptr)
        {
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
            gRudderRatioDataRef = XPLMFindDataRef("sim/flightmodel/controls/rudder_ratio");
        }
        if (gOverrideGearBrakeDataRef == nullptr)
        {
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
        if (gTireRotSpeedDataRef == nullptr)
        {
            gTireRotSpeedDataRef = XPLMFindDataRef("sim/flightmodel2/gear/tire_rotation_speed_rad_sec");
            if (gTireRotSpeedDataRef == nullptr)
            {
                gTireRotSpeedDataRef = XPLMFindDataRef("sim/flightmodel/parts/tire_rot_speed_rad_sec");
            }
        }
        if (gGearRadiiDataRef == nullptr)
        {
            gGearRadiiDataRef = XPLMFindDataRef("sim/flightmodel2/gear/tire_radius_mtrs");
            if (gGearRadiiDataRef == nullptr)
            {
                gGearRadiiDataRef = XPLMFindDataRef("sim/aircraft/parts/acf_gear_tirrad");
            }
            if (gGearRadiiDataRef == nullptr)
            {
                gGearRadiiDataRef = XPLMFindDataRef("sim/aircraft/parts/acf_gear_radi");
            }
        }
        if (gHoldBrakesRegularCmd == nullptr)
        {
            gHoldBrakesRegularCmd = XPLMFindCommand("sim/flight_controls/brakes_regular");
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
        if (gSteerDeg1DataRef != nullptr && gSteerDeg2DataRef != nullptr)
        {
            const float steer1 = XPLMGetDataf(gSteerDeg1DataRef);
            const float steer2 = XPLMGetDataf(gSteerDeg2DataRef);

            return std::abs(steer1) <= kMaxSteerDegForCastering &&
                   std::abs(steer2) <= kMaxSteerDegForCastering;
        }

        return false;
    }

    // Enables or disables X-Plane's native auto-toe-brake helper logic.
    void SetSimSteeringOverrides(bool overrideEnabled)
    {
        if (gOverrideGearBrakeDataRef != nullptr)
        {
            XPLMSetDatai(gOverrideGearBrakeDataRef, overrideEnabled ? 1 : 0);
        }
    }

    // Removes any additive brake force.
    void ReleaseDifferentialBraking()
    {
        gIsDifferentialBrakingActive = false;
        gTimeSinceLastDiffBrakeLog = kDiffBrakeLogInterval;
        gTargetProgressiveBrake = 0.0f;
        gBrakeHoldDuration = 0.0f;
        gCurrentProgressiveBrake = 0.0f;
        gAbsReleaseTimerLeft = 0.0f;
        gAbsReleaseTimerRight = 0.0f;

        if (gIsHoldingRegularBrakes)
        {
            if (gHoldBrakesRegularCmd != nullptr)
            {
                XPLMCommandEnd(gHoldBrakesRegularCmd);
            }
            gIsHoldingRegularBrakes = false;
        }

        if (gLeftBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gLeftBrakeAddDataRef, 0.0f);
        }
        if (gRightBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gRightBrakeAddDataRef, 0.0f);
        }
    }

    // Evaluates the aircraft steering type, inspects settings, logs changes, and updates plugin state.
    void UpdateAircraftSteeringType()
    {
        const bool wasCasteringAircraft = gIsCasteringAircraft;
        gIsCasteringAircraft = IsCasteringAircraftLoaded();

        // Get aircraft model name
        char fileName[256];
        char path[512];
        std::memset(fileName, 0, sizeof(fileName));
        std::memset(path, 0, sizeof(path));
        XPLMGetNthAircraftModel(0, fileName, path);

        std::string acfName = fileName;
        if (acfName.empty())
        {
            std::string pathStr(path);
            const auto lastSlash = pathStr.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                acfName = pathStr.substr(lastSlash + 1);
            }
        }
        if (acfName.empty())
        {
            acfName = "Unknown.acf";
        }

        gCurrentAircraftName = acfName;

        // Inspect settings: if not flown before, default both on and save. If flown, load them.
        gCurrentAircraftSettings = gSettingsManager.GetOrInitAircraftSettings(gCurrentAircraftName);
        SettingsUI::SetCurrentAircraft(gCurrentAircraftName, gCurrentAircraftSettings);

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

        // Apply steering overrides according to differential braking setting
        SetSimSteeringOverrides(gCurrentAircraftSettings.enableDifferentialBraking);

        if (!gIsCasteringAircraft || !gCurrentAircraftSettings.enableDifferentialBraking)
        {
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
    // brake force (0.0 - kMaxBrakeForce), applying the deadzone and linear
    // response curve described above.
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

    // Custom command handler for progressive braking with ABS.
    int ProgressiveBrakeCmdHandler(XPLMCommandRef /*inCommand*/,
                                   XPLMCommandPhase inPhase,
                                   void* /*inRefcon*/)
    {
        if (gCurrentAircraftSettings.enableBrakingEnhancements)
        {
            if (gIsHoldingRegularBrakes)
            {
                if (gHoldBrakesRegularCmd != nullptr)
                {
                    XPLMCommandEnd(gHoldBrakesRegularCmd);
                }
                gIsHoldingRegularBrakes = false;
            }

            if (inPhase == xplm_CommandBegin || inPhase == xplm_CommandContinue)
            {
                gTargetProgressiveBrake = 1.0f;
            }
            else if (inPhase == xplm_CommandEnd)
            {
                gTargetProgressiveBrake = 0.0f;
                gBrakeHoldDuration = 0.0f;
            }
        }
        else
        {
            gTargetProgressiveBrake = 0.0f;
            gBrakeHoldDuration = 0.0f;
            gCurrentProgressiveBrake = 0.0f;

            if (gHoldBrakesRegularCmd == nullptr)
            {
                gHoldBrakesRegularCmd = XPLMFindCommand("sim/flight_controls/brakes_regular");
            }

            if (gHoldBrakesRegularCmd != nullptr)
            {
                if (inPhase == xplm_CommandBegin)
                {
                    if (!gIsHoldingRegularBrakes)
                    {
                        XPLMCommandBegin(gHoldBrakesRegularCmd);
                        gIsHoldingRegularBrakes = true;
                    }
                }
                else if (inPhase == xplm_CommandEnd)
                {
                    if (gIsHoldingRegularBrakes)
                    {
                        XPLMCommandEnd(gHoldBrakesRegularCmd);
                        gIsHoldingRegularBrakes = false;
                    }
                }
            }
        }
        return 0;
    }

    // The main flight loop callback. Runs every frame.
    float FlightLoopCallback(float inElapsedSinceLastCall,
                             float /*inElapsedTimeSinceLastFlightLoop*/,
                             int /*inCounter*/,
                             void* /*inRefcon*/)
    {
        FindDataRefsIfNeeded();

        const bool onGround = IsOnGround();

        float groundSpeedMs = 0.0f;
        bool hasGroundSpeed = false;
        if (gGroundSpeedDataRef != nullptr)
        {
            groundSpeedMs = XPLMGetDataf(gGroundSpeedDataRef);
            hasGroundSpeed = true;
        }

        bool slowEnoughForBraking;
        if (hasGroundSpeed)
        {
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

        const bool shouldUseDifferentialBraking =
            gCurrentAircraftSettings.enableDifferentialBraking && onGround && slowEnoughForBraking;

        // Log ground/air transitions once, as required by design.md.
        if (!gHasLoggedGroundState || onGround != gLastLoggedOnGround)
        {
            LogMessage(onGround ? "Aircraft is on the ground."
                                 : "Aircraft is in the air.");
            gHasLoggedGroundState = true;
            gLastLoggedOnGround = onGround;
        }

        // Log the switch into/out of differential braking mode once per transition.
        if (shouldUseDifferentialBraking != gIsDifferentialBrakingActive)
        {
            gIsDifferentialBrakingActive = shouldUseDifferentialBraking;
            LogMessage(gIsDifferentialBrakingActive
                           ? "On ground and below braking speed threshold; "
                             "differential braking is now ACTIVE."
                           : "Differential braking is now INACTIVE.");
        }

        // Step A: Update Progressive Brake Base Pressure (Braking Enhancements)
        if (gCurrentAircraftSettings.enableBrakingEnhancements)
        {
            if (gTargetProgressiveBrake > 0.0f)
            {
                gBrakeHoldDuration += inElapsedSinceLastCall;
            }
            else
            {
                gBrakeHoldDuration = 0.0f;
            }

            float effectiveTarget = gTargetProgressiveBrake;
            if (effectiveTarget > 0.0f && gBrakeHoldDuration < kStage1Duration)
            {
                effectiveTarget = std::min(effectiveTarget, kStage1BrakeCap);
            }

            if (gCurrentProgressiveBrake < effectiveTarget)
            {
                gCurrentProgressiveBrake += kProgressiveRampUpRate * inElapsedSinceLastCall;
                if (gCurrentProgressiveBrake > effectiveTarget)
                {
                    gCurrentProgressiveBrake = effectiveTarget;
                }
                if (gCurrentProgressiveBrake > 1.0f)
                {
                    gCurrentProgressiveBrake = 1.0f;
                }
            }
            else if (gCurrentProgressiveBrake > effectiveTarget)
            {
                gCurrentProgressiveBrake -= kProgressiveRampDownRate * inElapsedSinceLastCall;
                if (gCurrentProgressiveBrake < effectiveTarget)
                {
                    gCurrentProgressiveBrake = effectiveTarget;
                }
                if (gCurrentProgressiveBrake < 0.0f)
                {
                    gCurrentProgressiveBrake = 0.0f;
                }
            }
        }
        else
        {
            gTargetProgressiveBrake = 0.0f;
            gBrakeHoldDuration = 0.0f;
            gCurrentProgressiveBrake = 0.0f;
        }

        // Step B: Calculate Differential Braking
        float diffBrakeLeft = 0.0f;
        float diffBrakeRight = 0.0f;
        if (gIsCasteringAircraft && gIsDifferentialBrakingActive && gCurrentAircraftSettings.enableDifferentialBraking)
        {
            const float yawInput = gYawInputDataRef != nullptr
                                        ? XPLMGetDataf(gYawInputDataRef)
                                        : 0.0f;

            float targetBrakeBalance = 0.0f;
            if (yawInput < 0.0f)
            {
                targetBrakeBalance = -MapYawMagnitudeToBrakeForce(-yawInput);
            }
            else if (yawInput > 0.0f)
            {
                targetBrakeBalance = MapYawMagnitudeToBrakeForce(yawInput);
            }

            diffBrakeLeft = targetBrakeBalance < 0.0f ? -targetBrakeBalance : 0.0f;
            diffBrakeRight = targetBrakeBalance > 0.0f ? targetBrakeBalance : 0.0f;

            if (gRudderRatioDataRef != nullptr)
            {
                XPLMSetDataf(gRudderRatioDataRef, 0.0f);
            }
        }

        // Step C: Merge Forces
        float combinedLeft = diffBrakeLeft + gCurrentProgressiveBrake;
        float combinedRight = diffBrakeRight + gCurrentProgressiveBrake;
        combinedLeft = std::min(combinedLeft, 1.0f);
        combinedRight = std::min(combinedRight, 1.0f);

        // Step D: Anti-Skid (ABS) Monitoring (Braking Enhancements)
        const float groundspeed = hasGroundSpeed ? groundSpeedMs : 0.0f;
        if (gCurrentAircraftSettings.enableBrakingEnhancements &&
            groundspeed > kMinAbsGroundspeedMs &&
            (combinedLeft > 0.0f || combinedRight > 0.0f) &&
            gTireRotSpeedDataRef != nullptr && gGearRadiiDataRef != nullptr)
        {
            float tireRotSpeed[8] = {0.0f};
            float gearRadii[8] = {0.0f};
            XPLMGetDatavf(gTireRotSpeedDataRef, tireRotSpeed, 0, 8);
            XPLMGetDatavf(gGearRadiiDataRef, gearRadii, 0, 8);

            const float wheel_radius_l = gearRadii[1];
            const float wheel_radius_r = gearRadii[2];

            if (wheel_radius_l > 0.0f && wheel_radius_r > 0.0f)
            {
                const float wheel_speed_l = wheel_radius_l * tireRotSpeed[1];
                const float wheel_speed_r = wheel_radius_r * tireRotSpeed[2];

                // Left Wheel
                if (gAbsReleaseTimerLeft > 0.0f)
                {
                    gAbsReleaseTimerLeft -= inElapsedSinceLastCall;
                    if (gAbsReleaseTimerLeft <= 1e-5f)
                    {
                        gAbsReleaseTimerLeft = 0.0f;
                    }
                    combinedLeft = 0.0f;
                }
                else if (wheel_speed_l < (groundspeed * kSkidSlipRatio))
                {
                    gAbsReleaseTimerLeft = kAbsPulseTime;
                    combinedLeft = 0.0f;
                }

                // Right Wheel
                if (gAbsReleaseTimerRight > 0.0f)
                {
                    gAbsReleaseTimerRight -= inElapsedSinceLastCall;
                    if (gAbsReleaseTimerRight <= 1e-5f)
                    {
                        gAbsReleaseTimerRight = 0.0f;
                    }
                    combinedRight = 0.0f;
                }
                else if (wheel_speed_r < (groundspeed * kSkidSlipRatio))
                {
                    gAbsReleaseTimerRight = kAbsPulseTime;
                    combinedRight = 0.0f;
                }
            }
            else
            {
                gAbsReleaseTimerLeft = 0.0f;
                gAbsReleaseTimerRight = 0.0f;
            }
        }
        else
        {
            gAbsReleaseTimerLeft = 0.0f;
            gAbsReleaseTimerRight = 0.0f;
        }

        // Step E: Apply to Datarefs
        if (gLeftBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gLeftBrakeAddDataRef, combinedLeft);
        }
        if (gRightBrakeAddDataRef != nullptr)
        {
            XPLMSetDataf(gRightBrakeAddDataRef, combinedRight);
        }

        if (kEnableDiffBrakingLog && (combinedLeft > 0.0f || combinedRight > 0.0f))
        {
            gTimeSinceLastDiffBrakeLog += inElapsedSinceLastCall;
            if (gTimeSinceLastDiffBrakeLog >= kDiffBrakeLogInterval)
            {
                gTimeSinceLastDiffBrakeLog = 0.0f;
                const float yawInput = gYawInputDataRef != nullptr ? XPLMGetDataf(gYawInputDataRef) : 0.0f;
                const float actualLeftBrakeRatio =
                    gLeftBrakeRatioDataRef != nullptr ? XPLMGetDataf(gLeftBrakeRatioDataRef) : -1.0f;
                const float actualRightBrakeRatio =
                    gRightBrakeRatioDataRef != nullptr ? XPLMGetDataf(gRightBrakeRatioDataRef) : -1.0f;
                char buffer[220];
                std::snprintf(buffer, sizeof(buffer),
                              "Applying differential braking: rudder=%.2f left_brake_add=%.2f right_brake_add=%.2f "
                              "actual_left_brake=%.2f actual_right_brake=%.2f",
                              yawInput, combinedLeft, combinedRight, actualLeftBrakeRatio, actualRightBrakeRatio);
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

    // Resolve plugin path and initialize settings
    char outFilePath[512] = {0};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, outFilePath, nullptr, nullptr);
    std::string settingsPath = SettingsManager::ResolveDefaultSettingsPath(outFilePath);
    gSettingsManager.SetSettingsFilePath(settingsPath);
    gSettingsManager.Load();

    // Initialize Dear ImGui UI & Menu
    SettingsUI::Initialize();
    AboutUI::Initialize();
    SettingsUI::SetOnSettingsChangedCallback([](const std::string& acfName, const AircraftSettings& settings) {
        gCurrentAircraftSettings = settings;
        gSettingsManager.SetAircraftSettings(acfName, settings);
        gSettingsManager.Save();

        SetSimSteeringOverrides(gCurrentAircraftSettings.enableDifferentialBraking);

        if (!gCurrentAircraftSettings.enableDifferentialBraking)
        {
            if (gLeftBrakeAddDataRef != nullptr) XPLMSetDataf(gLeftBrakeAddDataRef, 0.0f);
            if (gRightBrakeAddDataRef != nullptr) XPLMSetDataf(gRightBrakeAddDataRef, 0.0f);
        }

        if (gCurrentAircraftSettings.enableBrakingEnhancements)
        {
            if (gIsHoldingRegularBrakes)
            {
                if (gHoldBrakesRegularCmd != nullptr)
                {
                    XPLMCommandEnd(gHoldBrakesRegularCmd);
                }
                gIsHoldingRegularBrakes = false;
            }
        }
        else
        {
            gTargetProgressiveBrake = 0.0f;
            gBrakeHoldDuration = 0.0f;
            gCurrentProgressiveBrake = 0.0f;
            gAbsReleaseTimerLeft = 0.0f;
            gAbsReleaseTimerRight = 0.0f;
        }
    });

    gProgressiveBrakeCmd = XPLMCreateCommand("diffbrakeplugin/braking/hold_brakes_progressive_abs",
                                             "Hold Brakes (Progressive with ABS)");
    if (gProgressiveBrakeCmd != nullptr)
    {
        XPLMRegisterCommandHandler(gProgressiveBrakeCmd, ProgressiveBrakeCmdHandler, 1, nullptr);
    }

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, kFlightLoopInterval, nullptr);

    return 1;
}

PLUGIN_API void XPluginStop()
{
    AboutUI::Cleanup();
    SettingsUI::Cleanup();

    if (gProgressiveBrakeCmd != nullptr)
    {
        XPLMUnregisterCommandHandler(gProgressiveBrakeCmd, ProgressiveBrakeCmdHandler, 1, nullptr);
        gProgressiveBrakeCmd = nullptr;
    }

    SetSimSteeringOverrides(false);
    ReleaseDifferentialBraking();
    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
}

PLUGIN_API int XPluginEnable()
{
    FindDataRefsIfNeeded();
    UpdateAircraftSteeringType();
    return 1;
}

PLUGIN_API void XPluginDisable()
{
    AboutUI::Hide();
    SettingsUI::Hide();
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
        if (reinterpret_cast<intptr_t>(inParam) == XPLM_USER_AIRCRAFT)
        {
            UpdateAircraftSteeringType();
        }
    }
}
