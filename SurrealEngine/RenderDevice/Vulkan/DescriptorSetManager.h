#pragma once

#include "SceneTextures.h"
#include <unordered_map>
#include <surrealgpu/vulkanbuilders.h>

class VulkanRenderDevice;
class CachedTexture;

struct TexDescriptorKey
{
	TexDescriptorKey(CachedTexture* tex, CachedTexture* lightmap, CachedTexture* detailtex, CachedTexture* macrotex, uint32_t sampler) : tex(tex), lightmap(lightmap), detailtex(detailtex), macrotex(macrotex), sampler(sampler) { }

	bool operator==(const TexDescriptorKey& other) const
	{
		return tex == other.tex && lightmap == other.lightmap && detailtex == other.detailtex && macrotex == other.macrotex && sampler == other.sampler;
	}

	bool operator<(const TexDescriptorKey& other) const
	{
		if (tex != other.tex)
			return tex < other.tex;
		else if (lightmap != other.lightmap)
			return lightmap < other.lightmap;
		else if (detailtex != other.detailtex)
			return detailtex < other.detailtex;
		else if (macrotex != other.macrotex)
			return macrotex < other.macrotex;
		else
			return sampler < other.sampler;
	}

	CachedTexture* tex;
	CachedTexture* lightmap;
	CachedTexture* detailtex;
	CachedTexture* macrotex;
	uint32_t sampler;
};

template<> struct std::hash<TexDescriptorKey>
{
	std::size_t operator()(const TexDescriptorKey& k) const
	{
		return (((std::size_t)k.tex ^ (std::size_t)k.lightmap));
	}
};

class DescriptorSetManager
{
public:
	DescriptorSetManager(VulkanRenderDevice* renderer);
	~DescriptorSetManager();

	void ClearCache();

	// Bindless path: every texture lives in one array the shader indexes from the vertex data.
	bool IsTextureArrayFull() const { return Textures.NextBindlessIndex + 4 > MaxBindlessTextures; }
	int GetTextureArrayIndex(uint32_t PolyFlags, CachedTexture* tex, bool clamp = false);

	// Non-bindless path: one four-binding set per texture combination, bound per batch.
	bool IsTextureSetCacheFull() const { return (int)Textures.SceneSets.size() + 1 > MaxSceneSets; }
	VulkanDescriptorSet* GetTextureSet(uint32_t PolyFlags, CachedTexture* tex, CachedTexture* lightmap, CachedTexture* macrotex, CachedTexture* detailtex, bool clamp = false);

	VulkanDescriptorSet* GetBindlessSet() { return Textures.BindlessSet.get(); }
	VulkanDescriptorSet* GetPresentSet() { return Present.Set.get(); }
	VulkanDescriptorSet* GetBloomPPImageSet() { return Bloom.PPImageSet.get(); }
	VulkanDescriptorSet* GetBloomVTextureSet(int level) { return Bloom.VTextureSets[level].get(); }
	VulkanDescriptorSet* GetBloomHTextureSet(int level) { return Bloom.HTextureSets[level].get(); }

	void UpdateBindlessSet();
	void UpdateFrameDescriptors();

	static const int MaxBindlessTextures = 16536;
	static const int MaxSceneSets = 16384;
	static const int SceneSetsPerPool = 1024;

	// The scene pipeline layout takes whichever of the two the device can do.
	VulkanDescriptorSetLayout* GetTextureSetLayout() { return Textures.BindlessLayout ? Textures.BindlessLayout.get() : Textures.SceneLayout.get(); }
	VulkanDescriptorSetLayout* GetPresentLayout() { return Present.Layout.get(); }
	VulkanDescriptorSetLayout* GetBloomLayout() { return Bloom.Layout.get(); }

private:
	void CreateBindlessTextureSet();
	void CreateSceneTextureLayout();
	VulkanDescriptorSet* AllocSceneTextureSet();
	void CreatePresentLayout();
	void CreatePresentSet();
	void CreateBloomLayout();
	void CreateBloomSets();

	VulkanRenderDevice* renderer = nullptr;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> BindlessLayout;
		std::unique_ptr<VulkanDescriptorPool> BindlessPool;
		std::unique_ptr<VulkanDescriptorSet> BindlessSet;
		WriteDescriptors WriteBindless;
		int NextBindlessIndex = 0;

		// Declared before the sets so the pools outlive them.
		std::unique_ptr<VulkanDescriptorSetLayout> SceneLayout;
		std::vector<std::unique_ptr<VulkanDescriptorPool>> ScenePools;
		std::vector<std::unique_ptr<VulkanDescriptorSet>> SceneSets;
		std::unordered_map<TexDescriptorKey, VulkanDescriptorSet*> SceneSetCache;
		int ScenePoolSetsLeft = 0;

	} Textures;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> Layout;
		std::unique_ptr<VulkanDescriptorPool> Pool;
		std::unique_ptr<VulkanDescriptorSet> Set;
	} Present;

	struct
	{
		std::unique_ptr<VulkanDescriptorSetLayout> Layout;
		std::unique_ptr<VulkanDescriptorPool> Pool;
		std::unique_ptr<VulkanDescriptorSet> VTextureSets[NumBloomLevels];
		std::unique_ptr<VulkanDescriptorSet> HTextureSets[NumBloomLevels];
		std::unique_ptr<VulkanDescriptorSet> PPImageSet;
	} Bloom;
};
