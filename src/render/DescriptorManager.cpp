#include "DescriptorManager.h"

#include <cassert>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// DescriptorSetLayoutBuilder
// ---------------------------------------------------------------------------

DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::AddBinding(
	uint32_t binding,
	vk::DescriptorType type,
	vk::ShaderStageFlags stageFlags,
	uint32_t count)
{
	m_bindings.push_back(
		vk::DescriptorSetLayoutBinding(binding, type, count, stageFlags));
	// Push a zero-flag entry to keep m_bindingFlags aligned with m_bindings
	m_bindingFlags.push_back(vk::DescriptorBindingFlags{});
	return *this;
}

DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::AddBindingWithFlags(
	uint32_t binding,
	vk::DescriptorType type,
	vk::ShaderStageFlags stageFlags,
	vk::DescriptorBindingFlags flags,
	uint32_t count)
{
	m_bindings.push_back(
		vk::DescriptorSetLayoutBinding(binding, type, count, stageFlags));
	m_bindingFlags.push_back(flags);
	return *this;
}

std::vector<vk::DescriptorSetLayoutBinding> DescriptorSetLayoutBuilder::Build()
{
	m_bindingFlags.clear();
	return std::move(m_bindings);
}

DescriptorSetLayout DescriptorSetLayoutBuilder::Build(const vk::raii::Device& device)
{
	// Check if any binding has non-zero flags
	bool hasFlags = false;
	for (auto f : m_bindingFlags)
	{
		if (f != vk::DescriptorBindingFlags{})
		{
			hasFlags = true;
			break;
		}
	}

	if (hasFlags)
	{
		return DescriptorSetLayout(device, std::move(m_bindings), std::move(m_bindingFlags));
	}
	else
	{
		return DescriptorSetLayout(device, std::move(m_bindings));
	}
}

DescriptorSetLayoutBuilder BuildLayout()
{
	return DescriptorSetLayoutBuilder{};
}

// ---------------------------------------------------------------------------
// DescriptorSetLayout
// ---------------------------------------------------------------------------

DescriptorSetLayout::DescriptorSetLayout(
	const vk::raii::Device& device,
	const std::vector<vk::DescriptorSetLayoutBinding>& bindings)
	: m_bindings(bindings)
{
	vk::DescriptorSetLayoutCreateInfo createInfo(
		vk::DescriptorSetLayoutCreateFlags{}, bindings);
	m_layout = vk::raii::DescriptorSetLayout(device, createInfo);
}

DescriptorSetLayout::DescriptorSetLayout(
	const vk::raii::Device& device,
	const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
	const std::vector<vk::DescriptorBindingFlags>& bindingFlags)
	: m_bindings(bindings)
{
	assert(bindings.size() == bindingFlags.size()
	       && "Binding flags vector must match bindings vector size");

	vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo;
	flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
	flagsInfo.pBindingFlags = bindingFlags.data();

	// If any binding uses UPDATE_AFTER_BIND, the layout itself must be
	// created with UPDATE_AFTER_BIND_POOL (VUID-03000).
	vk::DescriptorSetLayoutCreateFlags layoutFlags;
	for (auto f : bindingFlags)
	{
		if (f & vk::DescriptorBindingFlagBits::eUpdateAfterBind)
		{
			layoutFlags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
			break;
		}
	}

	vk::DescriptorSetLayoutCreateInfo createInfo(layoutFlags, bindings);
	createInfo.pNext = &flagsInfo;

	m_layout = vk::raii::DescriptorSetLayout(device, createInfo);
}

// ---------------------------------------------------------------------------
// DescriptorSet
// ---------------------------------------------------------------------------

DescriptorSet::DescriptorSet(vk::raii::DescriptorSet&& set,
                             const vk::raii::Device* device)
	: m_set(std::move(set)), m_device(device)
{
}

void DescriptorSet::WriteBuffer(uint32_t binding,
                                const vk::DescriptorBufferInfo& bufferInfo,
                                vk::DescriptorType type)
{
	vk::WriteDescriptorSet write(
		*m_set,       // dstSet
		binding,      // dstBinding
		0,            // dstArrayElement
		1,            // descriptorCount
		type,         // descriptorType
		nullptr,      // pImageInfo
		&bufferInfo,  // pBufferInfo
		nullptr       // pTexelBufferView
	);

	m_device->updateDescriptorSets(write, nullptr);
}

void DescriptorSet::WriteImage(uint32_t binding,
                               const vk::DescriptorImageInfo& imageInfo,
                               vk::DescriptorType type)
{
	vk::WriteDescriptorSet write(
		*m_set,       // dstSet
		binding,      // dstBinding
		0,            // dstArrayElement
		1,            // descriptorCount
		type,         // descriptorType
		&imageInfo,   // pImageInfo
		nullptr,      // pBufferInfo
		nullptr       // pTexelBufferView
	);

	m_device->updateDescriptorSets(write, nullptr);
}

#ifdef _DEBUG
void DescriptorSet::SetDebugName(const char* name)
{
	if (!name || !*name)
	{
		return;
	}

	const vk::DebugUtilsObjectNameInfoEXT nameInfo(
		vk::ObjectType::eDescriptorSet,
		reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(*m_set)),
		name);
	m_device->setDebugUtilsObjectNameEXT(nameInfo);
}
#endif

// ---------------------------------------------------------------------------
// DescriptorPool
// ---------------------------------------------------------------------------

DescriptorPool::DescriptorPool(
	const vk::raii::Device& device,
	uint32_t maxSets,
	const std::vector<vk::DescriptorPoolSize>& poolSizes)
	: m_device(&device)
{
	vk::DescriptorPoolCreateInfo createInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
		vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		maxSets,
		poolSizes);

	m_pool = vk::raii::DescriptorPool(device, createInfo);
}

std::vector<DescriptorSet> DescriptorPool::Allocate(
	const DescriptorSetLayout& layout,
	uint32_t count)
{
	if (count == 0)
	{
		return {};
	}

	// Build allocation info with count copies of the same layout
	std::vector<vk::DescriptorSetLayout> rawLayouts(
		count, *layout.layout());

	vk::DescriptorSetAllocateInfo allocInfo(*m_pool, rawLayouts);

	std::vector<vk::raii::DescriptorSet> vkSets;
	try
	{
		vkSets = m_device->allocateDescriptorSets(allocInfo);
	}
	catch (const std::exception&)
	{
		throw std::runtime_error(
			"DescriptorPool::Allocate: Failed to allocate " +
			std::to_string(count) + " descriptor set(s). Pool exhausted.");
	}

	// Wrap each vk::raii::DescriptorSet in our DescriptorSet
	std::vector<DescriptorSet> result;
	result.reserve(vkSets.size());
	for (auto& vkSet : vkSets)
	{
		result.emplace_back(std::move(vkSet), m_device);
	}

	return result;
}

std::vector<vk::DescriptorPoolSize> DescriptorPool::CalculatePoolSizes(
	const std::vector<const DescriptorSetLayout*>& layouts,
	uint32_t multiplier)
{
	// Accumulate descriptor counts by type using linear search
	// (pool types are few enough that O(n²) is fine)
	std::vector<std::pair<vk::DescriptorType, uint32_t>> typeCounts;

	for (const auto* layout : layouts)
	{
		if (!layout)
		{
			continue;
		}

		for (const auto& binding : layout->bindings())
		{
			bool found = false;
			for (auto& tc : typeCounts)
			{
				if (tc.first == binding.descriptorType)
				{
					tc.second += binding.descriptorCount;
					found = true;
					break;
				}
			}
			if (!found)
			{
				typeCounts.emplace_back(binding.descriptorType,
				                        binding.descriptorCount);
			}
		}
	}

	// Convert to pool sizes with multiplier applied
	std::vector<vk::DescriptorPoolSize> result;
	result.reserve(typeCounts.size());
	for (const auto& [type, count] : typeCounts)
	{
		result.push_back({type, count * multiplier});
	}

	return result;
}

} // namespace neurus
