#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/Widgets.h"
#include "Version.h"

#include <imgui/imgui.h>

void DrawAboutPage()
{
	ui::TextHeading("Space Calibrator");
	ImGui::Text("Version v" SPACECAL_VERSION_STRING " (" SPACECAL_CHANNEL ")");
	ImGui::Spacing();
	ImGui::TextWrapped("Aligns multiple VR tracking systems so mixed hardware works together in one play space, with "
	                   "continuous drift correction and tracker smoothing.");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::TextDisabled("Space Calibrator contributors:");
	ImGui::TextDisabled("tach, pushrax, bd_, ArcticFox, hekky, pimaker");
	ImGui::Spacing();
	ImGui::TextDisabled("Licensed under the MIT license. See LICENSE and NOTICE in the install folder.");
}
