
#include "Precomp.h"
#include "DescriptorSetManager.h"
#include "VulkanRenderDevice.h"
#include "CachedTexture.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Resources/Level/UModel.h"

DescriptorSetManager::DescriptorSetManager(VulkanRenderDevice* renderer) : renderer(renderer)
{
	if (renderer->SupportsBindless)
		CreateBindlessTextureSet();
	else
		CreateSceneTextureLayout();
	CreatePresentLayout();
	CreatePresentSet();
	CreateBloomLayout();
	CreateBloomSets();
}

DescriptorSetManager::~DescriptorSetManager()
{
}

void DescriptorSetManager::ClearCache()
{
	Textures.WriteBindless = WriteDescriptors();
	Textures.NextBindlessIndex = 0;

	Textures.SceneSetCache.clear();
	Textures.SceneSets.clear();
	Textures.ScenePools.clear();
	Textures.ScenePoolSetsLeft = 0;
}

int DescriptorSetManager::GetTextureArrayIndex(uint32_t PolyFlags, CachedTexture* tex, bool clamp)
{
	if (Textures.NextBindlessIndex == MaxBindlessTextures)
	{
		static bool firstCall = true;
		if (firstCall)
		{
			LogMessage("============================================================================");
			LogMessage("VulkanDrv encountered more than " + std::to_string(MaxBindlessTextures) + " textures");
			LogMessage("============================================================================");
			firstCall = false;
		}
		return 0; // Oh oh, we are out of texture slots
	}

	if (Textures.NextBindlessIndex == 0)
	{
		Textures.WriteBindless.AddCombinedImageSampler(Textures.BindlessSet.get(), 0, 0, renderer->Textures->NullTextureView.get(), renderer->Samplers->Samplers[0].get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Textures.NextBindlessIndex = 1;
	}

	if (!tex)
		return 0;

	uint32_t samplermode = 0;
	if (PolyFlags & PF_NoSmooth) samplermode |= 1;
	if (clamp) samplermode |= 2;

	int index = tex->BindlessIndex[samplermode];
	if (index != -1)
		return index;

	index = Textures.NextBindlessIndex++;

	VulkanSampler* sampler = renderer->Samplers->Samplers[samplermode].get();
	Textures.WriteBindless.AddCombinedImageSampler(Textures.BindlessSet.get(), 0, index, tex->imageView.get(), sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	tex->BindlessIndex[samplermode] = index;
	return index;
}

VulkanDescriptorSet* DescriptorSetManager::GetTextureSet(uint32_t PolyFlags, CachedTexture* tex, CachedTexture* lightmap, CachedTexture* macrotex, CachedTexture* detailtex, bool clamp)
{
	uint32_t samplermode = 0;
	if (PolyFlags & PF_NoSmooth) samplermode |= 1;
	if (clamp) samplermode |= 2;

	TexDescriptorKey key(tex, lightmap, detailtex, macrotex, samplermode);
	auto it = Textures.SceneSetCache.find(key);
	if (it != Textures.SceneSetCache.end())
		return it->second;

	VulkanDescriptorSet* descriptorSet = AllocSceneTextureSet();

	// The sampler mode only applies to the surface texture, as in the bindless path.
	VulkanSampler* sampler = renderer->Samplers->Samplers[samplermode].get();
	VulkanSampler* plainSampler = renderer->Samplers->Samplers[0].get();
	VulkanImageView* nulltex = renderer->Textures->NullTextureView.get();

	// Binding order matches textureBinds.xyzw in the bindless shader.
	WriteDescriptors write;
	write.AddCombinedImageSampler(descriptorSet, 0, tex ? tex->imageView.get() : nulltex, tex ? sampler : plainSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.AddCombinedImageSampler(descriptorSet, 1, macrotex ? macrotex->imageView.get() : nulltex, plainSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.AddCombinedImageSampler(descriptorSet, 2, detailtex ? detailtex->imageView.get() : nulltex, plainSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.AddCombinedImageSampler(descriptorSet, 3, lightmap ? lightmap->imageView.get() : nulltex, plainSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.Execute(renderer->Device.get());

	Textures.SceneSetCache[key] = descriptorSet;
	return descriptorSet;
}

VulkanDescriptorSet* DescriptorSetManager::AllocSceneTextureSet()
{
	if (Textures.ScenePoolSetsLeft == 0)
	{
		Textures.ScenePools.push_back(DescriptorPoolBuilder()
			.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SceneSetsPerPool * 4)
			.MaxSets(SceneSetsPerPool)
			.DebugName("SceneTexturePool")
			.Create(renderer->Device.get()));
		Textures.ScenePoolSetsLeft = SceneSetsPerPool;
	}

	Textures.SceneSets.push_back(Textures.ScenePools.back()->allocate(Textures.SceneLayout.get()));
	Textures.ScenePoolSetsLeft--;
	return Textures.SceneSets.back().get();
}

void DescriptorSetManager::UpdateBindlessSet()
{
	Textures.WriteBindless.Execute(renderer->Device.get());
	Textures.WriteBindless = WriteDescriptors();
}

void DescriptorSetManager::CreateBindlessTextureSet()
{
	Textures.BindlessPool = DescriptorPoolBuilder()
		.Flags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxBindlessTextures)
		.MaxSets(MaxBindlessTextures)
		.DebugName("TextureBindlessPool")
		.Create(renderer->Device.get());

	Textures.BindlessLayout = DescriptorSetLayoutBuilder()
		.Flags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT)
		.AddBinding(
			0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			MaxBindlessTextures,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT)
		.DebugName("TextureBindlessLayout")
		.Create(renderer->Device.get());

	Textures.BindlessSet = Textures.BindlessPool->allocate(Textures.BindlessLayout.get(), MaxBindlessTextures);
}

void DescriptorSetManager::CreateSceneTextureLayout()
{
	Textures.SceneLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.DebugName("SceneTextureLayout")
		.Create(renderer->Device.get());
}

void DescriptorSetManager::CreatePresentLayout()
{
	Present.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.DebugName("PresentLayout")
		.Create(renderer->Device.get());
}

void DescriptorSetManager::CreatePresentSet()
{
	Present.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
		.MaxSets(1)
		.DebugName("PresentPool")
		.Create(renderer->Device.get());
	Present.Set = Present.Pool->allocate(Present.Layout.get());
}

void DescriptorSetManager::CreateBloomLayout()
{
	Bloom.Layout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
		.DebugName("BloomLayout")
		.Create(renderer->Device.get());
}

void DescriptorSetManager::CreateBloomSets()
{
	Bloom.Pool = DescriptorPoolBuilder()
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (NumBloomLevels * 2 + 1) * 2)
		.MaxSets(NumBloomLevels * 2 + 1)
		.DebugName("BloomPool")
		.Create(renderer->Device.get());

	for (int level = 0; level < NumBloomLevels; level++)
	{
		Bloom.HTextureSets[level] = Bloom.Pool->allocate(Bloom.Layout.get());
		Bloom.VTextureSets[level] = Bloom.Pool->allocate(Bloom.Layout.get());
	}

	Bloom.PPImageSet = Bloom.Pool->allocate(Bloom.Layout.get());
}

void DescriptorSetManager::UpdateFrameDescriptors()
{
	auto textures = renderer->Textures.get();
	auto samplers = renderer->Samplers.get();

	WriteDescriptors write;
	write.AddCombinedImageSampler(Present.Set.get(), 0, textures->Scene->PPImageView[0].get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.AddCombinedImageSampler(Present.Set.get(), 1, textures->DitherImageView.get(), samplers->PPNearestRepeat.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	for (int level = 0; level < NumBloomLevels; level++)
	{
		write.AddCombinedImageSampler(GetBloomHTextureSet(level), 0, textures->Scene->BloomBlurLevels[level].HTextureView.get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		write.AddCombinedImageSampler(GetBloomVTextureSet(level), 0, textures->Scene->BloomBlurLevels[level].VTextureView.get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	write.AddCombinedImageSampler(Bloom.PPImageSet.get(), 0, textures->Scene->PPImageView[0].get(), samplers->PPLinearClamp.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	write.Execute(renderer->Device.get());
}
