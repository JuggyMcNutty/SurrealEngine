
#include "Precomp.h"
#include "LightmapBuilder.h"
#include "Engine.h"
#include "Packages/Engine/USurrealClient.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Core/UClass.h"
#include "RenderDevice/RenderDevice.h"
#include "Math/hsb.h"
#include <cstring>

#ifdef USE_SSE2
#include <immintrin.h>
#endif

void LightmapBuilder::Setup(UModel* model, const Coords& mapCoords, int lightMap)
{
	const LightMapIndex& lmindex = model->LightMap[lightMap];

	width = lmindex.UClamp;
	height = lmindex.VClamp;
	normal = normalize(mapCoords.ZAxis);
	base = mapCoords.Origin;

	// Stop allocations over time by building up a reserve

	size_t size = (size_t)width * height;
	if (points.size() < size)
		points.resize(size);
	if (lightcolors.size() < size)
		lightcolors.resize(size);
	if (illuminationmap.size() < size)
		illuminationmap.resize(size);

	CalcWorldLocations(mapCoords, lmindex);
}

void LightmapBuilder::SetAmbientLight(UZoneInfo* zoneActor)
{
	// Initialize lightmap with the ambient color

	vec3 ambientColor = hsbtorgb(zoneActor->AmbientHue(), zoneActor->AmbientSaturation(), zoneActor->AmbientBrightness()); // To do: is this the correct scale?
	// To do: is there more ambient light than just from the zone?

	for (vec3& c : lightcolors)
		c = ambientColor;

	// To do: how does polyflags affect the lightmap (if at all)?

	//bool isSpecialLit = (surface.PolyFlags & PF_SpecialLit) == PF_SpecialLit;
	//bool isTranslucent = (surface.PolyFlags & PF_Translucent) == PF_Translucent;
}

void LightmapBuilder::LoadStaticLight(const Array<vec3>& staticLightColors)
{
	if (lightcolors.size() < staticLightColors.size())
		lightcolors.resize(staticLightColors.size());
	std::memcpy(lightcolors.data(), staticLightColors.data(), staticLightColors.size() * sizeof(vec3));
}

void LightmapBuilder::SaveStaticLight(Array<vec3>& staticLightColors)
{
	staticLightColors.resize(width * height);
	std::memcpy(staticLightColors.data(), lightcolors.data(), width * height * sizeof(vec3));
}

void LightmapBuilder::AddStaticLights(UModel* model, int lightMap)
{
	size_t count = (size_t)width * height;

	const LightMapIndex& lmindex = model->LightMap[lightMap];
	if (lmindex.LightActors >= 0)
	{
		UActor** lightlist = &model->Lights[lmindex.LightActors];
		for (int lightindex = 0; lightlist[lightindex] != nullptr; lightindex++)
		{
			UActor* light = lightlist[lightindex];
			if (light->LightType() != LT_None && light->LightBrightness() > 0)
			{
				// An animating light is added over the kept static map
				// each frame instead (AddAnimatedLights), unless the
				// client's NoDynamicLights stills it into the map here
				if (LightAnimates(light) && !engine->client->NoDynamicLights)
					continue;
				FindLitSpans(light);
				if (spans.empty())
					continue;
				Shadow.Load(model, lightMap, lightindex);
				Effect.Run(light, width, spans, WorldLocations(), base, WorldNormal(), Shadow.Pixels(), illuminationmap.data());
				AddLightContribution(light);
			}
		}
	}
}

void LightmapBuilder::AddAnimatedLights(UModel* model, int lightMap, const Array<int>& lightIndices)
{
	// The surface's own animating lights, each over its shadow bits, added
	// to the loaded static map every frame
	const LightMapIndex& lmindex = model->LightMap[lightMap];
	if (lmindex.LightActors < 0)
		return;
	UActor** lightlist = &model->Lights[lmindex.LightActors];
	for (int lightindex : lightIndices)
	{
		UActor* light = lightlist[lightindex];
		if (light->LightType() != LT_None && light->LightBrightness() > 0)
		{
			FindLitSpans(light);
			if (spans.empty())
				continue;
			Shadow.Load(model, lightMap, lightindex);
			Effect.Run(light, width, spans, WorldLocations(), base, WorldNormal(), Shadow.Pixels(), illuminationmap.data());
			AddLightContribution(light);
		}
	}
}

bool LightmapBuilder::LightAnimates(UActor* light)
{
	uint8_t lightType = light->LightType();
	if (lightType > LT_Steady && lightType != LT_BackdropLight)
		return true;

	switch (light->LightEffect())
	{
	case LE_Searchlight:
		return light->LightPeriod() != 0;
	case LE_TorchWaver:
	case LE_FireWaver:
	case LE_WateryShimmer:
	case LE_SlowWave:
	case LE_FastWave:
	case LE_Shock:
	case LE_Disco:
	case LE_Interference:
	case LE_Rotor:
		return true;
	default:
		return false;
	}
}

void LightmapBuilder::AddDynamicLights(UModel* model, int lightMap, const Array<UActor*>& lights)
{
	size_t count = (size_t)width * height;
	Shadow.Clear(model, lightMap);
	for (UActor* light : lights)
	{
		if (light->LightType() != LT_None && light->LightBrightness() > 0)
		{
			FindLitSpans(light);
			Effect.Run(light, width, spans, WorldLocations(), base, WorldNormal(), Shadow.Pixels(), illuminationmap.data());
			AddLightContribution(light);
		}
	}
}

void LightmapBuilder::FindLitSpans(UActor* light)
{
	spans.clear();

	// A cylinder light reaches any height, so its reach is not a sphere
	if (width < 2 || light->LightEffect() == LE_Cylinder)
	{
		for (int y = 0; y < height; y++)
			spans.push_back({ y, 0, width });
		return;
	}

	// Each row of texel positions is a line, p(x) = row[0] + x * D, D the step
	// from one texel to the next (CalcWorldLocations interpolates between the
	// row's ends, so D is taken over the whole row, not from two neighbours).
	// The texels within the radius are where |p(x) - light|^2 < radius^2; one
	// texel of margin on each side covers the rounding in the positions.
	vec3 lightLocation = light->Location();
	float radius = light->WorldLightRadius();
	float radiusSquared = radius * radius;
	for (int y = 0; y < height; y++)
	{
		const vec3* row = &points[y * width];
		vec3 P = row[0] - lightLocation;
		vec3 D = (row[width - 1] - row[0]) * (1.0f / (width - 1));
		float dd = dot(D, D), pd = dot(P, D), pp = dot(P, P);
		if (!(dd > 0.0f))
		{
			if (pp < radiusSquared)
				spans.push_back({ y, 0, width });
			continue;
		}
		float disc = pd * pd - dd * (pp - radiusSquared);
		if (!(disc > 0.0f))
			continue;
		float s = std::sqrt(disc);
		float x0 = std::floor((-pd - s) / dd) - 1.0f;
		float x1 = std::ceil((-pd + s) / dd) + 2.0f;
		int ix0 = (int)std::clamp(x0, 0.0f, (float)width);
		int ix1 = (int)std::clamp(x1, 0.0f, (float)width);
		if (ix0 < ix1)
			spans.push_back({ y, ix0, ix1 });
	}
}

void LightmapBuilder::AddLightContribution(UActor* light)
{
	vec3 lightcolor = GetLightColor(light);
	for (const LightmapSpan& span : spans)
	{
		int offset = span.y * width + span.x0;
		AddLightContribution(lightcolor, illuminationmap.data() + offset, (float*)(lightcolors.data() + offset), span.x1 - span.x0);
	}
}

void LightmapBuilder::AddLightContribution(const vec3& lightcolor, const float* src, float* dest, int size)
{
	float lightcolorR = lightcolor.r;
	float lightcolorG = lightcolor.g;
	float lightcolorB = lightcolor.b;

#ifdef USE_SSE2
	__m128 mmlightcolor0 = _mm_setr_ps(lightcolorR, lightcolorG, lightcolorB, lightcolorR);
	__m128 mmlightcolor1 = _mm_setr_ps(lightcolorG, lightcolorB, lightcolorR, lightcolorG);
	__m128 mmlightcolor2 = _mm_setr_ps(lightcolorB, lightcolorR, lightcolorG, lightcolorB);
	int sse_size = size / 4 * 4;
	for (size_t i = 0; i < sse_size; i += 4)
	{
		// To do: memory align buffers
		__m128 s = _mm_loadu_ps(src);
		__m128 s0 = _mm_shuffle_ps(s, s, _MM_SHUFFLE(1, 0, 0, 0));
		__m128 s1 = _mm_shuffle_ps(s, s, _MM_SHUFFLE(2, 2, 1, 1));
		__m128 s2 = _mm_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 2));
		__m128 one = _mm_set_ps1(1.0f);
		_mm_storeu_ps(dest, _mm_min_ps(_mm_add_ps(_mm_loadu_ps(dest), _mm_mul_ps(s0, mmlightcolor0)), one));
		_mm_storeu_ps(dest + 4, _mm_min_ps(_mm_add_ps(_mm_loadu_ps(dest + 4), _mm_mul_ps(s1, mmlightcolor1)), one));
		_mm_storeu_ps(dest + 8, _mm_min_ps(_mm_add_ps(_mm_loadu_ps(dest + 8), _mm_mul_ps(s2, mmlightcolor2)), one));
		src += 4;
		dest += 3 * 4;
	}
#else
	int sse_size = 0;
#endif
	for (size_t i = sse_size; i < size; i++)
	{
		float s = *src;
		float r = s * lightcolorR;
		float g = s * lightcolorG;
		float b = s * lightcolorB;
		r = r <= 1.0f ? r : 1.0f;
		g = g <= 1.0f ? g : 1.0f;
		b = b <= 1.0f ? b : 1.0f;
		dest[0] += r;
		dest[1] += g;
		dest[2] += b;
		src++;
		dest += 3;
	}
}

vec3 LightmapBuilder::GetLightColor(UActor* light)
{
	constexpr float phaseScale = (1.0f / 255.0f);
	constexpr float periodSpeed = 40.0f;
	constexpr float turnsToRadians = 2.0f * 3.14159265359f;
	constexpr float strobeSpeed = 10.0f;
	switch (light->LightType())
	{
	default:
	case LT_Steady:
	case LT_BackdropLight:
		return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
	case LT_Pulse:
	{
		float pulseTurns = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		float pulse = std::sin(pulseTurns * turnsToRadians);
		float brightness = light->LightBrightness() * (0.65f + 0.35f * pulse);
		return hsbtorgb(light->LightHue(), light->LightSaturation(), (uint8_t)std::clamp(brightness, 0.0f, 255.0f));
	}
	case LT_SubtlePulse:
	{
		float pulseTurns = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		float pulse = std::sin(pulseTurns * turnsToRadians);
		float brightness = light->LightBrightness() * (0.8f + 0.2f * pulse);
		return hsbtorgb(light->LightHue(), light->LightSaturation(), (uint8_t)std::clamp(brightness, 0.0f, 255.0f));
	}
	case LT_Blink:
		if (std::fmod(light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1), 2.0f) < 1.0f)
			return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
		else
			return vec3(0.0f);
	case LT_Strobe:
		if (std::fmod(light->Level()->TimeSeconds() * strobeSpeed, 2.0f) < 1.0f)
			return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
		else
			return vec3(0.0f);
	case LT_Flicker:
		if (light->Light.FlickerRandom)
			return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
		else
			return vec3(0.0f);
	case LT_TexturePaletteOnce:
	{
		if (light->LifeSpan() <= 0.0f || !light->Skin() || !light->Skin()->Palette())
			return vec3(0.0f);

		float t = light->LifeSpan() / light->Class->GetDefaultObject<UActor>()->LifeSpan();
		UPalette* palette = light->Skin()->Palette();
		if (palette->Colors.empty())
			return vec3(0.0f);
		uint32_t color = palette->Colors[(int)(t * palette->Colors.size())];
		return vec3(
			((color >> 16) & 0xff) * (1.0f / 255.0f),
			((color >> 8) & 0xff) * (1.0f / 255.0f),
			(color & 0xff) * (1.0f / 255.0f));
	}
	case LT_TexturePaletteLoop:
	{
		if (light->LifeSpan() <= 0.0f || !light->Skin() || !light->Skin()->Palette())
			return vec3(0.0f);

		float t = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		t -= std::floor(t);

		UPalette* palette = light->Skin()->Palette();
		if (palette->Colors.empty())
			return vec3(0.0f);
		uint32_t color = palette->Colors[(int)(t * palette->Colors.size())];
		return vec3(
			((color >> 16) & 0xff) * (1.0f / 255.0f),
			((color >> 8) & 0xff) * (1.0f / 255.0f),
			(color & 0xff) * (1.0f / 255.0f));
	}
	}
}

void LightmapBuilder::CalcWorldLocations(Coords MapCoords, const LightMapIndex& lmindex)
{
	// Note: this could be simplified a lot for better performance

	// Allow optimizer to move them into registers
	int width = this->width;
	int height = this->height;

	float UDot = dot(MapCoords.XAxis, MapCoords.Origin);
	float VDot = dot(MapCoords.YAxis, MapCoords.Origin);
	float LMUPan = UDot + lmindex.PanX - 0.5f * lmindex.UScale;
	float LMVPan = VDot + lmindex.PanY - 0.5f * lmindex.VScale;
	float LMUMult = 1.0f / lmindex.UScale;
	float LMVMult = 1.0f / lmindex.VScale;

	vec3 p[3] =
	{
		MapCoords.Origin,
		MapCoords.Origin + MapCoords.XAxis,
		MapCoords.Origin + MapCoords.YAxis
	};

	vec2 uv[3];
	for (int j = 0; j < 3; j++)
	{
		uv[j] =
		{
			(dot(MapCoords.XAxis, p[j]) - LMUPan) * LMUMult,
			(dot(MapCoords.YAxis, p[j]) - LMVPan) * LMVMult
		};
	}

	float leftDX = uv[2].x - uv[0].x;
	float leftDY = uv[2].y - uv[0].y;
	float leftStep = leftDX / leftDY;
	float rightDX = uv[2].x - uv[1].x;
	float rightDY = uv[2].y - uv[1].y;
	float rightStep = rightDX / rightDY;

	for (int y = 0; y < height; y++)
	{
		float x0 = uv[0].x + leftStep * (y + 0.5f - uv[0].y) + 0.5f;
		float x1 = uv[1].x + rightStep * (y + 0.5f - uv[1].y) + 0.5f;
		float t0 = (y + 0.5f - uv[0].y) / leftDY;
		float t1 = (y + 0.5f - uv[1].y) / rightDY;
		vec3 p0 = mix(p[0], p[2], t0);
		vec3 p1 = mix(p[1], p[2], t1);
		if (x1 < x0)
		{
			std::swap(x0, x1);
			std::swap(p0, p1);
		}

		vec3* dest = &points[y * width];
		for (int i = 0; i < width; i++)
		{
			float t = (i + 0.5f - x0) / (x1 - x0);
			dest[i] = mix(p0, p1, t);
		}
	}
}
