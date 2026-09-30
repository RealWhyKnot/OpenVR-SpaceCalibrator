#pragma once

#include <imgui/imgui.h>

struct DashboardPointer
{
	bool valid = false;
	float x = 0.0f;
	float y = 0.0f;

	void Move(ImGuiIO& io, float newX, float newY)
	{
		valid = true;
		x = newX;
		y = newY;
		io.AddMousePosEvent(x, y);
	}

	void Reassert(ImGuiIO& io) const
	{
		if (valid) {
			io.AddMousePosEvent(x, y);
		}
	}
};
