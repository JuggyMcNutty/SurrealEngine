#pragma once

#include "Packages/Engine/URenderIterator.h"

class UActor;

// One beam of LaserIterator's Beams[8]: the script's sBeam, laid out as the
// engine packs script structs (a bool is a 32-bit bitfield).
struct DXBeam
{
	union
	{
		uint32_t bActive : 1;
		uint32_t flags;
	};
	vec3 Location;
	Rotator Rotation;
	float Length;
	int NumSegments;
};

// LaserEmitter's iterator: each beam drawn as a run of segments through one
// proxy actor; bRandomBeam is ElectricityEmitter's arcing
// (docs/re/deusex-dll.md, particles and lasers).
class ULaserIterator : public URenderIterator
{
public:
	using URenderIterator::URenderIterator;

	// Moves and turns the proxy onto the current item's stretch of its
	// beam; after the last item, leaves it at a segment chosen at random.
	UActor* CurrentItem() override;

	FixedArrayView<DXBeam, 8> Beams() { return FixedArray<DXBeam, 8>(PropOffsets_LaserIterator.Beams); }
	vec3& PrevLoc() { return Value<vec3>(PropOffsets_LaserIterator.prevloc); }
	vec3& PrevRand() { return Value<vec3>(PropOffsets_LaserIterator.prevRand); }
	vec3& SavedLoc() { return Value<vec3>(PropOffsets_LaserIterator.savedLoc); }
	Rotator& SavedRot() { return Value<Rotator>(PropOffsets_LaserIterator.SavedRot); }
	int& NextItem() { return Value<int>(PropOffsets_LaserIterator.NextItem); }
	UActor*& Proxy() { return Value<UActor*>(PropOffsets_LaserIterator.proxy); }
	BitfieldBool bRandomBeam() { return BoolValue(PropOffsets_LaserIterator.bRandomBeam); }

private:
	UActor* PlaceProxy(const DXBeam& beam, int segment);
};
