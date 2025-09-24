#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
typedef SDL_Window Next_Window;
#include <vulkan/vulkan.h>
#if ANDROID
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
#endif
#undef APIENTRY

#define DEFAULT_NON_COPIABLE(ClassName) \
ClassName(const ClassName&) = delete; \
ClassName(ClassName&&) = delete; \
ClassName& operator = (const ClassName&) = delete; \
ClassName& operator = (ClassName&&) = delete; \

#define VULKAN_NON_COPIABLE(ClassName) \
	ClassName(const ClassName&) = delete; \
	ClassName(ClassName&&) = delete; \
	ClassName& operator = (const ClassName&) = delete; \
	ClassName& operator = (ClassName&&) = delete; \
	static const char* StaticClass() {return #ClassName;} \
	virtual const char* GetActualClassName() const {return #ClassName;}

#define VULKAN_HANDLE(VulkanHandleType, name) \
public: \
	VulkanHandleType Handle() const { return name; } \
protected: \
	VulkanHandleType name{};

namespace Vulkan
{
	void Check(VkResult result, const char* operation);
	const char* ToString(VkResult result);
}
