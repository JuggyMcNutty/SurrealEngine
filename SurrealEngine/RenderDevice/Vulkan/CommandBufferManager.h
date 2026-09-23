#pragma once

#include <surrealgpu/vulkanobjects.h>

class VulkanRenderDevice;

class CommandBufferManager
{
public:
	CommandBufferManager(VulkanRenderDevice* renderer);
	~CommandBufferManager();

	void WaitForTransfer();
	void SubmitCommands(bool present, int presentWidth, int presentHeight, bool presentFullscreen, bool wait = true);

	// Waits for a frame SubmitCommands left in flight (wait = false), if there
	// is one. Anything that writes a buffer, image or descriptor the GPU may
	// still be reading calls this first.
	void WaitForFrame();
	VulkanCommandBuffer* GetTransferCommands();
	VulkanCommandBuffer* GetDrawCommands();
	void DeleteFrameObjects();

	struct DeleteList
	{
		std::vector<std::unique_ptr<VulkanImage>> images;
		std::vector<std::unique_ptr<VulkanImageView>> imageViews;
		std::vector<std::unique_ptr<VulkanBuffer>> buffers;
		std::vector<std::unique_ptr<VulkanDescriptorSet>> descriptors;
	};
	std::unique_ptr<DeleteList> FrameDeleteList;

	std::shared_ptr<VulkanSwapChain> SwapChain;
	int PresentImageIndex = -1;
	bool UsingVsync = false;
	bool UsingHdr = false;

private:
	VulkanRenderDevice* renderer = nullptr;

	std::unique_ptr<VulkanSemaphore> ImageAvailableSemaphore;
	std::vector<std::unique_ptr<VulkanSemaphore>> RenderFinishedSemaphores;
	std::unique_ptr<VulkanSemaphore> TransferSemaphore;
	std::unique_ptr<VulkanFence> RenderFinishedFence;
	std::unique_ptr<VulkanCommandPool> CommandPool;
	std::unique_ptr<VulkanCommandBuffer> DrawCommands;
	std::unique_ptr<VulkanCommandBuffer> TransferCommands;

	// The frame in flight: its command buffers live until its fence signals.
	bool FramePending = false;
	std::unique_ptr<VulkanCommandBuffer> PendingDrawCommands;
	std::unique_ptr<VulkanCommandBuffer> PendingTransferCommands;
};
