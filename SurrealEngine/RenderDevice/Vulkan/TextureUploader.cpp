
#include "Precomp.h"
#include "TextureUploader.h"
#include "RenderDevice/RenderDevice.h"

#ifdef USE_NEON
#include <arm_neon.h>
#endif

#ifdef USE_SSE2
#include <immintrin.h>
#endif

TextureUploader* TextureUploader::GetUploader(TextureFormat format, VkPhysicalDevice physicalDevice)
{
	static std::map<TextureFormat, std::unique_ptr<TextureUploader>> Uploaders;
	if (Uploaders.empty())
	{
		// Original.
		Uploaders[TextureFormat::P8].reset(new TextureUploader_P8());
		Uploaders[TextureFormat::BGRA8_LM].reset(new TextureUploader_BGRA8_LM());
		Uploaders[TextureFormat::R5G6B5].reset(new TextureUploader_Simple(VK_FORMAT_R5G6B5_UNORM_PACK16, 2));
		// Note: according to formats this should have been VK_FORMAT_BC1_RGB_UNORM_BLOCK, but some textures does use the alpha bit!
		Uploaders[TextureFormat::BC1].reset(new TextureUploader_4x4Block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8));
		Uploaders[TextureFormat::RGB8].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8_UNORM, 3));
		Uploaders[TextureFormat::BGRA8].reset(new TextureUploader_Simple(VK_FORMAT_B8G8R8A8_UNORM, 4));

		// S3TC (continued).
		Uploaders[TextureFormat::BC2].reset(new TextureUploader_4x4Block(VK_FORMAT_BC2_UNORM_BLOCK, 16));
		Uploaders[TextureFormat::BC3].reset(new TextureUploader_4x4Block(VK_FORMAT_BC3_UNORM_BLOCK, 16));

		// RGTC.
		Uploaders[TextureFormat::BC4].reset(new TextureUploader_4x4Block(VK_FORMAT_BC4_UNORM_BLOCK, 8));
		Uploaders[TextureFormat::BC4_S].reset(new TextureUploader_4x4Block(VK_FORMAT_BC4_SNORM_BLOCK, 8));
		Uploaders[TextureFormat::BC5].reset(new TextureUploader_4x4Block(VK_FORMAT_BC5_UNORM_BLOCK, 16));
		Uploaders[TextureFormat::BC5_S].reset(new TextureUploader_4x4Block(VK_FORMAT_BC5_SNORM_BLOCK, 16));

		// BPTC.
		Uploaders[TextureFormat::BC7].reset(new TextureUploader_4x4Block(VK_FORMAT_BC7_UNORM_BLOCK, 16));
		Uploaders[TextureFormat::BC6H_S].reset(new TextureUploader_4x4Block(VK_FORMAT_BC6H_SFLOAT_BLOCK, 16));
		Uploaders[TextureFormat::BC6H].reset(new TextureUploader_4x4Block(VK_FORMAT_BC6H_UFLOAT_BLOCK, 16));

		// Normalized RGBA.
		Uploaders[TextureFormat::RGBA16].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16A16_UNORM, 8));
		Uploaders[TextureFormat::RGBA16_S].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16A16_SNORM, 8));
		//Uploaders[TextureFormat::RGBA32].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32A32_UNORM, 16));
		//Uploaders[TextureFormat::RGBA32_S].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32A32_SNORM, 16));

		// S3TC (continued).
		Uploaders[TextureFormat::BC1_PA].reset(new TextureUploader_4x4Block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8));

		// Normalized RGBA (continued).
		Uploaders[TextureFormat::R8].reset(new TextureUploader_Simple(VK_FORMAT_R8_UNORM, 1));
		Uploaders[TextureFormat::R8_S].reset(new TextureUploader_Simple(VK_FORMAT_R8_SNORM, 1));
		Uploaders[TextureFormat::R16].reset(new TextureUploader_Simple(VK_FORMAT_R16_UNORM, 2));
		Uploaders[TextureFormat::R16_S].reset(new TextureUploader_Simple(VK_FORMAT_R16_SNORM, 2));
		//Uploaders[TextureFormat::R32].reset(new TextureUploader_Simple(VK_FORMAT_R32_UNORM, 4));
		//Uploaders[TextureFormat::R32_S].reset(new TextureUploader_Simple(VK_FORMAT_R32_SNORM, 4));
		Uploaders[TextureFormat::RG8].reset(new TextureUploader_Simple(VK_FORMAT_R8G8_UNORM, 2));
		Uploaders[TextureFormat::RG8_S].reset(new TextureUploader_Simple(VK_FORMAT_R8G8_SNORM, 2));
		Uploaders[TextureFormat::RG16].reset(new TextureUploader_Simple(VK_FORMAT_R16G16_UNORM, 4));
		Uploaders[TextureFormat::RG16_S].reset(new TextureUploader_Simple(VK_FORMAT_R16G16_SNORM, 4));
		//Uploaders[TextureFormat::RG32].reset(new TextureUploader_Simple(VK_FORMAT_R32G32_UNORM, 8));
		//Uploaders[TextureFormat::RG32_S].reset(new TextureUploader_Simple(VK_FORMAT_R32G32_SNORM, 8));
		Uploaders[TextureFormat::RGB8_S].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8_SNORM, 3));
		Uploaders[TextureFormat::RGB16_].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16_UNORM, 6));
		Uploaders[TextureFormat::RGB16_S].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16_SNORM, 6));
		//Uploaders[TextureFormat::RGB32].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32_UNORM, 12));
		//Uploaders[TextureFormat::RGB32_S].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32_SNORM, 12));
		Uploaders[TextureFormat::RGBA8_].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8A8_UNORM, 4));
		Uploaders[TextureFormat::RGBA8_S].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8A8_SNORM, 4));

		// Floating point RGBA.
		Uploaders[TextureFormat::R16_F].reset(new TextureUploader_Simple(VK_FORMAT_R16_SFLOAT, 2));
		Uploaders[TextureFormat::R32_F].reset(new TextureUploader_Simple(VK_FORMAT_R32_SFLOAT, 4));
		Uploaders[TextureFormat::RG16_F].reset(new TextureUploader_Simple(VK_FORMAT_R16G16_SFLOAT, 4));
		Uploaders[TextureFormat::RG32_F].reset(new TextureUploader_Simple(VK_FORMAT_R32G32_SFLOAT, 8));
		Uploaders[TextureFormat::RGB16_F].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16_SFLOAT, 6));
		Uploaders[TextureFormat::RGB32_F].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32_SFLOAT, 12));
		Uploaders[TextureFormat::RGBA16_F].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16A16_SFLOAT, 8));
		Uploaders[TextureFormat::RGBA32_F].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32A32_SFLOAT, 16));

		// ETC1/ETC2/EAC.
		//Uploaders[TextureFormat::ETC1].reset(new TextureUploader_4x4Block(VK_FORMAT_ETC1_R8G8B8_UNORM_BLOCK, 8));
		Uploaders[TextureFormat::ETC2].reset(new TextureUploader_4x4Block(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, 8));
		Uploaders[TextureFormat::ETC2_PA].reset(new TextureUploader_4x4Block(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, 8));
		Uploaders[TextureFormat::ETC2_RGB_EAC_A].reset(new TextureUploader_4x4Block(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, 16));
		Uploaders[TextureFormat::EAC_R].reset(new TextureUploader_4x4Block(VK_FORMAT_EAC_R11_UNORM_BLOCK, 8));
		Uploaders[TextureFormat::EAC_R_S].reset(new TextureUploader_4x4Block(VK_FORMAT_EAC_R11_SNORM_BLOCK, 8));
		Uploaders[TextureFormat::EAC_RG].reset(new TextureUploader_4x4Block(VK_FORMAT_EAC_R11G11_UNORM_BLOCK, 16));
		Uploaders[TextureFormat::EAC_RG_S].reset(new TextureUploader_4x4Block(VK_FORMAT_EAC_R11G11_SNORM_BLOCK, 16));

		// ASTC.
		Uploaders[TextureFormat::ASTC_4x4].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 4, 4, 16));
		Uploaders[TextureFormat::ASTC_5x4].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_5x4_UNORM_BLOCK, 5, 4, 16));
		Uploaders[TextureFormat::ASTC_5x5].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_5x5_UNORM_BLOCK, 5, 5, 16));
		Uploaders[TextureFormat::ASTC_6x5].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_6x5_UNORM_BLOCK, 6, 5, 16));
		Uploaders[TextureFormat::ASTC_6x6].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_6x6_UNORM_BLOCK, 6, 6, 16));
		Uploaders[TextureFormat::ASTC_8x5].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_8x5_UNORM_BLOCK, 8, 5, 16));
		Uploaders[TextureFormat::ASTC_8x6].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_8x6_UNORM_BLOCK, 8, 6, 16));
		Uploaders[TextureFormat::ASTC_8x8].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_8x8_UNORM_BLOCK, 8, 8, 16));
		Uploaders[TextureFormat::ASTC_10x5].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_10x5_UNORM_BLOCK, 10, 5, 16));
		Uploaders[TextureFormat::ASTC_10x6].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_10x6_UNORM_BLOCK, 10, 6, 16));
		Uploaders[TextureFormat::ASTC_10x8].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_10x8_UNORM_BLOCK, 10, 8, 16));
		Uploaders[TextureFormat::ASTC_10x10].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_10x10_UNORM_BLOCK, 10, 10, 16));
		Uploaders[TextureFormat::ASTC_12x10].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_12x10_UNORM_BLOCK, 12, 10, 16));
		Uploaders[TextureFormat::ASTC_12x12].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_12x12_UNORM_BLOCK, 12, 12, 16));
		//Uploaders[TextureFormat::ASTC_3x3x3].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_3x3x3_UNORM_BLOCK, 3, 3, 3, 16));
		//Uploaders[TextureFormat::ASTC_4x3x3].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_4x3x3_UNORM_BLOCK, 4, 3, 3, 16));
		//Uploaders[TextureFormat::ASTC_4x4x3].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_4x4x3_UNORM_BLOCK, 4, 4, 3, 16));
		//Uploaders[TextureFormat::ASTC_4x4x4].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_4x4x4_UNORM_BLOCK, 4, 4, 4, 16));
		//Uploaders[TextureFormat::ASTC_5x4x4].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_5x4x4_UNORM_BLOCK, 5, 4, 4, 16));
		//Uploaders[TextureFormat::ASTC_5x5x4].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_5x5x4_UNORM_BLOCK, 5, 5, 4, 16));
		//Uploaders[TextureFormat::ASTC_5x5x5].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_5x5x5_UNORM_BLOCK, 5, 5, 5, 16));
		//Uploaders[TextureFormat::ASTC_6x5x5].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_6x5x5_UNORM_BLOCK, 6, 5, 5, 16));
		//Uploaders[TextureFormat::ASTC_6x6x5].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_6x6x5_UNORM_BLOCK, 6, 6, 5, 16));
		//Uploaders[TextureFormat::ASTC_6x6x6].reset(new TextureUploader_3DBlock(VK_FORMAT_ASTC_6x6x6_UNORM_BLOCK, 6, 6, 6, 16));

		// PVRTC.
		// Requires VK_IMG_format_pvrtc
		//Uploaders[VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_12x12_UNORM_BLOCK, 8, 4, 8));
		//Uploaders[VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG].reset(new TextureUploader_4x4Block(VK_FORMAT_ASTC_12x12_UNORM_BLOCK, 8));
		//Uploaders[VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG].reset(new TextureUploader_2DBlock(VK_FORMAT_ASTC_12x12_UNORM_BLOCK, 8, 4, 8));
		//Uploaders[VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG].reset(new TextureUploader_4x4Block(VK_FORMAT_ASTC_12x12_UNORM_BLOCK, 8));

		// RGBA (Integral).
		Uploaders[TextureFormat::R8_UI].reset(new TextureUploader_Simple(VK_FORMAT_R8_UINT, 1));
		Uploaders[TextureFormat::R8_I].reset(new TextureUploader_Simple(VK_FORMAT_R8_SINT, 1));
		Uploaders[TextureFormat::R16_UI].reset(new TextureUploader_Simple(VK_FORMAT_R16_UINT, 2));
		Uploaders[TextureFormat::R16_I].reset(new TextureUploader_Simple(VK_FORMAT_R16_SINT, 2));
		Uploaders[TextureFormat::R32_UI].reset(new TextureUploader_Simple(VK_FORMAT_R32_UINT, 4));
		Uploaders[TextureFormat::R32_I].reset(new TextureUploader_Simple(VK_FORMAT_R32_SINT, 4));
		Uploaders[TextureFormat::RG8_UI].reset(new TextureUploader_Simple(VK_FORMAT_R8G8_UINT, 2));
		Uploaders[TextureFormat::RG8_I].reset(new TextureUploader_Simple(VK_FORMAT_R8G8_SINT, 2));
		Uploaders[TextureFormat::RG16_UI].reset(new TextureUploader_Simple(VK_FORMAT_R16G16_UINT, 4));
		Uploaders[TextureFormat::RG16_I].reset(new TextureUploader_Simple(VK_FORMAT_R16G16_SINT, 4));
		Uploaders[TextureFormat::RG32_UI].reset(new TextureUploader_Simple(VK_FORMAT_R32G32_UINT, 8));
		Uploaders[TextureFormat::RG32_I].reset(new TextureUploader_Simple(VK_FORMAT_R32G32_SINT, 8));
		Uploaders[TextureFormat::RGB8_UI].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8_UINT, 3));
		Uploaders[TextureFormat::RGB8_I].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8_SINT, 3));
		Uploaders[TextureFormat::RGB16_UI].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16_UINT, 6));
		Uploaders[TextureFormat::RGB16_I].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16_SINT, 6));
		Uploaders[TextureFormat::RGB32_UI].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32_UINT, 12));
		Uploaders[TextureFormat::RGB32_I].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32_SINT, 12));
		Uploaders[TextureFormat::RGBA8_UI].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8A8_UINT, 4));
		Uploaders[TextureFormat::RGBA8_I].reset(new TextureUploader_Simple(VK_FORMAT_R8G8B8A8_SINT, 4));
		Uploaders[TextureFormat::RGBA16_UI].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16A16_UINT, 8));
		Uploaders[TextureFormat::RGBA16_I].reset(new TextureUploader_Simple(VK_FORMAT_R16G16B16A16_SINT, 8));
		Uploaders[TextureFormat::RGBA32_UI].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32A32_UINT, 16));
		Uploaders[TextureFormat::RGBA32_I].reset(new TextureUploader_Simple(VK_FORMAT_R32G32B32A32_SINT, 16));

		// Special.
		//Uploaders[TextureFormat::ARGB8].reset(new TextureUploader_ARGB8());
		//Uploaders[TextureFormat::ABGR8].reset(new TextureUploader_ABGR8());
		Uploaders[TextureFormat::RGB10A2].reset(new TextureUploader_RGB10A2());
		Uploaders[TextureFormat::RGB10A2_UI].reset(new TextureUploader_RGB10A2_UI());
		Uploaders[TextureFormat::RGB10A2_LM].reset(new TextureUploader_RGB10A2_LM());
		//Uploaders[TextureFormat::RGB9E5].reset(new TextureUploader_RGB9E5());
		//Uploaders[TextureFormat::P8_RGB9E5].reset(new TextureUploader_P8_RGB9E5());
		//Uploaders[TextureFormat::R1].reset(new TextureUploader_R1());
		//Uploaders[TextureFormat::RGB10A2_S].reset(new TextureUploader_RGB10A2_S());
		//Uploaders[TextureFormat::RGB10A2_I].reset(new TextureUploader_RGB10A2_I());
		//Uploaders[TextureFormat::R11G11B10_F].reset(new TextureUploader_R11G11B10_F());

		// Normalized BGR.
		//Uploaders[TextureFormat::B5G6R5].reset(new TextureUploader_B5G6R5());
		//Uploaders[TextureFormat::BGR8].reset(new TextureUploader_BGR8());

		// Double precission floating point RGBA.
		//Uploaders[TextureFormat::R64_F].reset(new TextureUploader_R64_F());
		//Uploaders[TextureFormat::RG64_F].reset(new TextureUploader_RG64_F());
		//Uploaders[TextureFormat::RGB64_F].reset(new TextureUploader_RGB64_F());
		//Uploaders[TextureFormat::RGBA64_F].reset(new TextureUploader_RGBA64_F());
	}

	auto it = Uploaders.find(format);
	if (it != Uploaders.end())
	{
		if (physicalDevice == VK_NULL_HANDLE || !it->second)
			return it->second.get();

		// The canonical format exists, but the GPU may not be able to sample it.
		// Desktop BCn formats and RGB8 are optional in Vulkan and missing on
		// several mobile GPUs (the PowerVR Rogue GE8300, for one). Sampling an
		// unsupported format is undefined behavior and produces garbage, so
		// substitute a CPU decoder writing a format the device does support.
		// A format that samples but cannot be linearly filtered is just as broken
		// with the engine's linear samplers, so it falls back the same way (the
		// GE8300 samples RGBA32F but cannot filter it, which turns every lightmap
		// and fog map into speckle).
		VkFormatProperties props = {};
		vkGetPhysicalDeviceFormatProperties(physicalDevice, it->second->GetVkFormat(), &props);
		if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
			(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
			return it->second.get();

		static std::map<TextureFormat, std::unique_ptr<TextureUploader>> Decoders;
		if (Decoders.empty())
		{
			Decoders[TextureFormat::BC1].reset(new TextureUploader_BC1_Decode());
			Decoders[TextureFormat::BC1_PA].reset(new TextureUploader_BC1_Decode());
			Decoders[TextureFormat::BC2].reset(new TextureUploader_BC2_Decode());
			Decoders[TextureFormat::BC3].reset(new TextureUploader_BC3_Decode());
			Decoders[TextureFormat::BC4].reset(new TextureUploader_BC4_Decode());
			Decoders[TextureFormat::BC5].reset(new TextureUploader_BC5_Decode());
			Decoders[TextureFormat::RGB8].reset(new TextureUploader_RGB8_Decode());
			Decoders[TextureFormat::RGBA32_F].reset(new TextureUploader_RGBA32F_Decode());
		}
		auto dec = Decoders.find(format);
		return dec != Decoders.end() ? dec->second.get() : nullptr;
	}
	else
		return nullptr;
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_P8::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_P8::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width;
	uint8_t* src = mip->Data.data() + x + y * pitch;
	TextureColor* Ptr = (TextureColor*)d;
	if (masked)
	{
		TextureColor translucent(0, 0, 0, 0);
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j++)
			{
				int idx = src[j];
				*Ptr++ = (idx != 0) ? palette[idx] : translucent;
			}
			src += pitch;
		}
	}
	else
	{
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j++)
			{
				int idx = src[j];
				*Ptr++ = palette[idx];
			}
			src += pitch;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_BGRA8_LM::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_BGRA8_LM::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
#ifdef USE_SSE2
	int pitch = mip->Width;
	TextureColor* src = ((TextureColor*)mip->Data.data()) + x + y * pitch;
	auto Ptr = (TextureColor*)dst;
	if (w % 4 == 0)
	{
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j += 4)
			{
				__m128i p = _mm_loadu_si128((const __m128i*)(src + j));
				__m128i p_hi = _mm_unpackhi_epi8(p, _mm_setzero_si128());
				__m128i p_lo = _mm_unpacklo_epi8(p, _mm_setzero_si128());
				p_hi = _mm_shufflehi_epi16(p_hi, _MM_SHUFFLE(3, 0, 1, 2));
				p_hi = _mm_shufflelo_epi16(p_hi, _MM_SHUFFLE(3, 0, 1, 2));
				p_hi = _mm_slli_epi16(p_hi, 1);
				p_lo = _mm_shufflehi_epi16(p_lo, _MM_SHUFFLE(3, 0, 1, 2));
				p_lo = _mm_shufflelo_epi16(p_lo, _MM_SHUFFLE(3, 0, 1, 2));
				p_lo = _mm_slli_epi16(p_lo, 1);
				p = _mm_packus_epi16(p_lo, p_hi);
				_mm_storeu_si128((__m128i*)(Ptr + j), p);
			}
			Ptr += w;
			src += pitch;
		}
	}
	else
	{
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j++)
			{
				TextureColor Src = src[j];
				Ptr->R = Src.B << 1;
				Ptr->G = Src.G << 1;
				Ptr->B = Src.R << 1;
				Ptr->A = Src.A << 1;
				Ptr++;
			}
			src += pitch;
		}
	}
#else
	int pitch = mip->Width;
	TextureColor* src = ((TextureColor*)mip->Data.data()) + x + y * pitch;
	auto Ptr = (TextureColor*)dst;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			TextureColor Src = src[j];
			Ptr->R = Src.B << 1;
			Ptr->G = Src.G << 1;
			Ptr->B = Src.R << 1;
			Ptr->A = Src.A << 1;
			Ptr++;
		}
		src += pitch;
	}
#endif
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_RGB10A2::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 8;
}

void TextureUploader_RGB10A2::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width;
	uint32_t* src = ((uint32_t*)mip->Data.data()) + x + y * pitch;
	uint16_t* Ptr = (uint16_t*)dst;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			uint32_t c = *Ptr;
			uint32_t r = (c >> 22) & 0x3ff;
			uint32_t g = (c >> 12) & 0x3ff;
			uint32_t b = (c >> 2) & 0x3ff;
			uint32_t a = c & 0x3;

			r = r * 0xffff / 0x3ff;
			g = g * 0xffff / 0x3ff;
			b = b * 0xffff / 0x3ff;
			a = a * 0xffff / 0x3;

			*(Ptr++) = r;
			*(Ptr++) = g;
			*(Ptr++) = b;
			*(Ptr++) = a;
		}
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_RGB10A2_UI::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 8;
}

void TextureUploader_RGB10A2_UI::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width;
	uint32_t* src = ((uint32_t*)mip->Data.data()) + x + y * pitch;
	uint16_t* Ptr = (uint16_t*)dst;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			uint32_t c = *Ptr;
			uint32_t r = (c >> 22) & 0x3ff;
			uint32_t g = (c >> 12) & 0x3ff;
			uint32_t b = (c >> 2) & 0x3ff;
			uint32_t a = c & 0x3;

			*(Ptr++) = r;
			*(Ptr++) = g;
			*(Ptr++) = b;
			*(Ptr++) = a;
		}
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_RGB10A2_LM::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 8;
}

void TextureUploader_RGB10A2_LM::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width;
	uint32_t* src = ((uint32_t*)mip->Data.data()) + x + y * pitch;
	uint16_t* Ptr = (uint16_t*)dst;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			uint32_t c = *Ptr;
			uint32_t r = (c >> 22) & 0x3ff;
			uint32_t g = (c >> 12) & 0x3ff;
			uint32_t b = (c >> 2) & 0x3ff;
			uint32_t a = c & 0x3;

			r = (r << 1) * 0xffff / 0xff;
			g = (g << 1) * 0xffff / 0xff;
			b = (b << 1) * 0xffff / 0xff;
			a = (a << 1) * 0xffff / 0x3;

			*(Ptr++) = r;
			*(Ptr++) = g;
			*(Ptr++) = b;
			*(Ptr++) = a;
		}
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_Simple::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * BytesPerPixel;
}

void TextureUploader_Simple::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width * BytesPerPixel;
	int size = w * BytesPerPixel;
	uint8_t* src = mip->Data.data() + x * BytesPerPixel + y * pitch;
	uint8_t* dst = (uint8_t*)d;
	for (int i = 0; i < h; i++)
	{
		memcpy(dst, src, size);
		dst += size;
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_4x4Block::GetUploadSize(int x, int y, int w, int h)
{
	int x0 = x / 4;
	int y0 = y / 4;
	int x1 = (x + w + 3) / 4;
	int y1 = (y + h + 3) / 4;
	return (x1 - x0) * (y1 - y0) * BytesPerBlock;
}

void TextureUploader_4x4Block::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int x0 = x / 4;
	int y0 = y / 4;
	int x1 = (x + w + 3) / 4;
	int y1 = (y + h + 3) / 4;

	int pitch = (mip->Width + 3) / 4 * BytesPerBlock;
	int size = (x1 - x0) * BytesPerBlock;
	uint8_t* src = mip->Data.data() + x0 * BytesPerBlock + y0 * pitch;
	uint8_t* dst = (uint8_t*)d;
	for (int i = y0; i < y1; i++)
	{
		memcpy(dst, src, size);
		dst += size;
		src += pitch;
	}
}


/////////////////////////////////////////////////////////////////////////////

int TextureUploader_2DBlock::GetUploadSize(int x, int y, int w, int h)
{
	int x0 = x / BlockX;
	int y0 = y / BlockY;
	int x1 = (x + w + BlockX - 1) / BlockX;
	int y1 = (y + h + BlockY - 1) / BlockY;
	return (x1 - x0) * (y1 - y0) * BytesPerBlock;
}

void TextureUploader_2DBlock::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int x0 = x / BlockX;
	int y0 = y / BlockY;
	int x1 = (x + w + BlockX - 1) / BlockX;
	int y1 = (y + h + BlockY - 1) / BlockY;

	int pitch = (mip->Width + BlockX - 1) / BlockX * BytesPerBlock;
	int size = (x1 - x0) * BytesPerBlock;
	uint8_t* src = mip->Data.data() + x0 * BytesPerBlock + y0 * pitch;
	uint8_t* dst = (uint8_t*)d;
	for (int i = y0; i < y1; i++)
	{
		memcpy(dst, src, size);
		dst += size;
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

// CPU decoders for GPUs that cannot sample a format natively. Each writes a
// simple unpacked format instead of the block compressed one.

static void DecodeAlphaBlock(const uint8_t* src, uint8_t out[16])
{
	uint8_t a0 = src[0];
	uint8_t a1 = src[1];
	uint8_t p[8];
	p[0] = a0;
	p[1] = a1;
	if (a0 > a1)
	{
		p[2] = (uint8_t)((6 * a0 + a1) / 7);
		p[3] = (uint8_t)((5 * a0 + 2 * a1) / 7);
		p[4] = (uint8_t)((4 * a0 + 3 * a1) / 7);
		p[5] = (uint8_t)((3 * a0 + 4 * a1) / 7);
		p[6] = (uint8_t)((2 * a0 + 5 * a1) / 7);
		p[7] = (uint8_t)((a0 + 6 * a1) / 7);
	}
	else
	{
		p[2] = (uint8_t)((4 * a0 + a1) / 5);
		p[3] = (uint8_t)((3 * a0 + 2 * a1) / 5);
		p[4] = (uint8_t)((2 * a0 + 3 * a1) / 5);
		p[5] = (uint8_t)((a0 + 4 * a1) / 5);
		p[6] = 0;
		p[7] = 255;
	}
	uint64_t idx = 0;
	for (int i = 0; i < 6; i++)
		idx |= (uint64_t)src[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
		out[i] = p[(idx >> (3 * i)) & 7];
}

static void DecodeColorBlock(const uint8_t* src, uint8_t* dst, int dstWidth, bool oneBitAlpha)
{
	uint16_t c0 = src[0] | (src[1] << 8);
	uint16_t c1 = src[2] | (src[3] << 8);
	uint32_t idx = src[4] | (src[5] << 8) | (src[6] << 16) | ((uint32_t)src[7] << 24);

	uint8_t r[4], g[4], b[4], a[4];
	r[0] = (uint8_t)(((c0 >> 11) << 3) | ((c0 >> 11) >> 2));
	g[0] = (uint8_t)((((c0 >> 5) & 63) << 2) | (((c0 >> 5) & 63) >> 4));
	b[0] = (uint8_t)(((c0 & 31) << 3) | ((c0 & 31) >> 2));
	r[1] = (uint8_t)(((c1 >> 11) << 3) | ((c1 >> 11) >> 2));
	g[1] = (uint8_t)((((c1 >> 5) & 63) << 2) | (((c1 >> 5) & 63) >> 4));
	b[1] = (uint8_t)(((c1 & 31) << 3) | ((c1 & 31) >> 2));

	if (oneBitAlpha && c0 <= c1)
	{
		// DXTC1 transparency: index 3 is a fully transparent texel.
		r[2] = (uint8_t)((r[0] + r[1]) / 2);
		g[2] = (uint8_t)((g[0] + g[1]) / 2);
		b[2] = (uint8_t)((b[0] + b[1]) / 2);
		a[2] = 255;
		r[3] = g[3] = b[3] = 0;
		a[3] = 0;
	}
	else
	{
		r[2] = (uint8_t)((2 * r[0] + r[1]) / 3);
		g[2] = (uint8_t)((2 * g[0] + g[1]) / 3);
		b[2] = (uint8_t)((2 * b[0] + b[1]) / 3);
		r[3] = (uint8_t)((r[0] + 2 * r[1]) / 3);
		g[3] = (uint8_t)((g[0] + 2 * g[1]) / 3);
		b[3] = (uint8_t)((b[0] + 2 * b[1]) / 3);
		a[2] = a[3] = 255;
	}
	a[0] = a[1] = 255;

	for (int i = 0; i < 16; i++)
	{
		int t = (idx >> (2 * i)) & 3;
		dst[i * 4 + 0] = r[t];
		dst[i * 4 + 1] = g[t];
		dst[i * 4 + 2] = b[t];
		dst[i * 4 + 3] = a[t];
	}
}

// Decode a block compressed mip range into dst (texels, row pitch w * dstbytes).
// x and y must be block aligned. outTexels gets one decoded texel per pixel.
template<int DstBytes, typename BlockToTexels>
static void UploadDecoded(void* d, UnrealMipmap* mip, int x, int y, int w, int h, int blockBytes, int blockX, int blockY, BlockToTexels blockToTexels)
{
	int bx0 = x / blockX;
	int by0 = y / blockY;
	int bx1 = (x + w + blockX - 1) / blockX;
	int by1 = (y + h + blockY - 1) / blockY;
	int blockCols = (mip->Width + blockX - 1) / blockX;
	int pitch = blockCols * blockBytes;
	uint8_t* dst = (uint8_t*)d;
	for (int by = by0; by < by1; by++)
	{
		for (int bx = bx0; bx < bx1; bx++)
		{
			const uint8_t* block = mip->Data.data() + ((size_t)by * blockCols + bx) * blockBytes;
			uint8_t texels[16 * 4]; // up to 4x4 RGBA8
			blockToTexels(block, texels);
			for (int ty = 0; ty < blockY; ty++)
			{
				int gy = by * blockY + ty - y;
				if (gy < 0 || gy >= h) continue;
				for (int tx = 0; tx < blockX; tx++)
				{
					int gx = bx * blockX + tx - x;
					if (gx < 0 || gx >= w) continue;
					memcpy(dst + ((size_t)gy * w + gx) * DstBytes, texels + ((size_t)ty * blockX + tx) * DstBytes, DstBytes);
				}
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_BC1_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_BC1_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	UploadDecoded<4>(d, mip, x, y, w, h, 8, 4, 4, [](const uint8_t* block, uint8_t* texels) {
		DecodeColorBlock(block, texels, 4, true);
	});
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_BC2_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_BC2_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	UploadDecoded<4>(d, mip, x, y, w, h, 16, 4, 4, [](const uint8_t* block, uint8_t* texels)
	{
		DecodeColorBlock(block + 8, texels, 4, false);
		// The first eight bytes hold four alpha bits per texel, low nibble first.
		for (int i = 0; i < 16; i++)
		{
			uint8_t bits = block[i / 2];
			texels[i * 4 + 3] = (i & 1) ? (bits >> 4) : (bits & 15);
			texels[i * 4 + 3] = (uint8_t)((texels[i * 4 + 3] << 4) | texels[i * 4 + 3]);
		}
	});
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_BC3_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_BC3_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	UploadDecoded<4>(d, mip, x, y, w, h, 16, 4, 4, [](const uint8_t* block, uint8_t* texels)
	{
		DecodeColorBlock(block + 8, texels, 4, false);
		uint8_t alpha[16];
		DecodeAlphaBlock(block, alpha);
		for (int i = 0; i < 16; i++)
			texels[i * 4 + 3] = alpha[i];
	});
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_BC4_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h;
}

void TextureUploader_BC4_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	UploadDecoded<1>(d, mip, x, y, w, h, 8, 4, 4, [](const uint8_t* block, uint8_t* texels)
	{
		DecodeAlphaBlock(block, texels);
	});
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_BC5_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 2;
}

void TextureUploader_BC5_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	UploadDecoded<2>(d, mip, x, y, w, h, 16, 4, 4, [](const uint8_t* block, uint8_t* texels)
	{
		uint8_t r[16], g[16];
		DecodeAlphaBlock(block, r);
		DecodeAlphaBlock(block + 8, g);
		for (int i = 0; i < 16; i++)
		{
			texels[i * 2 + 0] = r[i];
			texels[i * 2 + 1] = g[i];
		}
	});
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_RGB8_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_RGB8_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width * 3;
	uint8_t* src = mip->Data.data() + (size_t)x * 3 + (size_t)y * pitch;
	uint8_t* dst = (uint8_t*)d;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			dst[j * 4 + 0] = src[j * 3 + 0];
			dst[j * 4 + 1] = src[j * 3 + 1];
			dst[j * 4 + 2] = src[j * 3 + 2];
			dst[j * 4 + 3] = 255;
		}
		dst += (size_t)w * 4;
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int TextureUploader_RGBA32F_Decode::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void TextureUploader_RGBA32F_Decode::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked)
{
	int pitch = mip->Width * 16;
	float* src = (float*)(mip->Data.data() + (size_t)x * 16 + (size_t)y * pitch);
	uint8_t* dst = (uint8_t*)d;
#ifdef USE_NEON
	// Every lightmap rebuilt goes through here, several a frame on the
	// handheld. The same clamp, scale and truncation, four channels at once:
	// checked on the device against the loop below for every float from 0 to
	// 1 and for negatives, overflows, infinities and NaNs -- all the same.
	const float32x4_t zero = vdupq_n_f32(0.0f), one = vdupq_n_f32(1.0f), scale = vdupq_n_f32(255.0f), half = vdupq_n_f32(0.5f);
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			float32x4_t c = vld1q_f32(src + (size_t)j * 4);
			uint32x4_t lt = vcltq_f32(c, zero), gt = vcgtq_f32(c, one);
			c = vbslq_f32(lt, zero, vbslq_f32(gt, one, c));
			uint32x4_t u = vcvtq_u32_f32(vaddq_f32(vmulq_f32(c, scale), half));
			uint16x4_t u16 = vmovn_u32(u);
			uint8x8_t u8 = vmovn_u16(vcombine_u16(u16, u16));
			vst1_lane_u32((uint32_t*)(dst + (size_t)j * 4), vreinterpret_u32_u8(u8), 0);
		}
		dst += (size_t)w * 4;
		src = (float*)((uint8_t*)src + pitch);
	}
#else
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			const float* c = src + (size_t)j * 4;
			for (int ch = 0; ch < 4; ch++)
			{
				float v = c[ch];
				v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
				dst[j * 4 + ch] = (uint8_t)(v * 255.0f + 0.5f);
			}
		}
		dst += (size_t)w * 4;
		src = (float*)((uint8_t*)src + pitch);
	}
#endif
}
