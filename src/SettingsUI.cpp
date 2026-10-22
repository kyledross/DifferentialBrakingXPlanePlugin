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

#include "SettingsUI.h"
#include "AboutUI.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#if APL
    #include <OpenGL/gl.h>
#else
    #if IBM
        #include <windows.h>
    #endif
    #include <GL/gl.h>
#endif

namespace SettingsUI
{
    namespace
    {
        XPLMWindowID gWindow = nullptr;
        XPLMMenuID gMenu = nullptr;
        int gMenuItemIndex = -1;
        ImGuiContext* gImGuiContext = nullptr;

        std::string gCurrentAircraftName;
        AircraftSettings gCurrentSettings;
        OnSettingsChangedCallback gOnSettingsChanged;

        bool gInitialized = false;
        bool gBackendInitialized = false;
        std::chrono::steady_clock::time_point gLastFrameTime;

        void DrawWindowCallback(XPLMWindowID inWindowID, void* /*inRefcon*/)
        {
            if (XPLMGetWindowIsVisible(inWindowID) == 0)
            {
                return;
            }

            int left = 0, top = 0, right = 0, bottom = 0;
            XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
            int width = right - left;
            int height = top - bottom;

            if (width <= 0 || height <= 0)
            {
                return;
            }

            int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
            XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
            int screenWidth = screenRight - screenLeft;
            int screenHeight = screenTop - screenBottom;
            if (screenWidth <= 0 || screenHeight <= 0)
            {
                screenWidth = right;
                screenHeight = top;
            }

            GLint lastViewport[4] = {0};
            glGetIntegerv(GL_VIEWPORT, lastViewport);
            float scaleX = (screenWidth > 0 && lastViewport[2] > 0) ? (static_cast<float>(lastViewport[2]) / static_cast<float>(screenWidth)) : 1.0f;
            float scaleY = (screenHeight > 0 && lastViewport[3] > 0) ? (static_cast<float>(lastViewport[3]) / static_cast<float>(screenHeight)) : 1.0f;

            if (gImGuiContext != nullptr)
            {
                ImGui::SetCurrentContext(gImGuiContext);
            }

            if (!gBackendInitialized)
            {
#if APL
                const char* glsl_version = "#version 150";
#else
                const char* glsl_version = "#version 130";
#endif
                if (!ImGui_ImplOpenGL3_Init(glsl_version))
                {
                    return;
                }
                gBackendInitialized = true;
            }

            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - gLastFrameTime).count();
            gLastFrameTime = now;
            if (dt <= 0.0f || dt > 0.5f)
            {
                dt = 1.0f / 60.0f;
            }

            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(screenWidth), static_cast<float>(screenHeight));
            io.DisplayFramebufferScale = ImVec2(scaleX, scaleY);
            io.DeltaTime = dt;

            int mouseX = 0, mouseY = 0;
            XPLMGetMouseLocationGlobal(&mouseX, &mouseY);
            io.AddMousePosEvent(static_cast<float>(mouseX - screenLeft), static_cast<float>(screenTop - mouseY));

            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();

            float winX = static_cast<float>(left - screenLeft);
            float winY = static_cast<float>(screenTop - top);

            ImGui::SetNextWindowPos(ImVec2(winX, winY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)), ImGuiCond_Always);

            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                          ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoBackground |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus;

            if (ImGui::Begin("##DiffBrakeSettingsDialog", nullptr, windowFlags))
            {
                std::string aircraftLabel = "Aircraft: " + (gCurrentAircraftName.empty() ? std::string("(None)") : gCurrentAircraftName);
                ImGui::TextUnformatted(aircraftLabel.c_str());

                ImGui::Separator();
                ImGui::Spacing();

                bool changed = false;
                if (ImGui::Checkbox("Enable differential braking", &gCurrentSettings.enableDifferentialBraking))
                {
                    changed = true;
                }
                if (ImGui::Checkbox("Enable braking enhancements", &gCurrentSettings.enableBrakingEnhancements))
                {
                    changed = true;
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
                ImGui::TextWrapped("Reminder: Map your controller brake button to 'Hold Brakes (Progressive with ABS)' (diffbrakeplugin/braking/hold_brakes_progressive_abs).");
                ImGui::PopStyleColor();

                if (changed && gOnSettingsChanged)
                {
                    gOnSettingsChanged(gCurrentAircraftName, gCurrentSettings);
                }

                ImGui::Spacing();

                // OK button positioned in the lower-right corner
                float buttonWidth = 60.0f;
                float buttonHeight = 24.0f;
                float availWidth = ImGui::GetContentRegionAvail().x;
                float availHeight = ImGui::GetContentRegionAvail().y;

                if (availHeight > (buttonHeight + 4.0f))
                {
                    ImGui::Dummy(ImVec2(0.0f, availHeight - buttonHeight - 4.0f));
                }

                if (availWidth > buttonWidth)
                {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - buttonWidth);
                }

                if (ImGui::Button("OK", ImVec2(buttonWidth, buttonHeight)))
                {
                    Hide();
                }
            }
            ImGui::End();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        int HandleMouseClickCallback(XPLMWindowID /*inWindowID*/, int x, int y, XPLMMouseStatus inMouse, void* /*inRefcon*/)
        {
            if (gImGuiContext != nullptr)
            {
                ImGui::SetCurrentContext(gImGuiContext);
            }

            int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
            XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(static_cast<float>(x - screenLeft), static_cast<float>(screenTop - y));

            if (inMouse == xplm_MouseDown)
            {
                io.AddMouseButtonEvent(0, true);
            }
            else if (inMouse == xplm_MouseUp)
            {
                io.AddMouseButtonEvent(0, false);
            }

            return 1;
        }

        int HandleRightClickCallback(XPLMWindowID /*inWindowID*/, int x, int y, XPLMMouseStatus inMouse, void* /*inRefcon*/)
        {
            if (gImGuiContext != nullptr)
            {
                ImGui::SetCurrentContext(gImGuiContext);
            }

            int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
            XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(static_cast<float>(x - screenLeft), static_cast<float>(screenTop - y));

            if (inMouse == xplm_MouseDown)
            {
                io.AddMouseButtonEvent(1, true);
            }
            else if (inMouse == xplm_MouseUp)
            {
                io.AddMouseButtonEvent(1, false);
            }

            return 1;
        }

        XPLMCursorStatus HandleCursorCallback(XPLMWindowID /*inWindowID*/, int x, int y, void* /*inRefcon*/)
        {
            if (gImGuiContext != nullptr)
            {
                ImGui::SetCurrentContext(gImGuiContext);
            }

            int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
            XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(static_cast<float>(x - screenLeft), static_cast<float>(screenTop - y));
            return xplm_CursorDefault;
        }

        int HandleMouseWheelCallback(XPLMWindowID /*inWindowID*/, int x, int y, int /*wheel*/, int clicks, void* /*inRefcon*/)
        {
            if (gImGuiContext != nullptr)
            {
                ImGui::SetCurrentContext(gImGuiContext);
            }

            int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
            XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(static_cast<float>(x - screenLeft), static_cast<float>(screenTop - y));
            io.AddMouseWheelEvent(0.0f, static_cast<float>(clicks));
            return 1;
        }

        void HandleKeyCallback(XPLMWindowID /*inWindowID*/, char /*inKey*/, XPLMKeyFlags /*inFlags*/, char /*inVirtualKey*/, void* /*inRefcon*/, int /*losingFocus*/)
        {
        }

        void MenuHandler(void* /*inMenuRef*/, void* inItemRef)
        {
            intptr_t item = reinterpret_cast<intptr_t>(inItemRef);
            if (item == 1)
            {
                Toggle();
            }
            else if (item == 2)
            {
                AboutUI::Toggle();
            }
        }
    } // namespace

    void Initialize()
    {
        if (gInitialized)
        {
            return;
        }

        IMGUI_CHECKVERSION();
        gImGuiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(gImGuiContext);
        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;

        gLastFrameTime = std::chrono::steady_clock::now();

        // Create the XPLM Window
        XPLMCreateWindow_t winParams;
        winParams.structSize = sizeof(XPLMCreateWindow_t);
        winParams.left = 120;
        winParams.top = 580;
        winParams.right = 490;
        winParams.bottom = 390;
        winParams.visible = 0;
        winParams.drawWindowFunc = DrawWindowCallback;
        winParams.handleMouseClickFunc = HandleMouseClickCallback;
        winParams.handleKeyFunc = HandleKeyCallback;
        winParams.handleCursorFunc = HandleCursorCallback;
        winParams.handleMouseWheelFunc = HandleMouseWheelCallback;
        winParams.refcon = nullptr;
        winParams.layer = xplm_WindowLayerFloatingWindows;
        winParams.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
        winParams.handleRightClickFunc = HandleRightClickCallback;

        gWindow = XPLMCreateWindowEx(&winParams);
        if (gWindow != nullptr)
        {
            XPLMSetWindowTitle(gWindow, "Differential Braking Settings");
            XPLMSetWindowResizingLimits(gWindow, 320, 160, 650, 350);
        }

        CreateMenu();
        gInitialized = true;
    }

    void Cleanup()
    {
        DestroyMenu();

        if (gWindow != nullptr)
        {
            XPLMDestroyWindow(gWindow);
            gWindow = nullptr;
        }

        if (gImGuiContext != nullptr)
        {
            ImGui::SetCurrentContext(gImGuiContext);
            if (gBackendInitialized)
            {
                ImGui_ImplOpenGL3_Shutdown();
                gBackendInitialized = false;
            }
            ImGui::DestroyContext(gImGuiContext);
            gImGuiContext = nullptr;
        }

        gInitialized = false;
    }

    void CreateMenu()
    {
        if (gMenu != nullptr)
        {
            return;
        }

        XPLMMenuID pluginsMenu = XPLMFindPluginsMenu();
        if (pluginsMenu == nullptr)
        {
            return;
        }

        gMenuItemIndex = XPLMAppendMenuItem(pluginsMenu, "DiffBrakePlugin", nullptr, 1);
        if (gMenuItemIndex >= 0)
        {
            gMenu = XPLMCreateMenu("DiffBrakePlugin", pluginsMenu, gMenuItemIndex, MenuHandler, nullptr);
            if (gMenu != nullptr)
            {
                XPLMAppendMenuItem(gMenu, "Settings...", reinterpret_cast<void*>(1), 1);
                XPLMAppendMenuSeparator(gMenu);
                XPLMAppendMenuItem(gMenu, "About...", reinterpret_cast<void*>(2), 1);
            }
        }
    }

    void DestroyMenu()
    {
        if (gMenu != nullptr)
        {
            XPLMDestroyMenu(gMenu);
            gMenu = nullptr;
        }
        gMenuItemIndex = -1;
    }

    void Show()
    {
        if (gWindow != nullptr)
        {
            XPLMSetWindowIsVisible(gWindow, 1);
            XPLMBringWindowToFront(gWindow);
        }
    }

    void Hide()
    {
        if (gWindow != nullptr)
        {
            XPLMSetWindowIsVisible(gWindow, 0);
        }
    }

    void Toggle()
    {
        if (IsVisible())
        {
            Hide();
        }
        else
        {
            Show();
        }
    }

    bool IsVisible()
    {
        if (gWindow != nullptr)
        {
            return XPLMGetWindowIsVisible(gWindow) != 0;
        }
        return false;
    }

    void SetCurrentAircraft(const std::string& aircraftName, const AircraftSettings& settings)
    {
        gCurrentAircraftName = aircraftName;
        gCurrentSettings = settings;
    }

    void SetOnSettingsChangedCallback(OnSettingsChangedCallback callback)
    {
        gOnSettingsChanged = std::move(callback);
    }

    const OnSettingsChangedCallback& GetOnSettingsChangedCallback()
    {
        return gOnSettingsChanged;
    }

    XPLMWindowID GetWindow()
    {
        return gWindow;
    }
}
