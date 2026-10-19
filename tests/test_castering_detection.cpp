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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

// Include SDK headers needed for plugin types
#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

// Mock state
static std::map<std::string, float> g_mockFloatDataRefs;
static std::map<std::string, int> g_mockIntDataRefs;
static std::map<std::string, std::vector<float>> g_mockFloatArrayDataRefs;
static std::set<std::string> g_registeredDataRefs;
static std::vector<std::string> g_mockDebugLogs;
static std::map<XPLMDataRef, std::string> g_datarefToName;
static std::map<std::string, XPLMDataRef> g_nameToDataref;
static XPLMFlightLoop_f g_registeredFlightLoopCallback = nullptr;
static float g_registeredFlightLoopInterval = 0.0f;

struct MockCommand {
    std::string name;
    std::string description;
    XPLMCommandCallback_f handler = nullptr;
    int before = 0;
    void* refcon = nullptr;
};
static std::map<std::string, MockCommand> g_mockCommands;
static std::map<XPLMCommandRef, std::string> g_commandRefToName;

extern "C" {

XPLMDataRef XPLMFindDataRef(const char* inDataRefName) {
    if (g_mockFloatDataRefs.find(inDataRefName) == g_mockFloatDataRefs.end() &&
        g_mockIntDataRefs.find(inDataRefName) == g_mockIntDataRefs.end() &&
        g_mockFloatArrayDataRefs.find(inDataRefName) == g_mockFloatArrayDataRefs.end() &&
        g_registeredDataRefs.find(inDataRefName) == g_registeredDataRefs.end()) {
        return nullptr;
    }
    if (g_nameToDataref.find(inDataRefName) == g_nameToDataref.end()) {
        intptr_t newId = static_cast<intptr_t>(g_nameToDataref.size() + 1);
        XPLMDataRef handle = reinterpret_cast<XPLMDataRef>(newId);
        g_nameToDataref[inDataRefName] = handle;
        g_datarefToName[handle] = inDataRefName;
    }
    return g_nameToDataref[inDataRefName];
}

float XPLMGetDataf(XPLMDataRef inDataRef) {
    auto it = g_datarefToName.find(inDataRef);
    if (it != g_datarefToName.end()) {
        return g_mockFloatDataRefs[it->second];
    }
    return 0.0f;
}

void XPLMSetDataf(XPLMDataRef inDataRef, float inValue) {
    auto it = g_datarefToName.find(inDataRef);
    if (it != g_datarefToName.end()) {
        g_mockFloatDataRefs[it->second] = inValue;
    }
}

int XPLMGetDatai(XPLMDataRef inDataRef) {
    auto it = g_datarefToName.find(inDataRef);
    if (it != g_datarefToName.end()) {
        return g_mockIntDataRefs[it->second];
    }
    return 0;
}

void XPLMSetDatai(XPLMDataRef inDataRef, int inValue) {
    auto it = g_datarefToName.find(inDataRef);
    if (it != g_datarefToName.end()) {
        g_mockIntDataRefs[it->second] = inValue;
    }
}

int XPLMGetDatavf(XPLMDataRef inDataRef, float* outValues, int inOffset, int inMax) {
    auto it = g_datarefToName.find(inDataRef);
    if (it != g_datarefToName.end()) {
        const auto& vec = g_mockFloatArrayDataRefs[it->second];
        if (outValues == nullptr) {
            return static_cast<int>(vec.size());
        }
        int count = 0;
        for (int i = 0; i < inMax && (inOffset + i) < static_cast<int>(vec.size()); ++i) {
            outValues[i] = vec[inOffset + i];
            ++count;
        }
        return count;
    }
    return 0;
}

void XPLMSetDatavf(XPLMDataRef inDataRef, float* inValues, int inOffset, int inCount) {
    auto it = g_datarefToName.find(inDataRef);
    if (it != g_datarefToName.end()) {
        auto& vec = g_mockFloatArrayDataRefs[it->second];
        if (static_cast<int>(vec.size()) < inOffset + inCount) {
            vec.resize(inOffset + inCount, 0.0f);
        }
        for (int i = 0; i < inCount; ++i) {
            vec[inOffset + i] = inValues[i];
        }
    }
}

XPLMCommandRef XPLMCreateCommand(const char* inName, const char* inDescription) {
    if (g_mockCommands.find(inName) == g_mockCommands.end()) {
        MockCommand cmd;
        cmd.name = inName;
        cmd.description = inDescription ? inDescription : "";
        g_mockCommands[inName] = cmd;
    }
    intptr_t id = static_cast<intptr_t>(std::hash<std::string>{}(inName));
    XPLMCommandRef handle = reinterpret_cast<XPLMCommandRef>(id);
    g_commandRefToName[handle] = inName;
    return handle;
}

void XPLMRegisterCommandHandler(XPLMCommandRef inCommand, XPLMCommandCallback_f inHandler, int inBefore, void* inRefcon) {
    auto it = g_commandRefToName.find(inCommand);
    if (it != g_commandRefToName.end()) {
        g_mockCommands[it->second].handler = inHandler;
        g_mockCommands[it->second].before = inBefore;
        g_mockCommands[it->second].refcon = inRefcon;
    }
}

void XPLMUnregisterCommandHandler(XPLMCommandRef inCommand, XPLMCommandCallback_f inHandler, int /*inBefore*/, void* /*inRefcon*/) {
    auto it = g_commandRefToName.find(inCommand);
    if (it != g_commandRefToName.end()) {
        if (g_mockCommands[it->second].handler == inHandler) {
            g_mockCommands[it->second].handler = nullptr;
        }
    }
}

void XPLMDebugString(const char* inString) {
    g_mockDebugLogs.push_back(inString);
}

void XPLMRegisterFlightLoopCallback(XPLMFlightLoop_f inFlightLoop, float inInterval, void* inRefcon) {
    g_registeredFlightLoopCallback = inFlightLoop;
    g_registeredFlightLoopInterval = inInterval;
}

void XPLMUnregisterFlightLoopCallback(XPLMFlightLoop_f inFlightLoop, void* inRefcon) {
    if (g_registeredFlightLoopCallback == inFlightLoop) {
        g_registeredFlightLoopCallback = nullptr;
    }
}

static std::string g_mockAircraftFileName = "Generic.acf";
static std::string g_mockAircraftPath = "/path/to/Generic.acf";

void XPLMGetNthAircraftModel(int inIndex, char* outFileName, char* outPath) {
    if (outFileName) std::strcpy(outFileName, g_mockAircraftFileName.c_str());
    if (outPath) std::strcpy(outPath, g_mockAircraftPath.c_str());
}

} // extern "C"

// Include the source directly so all internal state is accessible
#include "../src/DiffBrakePlugin.cpp"

void ResetMockValues() {
    g_mockFloatDataRefs.clear();
    g_mockIntDataRefs.clear();
    g_mockFloatArrayDataRefs.clear();
    g_mockDebugLogs.clear();
    gTireRotSpeedDataRef = nullptr;
    gGearRadiiDataRef = nullptr;
    gAbsReleaseTimerLeft = 0.0f;
    gAbsReleaseTimerRight = 0.0f;
    gTargetProgressiveBrake = 0.0f;
    gBrakeHoldDuration = 0.0f;
    gCurrentProgressiveBrake = 0.0f;
}

int main() {
    printf("Starting Castering Detection Plugin Tests...\n");

    g_registeredDataRefs.insert("sim/flightmodel/controls/l_brake_add");
    g_registeredDataRefs.insert("sim/flightmodel/controls/r_brake_add");
    g_registeredDataRefs.insert("sim/cockpit2/controls/left_brake_ratio");
    g_registeredDataRefs.insert("sim/cockpit2/controls/right_brake_ratio");
    g_registeredDataRefs.insert("sim/operation/override/override_gearbrake");
    g_registeredDataRefs.insert("sim/flightmodel/controls/rudder_ratio");
    g_registeredDataRefs.insert("sim/flightmodel/failures/onground_any");
    g_registeredDataRefs.insert("sim/flightmodel/position/groundspeed");
    g_registeredDataRefs.insert("sim/flightmodel/engine/ENGN_thro");
    g_registeredDataRefs.insert("sim/joystick/yoke_heading_ratio");
    g_registeredDataRefs.insert("sim/aircraft/gear/acf_nw_steerdeg1");
    g_registeredDataRefs.insert("sim/aircraft/gear/acf_nw_steerdeg2");

    char name[256], sig[256], desc[256];
    int startResult = XPluginStart(name, sig, desc);
    TEST_ASSERT(startResult == 1);
    TEST_ASSERT(g_registeredFlightLoopCallback != nullptr);
    printf("  [PASS] XPluginStart\n");

    // Test 1: Start with steerable aircraft (e.g. Cessna 172 with 20 deg steer angle)
    ResetMockValues();
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 20.0f;
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 10.0f;

    int enableResult = XPluginEnable();
    TEST_ASSERT(enableResult == 1);
    TEST_ASSERT(!gIsCasteringAircraft);
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 1);
    printf("  [PASS] Steerable aircraft detection on plugin enable\n");

    // Flight loop should be inert for steerable aircraft
    g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
    g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 5.0f; // 5 m/s (~10 kts)
    g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 1.0f;
    g_mockFloatDataRefs["sim/flightmodel/controls/rudder_ratio"] = 1.0f;

    float result = g_registeredFlightLoopCallback(0.02f, 0.02f, 1, nullptr);
    TEST_ASSERT(result == -1.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/rudder_ratio"] == 1.0f);
    printf("  [PASS] Steerable aircraft flight loop inert\n");

    // Test 2: Load castering aircraft (e.g. Cirrus SR22 / Lancair Evolution, steerdeg = 0.0f)
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.0f;
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.0f;
    
    // Non-user aircraft load message should NOT trigger change (e.g. AI aircraft index 1)
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(1));
    TEST_ASSERT(!gIsCasteringAircraft);
    printf("  [PASS] AI aircraft load message ignored\n");

    // User aircraft load message (inParam == 0)
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
    TEST_ASSERT(gIsCasteringAircraft);
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 1);
    printf("  [PASS] User aircraft load message detects castering gear\n");

    // Flight loop should now engage differential braking
    g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
    g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 3.0f; // slow speed on ground
    g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 1.0f; // full right rudder

    // The full-rudder target must apply immediately, even for a short frame.
    result = g_registeredFlightLoopCallback(0.02f, 0.02f, 2, nullptr);
    TEST_ASSERT(result == -1.0f);
    TEST_ASSERT(gIsDifferentialBrakingActive);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == kMaxBrakeForce);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/rudder_ratio"] == 0.0f);
    // Verify that by default kEnableDiffBrakingLog is false and no "Applying differential braking" is logged
    TEST_ASSERT(!kEnableDiffBrakingLog);
    TEST_ASSERT(kDiffBrakeLogInterval == 0.25f);
    for (const auto& logMsg : g_mockDebugLogs) {
        TEST_ASSERT(logMsg.find("Applying differential braking") == std::string::npos);
    }
    printf("  [PASS] Castering aircraft differential braking active on taxi (no debug log flood)\n");

    // Test 3: Rudder input maps linearly to brake force without a ramp.
    g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 0.525f;
    result = g_registeredFlightLoopCallback(0.02f, 0.02f, 3, nullptr);
    const float expectedHalfBrake = 0.5f * kMaxBrakeForce;
    TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] -
                         expectedHalfBrake) < 0.000001f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
    printf("  [PASS] Rudder input applies immediate linear brake force\n");

    // Test 4: Threshold tolerance <= 0.05 deg
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.04f;
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.05f;
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
    TEST_ASSERT(gIsCasteringAircraft);
    printf("  [PASS] Deflection within threshold <= 0.05 deg detected as castering\n");

    // Test 5: Exceeding threshold > 0.05 deg
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.06f;
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.00f;
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
    TEST_ASSERT(!gIsCasteringAircraft);
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 1);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);
    printf("  [PASS] Deflection exceeding threshold > 0.05 deg detected as steerable & cleans up state\n");

    // Test 6: Cirrus SF50 with _gear/0/_gear_castors 1 in .acf file and non-zero steerdeg (10/40)
    {
        const char* sf50AcfPath = "/tmp/test_CirrusSF50.acf";
        std::ofstream sf50File(sf50AcfPath);
        sf50File << "PROPERTIES\n";
        sf50File << "P _gear/0/_gear_castors 1\n";
        sf50File << "P _gear/0/_steerdeg_hispeed 10.0\n";
        sf50File << "P _gear/0/_steerdeg_lospeed 40.0\n";
        sf50File.close();

        g_mockAircraftFileName = "CirrusSF50.acf";
        g_mockAircraftPath = sf50AcfPath;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 10.0f;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 40.0f;

        XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
        TEST_ASSERT(gIsCasteringAircraft);
        TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 1);
        printf("  [PASS] Cirrus SF50 with _gear_castors 1 in .acf detected as castering\n");
        std::remove(sf50AcfPath);
    }

    // Test 7: Cessna 172 with _gear/0/_gear_castors 0 in .acf file and steerdeg (10/10)
    {
        const char* c172AcfPath = "/tmp/test_Cessna172.acf";
        std::ofstream c172File(c172AcfPath);
        c172File << "PROPERTIES\n";
        c172File << "P _gear/0/_gear_castors 0\n";
        c172File << "P _gear/0/_steerdeg_hispeed 10.0\n";
        c172File << "P _gear/0/_steerdeg_lospeed 10.0\n";
        c172File.close();

        g_mockAircraftFileName = "Cessna_172SP.acf";
        g_mockAircraftPath = c172AcfPath;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 10.0f;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 10.0f;

        XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
        TEST_ASSERT(!gIsCasteringAircraft);
        TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 1);
        printf("  [PASS] Steerable Cessna 172 with _gear_castors 0 detected as steerable\n");
        std::remove(c172AcfPath);
    }

    // Test 8: Zero rudder on ground with castering aircraft (idle / straight taxi)
    // Differential braking is active, but no brake force is applied and no differential braking logs emitted
    {
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.0f;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.0f;
        XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
        TEST_ASSERT(gIsCasteringAircraft);

        g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
        g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 3.0f;
        g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 0.0f; // rudder centered (no steering)
        g_mockDebugLogs.clear();

        // Run multiple flight loop frames
        for (int frame = 0; frame < 20; ++frame) {
            result = g_registeredFlightLoopCallback(0.02f, 0.02f, frame, nullptr);
            TEST_ASSERT(result == -1.0f);
        }
        TEST_ASSERT(gIsDifferentialBrakingActive);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);
        for (const auto& logMsg : g_mockDebugLogs) {
            TEST_ASSERT(logMsg.find("Applying differential braking") == std::string::npos);
        }
        printf("  [PASS] Zero rudder input on ground produces no brake force and no debug log flood\n");
    }

    // Test 9: Disable plugin releases active braking and restores overrides
    g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 1.0f;
    result = g_registeredFlightLoopCallback(1.0f, 1.0f, 21, nullptr);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] > 0.0f);
    XPluginDisable();
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 0);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);
    TEST_ASSERT(!gIsDifferentialBrakingActive);
    TEST_ASSERT(!gIsCasteringAircraft);
    printf("  [PASS] Plugin disable releases braking and restores overrides\n");

    // Test 10: Progressive Brake Command Handler
    {
        TEST_ASSERT(gProgressiveBrakeCmd != nullptr);
        TEST_ASSERT(g_mockCommands.find("diffbrakeplugin/braking/hold_brakes_progressive_abs") != g_mockCommands.end());
        auto& cmdInfo = g_mockCommands["diffbrakeplugin/braking/hold_brakes_progressive_abs"];
        TEST_ASSERT(cmdInfo.description == "Hold Brakes (Progressive with ABS)");
        TEST_ASSERT(cmdInfo.handler != nullptr);

        // Begin command -> target becomes 1.0f
        int ret = cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandBegin, cmdInfo.refcon);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(gTargetProgressiveBrake == 1.0f);

        // Continue command -> target remains 1.0f
        ret = cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandContinue, cmdInfo.refcon);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(gTargetProgressiveBrake == 1.0f);

        // End command -> target becomes 0.0f
        ret = cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandEnd, cmdInfo.refcon);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(gTargetProgressiveBrake == 0.0f);
        printf("  [PASS] Progressive brake custom command handler\n");
    }

    // Test 11: Two-Stage Progressive Brake Ramp Up, Plateau, Escalation, and Ramp Down
    {
        // Start plugin again
        XPluginEnable();
        ResetMockValues();
        g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
        g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 3.0f;
        g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 0.0f; // no diff braking

        // Press command
        auto& cmdInfo = g_mockCommands["diffbrakeplugin/braking/hold_brakes_progressive_abs"];
        cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandBegin, cmdInfo.refcon);
        TEST_ASSERT(gTargetProgressiveBrake == 1.0f);
        gCurrentProgressiveBrake = 0.0f;
        gBrakeHoldDuration = 0.0f;

        // Stage 1 initial ramp: 2.5s * 0.1 = 0.25
        g_registeredFlightLoopCallback(2.5f, 2.5f, 30, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.25f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.25f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.25f) < 0.0001f);

        // Stage 1 cap: another 2.5s (5.0s total): reached 0.50f cap
        g_registeredFlightLoopCallback(2.5f, 2.5f, 31, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.50f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.50f) < 0.0001f);

        // Stage 1 plateau: hold for another 4.0s (9.0s total): remains capped at 0.50f
        g_registeredFlightLoopCallback(4.0f, 4.0f, 32, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.50f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.50f) < 0.0001f);

        // Stage 2 transition: hold for another 1.0s (10.0s total -> escalates past 10.0s: 0.50 + 1.0 * 0.1 = 0.60f)
        g_registeredFlightLoopCallback(1.0f, 1.0f, 33, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.60f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.60f) < 0.0001f);

        // Stage 2 completion: hold for another 4.0s (14.0s total -> reaches 1.0f full braking)
        g_registeredFlightLoopCallback(4.0f, 4.0f, 34, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 1.0f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 1.0f) < 0.0001f);

        // Stage 2 plateau: hold at 100%
        g_registeredFlightLoopCallback(1.0f, 1.0f, 35, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 1.0f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 1.0f) < 0.0001f);

        // Release command: ramp down for 0.5s: 1.0 - (0.5 * 0.5) = 0.75f
        cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandEnd, cmdInfo.refcon);
        TEST_ASSERT(gTargetProgressiveBrake == 0.0f);
        TEST_ASSERT(gBrakeHoldDuration == 0.0f);
        g_registeredFlightLoopCallback(0.5f, 0.5f, 36, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.75f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.75f) < 0.0001f);

        // Ramp down for another 1.0s: 0.75 - (1.0 * 0.5) = 0.25f
        g_registeredFlightLoopCallback(1.0f, 1.0f, 37, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.25f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.25f) < 0.0001f);

        // Ramp down for another 1.0s: 0.25 - (1.0 * 0.5) = -0.25 -> clamped to 0.0f
        g_registeredFlightLoopCallback(1.0f, 1.0f, 38, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.0f) < 0.0001f);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);

        // Re-trigger command after release: must reset duration and cap at Stage 1 (0.50f) again
        cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandBegin, cmdInfo.refcon);
        TEST_ASSERT(gTargetProgressiveBrake == 1.0f);
        TEST_ASSERT(gBrakeHoldDuration == 0.0f);
        // Ramp up for 5.0s: reaches 0.50f and remains capped at 0.50f
        g_registeredFlightLoopCallback(5.0f, 5.0f, 39, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.50f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.50f) < 0.0001f);

        cmdInfo.handler(gProgressiveBrakeCmd, xplm_CommandEnd, cmdInfo.refcon);
        g_registeredFlightLoopCallback(1.0f, 1.0f, 40, nullptr);
        TEST_ASSERT(std::abs(gCurrentProgressiveBrake - 0.0f) < 0.0001f);
        printf("  [PASS] Two-stage progressive brake ramp up, plateau, escalation, and ramp down\n");
    }

    // Test 12: Progressive Braking on Steerable Aircraft & Merge with Diff Braking
    {
        // Load castering aircraft
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.0f;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.0f;
        XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
        TEST_ASSERT(gIsCasteringAircraft);

        g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
        g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 3.0f;
        g_mockFloatDataRefs["sim/joystick/yoke_heading_ratio"] = 1.0f; // max right rudder -> diff right = 0.085f

        gCurrentProgressiveBrake = 0.5f;
        gTargetProgressiveBrake = 0.5f;

        g_registeredFlightLoopCallback(0.01f, 0.01f, 40, nullptr);
        // combinedLeft = 0.0f + 0.5f = 0.5f
        // combinedRight = 0.085f + 0.5f = 0.585f
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.5f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - (0.085f + 0.5f)) < 0.0001f);

        // Saturation clamping to 1.0f
        gBrakeHoldDuration = 12.0f;
        gCurrentProgressiveBrake = 0.95f;
        gTargetProgressiveBrake = 0.95f;
        g_registeredFlightLoopCallback(0.01f, 0.01f, 41, nullptr);
        // combinedRight = std::min(1.0f, 0.085f + 0.95f) = 1.0f
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 1.0f) < 0.0001f);

        // Steerable aircraft: diff brake is 0, progressive applies directly
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 10.0f;
        g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 10.0f;
        g_mockFloatDataRefs["sim/flightmodel/controls/rudder_ratio"] = 0.8f;
        XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
        TEST_ASSERT(!gIsCasteringAircraft);

        gCurrentProgressiveBrake = 0.4f;
        gTargetProgressiveBrake = 0.4f;
        g_registeredFlightLoopCallback(0.01f, 0.01f, 42, nullptr);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.4f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.4f) < 0.0001f);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/rudder_ratio"] == 0.8f); // rudder preserved
        printf("  [PASS] Merged diff braking and progressive braking on castering & steerable aircraft\n");
    }

    // Test 13: Universal Anti-Skid (ABS) Logic
    {
        ResetMockValues();
        g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
        gBrakeHoldDuration = 12.0f;
        gCurrentProgressiveBrake = 0.8f;
        gTargetProgressiveBrake = 0.8f;

        // Gear radii (index 1: left = 0.3m, index 2: right = 0.3m)
        std::vector<float> radii = {0.0f, 0.3f, 0.3f, 0.0f};
        g_mockFloatArrayDataRefs["sim/aircraft/parts/acf_gear_radi"] = radii;

        // Scenario A: Groundspeed <= kMinAbsGroundspeedMs (e.g. 4.0 m/s <= 5.0 m/s) -> ABS disabled
        g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 4.0f;
        // Even if wheels are locked (rot speed = 0)
        std::vector<float> lockedTires = {0.0f, 0.0f, 0.0f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel/parts/tire_rot_speed_rad_sec"] = lockedTires;

        g_registeredFlightLoopCallback(0.02f, 0.02f, 50, nullptr);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.8f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.8f) < 0.0001f);
        TEST_ASSERT(gAbsReleaseTimerLeft == 0.0f);
        TEST_ASSERT(gAbsReleaseTimerRight == 0.0f);

        // Scenario B: Groundspeed > 5.0 m/s (e.g. 10.0 m/s). Left wheel skidding, right wheel rolling freely.
        g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 10.0f;
        // Left wheel locked (0.0 rad/s -> wheel_speed_l = 0.0 < 4.0 m/s)
        // Right wheel rolling (33.333 rad/s -> wheel_speed_r = 10.0 >= 4.0 m/s)
        std::vector<float> tiresOneSkid = {0.0f, 0.0f, 33.3333f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel/parts/tire_rot_speed_rad_sec"] = tiresOneSkid;

        // Frame 1: Skid detected on left wheel -> pulse timer set to 0.1s, brake pulsed to 0.0f
        g_registeredFlightLoopCallback(0.02f, 0.02f, 51, nullptr);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.8f) < 0.0001f);
        TEST_ASSERT(std::abs(gAbsReleaseTimerLeft - 0.1f) < 0.0001f);
        TEST_ASSERT(gAbsReleaseTimerRight == 0.0f);

        // Frame 2: During pulse (dt = 0.04s) -> timer decrements to 0.06s, brake remains 0.0f
        g_registeredFlightLoopCallback(0.04f, 0.04f, 52, nullptr);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
        TEST_ASSERT(std::abs(gAbsReleaseTimerLeft - 0.06f) < 0.0001f);

        // Frame 3: Remaining pulse elapsed (dt = 0.06s) -> timer reaches 0.0s, brake remains 0.0f
        g_registeredFlightLoopCallback(0.06f, 0.06f, 53, nullptr);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
        TEST_ASSERT(std::abs(gAbsReleaseTimerLeft) < 0.0001f);

        // Frame 4: Wheel spin-up recovered (33.333 rad/s) -> normal braking restored on left wheel
        std::vector<float> tiresRecovered = {0.0f, 33.3333f, 33.3333f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel/parts/tire_rot_speed_rad_sec"] = tiresRecovered;
        g_registeredFlightLoopCallback(0.02f, 0.02f, 54, nullptr);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.8f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.8f) < 0.0001f);

        // Scenario C: Right wheel skid detected
        std::vector<float> tiresRightSkid = {0.0f, 33.3333f, 0.0f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel/parts/tire_rot_speed_rad_sec"] = tiresRightSkid;
        g_registeredFlightLoopCallback(0.02f, 0.02f, 55, nullptr);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.8f) < 0.0001f);
        TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);
        TEST_ASSERT(std::abs(gAbsReleaseTimerRight - 0.1f) < 0.0001f);
        printf("  [PASS] Universal ABS skid detection, pulse timer, and wheel recovery\n");
    }

    // Test 14: X-Plane 12 Datarefs & Missing Dataref Graceful Fallback
    {
        ResetMockValues();
        g_mockIntDataRefs["sim/flightmodel/failures/onground_any"] = 1;
        g_mockFloatDataRefs["sim/flightmodel/position/groundspeed"] = 10.0f;
        gBrakeHoldDuration = 12.0f;
        gCurrentProgressiveBrake = 0.7f;
        gTargetProgressiveBrake = 0.7f;

        // X-Plane 12 standard datarefs
        std::vector<float> xp12Radii = {0.0f, 0.35f, 0.35f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel2/gear/tire_radius_mtrs"] = xp12Radii;
        std::vector<float> xp12Tires = {0.0f, 30.0f, 30.0f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel2/gear/tire_rotation_speed_rad_sec"] = xp12Tires;

        // Force dataref resolution update
        gTireRotSpeedDataRef = nullptr;
        gGearRadiiDataRef = nullptr;
        FindDataRefsIfNeeded();
        TEST_ASSERT(gTireRotSpeedDataRef != nullptr);
        TEST_ASSERT(gGearRadiiDataRef != nullptr);

        g_registeredFlightLoopCallback(0.02f, 0.02f, 60, nullptr);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.7f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.7f) < 0.0001f);

        // When gear radii are 0, ABS must not falsely zero out braking force
        std::vector<float> zeroRadii = {0.0f, 0.0f, 0.0f, 0.0f};
        g_mockFloatArrayDataRefs["sim/flightmodel2/gear/tire_radius_mtrs"] = zeroRadii;
        g_registeredFlightLoopCallback(0.02f, 0.02f, 61, nullptr);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] - 0.7f) < 0.0001f);
        TEST_ASSERT(std::abs(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] - 0.7f) < 0.0001f);
        printf("  [PASS] X-Plane 12 datarefs and zero radius graceful fallback\n");
    }

    XPluginStop();
    TEST_ASSERT(g_registeredFlightLoopCallback == nullptr);
    TEST_ASSERT(g_mockCommands["diffbrakeplugin/braking/hold_brakes_progressive_abs"].handler == nullptr);
    printf("  [PASS] Plugin stop & unregister command handler\n");

    printf("\nAll tests passed successfully!\n");
    return 0;
}
