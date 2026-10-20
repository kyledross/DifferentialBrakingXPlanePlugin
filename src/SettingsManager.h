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

#pragma once

#include <map>
#include <string>

struct AircraftSettings
{
    bool enableDifferentialBraking = true;
    bool enableBrakingEnhancements = true;

    bool operator==(const AircraftSettings& other) const
    {
        return enableDifferentialBraking == other.enableDifferentialBraking &&
               enableBrakingEnhancements == other.enableBrakingEnhancements;
    }
};

class SettingsManager
{
public:
    SettingsManager() = default;
    explicit SettingsManager(std::string settingsFilePath);

    void SetSettingsFilePath(const std::string& path);
    const std::string& GetSettingsFilePath() const;

    bool Load();
    bool LoadFromFile(const std::string& filePath);

    bool Save() const;
    bool SaveToFile(const std::string& filePath) const;

    bool HasAircraftSettings(const std::string& aircraftName) const;
    AircraftSettings GetAircraftSettings(const std::string& aircraftName) const;
    void SetAircraftSettings(const std::string& aircraftName, const AircraftSettings& settings);

    // If aircraft has not been flown before, initializes default settings (both true),
    // records it, saves to file, and returns the settings.
    // If it has been flown, returns the existing settings.
    AircraftSettings GetOrInitAircraftSettings(const std::string& aircraftName);

    const std::map<std::string, AircraftSettings>& GetAllSettings() const;
    void Clear();

    static std::string ResolveDefaultSettingsPath(const std::string& pluginFilePath);

private:
    std::string m_settingsFilePath;
    std::map<std::string, AircraftSettings> m_aircraftSettings;
};
