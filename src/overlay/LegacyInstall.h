#pragma once

#include <Windows.h>
#include <string>

std::string GetRegistryString(const HKEY hKeyGroup, const char* szRegistryKey, const char* szRegistryPropKey) noexcept;
void CheckGithubVersionInstalledOnSteam();
bool IsGithubVersionInstalled();
bool UninstallGithubSpaceCalibrator();
