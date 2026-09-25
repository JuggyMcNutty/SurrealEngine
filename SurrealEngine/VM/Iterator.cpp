
#include "Precomp.h"
#include "Iterator.h"
#include "Packages/Core/UClass.h"
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

#include <algorithm>

// The slots of the level's actors that pass test, rebuilt only when the
// actor list has changed. The actor iterators tested every actor in the level
// for its class -- Liberty Island has ~2,500 -- at a cache miss each on the
// handheld.
template<typename Key, typename Test>
static const Array<int>& ActorSlots(std::map<Key, ULevelBase::ClassSlots>& indexes, ULevel* level, Key key, Test test)
{
	ULevelBase::ClassSlots& index = indexes[key];
	if (index.Version != level->ActorsVersion)
	{
		index.Slots.clear();
		const Array<UActor*>& actors = level->Actors;
		for (size_t i = 0; i < actors.size(); i++)
		{
			if (actors[i] && test(actors[i]))
				index.Slots.push_back((int)i);
		}
		index.Version = level->ActorsVersion;
	}
	return index.Slots;
}

// The slots of actors whose class, or a parent of it, has the name
static const Array<int>& ActorSlotsByName(ULevel* level, const NameString& className)
{
	return ActorSlots(level->ActorsByClassName, level, className.GetCompareIndex(), [&](UActor* actor) { return actor->IsA(className); });
}

AllObjectsIterator::AllObjectsIterator(UObject* BaseClass, UObject** ReturnValue, UObject* InOuter)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), InOuter(InOuter), m_Objects(GC::GetObjects()), m_Iterator(m_Objects.begin())
{
}

bool AllObjectsIterator::Next()
{
	while (m_Iterator != m_Objects.end())
	{
		auto currentObject = dynamic_cast<UObject*>(*m_Iterator);

		++m_Iterator;

		if (currentObject && currentObject->IsA(BaseClass->Name) && (!InOuter || InOuter == currentObject->Outer()))
		{
			*ReturnValue = currentObject;
			return true;
		}
	}
	*ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

AllActorsIterator::AllActorsIterator(UObject* BaseClass, UObject** ReturnValue, NameString MatchTag)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), MatchTag(MatchTag)
{
}

AllActorsIterator::AllActorsIterator(UObject* BaseClass, UObject** ReturnValue, NameString MatchTag, NameString MatchEvent)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), MatchTag(MatchTag), MatchEvent(MatchEvent)
{
}

AllActorsIterator::AllActorsIterator(UObject* BaseClass, UObject** ReturnValue, NameString MatchTag, NameString MatchEvent, bool bAllLevels)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), MatchTag(MatchTag), MatchEvent(MatchEvent), bAllLevels(bAllLevels)
{
}

bool AllActorsIterator::Next()
{
	bool matchTag = !MatchTag.IsNone();
	bool matchEvent = !MatchEvent.IsNone();

	const Array<UActor*>& actors = engine->Level->Actors;
	const Array<int>& slots = ActorSlotsByName(engine->Level, BaseClass->Name);
	for (auto it = std::lower_bound(slots.begin(), slots.end(), (int)index); it != slots.end(); ++it)
	{
		UActor* actor = actors[*it];
		index = (size_t)*it + 1;
		if (actor && (!matchTag || actor->Tag() == MatchTag) && (!matchEvent || actor->Event() == MatchEvent))
		{
			*ReturnValue = actor;
			return true;
		}
	}
	index = actors.size();
	*ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

AllFilesIterator::AllFilesIterator(const std::string& FileExtension, const std::string& FilePrefix, std::string& FileName) : FileExtension(FileExtension), FilePrefix(FilePrefix), FileName(FileName)
{
	auto packageNames = engine->packages->GetPackageNames();

	for (auto& packageName : packageNames)
	{
		auto package = engine->packages->GetPackage(packageName);

		if ((FileExtension.empty() || package->GetPackageFileExtension() == "." + FileExtension) &&
			(FilePrefix.empty() || StrTools::startswith(package->GetPackageFileName(), FilePrefix, true)))
		{
			FoundFiles.push_back(package->GetPackageFileName());
		}
	}

	iterator = FoundFiles.begin();
}

bool AllFilesIterator::Next()
{
	if (iterator == FoundFiles.end())
	{
		FileName = {};
		return false;
	}

	FileName = *iterator;
	iterator++;

	return true;
}

/////////////////////////////////////////////////////////////////////////////,

AllParticlesIterator::AllParticlesIterator(UObject** ReturnValue)
	: ReturnValue(ReturnValue)
{
}

bool AllParticlesIterator::Next()
{
	ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

BasedActorsIterator::BasedActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor) : BaseClass(BaseClass), Actor(Actor)
{
	for (UActor* based : Caller->BasedActors)
	{
		if (!based || !based->Class) continue;
		if (based->IsA(BaseClass->Name))
			BasedActors.push_back(based);
	}
	iterator = BasedActors.begin();
}

bool BasedActorsIterator::Next()
{
	if (iterator == BasedActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

ChildActorsIterator::ChildActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor) : BaseClass(BaseClass), Actor(Actor)
{
	for (UActor* levelActor : Caller->ChildActors)
	{
		if (levelActor->IsA(BaseClass->Name))
			ChildActors.push_back(levelActor);
	}

	iterator = ChildActors.begin();
}

bool ChildActorsIterator::Next()
{
	if (iterator == ChildActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////
CycleActorsIterator::CycleActorsIterator(UObject* BaseClass, UObject** Actor, int* outIndex) : BaseClass(UObject::TryCast<UStruct>(BaseClass)), Actor(Actor), outIndex(outIndex)
{
	size_t size = engine->Level->Actors.size();
	position = (outIndex && *outIndex >= 0 && (size_t)*outIndex < size) ? (size_t)*outIndex : 0;
}

// Every NPC's CheckEnemyPresence cycles through the pawns this way each tick,
// and a scan of the level's actors for the next one cost a cache miss per
// actor -- about a sixth of the game tick on the handheld. The level keeps
// the slots holding each class asked for, rebuilt when its actor list
// changes, so each step jumps to the next such slot. The order, the lap and
// the indexes are the scan's.
bool CycleActorsIterator::Next()
{
	ULevel* level = engine->Level;
	const Array<UActor*>& actors = level->Actors;
	size_t size = actors.size();
	if (!BaseClass || size == 0 || position >= size)
		return NextByScan();

	// Class pointers along the BaseStruct chain, as the scan tests them
	const Array<int>& slots = ActorSlots(level->ActorsByClass, level, BaseClass, [&](UActor* actor) {
		for (UStruct* cls = actor->Class; cls; cls = cls->BaseStruct)
		{
			if (cls == BaseClass)
				return true;
		}
		return false;
	});

	while (visited < size && !slots.empty())
	{
		// The next slot of the class after position, going round: the scan
		// would reach it after step more slots, the start's own slot last.
		auto it = std::upper_bound(slots.begin(), slots.end(), (int)position);
		size_t slot = (it != slots.end()) ? (size_t)*it : (size_t)slots.front();
		size_t step = (slot + size - position) % size;
		if (step == 0)
			step = size;
		if (visited + step > size)
			break;
		visited += step;
		position = slot;

		UActor* candidate = actors[slot];
		if (!candidate || candidate->bDeleteMe())
			continue;
		*Actor = candidate;
		if (outIndex)
			*outIndex = (int)position;
		return true;
	}
	visited = size;
	*Actor = nullptr;
	return false;
}

// No class, an empty level or a start past the end: the scan itself
bool CycleActorsIterator::NextByScan()
{
	const Array<UActor*>& actors = engine->Level->Actors;
	size_t size = actors.size();
	while (BaseClass && size > 0 && visited < size)
	{
		visited++;
		position = (position + 1) % size;
		UActor* candidate = actors[position];
		if (!candidate || candidate->bDeleteMe())
			continue;

		// Class pointers along the BaseStruct chain: the same test as
		// IsA(BaseClass->Name) without a name comparison per level.
		for (UStruct* cls = candidate->Class; cls; cls = cls->BaseStruct)
		{
			if (cls == BaseClass)
			{
				*Actor = candidate;
				if (outIndex)
					*outIndex = (int)position;
				return true;
			}
		}
	}
	*Actor = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

IntDescIterator::IntDescIterator(std::string& className, std::string* entryName, std::string* desc, std::optional<bool> bSingleNames)
	: ClassName(className), EntryName(entryName), Desc(desc), bSingleNames(bSingleNames)
{
	IntObjects = engine->packages->GetIntObjects(ClassName);

	if (bSingleNames && *bSingleNames)
	{
		// TODO: Remove duplicates
	}

	it = IntObjects.begin();
}

bool IntDescIterator::Next()
{
	if (it == IntObjects.end())
	{
		EntryName = nullptr;
		Desc = nullptr;
		return false;
	}

	if (EntryName)
		*EntryName = it->Name.ToString();

	if (Desc)
		*Desc = it->Description;

	it++;

	return true;
}

/////////////////////////////////////////////////////////////////////////////

RadiusActorsIterator::RadiusActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor, float Radius, vec3 Location) : BaseClass(BaseClass), Actor(Actor), Radius(Radius), Location(Location)
{
	const Array<UActor*>& actors = engine->Level->Actors;
	for (int slot : ActorSlotsByName(engine->Level, BaseClass->Name))
	{
		UActor* levelActor = actors[slot];
		if (length(levelActor->Location() - Location) <= Radius)
			RadiusActors.push_back(levelActor);
	}

	iterator = RadiusActors.begin();
}

bool RadiusActorsIterator::Next()
{
	if (iterator == RadiusActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

TouchingActorsIterator::TouchingActorsIterator(UActor* Caller, UObject* BaseClass, UObject** outActor) : BaseClass(BaseClass), outActor(outActor)
{
	CollisionHitList hitList = Caller->XLevel()->Collision.OverlapTest(Caller->Location(), Caller->CollisionHeight(), Caller->CollisionRadius(), true, false, false);
	for (auto& hit : hitList)
	{
		// Only allow the Actors of type BaseClass
		if (hit.Actor && hit.Actor->IsA(BaseClass->Name))
			TouchingActors.push_back(hit.Actor);
	}

	iterator = TouchingActors.begin();
}

bool TouchingActorsIterator::Next()
{
	if (iterator == TouchingActors.end())
	{
		*outActor = nullptr;
		return false;
	}
	
	*outActor = *iterator;
	iterator++;

	return *outActor;
}

/////////////////////////////////////////////////////////////////////////////

TraceActorsIterator::TraceActorsIterator(UActor* SelfActor, UObject* BaseClass, UObject** Actor, vec3* HitLoc, vec3* HitNorm, const vec3& End, const vec3& Start, const vec3& Extent) : SelfActor(SelfActor), BaseClass(BaseClass), Actor(Actor), HitLoc(HitLoc), HitNorm(HitNorm), End(End), Start(Start), Extent(Extent)
{
	TraceFlags flags;
	flags.movers = true;
	flags.world = true;
	flags.pawns = true;
	flags.others = true;
	flags.onlyProjectiles = false; // Should this be true or false?

	// Why is this tracing backwards? Is that correct?
	vec3 traceStart = End;
	vec3 traceEnd = Start;

	for (auto& hit : SelfActor->XLevel()->Collision.Trace(traceStart, traceEnd, Extent.z, Extent.x, flags.traceActors(), flags.traceWorld(), false))
	{
		if (hit.Actor && hit.Actor != SelfActor && hit.Actor->IsA(BaseClass->Name))
		{
			vec3 hitNormal = hit.Normal;
			vec3 hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
			tracedActors.push_back({ hit.Actor, *HitLoc, *HitNorm });
		}
	}

	iterator = tracedActors.begin();
}

bool TraceActorsIterator::Next()
{
	if (iterator == tracedActors.end() || tracedActors.empty())
	{
		*Actor = nullptr;
		*HitLoc = vec3(0.0f);
		*HitNorm = vec3(0.0f);
		return false;
	}

	*Actor = iterator->tracedActor;
	*HitLoc = iterator->HitLoc;
	*HitNorm = iterator->HitNorm;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

VisibleActorsIterator::VisibleActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor, float Radius, const vec3& Location) : BaseClass(BaseClass), Actor(Actor), Radius(Radius), Location(Location)
{
	const Array<UActor*>& actors = engine->Level->Actors;
	for (int slot : ActorSlotsByName(engine->Level, BaseClass->Name))
	{
		// Our checks (the slots are actors of the class of BaseClass):
		// * Whether the actor we're dealing with is not hidden
		// * Then whether the distance of the actor from our given Location is no more than Radius
		UActor* levelActor = actors[slot];
		if (!levelActor->bHidden() &&
			length(levelActor->Location() - Location) <= Radius && Caller->FastTrace(levelActor->Location(), Location))
		{
			VisibleActors.push_back(levelActor);
		}
	}

	iterator = VisibleActors.begin();
}

bool VisibleActorsIterator::Next()
{
	if (iterator == VisibleActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

VisibleCollidingActorsIterator::VisibleCollidingActorsIterator(UObject* BaseClass, UObject** ReturnValue, float Radius, const vec3& Location, bool IgnoreHidden) : BaseClass(BaseClass), ReturnValue(ReturnValue), Radius(Radius), Location(Location), IgnoreHidden(IgnoreHidden)
{
	HitActors = engine->Level->Collision.CollidingActors(Location, Radius);
}

bool VisibleCollidingActorsIterator::Next()
{
	size_t size = HitActors.size();
	while (index < size)
	{
		UActor* actor = HitActors[index++];
		if (actor && (IgnoreHidden || !actor->bHidden()) && actor->IsA(BaseClass->Name))
		{
			*ReturnValue = actor;
			return true;
		}
	}
	*ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

ZoneActorsIterator::ZoneActorsIterator(UZoneInfo* zone, UObject* BaseClass, UObject** Actor) : Zone(zone), BaseClass(BaseClass), Actor(Actor)
{
	for (UActor* levelActor : engine->Level->Actors)
	{
		if (levelActor && levelActor->Region().Zone == zone && levelActor->IsA(BaseClass->Name))
		{
			ZoneActors.push_back(levelActor);
		}
	}

	iterator = ZoneActors.begin();
}

bool ZoneActorsIterator::Next()
{
	if (iterator == ZoneActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

// The original's MultiLineCheck under the two Deus Ex trace iterators:
// the level's BSP first -- its hit's actor the LevelInfo -- with the line
// shortened to 5 units past the wall; then the actors along what is left,
// up to 64 hits in all, nearest first. Nothing beyond the first wall.
static CollisionHitList DeusExMultiLineCheck(const vec3& start, const vec3& end, const vec3& extent, bool visibilityOnly)
{
	float radius = length(extent.xy());
	float height = std::abs(extent.z);
	CollisionHitList all = engine->Level->Collision.Trace(start, end, height, radius, true, true, visibilityOnly);

	CollisionHit wall;
	bool haveWall = false;
	for (const CollisionHit& hit : all)
	{
		if (hit.Node)
		{
			wall = hit;
			haveWall = true;
			break;
		}
	}
	float limit = 1.0f;
	if (haveWall)
	{
		float lineLength = length(end - start);
		limit = lineLength > 0.0f ? std::min(1.0f, wall.Fraction + 5.0f / lineLength) : wall.Fraction;
	}

	CollisionHitList result;
	int count = 0;
	for (const CollisionHit& hit : all)
	{
		if (count >= 64 || hit.Fraction > limit)
			break;
		if (hit.Node)
		{
			if (haveWall && hit.Node == wall.Node && hit.Fraction == wall.Fraction)
			{
				CollisionHit levelHit = hit;
				levelHit.Actor = engine->LevelInfo;
				result.push_back(levelHit);
				count++;
			}
			continue;
		}
		result.push_back(hit);
		count++;
	}
	return result;
}

TraceTextureIterator::TraceTextureIterator(UObject* BaseClass, UObject** OutActor, NameString* TexName, NameString* TexGroup, int* Flags, vec3* HitLoc, vec3* HitNorm, const vec3& End, const vec3& Start, const vec3& Extent)
	: OutActor(OutActor), TexName(TexName), TexGroup(TexGroup), flags(Flags), HitLoc(HitLoc), HitNorm(HitNorm), End(End), Start(Start)
{
	m_CollList = DeusExMultiLineCheck(Start, End, Extent, false);
	m_Iterator = m_CollList.begin();
}

bool TraceTextureIterator::Next()
{
	if (m_Iterator == m_CollList.end())
	{
		*OutActor = nullptr;
		return false;
	}
	const CollisionHit& hit = *m_Iterator;
	++m_Iterator;

	// Every hit, in turn. A hit on the level gives the texture of the
	// surface hit, its group (the texture's outer) and the surface's
	// PolyFlags; an actor, no texture and flags 0.
	*OutActor = hit.Actor;
	*HitLoc = mix(Start, End, hit.Fraction);
	*HitNorm = hit.Normal;
	if (hit.Node)
	{
		const BspSurface& surface = engine->Level->Model->Surfaces[hit.Node->Surf];
		UTexture* texture = surface.Material;
		*TexName = texture ? texture->Name : NameString();
		*TexGroup = texture && texture->Outer() ? texture->Outer()->Name : NameString();
		*flags = surface.PolyFlags;
	}
	else
	{
		*TexName = NameString();
		*TexGroup = NameString();
		*flags = 0;
	}
	return true;
}

/////////////////////////////////////////////////////////////////////////////

TraceVisibleActorsIterator::TraceVisibleActorsIterator(UObject* BaseClass, UObject** OutActor, vec3* HitLoc, vec3* HitNorm, const vec3& End, const vec3& Start, const vec3& Extent)
	: OutActor(OutActor), HitLoc(HitLoc), HitNorm(HitNorm), End(End), Start(Start)
{
	m_CollList = DeusExMultiLineCheck(Start, End, Extent, true);
	m_Iterator = m_CollList.begin();
}

bool TraceVisibleActorsIterator::Next()
{
	if (m_Iterator == m_CollList.end())
	{
		*OutActor = nullptr;
		return false;
	}
	const CollisionHit& hit = *m_Iterator;
	++m_Iterator;

	*OutActor = hit.Actor;
	*HitLoc = mix(Start, End, hit.Fraction);
	*HitNorm = hit.Normal;
	return true;
}

