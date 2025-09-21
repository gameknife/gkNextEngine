#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

class Options final
{
public:

	class Help : public std::exception
	{
	public:

		Help() = default;
		~Help() = default;
	};

	Options(int argc, const char* argv[]);
	~Options() = default;

	// Application options.
	bool SaveFile{};
	bool RenderDoc{};
	bool NoDenoiser{};
	bool ForceSDR{};
	bool ReferenceMode{};
	uint32_t SuperResolution{};
	bool ForceNoRT{};
	bool ForceSoftGen{};
	bool HardwareQuery{};
	std::string locale{};

	// Renderer options.
	uint32_t Samples{};
	uint32_t Bounces{};
	uint32_t MaxBounces{};
	uint32_t RendererType{};
	uint32_t Temporal{};

	bool AdaptiveSample{};
	
	// Scene options.
	std::string SceneName{};
	std::string HDRIfile{};

	// Vulkan options
	uint32_t GpuIdx{};

	// Window options
	uint32_t Width{};
	uint32_t Height{};
	uint32_t PresentMode{};
	bool Fullscreen{};
};

extern Options* GOption;