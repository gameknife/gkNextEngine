#include "DescriptorSetLayout.hpp"
#include "Device.hpp"

namespace Vulkan {

DescriptorSetLayout::DescriptorSetLayout(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, bool bindless) :
	device_(device)
{
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
	std::vector<VkDescriptorBindingFlags> bindlessBindingFlags;

	for (const auto& binding : descriptorBindings)
	{
		VkDescriptorSetLayoutBinding b = {};
		b.binding = binding.Binding;
		b.descriptorCount = binding.DescriptorCount;
		b.descriptorType = binding.Type;
		b.stageFlags = binding.Stage;

		layoutBindings.push_back(b);
	}

	for ( int i = 0; i < layoutBindings.size() - 1; ++i )
	{
		bindlessBindingFlags.push_back( VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT );
	}
	bindlessBindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT);

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
	layoutInfo.pBindings = layoutBindings.data();

	// bindless stuff
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr
	};
	
	extendedInfo.bindingCount = layoutInfo.bindingCount;
	extendedInfo.pBindingFlags = bindlessBindingFlags.data();
	
	if( bindless )
	{
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
		layoutInfo.pNext = &extendedInfo;
	}

	Check(vkCreateDescriptorSetLayout(device.Handle(), &layoutInfo, nullptr, &layout_),
		"create descriptor set layout");
}

DescriptorSetLayout::~DescriptorSetLayout()
{
	if (layout_ != nullptr)
	{
		vkDestroyDescriptorSetLayout(device_.Handle(), layout_, nullptr);
		layout_ = nullptr;
	}
}

}
