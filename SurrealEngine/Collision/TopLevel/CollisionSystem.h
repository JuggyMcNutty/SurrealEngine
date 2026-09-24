#pragma once

#include "Math/vec.h"
#include <functional>

class ULevel;
class UActor;
class CollisionHitList;
class CollisionHit;

struct TraceFlags
{
	bool pawns = false;
	bool movers = false;
	bool others = false;
	bool world = false;
	bool zoneChanges = false;
	bool onlyProjectiles = false;

	bool traceActors() const { return pawns || movers || others || zoneChanges || onlyProjectiles; }
	bool traceWorld() const { return world; }
};

class CollisionSystem
{
public:
	bool IsOverlapping(UActor* actor1, UActor* actor2);
	CollisionHitList OverlapTest(UActor* actor);
	CollisionHitList OverlapTest(const vec3& location, float height, float radius, bool testActors, bool testWorld, bool visibilityOnly);

	bool TraceAnyHit(vec3 from, vec3 to, UActor* tracingActor, bool traceActors, bool traceWorld, bool visibilityOnly);
	CollisionHitList Trace(const vec3& from, const vec3& to, float height, float radius, bool traceActors, bool traceWorld, bool visibilityOnly);
	CollisionHitList TraceDecal(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly);
	CollisionHit TraceFirstHit(const vec3& from, const vec3& to, UActor* tracingActor, const vec3& extents, const TraceFlags& flags);

	// Whether sight along a segment is blocked: by world and mover surfaces
	// that block visibility, or by an actor on it that blocksSight says blocks
	// (Deus Ex's AI line of sight).
	bool SightBlocked(const vec3& from, const vec3& to, const std::function<bool(UActor* actor)>& blocksSight);

	Array<UActor*> CollidingActors(const vec3& origin, float radius);
	Array<UActor*> CollidingActors(const vec3& origin, float height, float radius);
	Array<UActor*> EncroachingActors(UActor* actor);

	void SetLevel(ULevel* level);
	void AddToCollision(UActor* actor);
	void RemoveFromCollision(UActor* actor);

private:
	static ivec3 GetStartExtents(const vec3& location, const vec3& extents)
	{
		int xx = (int)std::floor((location.x - extents.x) * (1.0f / 256.0f));
		int yy = (int)std::floor((location.y - extents.y) * (1.0f / 256.0f));
		int zz = (int)std::floor((location.z - extents.z) * (1.0f / 256.0f));
		return { xx, yy, zz };
	}

	static ivec3 GetRayStartExtents(const vec3& from, const vec3& to)
	{
		float x = std::min(from.x, to.x);
		float y = std::min(from.y, to.y);
		float z = std::min(from.z, to.z);
		int xx = (int)std::floor(x * (1.0f / 256.0f));
		int yy = (int)std::floor(y * (1.0f / 256.0f));
		int zz = (int)std::floor(z * (1.0f / 256.0f));
		return { xx, yy, zz };
	}

	static ivec3 GetSweepStartExtents(const vec3& from, const vec3& to, const vec3& extents)
	{
		float x = std::min(from.x, to.x) - extents.x;
		float y = std::min(from.y, to.y) - extents.y;
		float z = std::min(from.z, to.z) - extents.z;
		int xx = (int)std::floor(x * (1.0f / 256.0f));
		int yy = (int)std::floor(y * (1.0f / 256.0f));
		int zz = (int)std::floor(z * (1.0f / 256.0f));
		return { xx, yy, zz };
	}

	static ivec3 GetEndExtents(const vec3& location, const vec3& extents)
	{
		int xx = (int)std::floor((location.x + extents.x) * (1.0f / 256.0f)) + 1;
		int yy = (int)std::floor((location.y + extents.y) * (1.0f / 256.0f)) + 1;
		int zz = (int)std::floor((location.z + extents.z) * (1.0f / 256.0f)) + 1;
		return { xx, yy, zz };
	}

	static ivec3 GetRayEndExtents(const vec3& from, const vec3& to)
	{
		float x = std::max(from.x, to.x);
		float y = std::max(from.y, to.y);
		float z = std::max(from.z, to.z);
		int xx = (int)std::floor(x * (1.0f / 256.0f)) + 1;
		int yy = (int)std::floor(y * (1.0f / 256.0f)) + 1;
		int zz = (int)std::floor(z * (1.0f / 256.0f)) + 1;
		return { xx, yy, zz };
	}

	static ivec3 GetSweepEndExtents(const vec3& from, const vec3& to, const vec3& extents)
	{
		float x = std::max(from.x, to.x) + extents.x;
		float y = std::max(from.y, to.y) + extents.y;
		float z = std::max(from.z, to.z) + extents.z;
		int xx = (int)std::floor(x * (1.0f / 256.0f)) + 1;
		int yy = (int)std::floor(y * (1.0f / 256.0f)) + 1;
		int zz = (int)std::floor(z * (1.0f / 256.0f)) + 1;
		return { xx, yy, zz };
	}

	static uint32_t GetBucketId(int x, int y, int z)
	{
		return ((x & 0x3ff) << 20) | ((y & 0x3ff) << 10) | (z & 0x3ff);
	}

	// The actors whose collision boxes reach each 256-unit cell, in the order
	// they were added. An open-addressed table keyed by bucket id (linear
	// probing, a power-of-two size, at most half full): finding a cell is a
	// multiply and, for an empty one, usually one cache line of ids, where a
	// std::unordered_map of std::lists took a division and a chain of
	// pointers, and a list node allocated for every cell of every move. A
	// cell keeps its slot once used, so the table only ever grows.
	const Array<UActor*>& FindCell(uint32_t id) const;
	Array<UActor*>& Cell(uint32_t id); // made if new
	size_t FindSlot(uint32_t id) const; // NoSlot if the cell was never used
	void GrowCells();
	static uint32_t CellHash(uint32_t id) { uint32_t h = id * 0x9E3779B1u; return h ^ (h >> 16); }
	static const uint32_t NoCell = 0xffffffff; // never a bucket id: those have 30 bits
	static const size_t NoSlot = ~(size_t)0;

	ULevel* Level = nullptr;
	Array<uint32_t> CellIds; // NoCell where a slot is free
	Array<Array<UActor*>> CellActors;
	size_t CellCount = 0;
	inline static const Array<UActor*> NoActors;

	friend class CollisionTester;
};
