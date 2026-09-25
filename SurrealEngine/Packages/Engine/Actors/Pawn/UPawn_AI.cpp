
#include "Precomp.h"
#include "UPawn.h"
#include "UPlayerPawn.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Info/UPlayerReplicationInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include <queue>
#include <climits>
#include "Utils/Logger.h"
#include "Engine.h"

bool UPawn::LineOfSightTo(UActor* other, bool ignoreDistance)
{
	if (!other)
		return false;

	if (engine->LaunchInfo.IsUnreal1_227() &&
		(SightCheckType() == EPawnSightCheck::SEE_None ||
		(SightCheckType() == EPawnSightCheck::SEE_PlayersOnly && !Cast<UPawn>(other)->bIsPlayer())))
		return false;

	if (!ignoreDistance && length(Location() - other->Location()) > SightRadius())
		return false;

	vec3 eye_pos = Location();
	eye_pos.z += BaseEyeHeight();

	auto& origin = other->Location();
	auto top = origin + vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };
	auto bottom = origin - vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };

	return FastTrace(origin, eye_pos) || FastTrace(top, eye_pos) || FastTrace(bottom, eye_pos);
}

bool UPawn::CanSee(UActor* other)
{
	if (!other)
		return false;

	// Two fields to keep in mind of:
	// float SightRadius: Maximum seeing distance
	// float PeripheralVision: Cosine of limits of peripheral vision

	auto& origin = other->Location();
	auto top = origin + vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };
	auto bottom = origin - vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };

	vec3 eye_pos = Location();
	eye_pos.z += BaseEyeHeight();

	// Cannot see if the actor is too far away from the sight radius
	if (length(origin - eye_pos) > SightRadius())
		return false;

	// Cannot see if the actor is outside of the peripheral vision angles
	vec3 orientation = Coords::Rotation(Rotation()).XAxis;

	// Calculate the cosine of the vectors
	// which is basically A dot B / (|A| * |B|), or just the dot products of the normalized versions of A and B
	float cosine = dot(normalize(orientation), normalize(origin - eye_pos));
	float peripheralVision = PeripheralVision();
	if (cosine < peripheralVision)
		return false;

	return FastTrace(origin, eye_pos) || FastTrace(top, eye_pos) || FastTrace(bottom, eye_pos);
}

bool UPawn::CanHearNoise(UActor* source, float loudness)
{
	UPawn* noisePawn = UObject::Cast<UPawn>(source->Instigator());
	if (!noisePawn->bIsPlayer() && (!noisePawn->Enemy() || !noisePawn->Enemy()->bIsPlayer()))
	{
		if (!IsA(source->Class->Name) && !source->IsA(Class->Name))
			return false;
	}
	else if (UObject::TryCast<UPlayerPawn>(this))
	{
		return false;
	}

	vec3 delta = Location() - source->Location();
	float dist2 = dot(delta, delta);

	if (!bIsPlayer() || !Level()->Game()->bTeamGame() || !noisePawn->bIsPlayer() ||
		(engine->LaunchInfo.ue1Version > 219 && (!PlayerReplicationInfo() || !noisePawn->PlayerReplicationInfo() || (PlayerReplicationInfo()->Team() != noisePawn->PlayerReplicationInfo()->Team()))))
	{
		if (dist2 > (4000.0f * 4000.0f) * (loudness * loudness))
			return false;

		float perceived = std::min(1200000.f / dist2, 2.0f);
		Stimulus() = loudness * perceived + Alertness() * std::min(0.5f, perceived);
		if (Stimulus() < HearingThreshold())
			return false;
	}
	else if (dist2 > (4000.0f * 4000.0f) * (loudness * loudness))
	{
		return false;
	}

	return !XLevel()->Collision.TraceAnyHit(source->Location(), Location(), source, false, true, false);
}

void UPawn::ClientHearSound(UActor* actor, int id, USound* sound, const vec3& soundLocation, const vec3& parameters)
{
	LogUnimplemented("UPawn.ClientHearSound()");
}

UActor* UPawn::PickAnyTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	UActor* bestActor = nullptr;
	for (UActor* actor : XLevel()->Actors)
	{
		// We are only looking for targets that isn't a pawn (pawn uses PickTarget if it wants a pawn)
		if (!actor || actor == this || UObject::TryCast<UPawn>(actor) || !actor->bProjTarget())
			continue;

		if (CheckIfBestTarget(actor, bestAim, bestDist, FireDir, projStart))
			bestActor = actor;
	}
	return bestActor;
}

UActor* UPawn::PickTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	UActor* bestActor = nullptr;
	UPlayerReplicationInfo* ourPlayerInfo = engine->LaunchInfo.ue1Version > 219 ? PlayerReplicationInfo() : nullptr;
	bool teamGame = ourPlayerInfo && Level()->Game()->bTeamGame();
	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		// Skip dead pawns or ourselves
		if (pawn == this || pawn->Health() <= 0)
			continue;

		// Skip team mates
		if (engine->LaunchInfo.ue1Version > 219)
		{
			auto pawnPlayerInfo = pawn->PlayerReplicationInfo();
			if (teamGame && pawnPlayerInfo && ourPlayerInfo->Team() == pawnPlayerInfo->Team())
				continue;
		}

		if (CheckIfBestTarget(pawn, bestAim, bestDist, FireDir, projStart))
			bestActor = pawn;
	}
	return bestActor;
}

bool UPawn::CheckIfBestTarget(UActor* actor, float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	// Ignore targets behind us
	vec3 delta = actor->Location() - projStart;
	float angle = dot(FireDir, delta);
	if (angle < 0.0f)
		return false;

	// Skip things too far away
	float distance = length(delta);
	if (distance == 0.0f || distance > 2500.0f)
		return false;

	// Skip if we already have a target closer to the direction we are facing
	angle /= distance;
	if (angle < bestAim)
		return false;

	// Skip if we can't see the target
	if (!LineOfSightTo(actor, false))
		return false;

	// OK, this is better than what we have
	bestAim = angle;
	bestDist = distance;
	return true;
}

// The original's AICanHear (0x103c7680), which only the event manager
// calls: 0 unless other is bDetectable and the volume above 0; a radius of
// 800 when none above 0 is given; vertical distance counts double; at or
// beyond the radius 0, else (1 - distance / radius) times the volume, less
// the pawn's HearingThreshold, held between 0 and 1.
float UPawn::AICanHear(UActor* other, std::optional<float> volumeArg, std::optional<float> radiusArg)
{
	float volume = volumeArg.value_or(1.0f);
	float radius = radiusArg.value_or(0.0f);
	if (radius <= 0.0f)
		radius = 800.0f;
	if (!other || !other->bDetectable() || volume <= 0.0f)
		return 0.0f;

	vec3 delta = other->Location() - Location();
	delta.z *= 2.0f;
	float distance = length(delta);
	if (distance >= radius)
		return 0.0f;
	return std::clamp((1.0f - distance / radius) * volume - HearingThreshold(), 0.0f, 1.0f);
}

// How much of something size degrees across, angle degrees off the middle of
// a field of view fov degrees wide, is inside it: 1 all of it, 0 none, falling
// off linearly across the edge. From Engine.dll, where it serves
// APawn::AICanSee.
static float AIFieldOfViewShare(float angle, float fov, float size)
{
	while (angle <= -180.0f)
		angle += 360.0f;
	while (angle > 180.0f)
		angle -= 360.0f;
	if (angle < 0.0f)
		angle = -angle;

	float inner = (float)((fov - size) * 0.5);
	float outer = inner + size;
	if (angle > outer)
		return 0.0f;
	if (angle <= inner)
		return 1.0f;
	return 1.0f - (angle - inner) / size;
}

// How well this pawn sees another actor, 0 to 1, as Deus Ex's AI judges it
// (Engine.dll APawn::AICanSee; the defaults are its exec function's). From
// the eyes: how large the other looks -- nothing under MinAngularSize -- and,
// with bCheckDir, how much of it is inside the view (AIHorizontalFov wide,
// AspectRatio times narrower up and down, turned by AIAddViewRotation); with
// bCheckVisibility, how lit it is (AIVisibility); less VisibilityThreshold.
// With bCheckLOS, zero unless a line reaches it past the world and anything
// that blocks sight: its middle, or with bCheckCylinder a player's eyes, its
// top and its bottom, and anything else's sides.
float UPawn::AICanSee(UActor* other, std::optional<float> visibilityArg, std::optional<bool> bCheckVisibilityArg, std::optional<bool> bCheckDirArg, std::optional<bool> bCheckCylinderArg, std::optional<bool> bCheckLOSArg)
{
	float visibility = visibilityArg.value_or(1.0f);
	bool bCheckVisibility = bCheckVisibilityArg.value_or(true);
	bool bCheckDir = bCheckDirArg.value_or(true);
	bool bCheckCylinder = bCheckCylinderArg.value_or(false);
	bool bCheckLOS = bCheckLOSArg.value_or(true);

	if (!other || visibility <= 0.0f || !other->bDetectable())
		return 0.0f;

	vec3 eye = Location();
	eye.z += BaseEyeHeight();
	vec3 delta = other->Location() - eye;
	double distSq = (double)delta.x * delta.x + (double)delta.y * delta.y + (double)delta.z * delta.z;
	if (distSq < 1.0)
		distSq = 1.0;

	// Its apparent size squared, as the tangent of the angle it spans
	double radiusSq = (double)other->CollisionRadius() * other->CollisionRadius();
	double heightSq = (double)other->CollisionHeight() * other->CollisionHeight();
	double sizeSq = (radiusSq + heightSq) / distSq;
	if (sizeSq <= MinAngularSize())
		return 0.0f;
	if (sizeSq < 0.0003046792916483) // under a degree: its middle stands for it
		bCheckCylinder = false;
	visibility = (float)(visibility * sizeSq * 64.0);

	if (bCheckDir && visibility > 0.0f)
	{
		float verticalFov = AspectRatio() > 0.0f ? AIHorizontalFov() / AspectRatio() : 0.0f;
		float width = (float)(std::atan(std::sqrt(radiusSq / distSq)) * 114.59155902616465); // degrees across
		float height = (float)(std::atan(std::sqrt(heightSq / distSq)) * 114.59155902616465);

		Rotator view = (UObject::TryCast<UPlayerPawn>(this) ? ViewRotation() : Rotation()) + AIAddViewRotation();
		Coords axes = Coords::Rotation(view);
		vec3 local = { dot(delta, axes.XAxis), dot(delta, axes.YAxis), dot(delta, axes.ZAxis) };

		// FVector::Rotation's yaw and pitch, in its whole units
		const float unitsPerRadian = 65535.0f / (2.0f * 3.14159265358979f);
		int yaw = (int)(std::atan2(local.y, local.x) * unitsPerRadian);
		int pitch = (int)(std::atan2(local.z, std::sqrt(local.x * local.x + local.y * local.y)) * unitsPerRadian);
		visibility *= AIFieldOfViewShare((float)(yaw * 360.0 / 65536.0), AIHorizontalFov(), width);
		float vertical = AIFieldOfViewShare((float)(pitch * 360.0 / 65536.0), verticalFov, height);
		if (distSq < 22500.0 && vertical < 0.75f) // within 150 units, above or below still shows
			vertical = 0.75f;
		visibility *= vertical;
	}

	if (bCheckVisibility && visibility > 0.0f)
		visibility *= other->AIVisibility(true);

	visibility -= VisibilityThreshold();
	if (visibility < 0.0f)
		visibility = 0.0f;
	else if (visibility >= 1.0f)
		visibility = 1.0f;

	if (bCheckLOS && visibility > 0.0f)
	{
		// What blocks: any actor but this pawn, what owns it and the other,
		// if it blocks sight and is not hidden
		CollisionSystem& collision = XLevel()->Collision;
		auto blocksSight = [&](UActor* actor) {
			for (UActor* viewer = this; viewer; viewer = viewer->Owner())
			{
				if (actor == viewer)
					return false;
			}
			return actor != other && actor->bBlockSight() && !actor->bHidden();
		};
		auto reaches = [&](const vec3& point) { return !collision.SightBlocked(eye, point, blocksSight); };

		const vec3& center = other->Location();
		bool seen;
		if (!bCheckCylinder)
		{
			seen = reaches(center);
		}
		else
		{
			UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(other);
			seen = (player && reaches(center + vec3(0.0f, 0.0f, player->BaseEyeHeight()))) ||
				reaches(center + vec3(0.0f, 0.0f, other->CollisionHeight())) ||
				reaches(center - vec3(0.0f, 0.0f, other->CollisionHeight()));
			if (!player && !seen)
			{
				// Its sides, square to the line to it
				vec3 across = { delta.x, delta.y, 0.0f };
				float lengthSq = dot(across, across);
				across = lengthSq < 1e-8f ? vec3(0.0f) : across * (1.0f / std::sqrt(lengthSq));
				float radius = other->CollisionRadius();
				seen = reaches(center + vec3(across.y * radius, -across.x * radius, 0.0f)) ||
					reaches(center + vec3(-across.y * radius, across.x * radius, 0.0f));
			}
		}
		if (!seen)
			visibility = 0.0f;
	}
	return visibility;
}

float UPawn::AICanSmell(UActor* other, std::optional<float> smell)
{
	LogUnimplemented("Pawn.AICanSmell() [Deus Ex]");
	return 0.0f;
}

bool UPawn::PickWallAdjust()
{
	auto kneeHeight = CollisionHeight() * 0.45f;

	auto forwards = normalize(Acceleration().xy());

	auto afterJumpCollisionHit = TryMove(vec3(forwards, kneeHeight), true);

	if (afterJumpCollisionHit.Fraction == 1)
	{
		// Obstacle can be jumped over. Attempt jumping.
		bFromWall() = false;
		Velocity().z = JumpZ();
		SetPhysics(PHYS_Falling);
		Destination() = Location() + vec3(forwards, kneeHeight);

		return true;
	}

	// Obstacle cannot be jumped over. Try another direction
	auto direction = Focus() - Location();
	auto rightSideVec = normalize(cross(direction, vec3(0, 0, 1)));
	auto rightSideTest = TryMove(rightSideVec, true);
	if (rightSideTest.Fraction == 1)
	{
		// We can move to right instead
		bFromWall() = true;
		Destination() = Location() + rightSideVec;
		// Focus() = Location() + rightSideVec;

		return true;
	}

	auto leftSideVec = -rightSideVec;
	auto leftSideTest = TryMove(leftSideVec, true);
	if (leftSideTest.Fraction >= 1)
	{
		// We can move to left instead
		bFromWall() = true;
		Destination() = Location() + leftSideVec;
		// Focus() = Location() + leftSideVec;

		return true;
	}

	// Cannot go anywhere from here
	return false;
}

vec3 UPawn::EAdjustJump()
{
	UZoneInfo* zone = FootRegion().Zone;
	vec3 gravity = zone ? zone->ZoneGravity() : vec3(0.0f, 0.0f, -980.0f);

	const float dt = 0.05f;
	const float jumpZ = JumpZ();
	vec3 pos = Location();
	vec3 vel = vec3(0.0f, 0.0f, jumpZ);
	float time = 0.0f;
	const float maxSimTime = 5.0f;
	const float targetZ = Location().z;
	while (time < maxSimTime && pos.z < targetZ)
	{
		vel.z += gravity.z * dt;
		pos.z += vel.z * dt;
		time += dt;
		if (pos.z >= targetZ) break;
	}

	vec3 target = Focus();
	if (dot(target - Location(), target - Location()) < 0.001f)
		target = Destination();
	vec3 horizontalDir = normalize(target - Location());
	horizontalDir.z = 0.0f;

	vec3 horizontalVel = horizontalDir * (length(target - Location()) / std::max(time, 0.001f));

	float groundSpeed = GroundSpeed();
	float horizSpeed = length(horizontalVel);
	if (horizSpeed > groundSpeed)
		horizontalVel = horizontalVel * (groundSpeed / horizSpeed);

	return horizontalVel + vec3(0.0f, 0.0f, jumpZ);
}

// The original's calcMoveFlags: which reach specs the pawn may use.
int UPawn::CalcMoveFlags()
{
	int flags = 0;
	if (bCanWalk()) flags |= 1;      // R_WALK
	if (bCanFly()) flags |= 2;       // R_FLY
	if (bCanSwim()) flags |= 4;      // R_SWIM
	if (bCanJump()) flags |= 8;      // R_JUMP
	if (bCanOpenDoors()) flags |= 16;   // R_DOOR
	if (bCanDoSpecial()) flags |= 32;   // R_SPECIAL
	return flags;
}

// The original's AIDirectionReachable (0x103c78a0): whether the pawn can
// get, along a direction, to a spot whose distance from focus is between
// the two bounds. It moves the pawn itself in steps of its collision
// radius (held between 5 and 25 units, at most 100 of them), each the
// engine's own walk, fly or swim move, and puts it back.
bool UPawn::AIDirectionReachable(const vec3& focus, int yaw, int pitch, float minDist, float maxDist, vec3& bestDest)
{
	// A copy: the caller may hand the pawn's own Location(), a reference
	// into its property data, and the pawn moves below.
	vec3 focusPoint = focus;
	vec3 startLocation = Location();
	vec3 startVelocity = Velocity();
	bestDest = startLocation;

	// In a water zone it swims, along yaw and pitch; otherwise walking (or
	// swimming out of water) walks along the yaw alone and flying flies
	// along both; any other physics fails.
	bool inWater = Region().Zone && Region().Zone->bWaterZone();
	uint8_t physics = Physics();
	enum class Mode { Walk, Fly, Swim };
	Mode mode;
	if (inWater)
		mode = Mode::Swim;
	else if (physics == PHYS_Walking || physics == PHYS_Swimming)
		mode = Mode::Walk;
	else if (physics == PHYS_Flying)
		mode = Mode::Fly;
	else
		return false;

	Rotator direction((mode == Mode::Walk) ? 0 : pitch, yaw, 0);
	vec3 dir = Coords::Rotation(direction).XAxis;

	float step = std::clamp(CollisionRadius(), 5.0f, 25.0f);
	float maxStep = MaxStepHeight();

	// A walk step goes up, along and back to the floor; a drop past
	// MaxStepHeight below the start is a ledge, undone.
	enum class StepResult { Moved, Blocked, Ledge };
	auto moveStep = [&](float stepSize) -> StepResult
	{
		vec3 delta = dir * stepSize;
		if (mode != Mode::Walk)
			return TryMove(delta).Fraction < 1.0f ? StepResult::Blocked : StepResult::Moved;

		vec3 before = Location();
		TryMove(vec3(0.0f, 0.0f, maxStep));
		float wentUp = Location().z - before.z;
		CollisionHit lateral = TryMove(delta);
		CollisionHit down = TryMove(vec3(0.0f, 0.0f, -(wentUp + maxStep)));
		if (down.Fraction == 1.0f)
		{
			// No floor within MaxStepHeight below where it started: a ledge.
			SetLocation(before);
			return StepResult::Ledge;
		}
		if (lateral.Fraction < 0.5f)
			return StepResult::Blocked;
		return StepResult::Moved;
	};

	// A pain zone whose damage the pawn does not resist, and the void,
	// stop it; so does entering water, or leaving it when swimming.
	auto zoneStops = [&]() -> bool
	{
		if (Region().ZoneNumber == 0)
			return true;
		UZoneInfo* zone = Region().Zone;
		if (zone && zone->bPainZone() && zone->DamageType() != ReducedDamageType())
			return true;
		bool nowInWater = zone && zone->bWaterZone();
		return (mode == Mode::Swim) ? !nowInWater : nowInWater;
	};

	float prevDist = length(startLocation - focusPoint);
	bool prevIn = prevDist >= minDist && prevDist <= maxDist;
	bool found = false;
	float best = -1.0f;
	bool triedLedgeStep = false;

	for (int i = 0; i < 100; i++)
	{
		StepResult result = moveStep(step);
		if (result == StepResult::Ledge && mode == Mode::Walk && !triedLedgeStep)
		{
			// A walk stopped by a ledge tries once more with a step of
			// MaxStepHeight.
			triedLedgeStep = true;
			result = moveStep(maxStep);
		}
		if (result != StepResult::Moved)
			break;
		if (zoneStops())
			break;

		float dist = length(Location() - focusPoint);
		bool inRange = dist >= minDist && dist <= maxDist;
		if (inRange)
		{
			// While the spot is in range it goes on, keeping the farthest.
			if (dist > best)
			{
				best = dist;
				bestDest = Location();
				found = true;
			}
			// It stops on coming into the range from beyond.
			if (!prevIn && prevDist > maxDist)
				break;
		}
		else
		{
			// It stops on leaving the range, or on crossing it in one
			// step, which counts as found.
			if (prevIn)
				break;
			if ((prevDist < minDist && dist > maxDist) || (prevDist > maxDist && dist < minDist))
			{
				bestDest = Location();
				found = true;
				break;
			}
		}
		prevDist = dist;
		prevIn = inRange;
	}

	SetLocation(startLocation);
	Velocity() = startVelocity;
	return found;
}

// The original's AIPickRandomDestination (0x103c7fc0): up to tries
// directions from RandomBiasedRotation, each tested with
// AIDirectionReachable; a multiplier below 1 stops the pawn short of what
// it can reach.
bool UPawn::AIPickRandomDestination(float minDist, float maxDist, int centralYaw, float yawDistribution, int centralPitch, float pitchDistribution, int tries, float multiplier, vec3& dest)
{
	dest = Location();
	tries = std::max(tries, 1);
	multiplier = std::clamp(multiplier, 0.0001f, 1.0f);
	bool walking = !(Region().Zone && Region().Zone->bWaterZone()) && Physics() != PHYS_Flying;

	for (int i = 0; i < tries; i++)
	{
		Rotator direction = UActor::RandomBiasedRotation(centralYaw, yawDistribution, walking ? 0 : centralPitch, pitchDistribution);
		vec3 found;
		if (!AIDirectionReachable(Location(), direction.Yaw, direction.Pitch, minDist / multiplier, maxDist / multiplier, found))
			continue;
		if (multiplier < 1.0f)
		{
			// Try again to multiplier of the distance reached, so the pawn
			// stops short of what it can reach.
			float reached = length(found - Location()) * multiplier;
			if (!AIDirectionReachable(Location(), direction.Yaw, direction.Pitch, std::min(minDist, reached), reached, found))
				continue;
		}
		dest = found;
		return true;
	}
	return false;
}

// The original's GetPathnodeList (0x103c6490), which ReachablePathnodes
// iterates and ComputePathnodeDistances seeds from.
Array<std::pair<UNavigationPoint*, float>> UPawn::GetPathnodeList(UActor* fromPoint, bool usePrunedPaths)
{
	Array<std::pair<UNavigationPoint*, float>> nodes;

	// The start node: FromPoint when it is a navigation point; else the
	// pawn's MoveTarget when that is one the pawn overlaps; else the first
	// in the level's list the pawn overlaps.
	UNavigationPoint* start = UObject::TryCast<UNavigationPoint>(fromPoint);
	if (!start)
	{
		if (UNavigationPoint* target = UObject::TryCast<UNavigationPoint>(MoveTarget()))
		{
			if (IsOverlapping(target))
				start = target;
		}
	}
	if (!start)
	{
		for (UNavigationPoint* nav = Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
		{
			if (IsOverlapping(nav))
			{
				start = nav;
				break;
			}
		}
	}

	if (start)
	{
		// The far end of each of the start's paths whose reach spec the
		// pawn fits and may use, at the spec's distance.
		int moveFlags = CalcMoveFlags();
		auto& specs = XLevel()->ReachSpecs;
		auto addPaths = [&](FixedArrayView<int, 16> paths)
		{
			for (int i = 0; i < 16 && nodes.size() < 32; i++)
			{
				int index = paths[i];
				if (index < 0 || (size_t)index >= specs.size())
					continue;
				const LevelReachSpec& spec = specs[index];
				if (!spec.endActor || spec.endActor == start)
					continue;
				if (spec.collisionRadius < (int)CollisionRadius() || spec.collisionHeight < (int)CollisionHeight())
					continue;
				if ((spec.reachFlags & moveFlags) != spec.reachFlags)
					continue;
				nodes.push_back({ spec.endActor, (float)spec.distance });
			}
		};
		addPaths(start->Paths());
		if (usePrunedPaths)
			addPaths(start->PrunedPaths());
	}
	else
	{
		// With no start node, the nearest nodes within 1,000 units of
		// FromPoint, or of the pawn, that the pawn can reach, at the
		// straight distance.
		vec3 from = fromPoint ? UObject::Cast<UActor>(fromPoint)->Location() : Location();
		for (UNavigationPoint* nav = Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
		{
			float dist = length(nav->Location() - from);
			if (dist > 1000.0f)
				continue;
			if (!ActorReachable(nav, true))
				continue;
			nodes.push_back({ nav, dist });
		}
	}

	std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
	if (nodes.size() > 32)
		nodes.resize(32);
	return nodes;
}

// The original's ComputePathnodeDistances (0x103c8910): clears the paths,
// then sets each node's visitedWeight to its shortest distance over the
// path network from GetPathnodeList's nodes. No script calls it.
void UPawn::ComputePathnodeDistances(UActor* startActor)
{
	for (UNavigationPoint* nav = Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
		nav->visitedWeight() = INT_MAX;

	using Entry = std::pair<float, UNavigationPoint*>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
	for (auto& [node, dist] : GetPathnodeList(startActor, false))
	{
		if ((int)dist < node->visitedWeight())
		{
			node->visitedWeight() = (int)dist;
			queue.push({ dist, node });
		}
	}

	int moveFlags = CalcMoveFlags();
	auto& specs = XLevel()->ReachSpecs;
	while (!queue.empty())
	{
		auto [dist, node] = queue.top();
		queue.pop();
		if ((int)dist > node->visitedWeight())
			continue;
		for (int i = 0; i < 16; i++)
		{
			int index = node->Paths()[i];
			if (index < 0 || (size_t)index >= specs.size())
				continue;
			const LevelReachSpec& spec = specs[index];
			if (!spec.endActor)
				continue;
			if (spec.collisionRadius < (int)CollisionRadius() || spec.collisionHeight < (int)CollisionHeight())
				continue;
			if ((spec.reachFlags & moveFlags) != spec.reachFlags)
				continue;
			int next = node->visitedWeight() + spec.distance;
			if (next < spec.endActor->visitedWeight())
			{
				spec.endActor->visitedWeight() = next;
				queue.push({ (float)next, spec.endActor });
			}
		}
	}
}
