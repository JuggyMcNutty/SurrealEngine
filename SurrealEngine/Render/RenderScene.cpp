
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
	// Only where the RenderIterator native class is registered (its
	// registration in PackageManager matches this condition)
	bool hasRenderIterators = (engine->LaunchInfo.ue1Version < 400 || engine->LaunchInfo.IsDeusEx()) &&
		PropOffsets_Actor.RenderInterface.DataOffset != ~(size_t)0 &&
		PropOffsets_Actor.RenderIteratorClass.DataOffset != ~(size_t)0;
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
