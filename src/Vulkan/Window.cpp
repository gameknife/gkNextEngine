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

namespace
{
#if !ANDROID
	void GlfwErrorCallback(const int error, const char* const description)
	{
		SPDLOG_ERROR("ERROR: GLFW: {} (code: {})", description, error);
	}

	void GlfwKeyCallback(SDL_Window* window, const int key, const int scancode, const int action, const int mods)
	{
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		// if (this_->OnKey)
		// {
		// 	this_->OnKey(key, scancode, action, mods);
		// }
	}

	void GlfwCursorPositionCallback(SDL_Window* window, const double xpos, const double ypos)
	{
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		// if (this_->OnCursorPosition)
		// {
		// 	this_->OnCursorPosition(xpos, ypos);
		// }
	}

	void GlfwMouseButtonCallback(SDL_Window* window, const int button, const int action, const int mods)
	{
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		// if (this_->OnMouseButton)
		// {
		// 	this_->OnMouseButton(button, action, mods);
		// }
	}

	void GlfwScrollCallback(SDL_Window* window, const double xoffset, const double yoffset)
	{
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		// if (this_->OnScroll)
		// {
		// 	this_->OnScroll(xoffset, yoffset);
		// }
	}
	void GlfwSetDropCallback(SDL_Window* window, int path_count, const char* paths[])
	{
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		// if (this_->OnDropFile)
		// {
		// 	this_->OnDropFile(path_count, paths);
		// }
	}
	void GlfwOnFocusCallback(SDL_Window* window, int focus)
	{
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(window));
		// if (this_->OnFocus)
		// {
		// 	this_->OnFocus(window, focus);
		// }
	}
	// 添加手柄输入回调函数
	void GlfwJoystickCallback(int jid, int event)
	{
		// 当手柄连接或断开时触发
		// auto* const this_ = static_cast<Window*>(glfwGetWindowUserPointer(glfwGetCurrentContext()));
		// if (this_->OnJoystickConnection)
		// {
		// 	this_->OnJoystickConnection(jid, event);
		// }
	}
#endif
}

Window::Window(const WindowConfig& config) :
	config_(config)
{
	window_ = SDL_CreateWindow(config.Title.c_str(), config.Width, config.Height, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
	if( !window_ )
	{
		Throw(std::runtime_error("failed to init SDL Window."));
	}
	
#if !ANDROID
	// if ( !glfwJoystickIsGamepad(0) ) {
	// 	std::ifstream file(Utilities::FileHelper::GetNormalizedFilePath("assets/locale/gamecontrollerdb.txt"));
	// 	if(file.is_open())
	// 	{
	// 	    file.seekg(0, std::ios::end);
	// 	    size_t fileSize = file.tellg();
	// 	    file.seekg(0, std::ios::beg);
	//
	// 	    std::string mappings(fileSize, '\0');
	// 	    file.read(mappings.data(), fileSize);
	// 	    file.close();
	//
	// 	    glfwUpdateGamepadMappings(mappings.c_str());
	// 	}
	//
	// 	if (glfwJoystickIsGamepad(0)) {
	// 		SPDLOG_INFO("Gamepad: {}", glfwGetGamepadName(0));
	// 	}
	// }
	//
	// // hide title bar, handle in ImGUI Later
	// if (config.HideTitleBar)
	// {
	// 	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	// }
	//
	// glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	// glfwWindowHint(GLFW_RESIZABLE, config.Resizable ? GLFW_TRUE : GLFW_FALSE);
	//
	// auto* const monitor = config.Fullscreen ? glfwGetPrimaryMonitor() : nullptr;
	//
	// // Get the primary monitor scale
	// float xscale = 1.0f, yscale = 1.0f;
	//
	// // if (monitor) {
	// // 	glfwGetMonitorContentScale(monitor, &xscale, &yscale);
	// // } else {
	// // 	// Get content scale for windowed mode too
	// // 	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	// // 	if (primaryMonitor) {
	// // 		glfwGetMonitorContentScale(primaryMonitor, &xscale, &yscale);
	// // 	}
	// // }
	//
	// // indow dimensions based on DPI
	// int scaledWidth = static_cast<int>(config.Width * xscale);
	// int scaledHeight = static_cast<int>(config.Height * yscale);
	//
	// // create with hidden window
	// glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	// window_ = glfwCreateWindow(scaledWidth, scaledHeight, config.Title.c_str(), monitor, nullptr);
	// if (window_ == nullptr)
	// {
	// 	Throw(std::runtime_error("failed to create window"));
	// }
	//
	// // Center window position considering DPI
	// if (!config.Fullscreen) {
	// 	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	// 	if (mode) {
	// 		int windowPosX = (mode->width - scaledWidth) / 2;
	// 		int windowPosY = (mode->height - scaledHeight) / 2;
	// 		glfwSetWindowPos(window_, windowPosX, windowPosY);
	// 	}
	// }
	//
	// GLFWimage icon;
	// std::vector<uint8_t> data;
	// Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/textures/Vulkan.png", data);
	// icon.pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &icon.width, &icon.height, nullptr, 4);
	// if (icon.pixels == nullptr)
	// {
	// 	Throw(std::runtime_error("failed to load icon"));
	// }
	//
	// glfwSetWindowIcon(window_, 1, &icon);
	// stbi_image_free(icon.pixels);
	//
	// if (config.CursorDisabled)
	// {
	// 	glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	// }
	//
	// glfwSetWindowUserPointer(window_, this);
	// glfwSetKeyCallback(window_, GlfwKeyCallback);
	// glfwSetCursorPosCallback(window_, GlfwCursorPositionCallback);
	// glfwSetMouseButtonCallback(window_, GlfwMouseButtonCallback);
	// glfwSetScrollCallback(window_, GlfwScrollCallback);
	// glfwSetDropCallback(window_, GlfwSetDropCallback);
	// glfwSetWindowFocusCallback(window_, GlfwOnFocusCallback);
	// glfwSetJoystickCallback(GlfwJoystickCallback);
	
#else
	window_ = static_cast<GLFWwindow*>(config.AndroidNativeWindow);
#endif
}

Window::~Window()
{
#if !ANDROID
	if (window_ != nullptr)
	{
		SDL_DestroyWindow(window_);
		window_ = nullptr;
	}
#endif
}

float Window::ContentScale() const
{
#if !ANDROID
	float xscale = 1;
	float yscale = 1;
	//glfwGetWindowContentScale(window_, &xscale, &yscale);
#else
	float xscale = 1;
#endif
	return xscale;
}

VkExtent2D Window::FramebufferSize() const
{
#if !ANDROID
	int width, height;
	SDL_GetWindowSize(window_, &width, &height);
	return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
#else
	float aspect = ANativeWindow_getWidth(window_) / static_cast<float>(ANativeWindow_getHeight(window_));
	return VkExtent2D{ static_cast<uint32_t>(floorf(1280 * aspect)), 1280 };
#endif
}

VkExtent2D Window::WindowSize() const
{
#if !ANDROID
	int width, height;
	SDL_GetWindowSize(window_, &width, &height);
	return VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
#else
    return VkExtent2D{ (uint32_t)ANativeWindow_getWidth(window_), (uint32_t)ANativeWindow_getHeight(window_) };
#endif
}

const char* Window::GetKeyName(const int key, const int scancode) const
{
#if !ANDROID
	return "A";//glfwGetKeyName(key, scancode);
#else
	return "A";
#endif
}

std::vector<const char*> Window::GetRequiredInstanceExtensions() const
{
#if !ANDROID
	//uint32_t glfwExtensionCount = 0;
	//const char** glfwExtensions = SDL_Getexten(&glfwExtensionCount);
	//return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);

	uint32_t extensionCount = 0;
	auto extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensionNames, extensionNames + extensionCount);
#else
	return std::vector<const char*>();
#endif
}

double Window::GetTime() const
{
#if !ANDROID
	return SDL_GetTicks() / 1000.0;
#else
	return now_ms() / 1000.0;
#endif
}
	
void Window::Close()
{
#if !ANDROID
    SDL_Event e{};
    e.type = SDL_EventType::SDL_EVENT_WINDOW_CLOSE_REQUESTED;
	e.window.windowID = SDL_GetWindowID(window_);
    SDL_PushEvent(&e);
#endif
}
	
bool Window::IsMinimized() const
{
#if !ANDROID
	return SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED;
#else
	return false;
#endif
}

bool Window::IsMaximumed() const
{
#if !ANDROID
	//return glfwGetWindowAttrib(window_, GLFW_MAXIMIZED);
	return SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED;
#endif
	return false;
}

void Window::WaitForEvents() const
{
#if !ANDROID
	//glfwWaitEvents();
	SDL_Event event;
	while (SDL_WaitEvent(&event))
	{
		
	}
#endif
}

void Window::Show() const
{
#if !ANDROID
	//glfwShowWindow(window_);
#endif
}

void Window::Minimize() {
#if !ANDROID
	//glfwSetWindowSize(window_, 0,0);
	//glfwIconifyWindow(window_);
	SDL_MinimizeWindow(window_);
#endif
}

void Window::Maximum() {
#if !ANDROID
	//glfwMaximizeWindow(window_);
	SDL_MaximizeWindow(window_);
#endif
}

void Window::Restore()
{
#if !ANDROID
	//glfwRestoreWindow(window_);
	SDL_RestoreWindow(window_);
#endif
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
#if !ANDROID
	// glfwSetErrorCallback(GlfwErrorCallback);
	// if (!glfwInit())
	// {
	// 	Throw(std::runtime_error("glfwInit() failed"));
	// }
	//
	// if (!glfwVulkanSupported())
	// {
	// 	Throw(std::runtime_error("glfwVulkanSupported() failed"));
	// }
	if( !SDL_Init(SDL_INIT_VIDEO) )
	{
		Throw(std::runtime_error("failed to init SDL."));
	}
	if( !SDL_Vulkan_LoadLibrary(nullptr) )
	{
		//Throw(std::runtime_error("failed to init SDL Vulkan."));
	}
#endif
}

void Window::TerminateGLFW()
{
#if !ANDROID
	// glfwTerminate();
	// glfwSetErrorCallback(nullptr);
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
#endif
}
}
