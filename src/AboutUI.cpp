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
#include "LicenseData.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#if APL
    #include <unistd.h>
    #include <sys/wait.h>
    #include <OpenGL/gl.h>
#else
    #if IBM
        #include <windows.h>
        #include <shellapi.h>
    #else
        #include <unistd.h>
        #include <sys/wait.h>
    #endif
    #include <GL/gl.h>
#endif

#ifndef DIFF_BRAKE_PLUGIN_VERSION
    #define DIFF_BRAKE_PLUGIN_VERSION "development"
#endif

namespace AboutUI
{
    namespace
    {
        XPLMWindowID gWindow = nullptr;
        ImGuiContext* gImGuiContext = nullptr;

        bool gInitialized = false;
        bool gBackendInitialized = false;
        std::chrono::steady_clock::time_point gLastFrameTime;

        void OpenURL(const char* url)
        {
#if APL
            pid_t pid = fork();
            if (pid == 0)
            {
                if (fork() == 0)
                {
                    execlp("open", "open", url, static_cast<char*>(nullptr));
                    _exit(0);
                }
                _exit(0);
            }
            if (pid > 0)
            {
                waitpid(pid, nullptr, 0);
            }
#elif IBM
            ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
            pid_t pid = fork();
            if (pid == 0)
            {
                if (fork() == 0)
                {
                    execlp("xdg-open", "xdg-open", url, static_cast<char*>(nullptr));
                    _exit(0);
                }
                _exit(0);
            }
            if (pid > 0)
            {
                waitpid(pid, nullptr, 0);
            }
#endif
        }

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

            if (ImGui::Begin("##DiffBrakeAboutDialog", nullptr, windowFlags))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::Text("Enhanced Differential Brake Steering %s", DIFF_BRAKE_PLUGIN_VERSION);
                ImGui::PopStyleColor();

                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextUnformatted("Copyright 2026 Kyle D. Ross");
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                ImGui::TextWrapped("This software is not affiliated with, endorsed by, or supported by Laminar Research. "
                                   "All trademarks are the property of their respective owners, and are used herein for reference only.");
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextWrapped("Your support is very much appreciated and keeps development moving forward. "
                                   "If this software enhances your experience, please consider supporting the project:");
                ImGui::Spacing();

                if (ImGui::Button("Support / Donate (Buy Me a Coffee)"))
                {
                    OpenURL("https://buymeacoffee.com/kyledross");
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.65f, 1.0f, 1.0f));
                ImGui::TextUnformatted("https://buymeacoffee.com/kyledross");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
                if (ImGui::IsItemClicked())
                {
                    OpenURL("https://buymeacoffee.com/kyledross");
                }
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextUnformatted("License & Notices:");
                ImGui::Spacing();

                float buttonWidth = 60.0f;
                float buttonHeight = 24.0f;
                float availWidth = ImGui::GetContentRegionAvail().x;
                float availHeight = ImGui::GetContentRegionAvail().y;

                float childHeight = availHeight - buttonHeight - ImGui::GetStyle().ItemSpacing.y - 4.0f;
                if (childHeight < 80.0f)
                {
                    childHeight = 80.0f;
                }

                if (ImGui::BeginChild("LicenseRegion", ImVec2(0.0f, childHeight), true, ImGuiWindowFlags_HorizontalScrollbar))
                {
                    ImGui::TextUnformatted(kLicenseAndNoticeText);
                }
                ImGui::EndChild();

                ImGui::Spacing();

                availWidth = ImGui::GetContentRegionAvail().x;
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

        int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
        XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
        int screenW = screenRight - screenLeft;
        int screenH = screenTop - screenBottom;
        int width = 580;
        int height = 480;
        int left = screenLeft + (screenW > width ? (screenW - width) / 2 : 50);
        int top = screenBottom + (screenH > height ? (screenH + height) / 2 : (screenBottom + height + 50));
        int right = left + width;
        int bottom = top - height;

        XPLMCreateWindow_t winParams;
        winParams.structSize = sizeof(XPLMCreateWindow_t);
        winParams.left = left;
        winParams.top = top;
        winParams.right = right;
        winParams.bottom = bottom;
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
            XPLMSetWindowTitle(gWindow, "About Enhanced Differential Brake Steering");
            XPLMSetWindowResizingLimits(gWindow, 420, 320, 850, 700);
        }

        gInitialized = true;
    }

    void Cleanup()
    {
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

    XPLMWindowID GetWindow()
    {
        return gWindow;
    }
}
