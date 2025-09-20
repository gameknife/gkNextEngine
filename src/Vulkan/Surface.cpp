#include "Surface.hpp"
#include "Instance.hpp"
#include "Window.hpp"

namespace Vulkan {

Surface::Surface(const class Instance& instance) :
	instance_(instance)
{
#if !ANDROID
	SDL_Vulkan_CreateSurface(instance.Window().Handle(), instance.Handle(), nullptr, &surface_);
#else
	VkAndroidSurfaceCreateInfoKHR createInfo{
		.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
		.pNext = nullptr,
		.flags = 0,
		.window = instance.Window().Handle()};
	Check(vkCreateAndroidSurfaceKHR(instance.Handle(), &createInfo, nullptr,
								  &surface_),"create window surface");
#endif
}

Surface::~Surface()
{
	if (surface_ != nullptr)
	{
		vkDestroySurfaceKHR(instance_.Handle(), surface_, nullptr);
		surface_ = nullptr;
	}
}

}
