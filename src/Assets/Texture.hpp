#pragma once

#include "Common/CoreMinimal.hpp"
#include "Vulkan/Vulkan.hpp"
#include "Vulkan/Sampler.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "UniformBuffer.hpp"
#include "Vulkan/DescriptorSetLayout.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"

namespace Vulkan {
	class CommandPool;
	class DescriptorSetManager;
}

namespace Assets
{
	class TextureImage;

	enum class ETextureStatus : uint8
	{
		ETS_Loaded,
		ETS_Unloaded,
	};

	struct FTextureBindingGroup
	{
		uint32_t GlobalIdx_;
		ETextureStatus Status_;
	};
	
	class GlobalTexturePool final
	{
	public:
		GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool, Vulkan::CommandPool& command_pool_mt);
		~GlobalTexturePool();

		VkDescriptorSetLayout Layout() const { return descriptorSetManager_->DescriptorSetLayout().Handle(); }
		VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSetManager_->DescriptorSets().Handle(0); }

		void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
		void BindStorageTexture(uint32_t textureIdx, const Vulkan::ImageView& textureImage);
		uint32_t TryGetTexureIndex(const std::string& textureName) const;
		uint32_t RequestNewTextureFileAsync(const std::string& filename, bool hdr);
		uint32_t RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr, const unsigned char* data, size_t bytelength, bool srgb);
		
		uint32_t TotalTextures() const {return static_cast<uint32_t>(textureImages_.size());}
		const std::unordered_map<std::string, FTextureBindingGroup>& TotalTextureMap() {return textureNameMap_;}

		void FreeNonSystemTextures();
		void CreateDefaultTextures();
		
		static GlobalTexturePool* GetInstance() {return instance_;}
		static uint32_t LoadTexture(const std::string& texname, const std::string& mime, const unsigned char* data, size_t bytelength, bool srgb);
		static uint32_t LoadTexture(const std::string& filename, bool srgb);
		static uint32_t LoadHDRTexture(const std::string& filename);

		static TextureImage* GetTextureImage(uint32_t idx);
		static TextureImage* GetTextureImageByName(const std::string& name);
		static uint32_t GetTextureIndexByName(const std::string& name);

		std::vector<SphericalHarmonics>& GetHDRSphericalHarmonics() { return hdrSphericalHarmonics_; }
		Vulkan::CommandPool& GetMainThreadCommandPool() { return mainThreadCommandPool_; }

		Vulkan::DescriptorSetManager& GetDescriptorManager() { return *descriptorSetManager_; }
		Vulkan::DescriptorSetManager& GetStorageDescriptorManager() { return *storageDescriptorSetManager_; }
	private:
		static GlobalTexturePool* instance_;

		const class Vulkan::Device& device_;
		Vulkan::CommandPool& commandPool_;
		Vulkan::CommandPool& mainThreadCommandPool_;
		
		//VkDescriptorPool descriptorPool_{};
		//VkDescriptorSetLayout layout_{};
		//std::vector<VkDescriptorSet> descriptorSets_;

		std::vector<std::unique_ptr<TextureImage>> textureImages_;
		std::unordered_map<std::string, FTextureBindingGroup> textureNameMap_;

		std::vector<SphericalHarmonics> hdrSphericalHarmonics_;

		std::unique_ptr<TextureImage> defaultWhiteTexture_;

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::DescriptorSetManager> storageDescriptorSetManager_;
	};

}
