#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/IconGlyphs.h"
#include "ui/Widgets.h"

#include <imgui/imgui.h>

namespace {

	enum class LearnArticle
	{
		Home,
		Standard,
		Continuous,
		Smoothing,
	};

	LearnArticle currentArticle = LearnArticle::Home;

	void Bullet(const char* text)
	{
		ImGui::Bullet();
		ImGui::SameLine();
		ImGui::TextWrapped("%s", text);
	}

	void DrawHome()
	{
		ui::TextHeading("Learn");
		ImGui::TextWrapped("Short guides for getting the most out of Space Calibrator.");
		ImGui::Spacing();

		const ImVec2 card(360.0f, 0.0f);
		if (ui::IconButton(ICON_MI_MY_LOCATION, "Standard calibration", card)) {
			currentArticle = LearnArticle::Standard;
		}
		if (ui::IconButton(ICON_MI_SYNC, "Continuous calibration", card)) {
			currentArticle = LearnArticle::Continuous;
		}
		if (ui::IconButton(ICON_MI_WAVES, "Tracker smoothing", card)) {
			currentArticle = LearnArticle::Smoothing;
		}
	}

	void DrawStandard()
	{
		ui::TextHeading("Standard calibration");
		ImGui::TextWrapped("Standard calibration lines up two tracking systems once, for example lighthouse trackers with a "
		                   "standalone headset. Your reference device is the one you calibrate to, usually a controller. The "
		                   "target device is the unaligned one, typically a tracker.");
		ImGui::Spacing();
		Bullet("Hold the reference and target devices together in one hand, as firmly as you can so they never move apart.");
		Bullet("Pick both devices on the Calibration page, then press Start Calibration.");
		Bullet("Walk around your space while drawing a figure eight with your hand until the progress bar fills.");
		ImGui::Spacing();
		ImGui::TextWrapped("Once the bar fills, your devices line up instantly. If they don't, run it again with slower speed: "
		                   "Slow and Very Slow take longer but usually land a more accurate result.");
		ImGui::Spacing();
		ImGui::TextWrapped("Tracking drifts over time. If you keep having to recalibrate, switch to continuous calibration.");
	}

	void DrawContinuous()
	{
		ui::TextHeading("Continuous calibration");
		ImGui::TextWrapped("Continuous calibration keeps the two tracking systems aligned the whole session. It needs one "
		                   "tracker mounted on your headset (or held rigidly against it) so drift can be measured and corrected "
		                   "live.");
		ImGui::Spacing();
		Bullet("Attach the tracker to your headset. A fixed mount works far better than tape.");
		Bullet("Select the headset as reference and the mounted tracker as target, then press Continuous Calibration.");
		Bullet("Leave it running. The calibration refines itself as you play, and it restarts with SteamVR.");
		ImGui::Spacing();
		ImGui::TextWrapped("Hide tracker keeps the mounted tracker out of games. If corrections feel too eager or too lazy, "
		                   "adjust the recalibration threshold on the Settings page.");
	}

	void DrawSmoothing()
	{
		ui::TextHeading("Tracker smoothing");
		ImGui::TextWrapped("Smoothing steadies the trackers in the calibrated space without creating any extra devices. The "
		                   "filter opens up during fast movement, so a higher setting calms standing jitter without making "
		                   "dancing feel laggy.");
		ImGui::Spacing();
		Bullet("One slider, 0 to 100%. 0 is off. 50% suits most setups.");
		Bullet("Controllers are left alone by default since their input is latency sensitive; opt them in if you want.");
		Bullet("Changes apply immediately and are saved with your profile.");
	}

}

void DrawLearnPage()
{
	if (currentArticle != LearnArticle::Home) {
		if (ui::IconButton(ICON_MI_ARROW_BACK, "Back")) {
			currentArticle = LearnArticle::Home;
			return;
		}
		ImGui::Spacing();
	}

	switch (currentArticle) {
		case LearnArticle::Home: DrawHome(); break;
		case LearnArticle::Standard: DrawStandard(); break;
		case LearnArticle::Continuous: DrawContinuous(); break;
		case LearnArticle::Smoothing: DrawSmoothing(); break;
	}
}
