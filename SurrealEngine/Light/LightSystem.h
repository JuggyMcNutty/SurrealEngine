#pragma once

#include "Math/vec.h"
#include "LightActorTree.h"
#include "LightmapBuilder.h"
#include "Packages/Engine/Resources/Textures/UBitmap.h"
#include <unordered_map>
#include <list>

#ifdef USE_SSE2
#include <immintrin.h>
#endif

class ULevel;
class UActor;
class CollisionHitList;
class CollisionHit;
struct TextureInfo;
class UMover;

struct LightmapTexture
{
	TextureFormat Format;
	UnrealMipmap Mip;
	int LastStaticUpdate = -1;
	int LastDynamicUpdate = -1;
	Array<vec3> StaticLightColors;
};

class VertexLight
{
public:
	vec3 GetVertexLight(const vec3& location, const vec3& normal, bool unlit, bool twosided)
	{
		if (OriginalFormula)
			return GetVertexLightDX(location, normal, unlit, twosided);

		vec3 dynamicLight(0.0f);
		if (!unlit)
		{
			for (int i = 0, count = NumLights; i < count; i++)
			{
				vec3 L = Lights[i].Location - location;

				// Distance falloff
				float lensqr = dot(L, L);
				float distsqr = lensqr * Lights[i].InvRadiusSquared;
				if (distsqr < 1.0f)
				{
					float attenuation = LightDistanceFalloff(distsqr);

					// Diffuse light contribution
					float rcplen = 1.0f / std::sqrt(lensqr);
					L *= rcplen;
					float d = dot(L, normal);
					attenuation *= twosided ? std::abs(d) : std::max(d, 0.0f);
					dynamicLight += Lights[i].Color * attenuation;
				}

			}
			dynamicLight *= ScaleGlow;
		}
		else
		{
			dynamicLight = vec3(0.5f);
		}

		// Clamp final result and make it all brighter to match lightmaps
		vec3 color = (AmbientColor + dynamicLight) * 3.0f;
		color.r = std::min(color.r, 1.0f);
		color.g = std::min(color.g, 1.0f);
		color.b = std::min(color.b, 1.0f);
		return color;
	}

	// The original's per-vertex formula (docs/re/render-dll.md, lighting):
	// a diffuse term, (cos + 1)^2 - 1.5 of the angle to the light, and a
	// highlight, 6 cos^2 of the angle between the eye and the light's
	// reflection when it heads toward the eye, both times 1 - d/r and the
	// light's colour; the sum scaled by 1.4 ScaleGlow, the ambient added,
	// each channel at most 1. An unlit draw is mid-grey.
	vec3 GetVertexLightDX(const vec3& location, const vec3& normal, bool unlit, bool twosided)
	{
		if (unlit)
			return vec3(0.5f);

		vec3 sum(0.0f);
		vec3 eye = CameraLocation - location;
		float eyeLen2 = dot(eye, eye);
		vec3 eyeDir = eyeLen2 > 0.0f ? eye / std::sqrt(eyeLen2) : vec3(0.0f);
		for (int i = 0, count = NumLights; i < count; i++)
		{
			vec3 L = Lights[i].Location - location;
			float lensqr = dot(L, L);
			if (lensqr <= 0.0f)
				continue;
			float dist = std::sqrt(lensqr);
			float attenuation = 1.0f - dist * Lights[i].InvRadius;
			if (attenuation <= 0.0f)
				continue;
			vec3 Ldir = L / dist;
			float c = dot(Ldir, normal);
			if (twosided)
				c = std::abs(c);
			float diffuse = (c + 1.0f) * (c + 1.0f) - 1.5f;
			if (diffuse < 0.0f)
				diffuse = 0.0f;
			float highlight = 0.0f;
			float ce = dot(reflect(-Ldir, normal), eyeDir);
			if (ce > 0.0f)
				highlight = 6.0f * ce * ce;
			if (diffuse + highlight > 0.0f)
				sum += Lights[i].Color * ((diffuse + highlight) * attenuation);
		}
		vec3 color = sum * (1.4f * ScaleGlow) + AmbientColor;
		color.r = std::min(color.r, 1.0f);
		color.g = std::min(color.g, 1.0f);
		color.b = std::min(color.b, 1.0f);
		return color;
	}

	static float LightDistanceFalloff(float distsqr)
	{
		float v = std::sqrt(distsqr + 0.0001f);
		float v2 = v * v;
		float v3 = v2 * v;
		return std::min((1.0f + 2.0f * v3 - 3.0f * v2) / v, 1.0f);
	}

	vec4 GetVertexFog(const vec3& location)
	{
		vec4 color(0.0f);
		if (NumFogBalls == 0)
			return color;
#ifndef NOFOG
		vec3 view = CameraLocation;
		vec3 rayDirection = location - view;
#ifndef NOSSE
		float depth = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(dot(rayDirection, rayDirection))));
#else
		float depth = std::sqrt(dot(rayDirection, rayDirection));
#endif
		rayDirection *= (1.0f / depth);

		for (int i = 0, count = NumFogBalls; i < count; i++)
		{
			float fogamount = SphereDensity(view, rayDirection, FogBalls[i].lightpos, FogBalls[i].radius, depth) * FogBalls[i].brightness;
			float alpha = std::min(fogamount * FogBalls[i].fog, 1.0f);
			float invalpha = 1.0f - alpha;
			color.r = FogBalls[i].fogcolor.r * fogamount + color.r * invalpha;
			color.g = FogBalls[i].fogcolor.g * fogamount + color.g * invalpha;
			color.b = FogBalls[i].fogcolor.b * fogamount + color.b * invalpha;
			color.a = std::min(color.a + alpha, 1.0f);
		}
#endif
		return color;
	}

	static float SphereDensity(const vec3& rayOrigin, const vec3& rayDirection, const vec3& sphereCenter, float sphereRadius, float dbuffer)
	{
		// normalize the problem to the canonical sphere
		float rcp_radius = 1.0f / sphereRadius;
		float ndbuffer = dbuffer * rcp_radius;
		//vec3 rc = (rayOrigin - sphereCenter) * rcp_radius;
		float rc_x = (rayOrigin.x - sphereCenter.x) * rcp_radius;
		float rc_y = (rayOrigin.y - sphereCenter.y) * rcp_radius;
		float rc_z = (rayOrigin.z - sphereCenter.z) * rcp_radius;

		// find intersection with sphere
		float b = rayDirection.x * rc_x + rayDirection.y * rc_y + rayDirection.z * rc_z; // dot(rayDirection, rc)
		float c = rc_x * rc_x + rc_y * rc_y + rc_z * rc_z - 1.0f; // dot(rc, rc) - 1.0f
		float h = b * b - c;

		// not intersecting
		if (h < 0.0f) return 0.0f;

#ifndef NOSSE
		h = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(h)));
#else
		h = std::sqrt(h);
#endif

		//return h*h*h;

		float t1 = -b - h;
		float t2 = -b + h;

		// not visible (behind camera or behind ndbuffer)
		if (t2 < 0.0f || t1 > ndbuffer) return 0.0f;

		// clip integration segment from camera to ndbuffer
		t1 = std::max(t1, 0.0f);
		t2 = std::min(t2, ndbuffer);

		// analytical integration of an inverse squared density
		float i1 = -(c * t1 + b * t1 * t1 + t1 * t1 * t1 * (1.0f / 3.0f));
		float i2 = -(c * t2 + b * t2 * t2 + t2 * t2 * t2 * (1.0f / 3.0f));
		return (i2 - i1) * (3.0f / 4.0f);
	}

	// Light:

	vec3 AmbientColor;
	float ScaleGlow;
	bool OriginalFormula = false;
	enum { MaxLights = 16, MaxFogBalls = 4 };
	struct Light
	{
		vec3 Location;
		float InvRadiusSquared;
		float InvRadius;
		vec3 Color;
	} Lights[MaxLights];
	int NumLights;

	// Fog:

	vec3 CameraLocation;
	struct FogBall
	{
		vec3 fogcolor;
		float brightness;
		float fog;
		float radius;
		vec3 lightpos;
	} FogBalls[MaxFogBalls];
	int NumFogBalls;
};

class LightSystem
{
public:
	LightSystem();
	~LightSystem();

	void Tick(float levelTimeElapsed);

	void OnMapLoaded();

	void BeginFrame();
	void UpdateLightList(UActor* actor);

	void SetLevel(ULevel* level);

	TextureInfo GetMoverLightmap(UMover* mover, const Poly& poly, UZoneInfo* zoneActor, UModel* model);
	TextureInfo GetLevelLightmap(BspSurface& surface, UZoneInfo* zoneActor, UModel* model);

	TextureInfo GetMoverFogmap(UMover* mover, const Poly& poly, UZoneInfo* zoneActor, UModel* model);
	TextureInfo GetLevelFogmap(BspSurface& surface, UZoneInfo* zoneActor, UModel* model);

	void InitVertexLight(VertexLight& vertexlight, UActor* actor, UZoneInfo* zoneActor);
	void SetupForActorDX(VertexLight& out, UActor* actor);

	// The level's lights whose reach may take in a location, as of the last
	// frame drawn: BeginFrame builds their tree from each actor with a light
	// type and a brightness. The list lasts until the next call.
	const Array<UActor*>& LightsNear(const vec3& location) { LightTree.CollectLights(location, 0.0f); return LightTree.CollectedLights; }

private:
	TextureInfo GetLightmap(UModel* model, int lightmapIndex, const Coords& coords, UZoneInfo* zoneActor, const vec3& worldLocation, float radius, UMover* mover, bool specialLit);
	TextureInfo GetFogmap(UModel* model, int lightmapIndex, const Coords& coords, UZoneInfo* zoneActor);
	void CheckLight(UActor* light);
	const Array<UActor*>& CollectSurfaceLights(UModel* model, int lightmapIndex, const vec3& center, float radius);
	void UpdateFogmapTexture(uint32_t* texels, UModel* model, const Coords& mapCoords, int lightMap, UZoneInfo* zoneActor);

	ULevel* Level = nullptr;

	LightmapBuilder Builder;
	std::map<uint64_t, std::unique_ptr<LightmapTexture>> lmtextures;
	std::map<uint64_t, std::pair<int, std::unique_ptr<LightmapTexture>>> fogtextures;
	Array<UActor*> FogBalls;
	int FrameCounter = 0;
	int LightmapCheckCounter = 0;
	int NextMoverID = 1;

	float AmbientGlowTime = 0.0f;
	float AmbientGlowAmount = 0.0f;
	float LastTickElapsed = 0.0f;

	Array<UActor*> TempDynLightList;
	LightActorTree LightTree;

	// What the light tree was last built from: each light, where it is and
	// how far it reaches, in order. While that stays the same, so do the tree
	// and its every answer; LightTreeVersion counts the builds.
	struct TreeLight
	{
		UActor* Actor;
		vec3 Location;
		float Radius;
	};
	Array<TreeLight> TreeLights;
	uint64_t LightTreeVersion = 0;

	// Each lightmap's lights from the tree, as of the version they were found in
	struct SurfaceLights
	{
		uint64_t Version = 0;
		vec3 Center = vec3(0.0f);
		float Radius = 0.0f;
		Array<UActor*> Lights;
	};
	Array<SurfaceLights> SurfaceLightCache;
	UModel* SurfaceLightCacheModel = nullptr;
};
