#pragma once

enum class RenderDeviceType
{
	Vulkan,
	D3D11,
	D3D12,
	OpenGL
};

enum class AntialiasMode
{
	Off,
	MSAA2x,
	MSAA4x
};

enum class LightMode
{
	Normal,
	OneX,
	BrighterActors
};

enum class GammaMode
{
	D3D9,
	XOpenGL
};

class LauncherSettings
{
public:
	static LauncherSettings& Get();
	void Save();

	struct
	{
		RenderDeviceType Type = RenderDeviceType::Vulkan;
		bool UseVSync = true;
		AntialiasMode Antialias = AntialiasMode::MSAA4x;
		LightMode Light = LightMode::Normal;
		GammaMode Gamma = GammaMode::D3D9;
		bool GammaCorrectScreenshots = false;
		bool Hdr = false;
		int HdrScale = 128;
		bool Bloom = false;
		int BloomAmount = 128;
		bool UseDebugLayer = false;
	} RenderDevice;

	struct
	{
		Array<std::string> SearchList;
		int LastSelected = -1;
	} Games;

	// Controller support (Engine::UpdateGamepad). Layout is the name of the
	// binding preset a launcher last applied; the engine only carries it.
	struct
	{
		bool Enabled = true;
		float DeadZone = 0.2f;
		float LookSensitivityX = 1.0f;
		float LookSensitivityY = 1.0f;
		bool InvertY = false;
		float CursorSpeed = 1.0f;
		std::string Layout = "modern";
	} Gamepad;

	// Speed for fidelity, for slow hardware (UActor::ThinkThisFrame).
	struct
	{
		bool AiLevelOfDetail = false;
		float RenderScale = 1.0f; // the scene's size as a fraction of the window's (RenderDevice::GetRenderScale)
	} Performance;

private:
	LauncherSettings();
};
