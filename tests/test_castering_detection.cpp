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
static std::vector<std::string> g_mockDebugLogs;
static std::map<XPLMDataRef, std::string> g_datarefToName;
static std::map<std::string, XPLMDataRef> g_nameToDataref;
static XPLMFlightLoop_f g_registeredFlightLoopCallback = nullptr;
static float g_registeredFlightLoopInterval = 0.0f;

extern "C" {

XPLMDataRef XPLMFindDataRef(const char* inDataRefName) {
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
    g_mockDebugLogs.clear();
}

int main() {
    printf("Starting Castering Detection Plugin Tests...\n");

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
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 0);
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

    // Run flight loop callback
    result = g_registeredFlightLoopCallback(1.0f, 1.0f, 2, nullptr);
    TEST_ASSERT(result == -1.0f);
    TEST_ASSERT(gIsDifferentialBrakingActive);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] > 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/rudder_ratio"] == 0.0f);
    // Verify that by default kEnableDiffBrakingLog is false and no "Applying differential braking" is logged
    TEST_ASSERT(!kEnableDiffBrakingLog);
    TEST_ASSERT(kDiffBrakeLogInterval == 0.25f);
    for (const auto& logMsg : g_mockDebugLogs) {
        TEST_ASSERT(logMsg.find("Applying differential braking") == std::string::npos);
    }
    printf("  [PASS] Castering aircraft differential braking active on taxi (no debug log flood)\n");

    // Test 3: Threshold tolerance <= 0.05 deg
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.04f;
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.05f;
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
    TEST_ASSERT(gIsCasteringAircraft);
    printf("  [PASS] Deflection within threshold <= 0.05 deg detected as castering\n");

    // Test 4: Exceeding threshold > 0.05 deg
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg1"] = 0.06f;
    g_mockFloatDataRefs["sim/aircraft/gear/acf_nw_steerdeg2"] = 0.00f;
    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, reinterpret_cast<void*>(0));
    TEST_ASSERT(!gIsCasteringAircraft);
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 0);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/l_brake_add"] == 0.0f);
    TEST_ASSERT(g_mockFloatDataRefs["sim/flightmodel/controls/r_brake_add"] == 0.0f);
    printf("  [PASS] Deflection exceeding threshold > 0.05 deg detected as steerable & cleans up state\n");

    // Test 5: Cirrus SF50 with _gear/0/_gear_castors 1 in .acf file and non-zero steerdeg (10/40)
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

    // Test 6: Cessna 172 with _gear/0/_gear_castors 0 in .acf file and steerdeg (10/10)
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
        TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 0);
        printf("  [PASS] Steerable Cessna 172 with _gear_castors 0 detected as steerable\n");
        std::remove(c172AcfPath);
    }

    // Test 7: Zero rudder on ground with castering aircraft (idle / straight taxi)
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

    // Test 8: Disable plugin restores overrides
    XPluginDisable();
    TEST_ASSERT(g_mockIntDataRefs["sim/operation/override/override_gearbrake"] == 0);
    printf("  [PASS] Plugin disable restores overrides\n");

    XPluginStop();
    TEST_ASSERT(g_registeredFlightLoopCallback == nullptr);
    printf("  [PASS] Plugin stop\n");

    printf("\nAll tests passed successfully!\n");
    return 0;
}
