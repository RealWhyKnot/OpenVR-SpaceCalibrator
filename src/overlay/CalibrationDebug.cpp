#include "stdafx.h"

#include <string>
#include <vector>

#include <implot/implot.h>
#include "CalibrationCalc.h"
#include "CalibrationMetrics.h"
#include "UserInterface.h"
// ImPlotPoint (*ImPlotGetter)(int idx, void* user_data);
namespace {
	double refTime;

	struct LegendEntry
	{
		std::string name;
		ImVec4 color;
	};
	std::vector<LegendEntry> legendEntries;

	void DrawLegendRow()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float square = ImGui::GetTextLineHeight() * 0.6f;
		const float avail = ImGui::GetContentRegionAvail().x;
		float lineWidth = 0.0f;
		for (size_t i = 0; i < legendEntries.size(); ++i) {
			const LegendEntry& entry = legendEntries[i];
			const float width = square + style.ItemInnerSpacing.x + ImGui::CalcTextSize(entry.name.c_str()).x;
			if (i > 0 && lineWidth + style.ItemSpacing.x + width <= avail) {
				ImGui::SameLine();
				lineWidth += style.ItemSpacing.x + width;
			}
			else {
				lineWidth = width;
			}
			ImGui::BeginGroup();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float top = pos.y + (ImGui::GetTextLineHeight() - square) * 0.5f;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + square, top + square),
			                                          ImGui::GetColorU32(entry.color));
			ImGui::Dummy(ImVec2(square, ImGui::GetTextLineHeight()));
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::TextUnformatted(entry.name.c_str());
			ImGui::EndGroup();
		}
	}

	template <typename F> ImPlotPoint VPIndexer(int idx, void* ptr)
	{
		auto point = (*reinterpret_cast<const F*>(ptr))(idx);
		point.x -= refTime;
		return point;
	}

	template <typename F> void PlotLineG(const char* name, const F& f, int points)
	{
		const void* vp_f = &f;

		if (points > 0) {
			ImPlot::PlotLineG(name, VPIndexer<F>, const_cast<void*>(vp_f), points);
		}
		else {
			double x = -INFINITY;
			double y = 0;
			ImPlot::PlotLine(name, &x, &y, 1);
		}
		legendEntries.push_back({name, ImPlot::GetLastItemColor()});
	}

	template <typename F, typename G> void PlotShadedG(const char* name, const F& data, const G& reference, int count)
	{
		const void* vp_data = &data;
		const void* vp_reference = &reference;

		if (count > 0) {
			ImPlot::PlotShadedG(name, VPIndexer<F>, const_cast<void*>(vp_data), VPIndexer<G>, const_cast<void*>(vp_reference), count);
		}
		else {
			double x = -INFINITY;
			double y = 0;
			ImPlot::PlotShaded(name, &x, &y, &y, 1);
		}
	}

	void PlotLineG(const char* name, const Metrics::TimeSeries<double>& ts)
	{
		PlotLineG(
		    name,
		    [&](int index) {
			    const auto& p = ts[index];
			    return ImPlotPoint(p.first, p.second);
		    },
		    ts.size());
	}

	void PlotVector(const char* namePrefix, const Metrics::TimeSeries<Eigen::Vector3d>& ts)
	{
		std::string name(namePrefix);
		name += "X";
		PlotLineG(
		    name.c_str(),
		    [&](int index) {
			    const auto& p = ts[index];
			    return ImPlotPoint(p.first, p.second(0));
		    },
		    ts.size());

		name.pop_back();
		name += "Y";
		PlotLineG(
		    name.c_str(),
		    [&](int index) {
			    const auto& p = ts[index];
			    return ImPlotPoint(p.first, p.second(1));
		    },
		    ts.size());

		name.pop_back();
		name += "Z";
		PlotLineG(
		    name.c_str(),
		    [&](int index) {
			    const auto& p = ts[index];
			    return ImPlotPoint(p.first, p.second(2));
		    },
		    ts.size());
	}

	double lastMouseX = -INFINITY;
	bool wasHovered;

	std::vector<double> calAppliedTimeBuffer, calByRelPoseTimeBuffer;

	void PrepApplyTicks()
	{
		calAppliedTimeBuffer.clear();
		calByRelPoseTimeBuffer.clear();

		for (auto t : Metrics::calibrationApplied.data()) {
			if (t.second) {
				calAppliedTimeBuffer.push_back(t.first - refTime);
			}
			else {
				calByRelPoseTimeBuffer.push_back(t.first - refTime);
			}
		}
	}

	void AddApplyTicks()
	{
		if (calAppliedTimeBuffer.empty()) {
			double x = -INFINITY;
			ImPlot::PlotInfLines("##CalibrationAppliedTime", &x, 1);
		}
		else {
			ImPlot::PlotInfLines("##CalibrationAppliedTime", &calAppliedTimeBuffer[0], (int)calAppliedTimeBuffer.size());
		}

		if (calByRelPoseTimeBuffer.empty()) {
			double x = -INFINITY;
			ImPlot::PlotInfLines("##CalibrationAppliedTimeByRelPose", &x, 1);
		}
		else {
			ImPlot::PlotInfLines("##CalibrationAppliedTimeByRelPose", &calByRelPoseTimeBuffer[0], (int)calByRelPoseTimeBuffer.size());
		}

		ImPlot::SetNextLineStyle(ImVec4(0.5, 0.5, 1, 1));
		ImPlot::PlotInfLines("##TagLine", &lastMouseX, 1);

		if (ImPlot::IsPlotHovered()) {
			auto mousePos = ImPlot::GetPlotMousePos();
			lastMouseX = mousePos.x;
			wasHovered = true;
		}
	}

	struct GraphInfo
	{
		const char* name;
		void (*callback)();
	};

	void SetupXAxis()
	{
		ImPlot::SetupAxisLimits(ImAxis_X1, -Metrics::TimeSpan, 0, ImGuiCond_Always);
	}

	void G_PosOffset_RawComputed()
	{
		if (ImPlot::BeginPlot("##posOffsetRawComputed", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "mm", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, -200, 200, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotVector("", Metrics::posOffset_rawComputed);

			ImPlot::EndPlot();
		}
		DrawLegendRow();
	}

	void G_PosOffset_CurrentCal()
	{
		if (ImPlot::BeginPlot("##posOffsetCurrentCal", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "mm", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, -200, 200, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotVector("", Metrics::posOffset_currentCal);

			ImPlot::EndPlot();
		}
		DrawLegendRow();
	}

	void G_PosOffset_LastSample()
	{
		if (ImPlot::BeginPlot("##posOffsetLastSample", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "mm", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, -200, 200, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotVector("", Metrics::posOffset_lastSample);

			ImPlot::EndPlot();
		}
		DrawLegendRow();
	}

	void G_PosOffset_ByRelPose()
	{
		if (ImPlot::BeginPlot("##posOffsetByRelPose", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "mm", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, -200, 200, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotVector("", Metrics::posOffset_byRelPose);

			ImPlot::EndPlot();
		}
		DrawLegendRow();
	}

	void G_PosOffset_PosError()
	{
		if (ImPlot::BeginPlot("##Position error", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "mm (RMS)");
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 25, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotLineG("Candidate", Metrics::error_rawComputed);
			PlotLineG("Active", Metrics::error_currentCal);
			PlotLineG("By rel pose", Metrics::error_byRelPose);
			PlotLineG("CC rel pose", Metrics::error_currentCalRelPose);
			ImPlot::EndPlot();
		}
		DrawLegendRow();
	}

	void G_ComputationTime()
	{
		if (ImPlot::BeginPlot("##Computation Time", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "ms", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 200, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotLineG("Time", Metrics::computationTime);
			ImPlot::EndPlot();
		}
	}

	void G_JitterReference()
	{
		if (ImPlot::BeginPlot("##JitterReference", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 10, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotLineG("Reference jitter", Metrics::jitterRef);
			ImPlot::EndPlot();
		}
	}

	void G_JitterTarget()
	{
		if (ImPlot::BeginPlot("##JitterTarget", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, "", 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 10, ImGuiCond_Appearing);

			AddApplyTicks();

			PlotLineG("Target jitter", Metrics::jitterTarget);
			ImPlot::EndPlot();
		}
	}

	void G_AxisVariance()
	{
		static bool firstrun = true;
		static ImPlotColormap axisVarianceColormap;
		if (firstrun) {
			firstrun = false;

			auto defaultFirst = ImPlot::GetColormapColor(0);

			ImVec4 colors[] = {
			    ImPlot::GetColormapColor(0), ImPlot::GetColormapColor(1), {1, 0, 0, 1}, {0, 1, 0, 1}, {0.5, 0.5, 0.5, 1},
			};

			axisVarianceColormap = ImPlot::AddColormap("AxisVarianceColormap", colors, sizeof(colors) / sizeof(colors[0]));
		}

		if (ImPlot::BeginPlot("##Axis variance", ImVec2(-1, 0), ImPlotFlags_NoLegend)) {
			ImPlot::SetupAxes(nullptr, nullptr, 0, 0);
			SetupXAxis();
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 0.003, ImGuiCond_Always);

			AddApplyTicks();

			ImPlot::PushColormap(axisVarianceColormap);
			ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.5f);
			ImPlot::SetNextLineStyle(ImVec4(1, 0, 0, 1));
			PlotShadedG(
			    "##VarianceLow",
			    [&](int index) {
				    auto p = Metrics::axisIndependence[index];
				    p.second = std::min(p.second, CalibrationCalc::AxisVarianceThreshold);
				    return ImPlotPoint(p.first, p.second);
			    },
			    [&](int index) {
				    auto p = Metrics::axisIndependence[index];
				    return ImPlotPoint(p.first, 0);
			    },
			    Metrics::axisIndependence.size());

			ImPlot::SetNextLineStyle(ImVec4(0, 1, 0, 1));

			PlotShadedG(
			    "##VarianceHigh",
			    [&](int index) {
				    auto p = Metrics::axisIndependence[index];
				    p.second = std::max(p.second, CalibrationCalc::AxisVarianceThreshold);
				    return ImPlotPoint(p.first, p.second);
			    },
			    [&](int index) {
				    auto p = Metrics::axisIndependence[index];
				    return ImPlotPoint(p.first, CalibrationCalc::AxisVarianceThreshold);
			    },
			    Metrics::axisIndependence.size());

			PlotLineG("Datapoint", Metrics::axisIndependence);

			ImPlot::PopStyleVar(1);
			ImPlot::PopColormap(1);

			ImPlot::EndPlot();
		}
	}

	const struct GraphInfo graphs[] = {{"Position error", G_PosOffset_PosError},
	                                   {"Axis variance", G_AxisVariance},
	                                   {"Offset: raw computed", G_PosOffset_RawComputed},
	                                   {"Offset: current calibration", G_PosOffset_CurrentCal},
	                                   {"Offset: last sample", G_PosOffset_LastSample},
	                                   {"Offset: by relative pose", G_PosOffset_ByRelPose},
	                                   {"Processing time", G_ComputationTime},
	                                   {"Reference jitter", G_JitterReference},
	                                   {"Target jitter", G_JitterTarget}};

	const int N_GRAPHS = sizeof(graphs) / sizeof(graphs[0]);
}

void PushCalibrationApplyTime()
{
	Metrics::calibrationApplied.Push(true);
}


void ShowCalibrationDebug(int rows, int cols)
{
	static std::vector<int> curIndexes;

	// ImGui::ShowDemoWindow();
	// ImPlot::ShowDemoWindow();

	double initMouseX = lastMouseX;
	wasHovered = false;

	for (int i = (int)curIndexes.size(); i < rows * cols; i++) {
		curIndexes.push_back(i % N_GRAPHS);
	}

	auto avail = ImGui::GetContentRegionAvail();

	auto bgCol = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);

	ImGui::PushStyleColor(ImGuiCol_TableRowBg, bgCol);
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, bgCol);
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));

	if (!ImGui::BeginChild("##CalibrationDebug", ImVec2(avail.x, 0.0f), ImGuiChildFlags_AutoResizeY,
	                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoTitleBar)) {
		ImGui::EndChild();
		ImPlot::PopStyleColor(1);
		ImGui::PopStyleColor(2);
		return;
	}

	if (!ImGui::BeginTable("##CalibrationDebug", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
		ImGui::EndChild();
		ImPlot::PopStyleColor(1);
		ImGui::PopStyleColor(2);
		return;
	}

	double t = refTime = Metrics::timestamp();
	PrepApplyTicks();

	for (int r = 0; r < rows; r++) {
		ImGui::TableNextRow();
		for (int c = 0; c < cols; c++) {
			int i = r * cols + c;
			ImGui::TableSetColumnIndex(c);

			ImGui::PushID(i);

			ImGui::SetNextItemWidth(ImGui::GetColumnWidth());
			if (ImGui::BeginCombo("", graphs[curIndexes[i]].name, 0)) {
				for (int j = 0; j < N_GRAPHS; j++) {
					bool isSelected = j == curIndexes[i];
					if (ImGui::Selectable(graphs[j].name, isSelected)) {
						curIndexes[i] = j;
					}

					if (isSelected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			legendEntries.clear();
			graphs[curIndexes[i]].callback();

			ImGui::PopID();
		}
	}

	ImGui::EndTable();
	ImGui::EndChild();

	ImPlot::PopStyleColor(1);
	ImGui::PopStyleColor(2);

	if (!wasHovered) {
		lastMouseX = -INFINITY;
	}
}