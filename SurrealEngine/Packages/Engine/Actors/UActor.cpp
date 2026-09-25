
#include "Precomp.h"
#include "UActor.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/USpawnNotify.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Render/RenderSubsystem.h"
#include "LauncherSettings.h"

UActor* UActor::Spawn(UClass* SpawnClass, std::optional<UActor*> SpawnOwner, std::optional<NameString> SpawnTag, std::optional<vec3> SpawnLocation, std::optional<Rotator> SpawnRotation)
{
	if (!SpawnClass || SpawnClass->ClsFlags & ClassFlags::Abstract)
	{
		LogMessage("Could not spawn class: " + (SpawnClass ? SpawnClass->Name.ToString() : std::string("null")));
		return nullptr;
	}

	vec3 location = SpawnLocation ? *SpawnLocation : Location();
	Rotator rotation = SpawnRotation ? *SpawnRotation : Rotation();

	float radius = SpawnClass->GetDefaultObject<UActor>()->CollisionRadius();
	float height = SpawnClass->GetDefaultObject<UActor>()->CollisionHeight();
	bool bCollideWorld = SpawnClass->GetDefaultObject<UActor>()->bCollideWorld();
	bool bCollideWhenPlacing = SpawnClass->GetDefaultObject<UActor>()->bCollideWhenPlacing();
	if (bCollideWorld || bCollideWhenPlacing)
	{
		auto result = CheckLocation(location, radius, height, bCollideWorld || bCollideWhenPlacing);
		if (!result.first)
		{
			LogMessage("Could not find usable location when trying to spawn: " + SpawnClass->Name.ToString());
			return nullptr;
		}
		location = result.second;
	}

	// To do: package needs to be grabbed from outer, or the "transient package" if it is None, a virtual package for runtime objects
	// To do: find unique new name in the package
	static std::map<NameString, int> nextIndex;
	NameString name = SpawnClass->Name.ToString() + std::to_string(nextIndex[SpawnClass->Name]++);
	UActor* actor = UObject::Cast<UActor>(engine->LevelPackage->NewObject(name, UObject::Cast<UClass>(SpawnClass), ObjectFlags::Transient, true));

	actor->Outer() = XLevel()->Outer();
	actor->XLevel() = XLevel();
	actor->Level() = Level();
	actor->Tag() = (SpawnTag && !SpawnTag->IsNone()) ? *SpawnTag : SpawnClass->Name;
	actor->bTicked() = bTicked(); // To do: should it tick in the same world tick it was spawned in or wait until the next one?
	actor->Instigator() = Instigator();
	actor->Brush() = nullptr;
	actor->Location() = location;
	actor->OldLocation() = location;
	actor->Rotation() = rotation;
	actor->Region().Zone = actor->Level();
	// The original starts a spawned actor 10 s undrawn; the renderer keeps
	// it from here.
	actor->LastRenderTime() = Level()->TimeSeconds() - 10.0f;
	actor->Index = (int)XLevel()->Actors.size();
	XLevel()->Actors.push_back(actor);
	XLevel()->ActorsVersion++;
	XLevel()->Collision.AddToCollision(actor);

	actor->SetOwner(SpawnOwner.has_value() && SpawnOwner.value() ? *SpawnOwner : nullptr);

	if (Level()->bBegunPlay())
	{
		CallEvent(actor, EventName::Spawned);
		CallEvent(actor, EventName::PreBeginPlay);
		CallEvent(actor, EventName::BeginPlay);

		if (actor->bDeleteMe())
		{
			LogMessage("Object deleted itself during Spawn!");
			return nullptr;
		}

		// To do: we need to call EventName::EncroachingOn events here?

		actor->InitActorZone();

		CallEvent(actor, EventName::PostBeginPlay);
		CallEvent(actor, EventName::SetInitialState);
		if (engine->LaunchInfo.IsDeusEx())
			CallEvent(actor, "PostPostBeginPlay");

		actor->InitBase();

		if (engine->LaunchInfo.ue1Version >= 400)
		{
			static bool spawnNotificationLocked = false;
			if (!spawnNotificationLocked)
			{
				struct NotificationLockGuard
				{
					NotificationLockGuard() { spawnNotificationLocked = true; }
					~NotificationLockGuard() { spawnNotificationLocked = false; }
				} lockGuard;

				for (USpawnNotify* notifyObj = Level()->SpawnNotify(); notifyObj != nullptr; notifyObj = notifyObj->Next())
				{
					UClass* cls = notifyObj->ActorClass();
					if (cls && actor->IsA(cls->Name))
						actor = UObject::Cast<UGameInfo>(CallEvent(notifyObj, EventName::SpawnNotification, { ExpressionValue::ObjectValue(actor) }).ToObject());
				}
			}
		}
	}

	return actor;
}

bool UActor::Destroy()
{
	//engine->LogMessage("UActor.Destroy(" + Class->FriendlyName.ToString() + ")");

	if (bStatic() || bNoDelete())
		return false;
	if (bDeleteMe())
		return true;

	bDeleteMe() = true;

	//GotoState({}, {}); // What should happen to function calls after Destroy() has been called? Razor2 calls SetRoll afterwards!
	SetBase(nullptr, true);

	engine->audiodev->ActorDestroyed(this);

	ULevel* level = XLevel();

	RemoveFromBspNode();
	level->Collision.RemoveFromCollision(this);

	CallEvent(this, EventName::Destroyed);

	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor)
				UnTouch(actor);
	}
	else
	{
		for (const auto actor : Touching())
			if (actor)
				UnTouch(actor);
	}


	SetOwner(nullptr);

	while (!ChildActors.empty())
	{
		ChildActors.back()->SetOwner(nullptr);
	}
	while (!BasedActors.empty())
	{
		BasedActors.back()->SetBase(nullptr, true);
	}

	if (Index == -1)
		throw std::runtime_error("Actor index was never set!");
	level->Actors[Index] = nullptr;
	level->ActorsVersion++;
	level->ActorsHaveHoles = true;

	return true;
}

bool UActor::InStasis()
{
	// The original's InStasis, all of which must hold: bStasis;
	// bForceStasis, or physics none or rotating; not drawn for 5 s; its
	// zone not drawn for 5 s, or more than 1,200 units from the player;
	// single player, which the fork always is.
	if (!bStasis())
		return false;
	if (!bForceStasis() && Physics() != PHYS_None && Physics() != PHYS_Rotating)
		return false;
	float now = Level()->TimeSeconds();
	if (now - LastRenderTime() < 5.0f)
		return false;
	auto& zones = XLevel()->Model->Zones;
	int zone = Region().ZoneNumber;
	bool zoneDrawn = zone >= 0 && (size_t)zone < zones.size() && now - zones[zone].LastRenderTime < 5.0f;
	return !zoneDrawn || DistanceFromPlayer() > 1200.0f;
}

bool UActor::IsTransient()
{
	if (!TransientPropSearched)
	{
		TransientPropSearched = 1;
		for (UStruct* s = Class; s && TransientPropOffset.DataOffset == ~(size_t)0; s = s->StructParent)
		{
			for (UProperty* prop : s->Properties)
			{
				if (prop->Name == "bTransient" && UObject::TryCast<UBoolProperty>(prop))
				{
					TransientPropOffset = prop->DataOffset;
					break;
				}
			}
		}
	}
	return TransientPropOffset.DataOffset != ~(size_t)0 && BoolValue(TransientPropOffset);
}

void UActor::Tick(float elapsed)
{
	if (engine->LaunchInfo.IsDeusEx())
	{
		DistanceFromPlayer() = length(engine->viewport->Actor()->Location() - Location());

		// The original's tick does nothing else for an actor in stasis --
		// no script tick, physics, animation or timers -- and destroys a
		// transient one (the rats a container lets out).
		if (InStasis())
		{
			if (IsTransient())
				Destroy();
			return;
		}
	}

	TickAnimation(elapsed);
	if (engine->LaunchInfo.IsDeusEx())
		TickBlendAnimation(elapsed);

	float thinkElapsed = elapsed;
	bool think = ThinkThisFrame(elapsed, thinkElapsed);

	if (think && Role() >= ROLE_SimulatedProxy && IsEventEnabled(EventName::Tick))
	{
		CallEvent(this, EventName::Tick, { ExpressionValue::FloatValue(thinkElapsed) });
	}

	if (StateFrame)
	{
		if (StateFrame->LatentState == LatentRunState::Sleep)
		{
			SleepTimeLeft = std::max(SleepTimeLeft - elapsed, 0.0f);
			if (SleepTimeLeft == 0.0f)
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::FinishInterpolation)
		{
			if (!bInterpolating())
				StateFrame->LatentState = LatentRunState::Continue;
		}

		if (think && Role() >= ROLE_SimulatedProxy && StateFrame->LatentState == LatentRunState::Continue)
		{
			StateFrame->Tick();
		}
	}

	TickPhysics(elapsed);

	if (TimerRate() > 0.0f) // Role() == ROLE_Authority && RemoteRole() == ROLE_AutonomousProxy
	{
		TimerCounter() += elapsed;
		while (TimerRate() > 0.0f && TimerCounter() > TimerRate())
		{
			TimerCounter() -= TimerRate();
			if (!bTimerLoop())
				TimerRate() = 0.0f;
			CallEvent(this, EventName::Timer);
		}
	}
}

bool UActor::ThinkThisFrame(float elapsed, float& thinkElapsed)
{
	thinkElapsed = elapsed;
	if (!LauncherSettings::Get().Performance.AiLevelOfDetail)
		return true;

	if (AiLodPawn < 0)
	{
		AiLodPawn = (UObject::TryCast<UPawn>(this) && !UObject::TryCast<UPlayerPawn>(this)) ? 1 : 0;
		AiFramesSinceThought = Index % 3; // spread the pawns over the three frames
	}
	if (!AiLodPawn)
		return true;

	AiTimeSinceThought += elapsed;
	AiFramesSinceThought++;

	constexpr float nearDistance = 1500.0f; // ~28 m: close enough to fight
	constexpr float farDistance = 4000.0f; // ~76 m: beyond this, every sixth frame
	UActor* player = engine->viewport->Actor();
	bool seen = LastVisibleFrame >= engine->render->SceneFrameStart;
	float distance = player ? length(player->Location() - Location()) : 0.0f;
	bool near = player && distance < nearDistance;
	int interval = (player && distance > farDistance) ? 6 : 3;
	if (seen || near || AiFramesSinceThought >= interval)
	{
		thinkElapsed = AiTimeSinceThought;
		AiTimeSinceThought = 0.0f;
		AiFramesSinceThought = 0;
		return true;
	}
	return false;
}

bool UActor::Move(const vec3& delta)
{
	return TryMove(delta).Fraction == 1.0f;
}

bool UActor::MoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMoveSmooth(delta);
	return hit.Fraction != 1.0f;
}

void UActor::MakeNoise(float loudness)
{
	UPawn* noisePawn = UObject::Cast<UPawn>(Instigator());

	if (!noisePawn || Level()->NetMode() == NM_Client)
		return;

	float currentTime = Level()->TimeSeconds();
	vec3 delta1 = noisePawn->noise1spot() - Location();
	vec3 delta2 = noisePawn->noise2spot() - Location();
	if ((noisePawn->noise1time() > currentTime - 0.2f && dot(delta1, delta1) < 2500.0f && noisePawn->noise1loudness() >= 0.9f * loudness) ||
		(noisePawn->noise2time() > currentTime - 0.2f && dot(delta2, delta2) < 2500.0f && noisePawn->noise2loudness() >= 0.9f * loudness))
	{
		return;
	}

	if (noisePawn->noise1time() < currentTime - 0.18f)
	{
		noisePawn->noise1time() = currentTime;
		noisePawn->noise1spot() = Location();
		noisePawn->noise1loudness() = loudness;
	}
	else if (noisePawn->noise2time() < currentTime - 0.18f)
	{
		noisePawn->noise2time() = currentTime;
		noisePawn->noise2spot() = Location();
		noisePawn->noise2loudness() = loudness;
	}
	else if (dot(delta1, delta1) < 2500.0f)
	{
		noisePawn->noise1time() = currentTime;
		noisePawn->noise1spot() = Location();
		noisePawn->noise1loudness() = loudness;
	}
	else if (noisePawn->noise2loudness() <= loudness)
	{
		noisePawn->noise2time() = currentTime;
		noisePawn->noise2spot() = Location();
		noisePawn->noise2loudness() = loudness;
	}

	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		if (pawn != noisePawn && pawn->CanHearNoise(this, loudness))
		{
			CallEvent(pawn, EventName::HearNoise, { ExpressionValue::FloatValue(loudness), ExpressionValue::ObjectValue(this) });
		}
	}
}

bool UActor::PlayerCanSeeMe()
{
	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		if (pawn == this)
			continue;

		vec3 L = Location() - pawn->Location();
		float dist2 = dot(L, L);

		// Too far away
		if (dist2 > 500 * 500)
			continue;

		// Without behind view the pawn can only see in a 75 degree cone in front of them
		if (!pawn->bBehindView())
		{
			vec3 viewDirection = Coords::Rotation(pawn->ViewRotation()).XAxis;
			if (dot(viewDirection, L) < 0.2588190451f * dist2)
				continue;
		}

		// Try check for line of sight
		vec3 eyePos = pawn->Location();
		eyePos.z += pawn->BaseEyeHeight();
		if (pawn->FastTrace(Location(), eyePos))
			return true;
	}
	return false;
}

UTexture* UActor::CreateTextureFromScreenShot(UViewport* vport)
{
	LogUnimplemented("Actor.CreateTextureFromScreenShot");
	return nullptr;
}

UTexture* UActor::CreateTextureFromBMP(const std::string& name, const std::string& filename)
{
	LogUnimplemented("Actor.CreateTextureFromBMP");
	return nullptr;
}

bool UActor::SaveObjectAsFile(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.SaveObjectAsFile");
	return false;
}

bool UActor::LoadObjectAsFile(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.LoadObjectAsFile");
	return false;
}

bool UActor::SaveGameSaveInfo(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.SaveGameSaveInfo");
	return false;
}

bool UActor::LoadGameSaveInfo(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.LoadGameSaveInfo");
	return false;
}

bool UActor::IsOSVer2kOrXP()
{
	return true;
}

void UActor::AIClearEvent(const NameString& eventName)
{
	LogUnimplemented("Actor.AIClearEvent");
}

void UActor::AIClearEventCallback(const NameString& eventName)
{
	LogUnimplemented("Actor.AIClearEventCallback");
}

void UActor::AIEndEvent(const NameString& eventName, uint8_t eventType)
{
	LogUnimplemented("Actor.AIEndEvent");
}

float UActor::AIGetLightLevel(const vec3& Location)
{
	LogUnimplemented("Actor.AIGetLightLevel");
	return 1.0f;
}

void UActor::AISendEvent(const NameString& eventName, uint8_t eventType, std::optional<float> Value, std::optional<float> Radius)
{
	LogUnimplemented("Actor.AISendEvent");
	//LogUnimplemented(Name.ToString() + ": AISendEvent('" + eventName.ToString() + "')");
}

void UActor::AISetEventCallback(const NameString& eventName, const NameString& callback, std::optional<NameString> scoreCallback, std::optional<bool> bCheckVisibility, std::optional<bool> bCheckDir, std::optional<bool> bCheckCylinder, std::optional<bool> bCheckLOS)
{
	LogUnimplemented("Actor.AISetEventCallback");
	//LogMessage(Name.ToString() + ": AISetEventCallback('" + eventName.ToString() + "')");
}

void UActor::AIStartEvent(const NameString& eventName, uint8_t eventType, std::optional<float> Value, std::optional<float> Radius)
{
	LogUnimplemented("Actor.AIStartEvent");
	//LogUnimplemented(Name.ToString() + ": AIStartEvent('" + eventName.ToString() + "')");
}

// A light's share of what the AI sees by, 0 to 1: its brightness, a third of
// it for a fully saturated colour and all of it for white.
static float AILuminance(uint8_t brightness, uint8_t saturation)
{
	return (float)((saturation + 127.5) * (1.0 / 382.5) * (brightness * (1.0 / 255.0)));
}

// The light on an actor at a location, 0 to 1, as Deus Ex's AI sees it: half
// of each light reaching it -- a static one only with no wall or mover
// between -- the nearer its middle the more, and twice the zone's ambient
// light. An unlit actor is always lit. From Engine.dll, where it serves
// AActor::AIVisibility.
static float AILightAt(UActor* actor, const vec3& location)
{
	float lights = 0.0f;
	if (actor->bUnlit())
	{
		lights = 1.0f;
	}
	else
	{
		// The original tests every actor for a light reaching the location.
		// The light system's tree holds the same lights, as of the last frame
		// drawn, and gives the few whose reach may take it in; one destroyed
		// since is skipped.
		ULevel* level = actor->XLevel();
		for (UActor* light : level->Light.LightsNear(location))
		{
			if (light == actor || light->bDeleteMe() || light->LightType() == LT_None || light->LightBrightness() == 0)
				continue;

			float radius = light->WorldLightRadius();
			float radiusSq = radius * radius;
			vec3 d = location - light->Location();
			float distSq = dot(d, d);
			if (radiusSq <= distSq)
				continue;

			if (light->bStatic())
			{
				const TraceFlags flags = { .movers = true, .world = true };
				CollisionHit hit = level->Collision.TraceFirstHit(light->Location(), location, actor, vec3(0.0f), flags);
				if (hit.Actor || hit.Node)
					continue;
			}

			lights += AILuminance(light->LightBrightness(), light->LightSaturation()) * (1.0f - (float)std::sqrt(distSq / radiusSq));
		}
		lights *= 0.5f;
	}

	UZoneInfo* zone = actor->Region().Zone;
	float ambient = zone ? AILuminance(zone->AmbientBrightness(), zone->AmbientSaturation()) : 0.0f;
	return std::clamp(ambient * 2.0f + lights, 0.0f, 1.0f);
}

// How visible this actor is to Deus Ex's AI, 0 to 1 (Engine.dll
// AActor::AIVisibility): the light on it, found again a quarter second after
// the last finding and eased from one finding to the next, and with
// bIncludeVelocity (the default) up to half again as much when it moves at
// 30 to 200 units a second.
float UActor::AIVisibility(std::optional<bool> bIncludeVelocity)
{
	const float period = 0.25f;
	float now = Level()->TimeSeconds();
	float elapsed = now - VisUpdateTime();
	float visibility;
	if (elapsed > period * 2.0f)
	{
		VisUpdateTime() = now;
		visibility = AILightAt(this, Location());
		CurrentVisibility() = visibility;
		LastVisibility() = visibility;
	}
	else if (elapsed > period)
	{
		LastVisibility() = CurrentVisibility();
		VisUpdateTime() = now;
		CurrentVisibility() = AILightAt(this, Location());
		visibility = LastVisibility();
	}
	else
	{
		visibility = (CurrentVisibility() - LastVisibility()) * (elapsed / period) + LastVisibility();
	}

	if (bIncludeVelocity.value_or(true))
	{
		float speed = length(Velocity());
		float factor = std::clamp((speed - 30.0f) / (200.0f - 30.0f), 0.0f, 1.0f);
		visibility += factor * visibility * 0.5f;
	}
	return std::clamp(visibility, 0.0f, 1.0f);
}
