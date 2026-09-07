#include "stdafx.h"
#include "AppWindow.h"
#include "Constants.h"
#include "ui/Style.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <implot/implot.h>
#include <GL/gl3w.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <stb_image.h>

#include <cstdio>
#include <stdexcept>
#include <string>

AppWindowState AppWindow;

static bool immediateRedraw;

void RequestImmediateRedraw()
{
	immediateRedraw = true;
}

bool ConsumeImmediateRedraw()
{
	if (!immediateRedraw) return false;
	immediateRedraw = false;
	return true;
}

static void GLFWErrorCallback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

#ifdef DEBUG_LOGS
static void openGLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
                                const void* userParam)
{
	fprintf(stderr, "OpenGL Debug %u: %.*s\n", id, length, message);
}
#endif

enum DWMA_USE_IMMSERSIVE_DARK_MODE_ENUM
{
	DWMA_USE_IMMERSIVE_DARK_MODE = 20,
	DWMA_USE_IMMERSIVE_DARK_MODE_PRE_20H1 = 19,
};

static bool EnableDarkModeTopBar(const HWND windowHwmd)
{
	const BOOL darkBorder = TRUE;
	const bool ok = SUCCEEDED(DwmSetWindowAttribute(windowHwmd, DWMA_USE_IMMERSIVE_DARK_MODE, &darkBorder, sizeof(darkBorder))) ||
	                SUCCEEDED(DwmSetWindowAttribute(windowHwmd, DWMA_USE_IMMERSIVE_DARK_MODE_PRE_20H1, &darkBorder, sizeof(darkBorder)));
	return ok;
}

bool InitGlfw()
{
	if (!glfwInit()) return false;
	glfwSetErrorCallback(GLFWErrorCallback);
	return true;
}

void CreateGLFWWindow(bool startMinimized)
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, false);

#ifdef DEBUG_LOGS
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

	AppWindow.fboWidth = 1200;
	AppWindow.fboHeight = 800;

	AppWindow.window = glfwCreateWindow(AppWindow.fboWidth, AppWindow.fboHeight, "Space Calibrator", nullptr, nullptr);
	if (!AppWindow.window) throw std::runtime_error("Failed to create window");

	glfwMakeContextCurrent(AppWindow.window);
	glfwSwapInterval(1);
	gl3wInit();

	if (startMinimized) glfwIconifyWindow(AppWindow.window);
	HWND windowHwmd = glfwGetWin32Window(AppWindow.window);
	EnableDarkModeTopBar(windowHwmd);

	GLFWimage images[1] = {};
	std::string iconPath = AppCwd();
	iconPath += "\\taskbar_icon.png";
	images[0].pixels = stbi_load(iconPath.c_str(), &images[0].width, &images[0].height, 0, 4);
	glfwSetWindowIcon(AppWindow.window, 1, images);
	stbi_image_free(images[0].pixels);

#ifdef DEBUG_LOGS
	glDebugMessageCallback(openGLDebugCallback, nullptr);
	glEnable(GL_DEBUG_OUTPUT);
#endif

	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;
	ui::LoadFonts();

	ImGui_ImplGlfw_InitForOpenGL(AppWindow.window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	ui::ApplyStyle();

	glGenTextures(1, &AppWindow.fboTexture);
	glBindTexture(GL_TEXTURE_2D, AppWindow.fboTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, AppWindow.fboWidth, AppWindow.fboHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &AppWindow.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, AppWindow.fbo);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, AppWindow.fboTexture, 0);

	GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
	glDrawBuffers(1, drawBuffers);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		throw std::runtime_error("OpenGL framebuffer incomplete");
	}
}

void DestroyGLFWResources()
{
	if (AppWindow.fbo) glDeleteFramebuffers(1, &AppWindow.fbo);
	if (AppWindow.fboTexture) glDeleteTextures(1, &AppWindow.fboTexture);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
}
