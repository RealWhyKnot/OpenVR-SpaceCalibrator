#pragma once

struct GLFWwindow;

struct AppWindowState
{
	GLFWwindow* window = nullptr;
	unsigned int fbo = 0;
	unsigned int fboTexture = 0;
	int fboWidth = 0;
	int fboHeight = 0;
};

extern AppWindowState AppWindow;

bool InitGlfw();
void CreateGLFWWindow(bool startMinimized);
void DestroyGLFWResources();
void RequestImmediateRedraw();
bool ConsumeImmediateRedraw();
