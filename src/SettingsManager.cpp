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

#include "SettingsManager.h"
#include "json/json.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

using json = nlohmann::json;

SettingsManager::SettingsManager(std::string settingsFilePath)
    : m_settingsFilePath(std::move(settingsFilePath))
{
}

void SettingsManager::SetSettingsFilePath(const std::string& path)
{
    m_settingsFilePath = path;
}

const std::string& SettingsManager::GetSettingsFilePath() const
{
    return m_settingsFilePath;
}

bool SettingsManager::Load()
{
    if (m_settingsFilePath.empty())
    {
        return false;
    }
    return LoadFromFile(m_settingsFilePath);
}

bool SettingsManager::LoadFromFile(const std::string& filePath)
{
    if (filePath.empty())
    {
        return false;
    }

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return false;
    }

    try
    {
        json j;
        file >> j;

        if (!j.is_object())
        {
            return false;
        }

        m_aircraftSettings.clear();

        for (auto it = j.begin(); it != j.end(); ++it)
        {
            const std::string& acfName = it.key();
            const auto& val = it.value();

            AircraftSettings settings;
            if (val.is_object())
            {
                if (val.contains("enable_differential_braking"))
                {
                    settings.enableDifferentialBraking = val["enable_differential_braking"].get<bool>();
                }
                else if (val.contains("enableDifferentialBraking"))
                {
                    settings.enableDifferentialBraking = val["enableDifferentialBraking"].get<bool>();
                }
                else if (val.contains("differential_braking"))
                {
                    settings.enableDifferentialBraking = val["differential_braking"].get<bool>();
                }

                if (val.contains("enable_braking_enhancements"))
                {
                    settings.enableBrakingEnhancements = val["enable_braking_enhancements"].get<bool>();
                }
                else if (val.contains("enableBrakingEnhancements"))
                {
                    settings.enableBrakingEnhancements = val["enableBrakingEnhancements"].get<bool>();
                }
                else if (val.contains("braking_enhancements"))
                {
                    settings.enableBrakingEnhancements = val["braking_enhancements"].get<bool>();
                }
            }

            m_aircraftSettings[acfName] = settings;
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool SettingsManager::Save() const
{
    if (m_settingsFilePath.empty())
    {
        return false;
    }
    return SaveToFile(m_settingsFilePath);
}

bool SettingsManager::SaveToFile(const std::string& filePath) const
{
    if (filePath.empty())
    {
        return false;
    }

    try
    {
        std::filesystem::path p(filePath);
        if (p.has_parent_path())
        {
            std::filesystem::create_directories(p.parent_path());
        }

        json j = json::object();
        for (const auto& [acfName, settings] : m_aircraftSettings)
        {
            json acfObj;
            acfObj["enable_differential_braking"] = settings.enableDifferentialBraking;
            acfObj["enable_braking_enhancements"] = settings.enableBrakingEnhancements;
            j[acfName] = acfObj;
        }

        std::ofstream file(filePath);
        if (!file.is_open())
        {
            return false;
        }

        file << j.dump(4) << std::endl;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool SettingsManager::HasAircraftSettings(const std::string& aircraftName) const
{
    if (aircraftName.empty())
    {
        return false;
    }
    return m_aircraftSettings.find(aircraftName) != m_aircraftSettings.end();
}

AircraftSettings SettingsManager::GetAircraftSettings(const std::string& aircraftName) const
{
    auto it = m_aircraftSettings.find(aircraftName);
    if (it != m_aircraftSettings.end())
    {
        return it->second;
    }
    return AircraftSettings{};
}

void SettingsManager::SetAircraftSettings(const std::string& aircraftName, const AircraftSettings& settings)
{
    if (!aircraftName.empty())
    {
        m_aircraftSettings[aircraftName] = settings;
    }
}

AircraftSettings SettingsManager::GetOrInitAircraftSettings(const std::string& aircraftName)
{
    if (aircraftName.empty())
    {
        return AircraftSettings{};
    }

    auto it = m_aircraftSettings.find(aircraftName);
    if (it != m_aircraftSettings.end())
    {
        return it->second;
    }

    // New aircraft: default differential braking on and braking enhancements on
    AircraftSettings defaultSettings{true, true};
    m_aircraftSettings[aircraftName] = defaultSettings;
    Save();
    return defaultSettings;
}

const std::map<std::string, AircraftSettings>& SettingsManager::GetAllSettings() const
{
    return m_aircraftSettings;
}

void SettingsManager::Clear()
{
    m_aircraftSettings.clear();
}

std::string SettingsManager::ResolveDefaultSettingsPath(const std::string& pluginFilePath)
{
    if (pluginFilePath.empty())
    {
        return "settings.json";
    }

    try
    {
        std::filesystem::path pluginPath(pluginFilePath);
        std::filesystem::path dir = pluginPath.parent_path();

        // If plugin is inside a platform subfolder like "lin_x64", "mac_x64", or "win_x64",
        // check if settings.json exists in parent or target directory.
        std::string parentDirName = dir.filename().string();
        if (parentDirName == "lin_x64" || parentDirName == "mac_x64" || parentDirName == "win_x64")
        {
            std::filesystem::path parentSettings = dir.parent_path() / "settings.json";
            std::filesystem::path localSettings = dir / "settings.json";
            if (std::filesystem::exists(parentSettings))
            {
                return parentSettings.string();
            }
            if (std::filesystem::exists(localSettings))
            {
                return localSettings.string();
            }
            // By default, save alongside the plugin package in the plugin root folder
            return parentSettings.string();
        }

        return (dir / "settings.json").string();
    }
    catch (...)
    {
        return "settings.json";
    }
}
