
#include "Precomp.h"
#include "LightSystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Engine.h"
#include "Packages/Engine/USurrealClient.h"
#include "Render/RenderSubsystem.h"
#include "Math/hsb.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Packages/Engine/Resources/Level/UModel.h"

TextureInfo LightSystem::GetMoverLightmap(UMover* mover, const Poly& poly, UZoneInfo* zoneActor, UModel* model)
{
	Coords localCoords;
	localCoords.Origin = -poly.Base;
	localCoords.XAxis = poly.TextureU;
	localCoords.YAxis = poly.TextureV;
	localCoords.ZAxis = poly.Normal;

	vec3 scale = mover->MainScale().Scale;

	mat4 objectToWorld;
	if (mover->bDynamicLightMover())
	{
		vec3 moverLocation = mover->Location();
		Rotator moverRotation = mover->Rotation();
		objectToWorld = mat4::translate(moverLocation) * Coords::Rotation(moverRotation).ToMatrix() * mat4::scale(scale) * mat4::translate(-mover->PrePivot()) * localCoords.ToMatrix();
	}
	else
	{
		vec3 moverLocation = mover->BasePos() + mover->KeyPos()[mover->BrushRaytraceKey()];
		Rotator moverRotation = mover->BaseRot() + mover->KeyRot()[mover->BrushRaytraceKey()];
		objectToWorld = mat4::translate(moverLocation) * Coords::Rotation(moverRotation).ToMatrix() * mat4::scale(scale) * mat4::translate(-mover->PrePivot()) * localCoords.ToMatrix();
	}
	Coords worldCoords = Coords::FromMatrix(objectToWorld);

	if (!mover->Light.Calculated)
	{
		vec3 location = { 0.0f };
		float radius = 0.0f;
		if (!poly.Vertices.empty())
		{
			vec3 aabbMin = poly.Vertices.front();
			vec3 aabbMax = aabbMin;
			for (const vec3& v : poly.Vertices)
			{
				aabbMin.x = std::min(aabbMin.x, v.x);
				aabbMin.y = std::min(aabbMin.y, v.y);
				aabbMin.z = std::min(aabbMin.z, v.z);
				aabbMax.x = std::max(aabbMax.x, v.x);
				aabbMax.y = std::max(aabbMax.y, v.y);
				aabbMax.z = std::max(aabbMax.z, v.z);
			}
			auto halfmin = aabbMin * 0.5f;
			auto halfmax = aabbMax * 0.5f;
			location = halfmax + halfmin;
			radius = length(halfmax - halfmin);
		}
		mover->Light.Calculated = true;
		mover->Light.Center = location;
		mover->Light.Radius = radius;
	}

	return GetLightmap(
		model, poly.BrushPolyIndex, worldCoords, zoneActor,
		(objectToWorld * vec4(mover->Light.Center, 1.0f)).xyz(),
		mover->Light.Radius * std::max(scale.x, std::max(scale.y, scale.z)),
		mover->bDynamicLightMover() ? mover : nullptr, mover->bSpecialLit());
}

TextureInfo LightSystem::GetLevelLightmap(BspSurface& surface, UZoneInfo* zoneActor, UModel* model)
{
	Coords mapCoords;
	mapCoords.Origin = model->Points[surface.pBase];
	mapCoords.XAxis = model->Vectors[surface.vTextureU];
	mapCoords.YAxis = model->Vectors[surface.vTextureV];
	mapCoords.ZAxis = model->Vectors[surface.vNormal];
	return GetLightmap(model, surface.LightMap, mapCoords, zoneActor, surface.Center, surface.Radius, nullptr, surface.PolyFlags & PF_SpecialLit);
}

// The tree's lights for a lightmap's surface. Every visible surface asked
// the tree again every frame (~6 ms a frame on the handheld); its answer is
// now kept until the tree is rebuilt, or the surface's sphere differs.
const Array<UActor*>& LightSystem::CollectSurfaceLights(UModel* model, int lightmapIndex, const vec3& center, float radius)
{
	if (SurfaceLightCacheModel != model)
	{
		SurfaceLightCache.clear();
		SurfaceLightCacheModel = model;
	}
	if (SurfaceLightCache.size() <= (size_t)lightmapIndex)
		SurfaceLightCache.resize(std::max(model->LightMap.size(), (size_t)lightmapIndex + 1));

	SurfaceLights& entry = SurfaceLightCache[lightmapIndex];
	if (entry.Version != LightTreeVersion || entry.Center != center || entry.Radius != radius)
	{
		LightTree.CollectLights(center, radius);
		entry.Lights = LightTree.CollectedLights;
		entry.Version = LightTreeVersion;
		entry.Center = center;
		entry.Radius = radius;
	}
	return entry.Lights;
}

TextureInfo LightSystem::GetLightmap(UModel* model, int lightmapIndex, const Coords& coords, UZoneInfo* zoneActor, const vec3& worldLocation, float radius, UMover* dynamicMover, bool specialLit)
{
	if (lightmapIndex < 0)
		return {};

	LightMapIndex& lmindex = model->LightMap[lightmapIndex];

	uint32_t ambientID = (((uint32_t)zoneActor->AmbientHue()) << 16) | (((uint32_t)zoneActor->AmbientSaturation()) << 8) | (uint32_t)zoneActor->AmbientBrightness();
	uint64_t cacheID = (((uint64_t)lmindex.LMCacheID) << 32) | (((uint64_t)ambientID) << 8) | 1;

	if (dynamicMover)
	{
		// Place mover ID in upper 12 bits (4096 movers). This leaves 20 bits (1 million) for the lightmaps.
		if (dynamicMover->Light.MoverID == 0)
			dynamicMover->Light.MoverID = NextMoverID++;
		cacheID |= ((uint64_t)dynamicMover->Light.MoverID) << 52;
	}

	int checkCounter = LightmapCheckCounter++;

	// Collect lights for the lightmap and check if they changed

	int lastStaticUpdate = -100;
	int lastDynamicUpdate = -100;
	bool noDynamicLights = engine->client->NoDynamicLights;
	TempDynLightList.clear();
	TempAnimatedIndexList.clear();

	if (!dynamicMover)
	{
		// The surface's own lights: the still ones are the kept static
		// map's; an animating one is added over it every frame, through
		// its shadow bits (docs/re/render-dll.md, light maps). With
		// NoDynamicLights, animated lights count as static and moving
		// ones are left out.

		if (lmindex.LightActors >= 0)
		{
			UActor** lightlist = &model->Lights[lmindex.LightActors];
			for (int lightindex = 0; lightlist[lightindex] != nullptr; lightindex++)
			{
				UActor* light = lightlist[lightindex];
				CheckLight(light);
				if (!noDynamicLights && LightmapBuilder::LightAnimates(light))
				{
					TempAnimatedIndexList.push_back(lightindex);
					lastDynamicUpdate = FrameCounter;
				}
				else
				{
					lastStaticUpdate = std::max(lastStaticUpdate, light->Light.LastUpdate);
				}

				// Mark the light as visited so the second pass doesn't pick it up
				light->Light.LightmapCheckCounter = checkCounter;
			}
		}

		// The moving lights over the surface go into the dynamic light list:

		if (!noDynamicLights)
		{
			for (UActor* light : CollectSurfaceLights(model, lightmapIndex, worldLocation, radius))
			{
				if (light->Light.LightmapCheckCounter != checkCounter)
				{
					light->Light.LightmapCheckCounter = checkCounter;
					if (!light->bStatic() && !light->bNoDelete() && light->bSpecialLit() == specialLit)
					{
						CheckLight(light);
						lastDynamicUpdate = std::max(lastDynamicUpdate, light->Light.LastUpdate);
						TempDynLightList.push_back(light);
					}
				}
			}
		}
	}
	else
	{
		// A mover's maps are rebuilt when the mover moved or turned
		// since, or one of its lights changed
		if (!dynamicMover->Light.HasLastTransform ||
			dynamicMover->Light.LastLocation != dynamicMover->Location() ||
			dynamicMover->Light.LastRotation != dynamicMover->Rotation())
		{
			dynamicMover->Light.HasLastTransform = true;
			dynamicMover->Light.LastLocation = dynamicMover->Location();
			dynamicMover->Light.LastRotation = dynamicMover->Rotation();
			dynamicMover->Light.MovedFrame = FrameCounter;
		}
		lastDynamicUpdate = dynamicMover->Light.MovedFrame;

		for (UActor* light : dynamicMover->TouchingLights.List)
		{
			CheckLight(light);
			lastDynamicUpdate = std::max(lastDynamicUpdate, light->Light.LastUpdate);
			TempDynLightList.push_back(light);
		}
	}

	// If anything changed update the lightmap:

	bool bRealtimeChanged = false;
	auto& lmtexture = lmtextures[cacheID];
	if (!lmtexture || lmtexture->LastStaticUpdate != lastStaticUpdate || lmtexture->LastDynamicUpdate != lastDynamicUpdate)
	{
		engine->render->Stats.LightmapsUpdated++;

		Builder.Setup(model, coords, lightmapIndex);

		if (!lmtexture || lmtexture->Mip.Width != Builder.Width() || lmtexture->Mip.Height != Builder.Height())
		{
			lmtexture = std::make_unique<LightmapTexture>();
			lmtexture->Format = TextureFormat::RGBA32_F;
			lmtexture->Mip.Width = Builder.Width();
			lmtexture->Mip.Height = Builder.Height();
			lmtexture->Mip.Data.resize((size_t)lmtexture->Mip.Width * lmtexture->Mip.Height * sizeof(vec4));
		}

		if (dynamicMover)
		{
			Builder.SetAmbientLight(zoneActor);
		}
		else if (lmtexture->LastStaticUpdate != lastStaticUpdate)
		{
			Builder.SetAmbientLight(zoneActor);
			Builder.AddStaticLights(model, lightmapIndex);
			Builder.SaveStaticLight(lmtexture->StaticLightColors);
		}
		else if (lmtexture->StaticLightColors.size() == Builder.Width() * Builder.Height())
		{
			Builder.LoadStaticLight(lmtexture->StaticLightColors);
		}

		Builder.AddAnimatedLights(model, lightmapIndex, TempAnimatedIndexList);
		Builder.AddDynamicLights(model, lightmapIndex, TempDynLightList);

		UnrealMipmap& lmmip = lmtexture->Mip;
		vec4* dest = (vec4*)lmmip.Data.data();
		const vec3* src = Builder.Pixels();
		int count = lmmip.Width * lmmip.Height;
		for (int i = 0; i < count; i++)
		{
			dest[i].r = std::min(src[i].r, 1.0f);
			dest[i].g = std::min(src[i].g, 1.0f);
			dest[i].b = std::min(src[i].b, 1.0f);
			dest[i].a = 1.0f;
		}

		lmtexture->LastStaticUpdate = lastStaticUpdate;
		lmtexture->LastDynamicUpdate = lastDynamicUpdate;
		bRealtimeChanged = true;
	}

	TextureInfo texinfo;
	texinfo.bRealtimeChanged = bRealtimeChanged;
	texinfo.CacheID = cacheID;
	texinfo.Format = lmtexture->Format;
	texinfo.Mips = &lmtexture->Mip;
	texinfo.NumMips = 1;
	texinfo.USize = texinfo.Mips[0].Width;
	texinfo.VSize = texinfo.Mips[0].Height;
	texinfo.Pan = { lmindex.PanX, lmindex.PanY };
	texinfo.UScale = lmindex.UScale;
	texinfo.VScale = lmindex.VScale;
	return texinfo;
}

void LightSystem::CheckLight(UActor* light)
{
	if (light->Light.LastCheck == FrameCounter)
		return;

	light->Light.LastCheck = FrameCounter;

	vec3 location = light->Location();
	float radius = light->WorldLightRadius();
	uint8_t type = light->LightType();
	uint8_t effect = light->LightEffect();
	uint8_t hue = light->LightHue();
	uint8_t saturation = light->LightSaturation();
	uint8_t brightness = light->LightBrightness();
	if (
		location != light->Light.Location ||
		radius != light->Light.Radius ||
		type != light->Light.Type ||
		effect != light->Light.Effect ||
		hue != light->Light.Hue ||
		saturation != light->Light.Saturation ||
		brightness != light->Light.Brightness)
	{
		light->Light.Location = location;
		light->Light.Radius = radius;
		light->Light.Type = type;
		light->Light.Effect = effect;
		light->Light.Hue = hue;
		light->Light.Saturation = saturation;
		light->Light.Brightness = brightness;
		light->Light.LastUpdate = FrameCounter;
	}
}

static float LightDistanceFalloff(float distsqr)
{
	float v = std::sqrt(distsqr + 0.0001f);
	float v2 = v * v;
	float v3 = v2 * v;
	return std::min((1.0f + 2.0f * v3 - 3.0f * v2) / v, 1.0f);
}

void LightSystem::InitVertexLight(VertexLight& out, UActor* actor, UZoneInfo* zoneActor)
{
	// AmbientGlow value 255 is a special pulsating effect used for powerups
	float ambientGlow = actor->AmbientGlow() == 255 ? AmbientGlowAmount : actor->AmbientGlow() * (1.0f / 255.0f);
	out.AmbientColor = ambientGlow + hsbtorgb(zoneActor->AmbientHue(), zoneActor->AmbientSaturation(), zoneActor->AmbientBrightness());

	if (engine->LaunchInfo.IsDeusEx())
	{
		out.OriginalFormula = true;
		out.ScaleGlow = actor->ScaleGlow();
		SetupForActorDX(out, actor);
	}
	else
	{
	out.ScaleGlow = actor->ScaleGlow() * 1.5f;

	int lightIndex = 0;
	for (UActor* light : actor->TouchingLights.List)
	{
		out.Lights[lightIndex].Location = light->Location();
		out.Lights[lightIndex].Color = LightmapBuilder::GetLightColor(light);
		float invRadius = 1.0f / light->WorldLightRadius();
		out.Lights[lightIndex].InvRadiusSquared = invRadius * invRadius;
		lightIndex++;
		if (lightIndex == VertexLight::MaxLights)
			break;
	}
	out.NumLights = lightIndex;
	}

	out.CameraLocation = engine->CameraLocation;

	int fogIndex = 0;
	for (UActor* light : FogBalls)
	{
		if (light->FogInfo.brightness < 0.0f)
		{
			light->FogInfo.fogcolor = hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
			light->FogInfo.brightness = light->LightBrightness() * (1.0f / 255.0f) * light->VolumeBrightness() * (1.0f / 64.0f);
			light->FogInfo.fog = light->VolumeFog() * (1.0f / 255.0f);
			light->FogInfo.radius = light->WorldVolumetricRadius();
			light->FogInfo.location = light->Location();
		}

		out.FogBalls[fogIndex].fogcolor = light->FogInfo.fogcolor;
		out.FogBalls[fogIndex].brightness = light->FogInfo.brightness * 5.0f;
		out.FogBalls[fogIndex].fog = light->FogInfo.fog;
		out.FogBalls[fogIndex].radius = light->FogInfo.radius;
		out.FogBalls[fogIndex].lightpos = light->FogInfo.location;

		fogIndex++;
		if (fogIndex == VertexLight::MaxFogBalls)
			break;
	}
	out.NumFogBalls = fogIndex;
}

void LightSystem::SetupForActorDX(VertexLight& out, UActor* actor)
{
	// The original's SetupForActor picks an actor's lights once a draw
	// (docs/re/render-dll.md, lighting -- meshes): the static lights that
	// reach into the actor's leaf of the BSP, the moving lights in it, and
	// the lights it had last frame, each counted at its strength at the
	// actor's centre; the pick strongest first -- statics until 8 are
	// taken, the others while fewer than 8 in all are, none below an
	// eighth of the strongest; each picked light shadow-checked through
	// the BSP every 16 frames by the frame count and the light's index,
	// except a movable non-static one, always taken; and each fading in
	// and out over about a third of a second, lighting as it fades.
	auto& state = actor->MeshLights;
	out.NumLights = 0;

	if (actor->bUnlit())
	{
		state.List.clear();
		return;
	}

	if (state.LastFrame != FrameCounter)
	{
		state.LastFrame = FrameCounter;
		vec3 location = actor->Location();
		UModel* model = Level->Model;

		for (UActor::MeshLightEntry& entry : state.List)
			entry.Picked = false;

		// The candidates
		Array<UActor*> candidates;
		auto addCandidate = [&](UActor* light)
		{
			if (!light || light->bDeleteMe())
				return;
			if (light->LightType() == LT_None || light->LightBrightness() == 0)
				return;
			if (light->bSpecialLit() != actor->bSpecialLit())
				return;
			for (UActor* seen : candidates)
				if (seen == light)
					return;
			candidates.push_back(light);
		};

		int leaf = model->FindLeafAt(location);
		if (leaf >= 0 && (size_t)leaf < model->Leaves.size())
		{
			int index = model->Leaves[leaf].Permeating;
			if (index >= 0)
				for (size_t i = index; i < model->Lights.size() && model->Lights[i]; i++)
					addCandidate(model->Lights[i]);
		}
		vec3 extents = actor->BspInfo.BoundingBox.extents();
		LightTree.CollectLights(location, std::max(extents.x, std::max(extents.y, extents.z)));
		for (UActor* light : LightTree.CollectedLights)
		{
			if (light->bDynamicLight() || (!light->bStatic() && !light->bNoDelete()))
				addCandidate(light);
		}
		for (const UActor::MeshLightEntry& entry : state.List)
			addCandidate(entry.Light);

		// The strength at the actor's centre; a cylinder light counts with
		// three quarters of its brightness and radius
		struct Candidate
		{
			UActor* Light;
			float Strength;
			bool Static;
		};
		Array<Candidate> reaching;
		for (UActor* light : candidates)
		{
			float radius = light->WorldLightRadius();
			float brightnessScale = 1.0f;
			if (light->LightEffect() == LE_Cylinder)
			{
				radius *= 0.75f;
				brightnessScale = 0.75f;
			}
			if (radius <= 0.0f)
				continue;
			vec3 L = light->Location() - location;
			float dist = std::sqrt(dot(L, L));
			if (dist >= radius)
				continue;
			vec3 color = LightmapBuilder::GetLightColor(light);
			float brightness = std::max(color.r, std::max(color.g, color.b)) * brightnessScale;
			float strength = (1.0f - dist / radius) * brightness;
			if (strength <= 0.0f)
				continue;
			reaching.push_back({ light, strength, (bool)light->bStatic() });
		}
		std::sort(reaching.begin(), reaching.end(), [](const Candidate& a, const Candidate& b) { return a.Strength > b.Strength; });

		// The pick
		Array<const Candidate*> taken;
		for (const Candidate& candidate : reaching)
		{
			if (!candidate.Static)
				continue;
			taken.push_back(&candidate);
			if (taken.size() == 8)
				break;
		}
		for (const Candidate& candidate : reaching)
		{
			if (candidate.Static || taken.size() >= 8)
				continue;
			taken.push_back(&candidate);
		}
		float strongest = 0.0f;
		for (const Candidate* candidate : taken)
			strongest = std::max(strongest, candidate->Strength);

		for (const Candidate* candidate : taken)
		{
			if (candidate->Strength < strongest * 0.125f)
				continue;

			UActor::MeshLightEntry* entry = nullptr;
			for (UActor::MeshLightEntry& kept : state.List)
			{
				if (kept.Light == candidate->Light)
				{
					entry = &kept;
					break;
				}
			}
			if (!entry)
			{
				if (state.List.size() >= 16)
					continue;
				UActor::MeshLightEntry fresh;
				fresh.Light = candidate->Light;
				state.List.push_back(fresh);
				entry = &state.List.back();
			}
			entry->Picked = true;
			entry->Strength = candidate->Strength;

			// Shadowed or not
			UActor* light = entry->Light;
			if (light->bMovable() && !light->bStatic())
			{
				entry->Shadowed = false;
			}
			else if (entry->LastShadowFrame < 0 ||
				((FrameCounter + light->Name.GetCompareIndex()) & 15) == 0)
			{
				entry->Shadowed = engine->Level->Collision.TraceAnyHit(light->Location(), location, actor, false, true, true);
				entry->LastShadowFrame = FrameCounter;
			}
		}

		// The fades
		float elapsed = clamp(LastTickElapsed, 0.0f, 0.1f);
		for (size_t i = 0; i < state.List.size();)
		{
			UActor::MeshLightEntry& entry = state.List[i];
			bool lit = entry.Picked && !entry.Shadowed;
			entry.Fade = clamp(entry.Fade + (lit ? 3.0f : -3.0f) * elapsed, 0.0f, 1.0f);
			if (!entry.Picked && entry.Fade <= 0.0f)
			{
				state.List.erase(state.List.begin() + i);
				continue;
			}
			i++;
		}
	}

	int lightIndex = 0;
	for (const UActor::MeshLightEntry& entry : state.List)
	{
		if (entry.Fade <= 0.0f || entry.Light->bDeleteMe())
			continue;
		UActor* light = entry.Light;
		out.Lights[lightIndex].Location = light->Location();
		out.Lights[lightIndex].Color = LightmapBuilder::GetLightColor(light) * entry.Fade;
		float invRadius = 1.0f / light->WorldLightRadius();
		out.Lights[lightIndex].InvRadius = invRadius;
		out.Lights[lightIndex].InvRadiusSquared = invRadius * invRadius;
		lightIndex++;
		if (lightIndex == VertexLight::MaxLights)
			break;
	}
	out.NumLights = lightIndex;
}
