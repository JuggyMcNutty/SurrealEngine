
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "VisibleFrame.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/URenderIterator.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/UViewport.h"
#include "Math/hsb.h"
#include "Packages/Extension/Windows/UViewportWindow.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"

void RenderSubsystem::DrawScene()
{
	if (!engine->Level)
		return;

	engine->Level->Light.BeginFrame();
	TextureFrameCounter++;

	// Make sure all actors are at the right location in the BSP
	IteratorActors.clear();
	CoronaDynamicLights.clear();
	// Only where the RenderIterator native class is registered (its
	// registration in PackageManager matches this condition)
	bool hasRenderIterators = (engine->LaunchInfo.ue1Version < 400 || engine->LaunchInfo.IsDeusEx()) &&
		PropOffsets_Actor.RenderInterface.DataOffset != ~(size_t)0 &&
		PropOffsets_Actor.RenderIteratorClass.DataOffset != ~(size_t)0;
	bool isDeusEx = engine->LaunchInfo.IsDeusEx();
	for (UActor* actor : engine->Level->Actors)
	{
		if (actor)
		{
			actor->UpdateBspInfo();
			if (hasRenderIterators)
			{
				UpdateRenderInterface(actor);
				if (actor->RenderInterface())
					IteratorActors.push_back(actor);
			}
			// A dynamic light's corona is not in any leaf's permeating list
			if (isDeusEx && actor->bCorona() && actor->LightType() != 0 &&
				((!actor->bStatic() && !actor->bNoDelete()) || actor->bDynamicLight()))
				CoronaDynamicLights.push_back(actor);
		}
	}

	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(engine->CameraRotation).Inverse().ToMatrix() * Coords::Location(engine->CameraLocation).ToMatrix();
	SceneFrameStart = FrameCounter;
	MainFrame.Process(engine->CameraLocation, worldToView, Coords::Rotation(engine->CameraRotation));
	MainFrame.Draw();
	MainFrame.DrawCoronas();
}

void RenderSubsystem::UpdateRenderInterface(UActor* actor)
{
	// Without a RenderInterface, or with one that is no longer valid, the
	// renderer makes one: an object of the actor's RenderIteratorClass with
	// the actor as its outer. With the class cleared, the one there is goes
	// (docs/re/render-dll.md, render iterators).
	UClass* cls = actor->RenderIteratorClass();
	URenderIterator*& iterator = actor->RenderInterface();
	if (!cls)
	{
		iterator = nullptr;
		return;
	}
	if (iterator && iterator->Class != cls)
		iterator = nullptr;
	if (!iterator)
	{
		UObject* obj = engine->packages->GetTransientPackage()->NewObject(cls->Name, cls, ObjectFlags::Transient);
		iterator = UObject::TryCast<URenderIterator>(obj);
		if (iterator)
			iterator->Outer() = actor;
	}
}

void RenderSubsystem::DrawCoronasDX(VisibleFrame* frame)
{
	// The original's coronas (docs/re/render-dll.md, coronas): the lights
	// shining into the viewer's own leaf of the BSP -- its permeating
	// list, and the dynamic lights standing in it -- with bCorona and a
	// Skin, at any distance. One is seen when the line from the eye meets
	// no level geometry or mover, and no pawn or other actor but the
	// viewer's own pawn. Each frame every corona loses three times the
	// real seconds elapsed and each one seen gains twice that, so one
	// comes up, or goes, in about a third of a second; at 0 it is
	// dropped, and up to 32 are kept.
	UModel* model = engine->Level->Model;
	UActor* viewerPawn = engine->viewport->Actor();
	vec3 eye = frame->ViewLocation.xyz();

	// The viewer's leaf
	int leaf = -1;
	{
		vec4 location = vec4(eye, 1.0f);
		BspNode* nodes = model->Nodes.data();
		BspNode* node = nodes;
		while (true)
		{
			vec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
			bool swapFrontAndBack = dot(location, plane) < 0.0f;
			int front = node->Front;
			if (swapFrontAndBack)
				front = node->Back;
			if (front >= 0)
			{
				node = nodes + front;
			}
			else
			{
				leaf = swapFrontAndBack ? node->Leaf0 : node->Leaf1;
				break;
			}
		}
	}

	// Which lights, this frame
	Array<UActor*> candidates;
	if (leaf >= 0 && (size_t)leaf < model->Leaves.size())
	{
		int index = model->Leaves[leaf].Permeating;
		if (index >= 0)
		{
			for (size_t i = index; i < model->Lights.size() && model->Lights[i]; i++)
			{
				UActor* light = model->Lights[i];
				if (light->bCorona() && light->Skin() && !light->bDeleteMe())
					candidates.push_back(light);
			}
		}
	}
	for (UActor* light : CoronaDynamicLights)
	{
		if (light->bCorona() && light->Skin() && !light->bDeleteMe())
			candidates.push_back(light);
	}

	// Seen: the eye reaches the light past the world, movers, pawns and
	// other actors, but the viewer's own pawn
	auto blocksSight = [&](UActor* actor) { return actor != viewerPawn; };
	auto seenNow = [&](UActor* light)
	{
		for (UActor* candidate : candidates)
			if (candidate == light)
				return !engine->Level->Collision.SightBlocked(eye, light->Location(), blocksSight);
		return false;
	};

	// Fade on real time
	auto now = std::chrono::steady_clock::now();
	float elapsed = 0.0f;
	if (CoronaLastUpdate.time_since_epoch().count() != 0)
		elapsed = clamp(std::chrono::duration<float>(now - CoronaLastUpdate).count(), 0.0f, 0.1f);
	CoronaLastUpdate = now;

	for (size_t i = 0; i < CoronaStates.size();)
	{
		CoronaState& state = CoronaStates[i];
		bool seen = !state.Light->bDeleteMe() && seenNow(state.Light);
		state.Brightness -= 3.0f * elapsed;
		if (seen)
			state.Brightness += 6.0f * elapsed;
		state.Brightness = std::min(state.Brightness, 1.0f);
		if (state.Brightness <= 0.0f && !seen)
		{
			CoronaStates.erase(CoronaStates.begin() + i);
			continue;
		}
		state.Brightness = std::max(state.Brightness, 0.0f);
		i++;
	}
	for (UActor* light : candidates)
	{
		bool known = false;
		for (const CoronaState& state : CoronaStates)
		{
			if (state.Light == light)
			{
				known = true;
				break;
			}
		}
		if (!known && CoronaStates.size() < 32 &&
			!engine->Level->Collision.SightBlocked(eye, light->Location(), blocksSight))
		{
			CoronaState state;
			state.Light = light;
			state.Brightness = 3.0f * elapsed;
			CoronaStates.push_back(state);
		}
	}

	// Drawn at the light's place on screen, if in front of the eye: a
	// square a fifth of the view's width times the light's DrawScale,
	// whatever the distance, translucent, in the colour of its hue and
	// saturation times the brightness
	for (const CoronaState& state : CoronaStates)
	{
		UActor* light = state.Light;
		if (state.Brightness <= 0.0f || light->bDeleteMe() || !light->Skin())
			continue;

		vec4 pos = frame->Frame.WorldToView * frame->Frame.ObjectToWorld * vec4(light->Location(), 1.0f);
		if (pos.z < 1.0f)
			continue;
		vec4 clip = frame->Frame.Projection * pos;
		float x = frame->Frame.FX2 + clip.x / clip.w * frame->Frame.FX2;
		float y = frame->Frame.FY2 + clip.y / clip.w * frame->Frame.FY2;

		UTexture* skin = light->Skin();
		engine->render->UpdateTexture(skin);

		TextureInfo texinfo;
		texinfo.CacheID = (uint64_t)(ptrdiff_t)skin;
		texinfo.Texture = skin->GetAnimTexture();
		texinfo.Format = texinfo.Texture->UsedFormat;
		texinfo.Mips = texinfo.Texture->UsedMipmaps.data();
		texinfo.NumMips = (int)texinfo.Texture->UsedMipmaps.size();
		texinfo.USize = texinfo.Texture->USize();
		texinfo.VSize = texinfo.Texture->VSize();
		if (texinfo.Texture->Palette())
			texinfo.Palette = (TextureColor*)texinfo.Texture->Palette()->Colors.data();
		engine->render->UpdateTexture(texinfo.Texture);

		float width = (float)skin->UsedMipmaps.front().Width;
		float height = (float)skin->UsedMipmaps.front().Height;
		float size = light->DrawScale() * frame->Frame.FX * 0.2f;
		vec3 lightcolor = hsbtorgb(light->LightHue(), light->LightSaturation(), 255) * state.Brightness;

		frame->Device->DrawTile(
			&frame->Frame,
			texinfo, x - size * 0.5f,
			y - size * 0.5f,
			size,
			size,
			0.0f,
			0.0f,
			width,
			height,
			2.0f,
			vec4(lightcolor, 1.0f),
			vec4(0.0f),
			PF_Translucent);
	}
}

void RenderSubsystem::DrawViewport(UViewportWindow* viewport)
{
	if (!engine->Level)
		return;

	float x = 0.0f, y = 0.0f;
	engine->dxRootWindow->ConvertCoordinates(viewport, 0.0f, 0.0f, engine->dxRootWindow, x, y);
	engine->dxRootWindow->SetRenderViewport(x, y, viewport->Width(), viewport->Height());

	if (viewport->bClearZ())
		Device->ClearZ();

	bool originActorWasHidden = false;
	vec3 location;
	Rotator rotation(0,0,0);
	if (UActor* originActor = viewport->originActor())
	{
		originActorWasHidden = originActor->bHidden();
		originActor->bHidden() = true;
		location = originActor->Location() + viewport->relLocation();
		if (viewport->bUseEyeHeight())
		{
			if (auto pawn = UObject::TryCast<UPawn>(originActor))
				location.z += pawn->BaseEyeHeight();
		}
		rotation = originActor->Rotation();
	}
	else
	{
		location = viewport->Location() + viewport->relLocation();
		rotation = viewport->Rotation();
	}

	if (viewport->bUseViewRotation())
	{
		rotation = engine->CameraRotation;
	}
	else if (UActor* watchActor = viewport->watchActor())
	{
		vec3 lookAt = watchActor->Location();
		if (viewport->bWatchEyeHeight())
		{
			if (auto pawn = UObject::TryCast<UPawn>(watchActor))
				lookAt.z += pawn->BaseEyeHeight();
		}
		rotation = Rotator::FromVector(lookAt - location);
	}

	rotation += viewport->relRotation();

	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(rotation).Inverse().ToMatrix() * Coords::Location(location).ToMatrix();
	MainFrame.Process(location, worldToView, Coords::Rotation(rotation));
	MainFrame.Draw();
	MainFrame.DrawCoronas();

	Device->SetSceneNode(&Canvas.Frame);

	engine->dxRootWindow->ResetRenderViewport();

	if (UActor* originActor = viewport->originActor())
		originActor->bHidden() = originActorWasHidden;
}
