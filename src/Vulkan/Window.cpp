#include "Window.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/StbImage.hpp"
#include "Common/CoreMinimal.hpp"

#include "Options.hpp"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>

#if ANDROID
#include <time.h>

static double now_ms(void) {

    struct timespec res;
    clock_gettime(CLOCK_REALTIME, &res);
    return 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;
}
#endif

namespace Vulkan {
	
Window::Window(const WindowConfig& config) :
	config_(config)
{
	window_ = SDL_CreateWindow(config.Title.c_str(), config.Width, config.Height, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
	if( !window_ )
	{
		Throw(std::runtime_error("failed to init SDL Window."));
	}
}

Window::~Window()
{
	if (window_ != nullptr)
	{
		SDL_DestroyWindow(window_);
		window_ = nullptr;
	}
}

float Window::ContentScale() const
{
	float xscale = 1;
	xscale = SDL_GetWindowDisplayScale(window_);
	return xscale;
}

VkExtent2D Window::FramebufferSize() const
{
	int width, height;
	SDL_GetWindowSize(window_, &width, &height);
	return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

VkExtent2D Window::WindowSize() const
{
	int width, height;
	SDL_GetWindowSize(window_, &width, &height);
	return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

const char* Window::GetKeyName(const int key, const int scancode) const
{
	return "A";//glfwGetKeyName(key, scancode);
}

std::vector<const char*> Window::GetRequiredInstanceExtensions() const
{
	uint32_t extensionCount = 0;
	auto extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
	return std::vector<const char*>(extensionNames, extensionNames + extensionCount);
}

double Window::GetTime() const
{
	return SDL_GetTicks() / 1000.0;
}
	
void Window::Close()
{
    SDL_Event e{};
    e.type = SDL_EventType::SDL_EVENT_WINDOW_CLOSE_REQUESTED;
	e.window.windowID = SDL_GetWindowID(window_);
    SDL_PushEvent(&e);
}
	
bool Window::IsMinimized() const
{
	return SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED;
}

bool Window::IsMaximumed() const
{
	//return glfwGetWindowAttrib(window_, GLFW_MAXIMIZED);
	return SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED;
}

void Window::WaitForEvents() const
{
	//glfwWaitEvents();
	SDL_Event event;
	while (SDL_WaitEvent(&event))
	{
		
	}
}

void Window::Show() const
{
	SDL_ShowWindow(window_);
}

void Window::Minimize() {
	SDL_MinimizeWindow(window_);
}

void Window::Maximum() {
	SDL_MaximizeWindow(window_);
}

void Window::Restore()
{
	SDL_RestoreWindow(window_);
}

constexpr double CLOSE_AREA_WIDTH = 0;
constexpr double TITLE_AREA_HEIGHT = 55;	
void Window::attemptDragWindow() {
#if !ANDROID
	float x {};
	float y {};
	SDL_MouseButtonFlags flag = SDL_GetMouseState(&x, &y);
	
	if (flag == SDL_BUTTON_LEFT && dragState == 0) {
		SDL_GetWindowSize(window_, &w_xsiz, &w_ysiz);

		s_xpos = x;
		s_ypos = y;
		dragState = 1;
	}
	if (flag == SDL_BUTTON_LEFT && dragState == 1) {
		double c_xpos, c_ypos;
		int w_xpos, w_ypos;
		SDL_GetWindowPosition(window_, &w_xpos, &w_ypos);

		c_xpos = x;
		c_ypos = y;
		
		if (
			s_xpos >= 0 && s_xpos <= (static_cast<double>(w_xsiz) - CLOSE_AREA_WIDTH) &&
			s_ypos >= 0 && s_ypos <= TITLE_AREA_HEIGHT) {
			SDL_SetWindowPosition(window_, w_xpos + static_cast<int>(c_xpos - s_xpos), w_ypos + static_cast<int>(c_ypos - s_ypos));
			}
		if (
			s_xpos >= (static_cast<double>(w_xsiz) - 15) && s_xpos <= (static_cast<double>(w_xsiz)) &&
			s_ypos >= (static_cast<double>(w_ysiz) - 15) && s_ypos <= (static_cast<double>(w_ysiz))) {
			SDL_SetWindowSize(window_, w_xsiz + static_cast<int>(c_xpos - s_xpos), w_ysiz + static_cast<int>(c_ypos - s_ypos));
			}
	}
	if (flag != SDL_BUTTON_LEFT && dragState == 1) {
		dragState = 0;
	}
#endif
}

// 添加一个轮询手柄输入的方法
void Window::PollGamepadInput()
{
#if !ANDROID
	// 检查手柄是否连接并可用(GLFW支持最多16个手柄，GLFW_JOYSTICK_1到GLFW_JOYSTICK_16)
	// for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; jid++)
	// {
	// 	if (glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid))
	// 	{
	// 		GLFWgamepadstate state;
	// 		if (glfwGetGamepadState(jid, &state))
	// 		{
	// 			// 获取左摇杆输入
	// 			float leftStickX = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
	// 			float leftStickY = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
 //
	// 			// 获取右摇杆输入
	// 			float rightStickX = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
	// 			float rightStickY = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
 //
	// 			// 获取扳机键输入
	// 			float leftTrigger = state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
	// 			float rightTrigger = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];
 //
	// 			// 触发手柄输入回调
	// 			if (this->OnGamepadInput)
	// 			{
	// 				this->OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
	// 			}
 //
	// 			// 一旦找到一个可用的手柄就退出循环(或者你可以处理多个手柄)
	// 			break;
	// 		}
	// 	}
	// }
#endif
}
	
void Window::InitGLFW()
{
	if( !SDL_Init(SDL_INIT_VIDEO) )
	{
		Throw(std::runtime_error("failed to init SDL."));
	}
	if( !SDL_Vulkan_LoadLibrary(nullptr) )
	{
		Throw(std::runtime_error("failed to init SDL Vulkan."));
	}
}

void Window::TerminateGLFW()
{
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
}
}
