
#include "Precomp.h"
#include "CollisionSystem.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Math/floating.h"
#include "Math/coords.h"
#include "Collision/BottomLevel/TraceRayModel.h"
#include "Collision/TopLevel/OverlapTest.h"
#include "Collision/TopLevel/TraceTest.h"

bool CollisionSystem::IsOverlapping(UActor* actor1, UActor* actor2)
{
	OverlapTester overlap(this);
	return overlap.IsOverlapping(actor1, actor2);
}

CollisionHitList CollisionSystem::OverlapTest(UActor* actor)
{
	return OverlapTest(actor->Location(), actor->CollisionHeight(), actor->CollisionRadius(), true, false, false);
}

CollisionHitList CollisionSystem::OverlapTest(const vec3& location, float height, float radius, bool testActors, bool testWorld, bool visibilityOnly)
{
	OverlapTester overlap(this);
	return overlap.TestOverlap(location, height, radius, testActors, testWorld, visibilityOnly);
}

bool CollisionSystem::TraceAnyHit(vec3 from, vec3 to, UActor* tracingActor, bool traceActors, bool traceWorld, bool visibilityOnly)
{
	TraceTester trace(this);
	return trace.TraceAnyHit(from, to, tracingActor, traceActors, traceWorld, visibilityOnly);
}

CollisionHitList CollisionSystem::Trace(const vec3& from, const vec3& to, float height, float radius, bool traceActors, bool traceWorld, bool visibilityOnly)
{
	TraceTester trace(this);
	return trace.Trace(from, to, height, radius, traceActors, traceWorld, visibilityOnly);
}

CollisionHitList CollisionSystem::TraceDecal(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly)
{
	TraceRayModel trace;
	return trace.Trace(Level->Model, origin, tmin, dirNormalized, tmax, visibilityOnly);
}

bool CollisionSystem::SightBlocked(const vec3& from, const vec3& to, const std::function<bool(UActor* actor)>& blocksSight)
{
	TraceTester trace(this);
	return trace.SightBlocked(from, to, blocksSight);
}

CollisionHit CollisionSystem::TraceFirstHit(const vec3& from, const vec3& to, UActor* tracingActor, const vec3& extents, const TraceFlags& flags)
{
	for (const CollisionHit& hit : Trace(from, to, extents.z, extents.x, flags.traceActors(), flags.traceWorld(), false))
	{
		if (hit.Actor && (!tracingActor || !tracingActor->IsOwnedBy(hit.Actor)))
		{
			if (hit.Actor->IsA("Pawn"))
			{
				if (flags.pawns)
					return hit;
			}
			else if (hit.Actor->IsA("Mover"))
			{
				if (flags.movers)
					return hit;
			}
			else if (hit.Actor->IsA("ZoneInfo"))
			{
				if (flags.zoneChanges)
					return hit;
			}
			else if (flags.others)
			{
				if (!flags.onlyProjectiles || hit.Actor->bProjTarget() || (hit.Actor->bBlockActors() && hit.Actor->bBlockPlayers()))
					return hit;
			}
		}
		else if (flags.world && !hit.Actor)
		{
			CollisionHit worldHit = hit;
			if (tracingActor)
				worldHit.Actor = tracingActor->Level();
			return worldHit;
		}
	}
	return {};
}

Array<UActor*> CollisionSystem::CollidingActors(const vec3& origin, float radius)
{
	OverlapTester overlap(this);
	return overlap.CollidingActors(origin, radius);
}

Array<UActor*> CollisionSystem::CollidingActors(const vec3& origin, float height, float radius)
{
	OverlapTester overlap(this);
	return overlap.CollidingActors(origin, height, radius);
}

Array<UActor*> CollisionSystem::EncroachingActors(UActor* actor)
{
	OverlapTester overlap(this);
	return overlap.EncroachingActors(actor);
}

void CollisionSystem::SetLevel(ULevel* level)
{
	Level = level;
}

void CollisionSystem::AddToCollision(UActor* actor)
{
	if (actor->bCollideActors())
	{
		vec3 location = actor->Location();
		float height = actor->CollisionHeight();
		float radius = actor->CollisionRadius();
		vec3 extents = { radius, radius, height };

		if (UMover* mover = UObject::TryCast<UMover>(actor))
		{
			UModel* brush = mover->Brush();
			if (brush)
			{
				mat4 objectToWorld = mat4::translate(actor->Location()) * Coords::Rotation(actor->Rotation()).ToMatrix() * mat4::scale(mover->MainScale().Scale) * mat4::translate(-actor->PrePivot());
				BBox bbox = brush->BoundingBox.transform(objectToWorld);
				location = bbox.center();
				extents = bbox.extents() + 0.1f; // 0.1 for numerical stability
			}
		}

		actor->Collision.Inserted = true;
		actor->Collision.Location = location;
		actor->Collision.Extents = extents;

		ivec3 start = GetStartExtents(location, extents);
		ivec3 end = GetEndExtents(location, extents);
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					Cell(GetBucketId(x, y, z)).push_back(actor);
				}
			}
		}
	}
}

void CollisionSystem::RemoveFromCollision(UActor* actor)
{
	if (actor->Collision.Inserted)
	{
		vec3 location = actor->Collision.Location;
		vec3 extents = actor->Collision.Extents;

		ivec3 start = GetStartExtents(location, extents);
		ivec3 end = GetEndExtents(location, extents);
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					// Every entry, keeping the others in order, as std::list::remove did
					size_t slot = FindSlot(GetBucketId(x, y, z));
					if (slot != NoSlot)
					{
						Array<UActor*>& cell = CellActors[slot];
						cell.erase(std::remove(cell.begin(), cell.end(), actor), cell.end());
					}
				}
			}
		}

		actor->Collision.Inserted = false;
	}
}

size_t CollisionSystem::FindSlot(uint32_t id) const
{
	if (CellIds.empty())
		return NoSlot;
	size_t mask = CellIds.size() - 1;
	for (size_t slot = CellHash(id) & mask; ; slot = (slot + 1) & mask)
	{
		uint32_t cellId = CellIds[slot];
		if (cellId == id)
			return slot;
		if (cellId == NoCell)
			return NoSlot;
	}
}

const Array<UActor*>& CollisionSystem::FindCell(uint32_t id) const
{
	size_t slot = FindSlot(id);
	return slot != NoSlot ? CellActors[slot] : NoActors;
}

Array<UActor*>& CollisionSystem::Cell(uint32_t id)
{
	if ((CellCount + 1) * 2 > CellIds.size())
		GrowCells();
	size_t mask = CellIds.size() - 1;
	size_t slot = CellHash(id) & mask;
	while (CellIds[slot] != id && CellIds[slot] != NoCell)
		slot = (slot + 1) & mask;
	if (CellIds[slot] == NoCell)
	{
		CellIds[slot] = id;
		CellCount++;
	}
	return CellActors[slot];
}

void CollisionSystem::GrowCells()
{
	size_t capacity = std::max<size_t>(4096, CellIds.size() * 2);
	Array<uint32_t> ids(capacity, NoCell);
	Array<Array<UActor*>> actors(capacity);
	size_t mask = capacity - 1;
	for (size_t i = 0; i < CellIds.size(); i++)
	{
		if (CellIds[i] == NoCell)
			continue;
		size_t slot = CellHash(CellIds[i]) & mask;
		while (ids[slot] != NoCell)
			slot = (slot + 1) & mask;
		ids[slot] = CellIds[i];
		actors[slot] = std::move(CellActors[i]);
	}
	CellIds = std::move(ids);
	CellActors = std::move(actors);
}
