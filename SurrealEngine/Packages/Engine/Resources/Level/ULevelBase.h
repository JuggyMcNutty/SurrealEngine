#pragma once

#include "Packages/Core/UObject.h"

class UActor;
class UStruct;

class ULevelBase : public UObject
{
public:
	using UObject::UObject;

	void Load(ObjectStream* stream) override;
	void Save(PackageStreamWriter* stream) override;

	Array<UActor*> Actors;

	// Bumped whenever Actors changes: an actor added, one destroyed (its slot
	// nulled), the list compacted.
	uint64_t ActorsVersion = 0;

	// Whether a slot of Actors was nulled since the list was last compacted
	bool ActorsHaveHoles = true;

	// The actor iterators' indexes: the slots of Actors holding an actor of
	// a class (destroyed or not), as of ActorsVersion. CycleActors tests the
	// class by pointer, the others by name, as they always did.
	struct ClassSlots
	{
		uint64_t Version = ~0ull;
		Array<int> Slots;
	};
	std::map<UStruct*, ClassSlots> ActorsByClass;
	std::map<int, ClassSlots> ActorsByClassName; // by the name's compare index

	std::string Protocol;
	std::string Host;
	int Port = 0;
	int Unknown = 0;
	std::string Map;
	Array<std::string> Options;
	std::string Portal;
};
