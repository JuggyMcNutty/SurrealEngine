#pragma once

#include "Packages/Engine/URenderIterator.h"

class UActor;

// One particle of ParticleIterator's Particles[64]: the script's sParticle,
// laid out as the engine packs script structs (a bool is a 32-bit bitfield).
struct DXParticle
{
	union
	{
		uint32_t bActive : 1;
		uint32_t flags;
	};
	vec3 InitVel;
	vec3 Velocity;
	vec3 Location;
	float DrawScale;
	float ScaleGlow;
	float LifeSpan;
};

// ParticleGenerator's iterator: smoke, steam, water and sparks, drawn as up
// to 64 particles through one proxy actor
// (docs/re/deusex-dll.md, particles and lasers).
class UParticleIterator : public URenderIterator
{
public:
	using URenderIterator::URenderIterator;

	// The original's native behind the script's Update: ages each living
	// particle, and drifts, rises or falls, grows, fades and moves it.
	void UpdateParticles(float deltaTime);

	// Moves the proxy to the current particle with its scale and glow; a
	// particle that cannot be moved to is deleted.
	UActor* CurrentItem() override;

	FixedArrayView<DXParticle, 64> Particles() { return FixedArray<DXParticle, 64>(PropOffsets_ParticleIterator.Particles); }
	int& NextFreeParticle() { return Value<int>(PropOffsets_ParticleIterator.nextFreeParticle); }
	UActor*& Proxy() { return Value<UActor*>(PropOffsets_ParticleIterator.proxy); }
	BitfieldBool bOwnerUsesGravity() { return BoolValue(PropOffsets_ParticleIterator.bOwnerUsesGravity); }
	BitfieldBool bOwnerScales() { return BoolValue(PropOffsets_ParticleIterator.bOwnerScales); }
	BitfieldBool bOwnerFades() { return BoolValue(PropOffsets_ParticleIterator.bOwnerFades); }
	float& OwnerZoneGravity() { return Value<float>(PropOffsets_ParticleIterator.OwnerZoneGravity); }
	float& OwnerRiseRate() { return Value<float>(PropOffsets_ParticleIterator.OwnerRiseRate); }
	float& OwnerLifeSpan() { return Value<float>(PropOffsets_ParticleIterator.OwnerLifeSpan); }
	float& OwnerDrawScale() { return Value<float>(PropOffsets_ParticleIterator.OwnerDrawScale); }

private:
	void DeleteParticle(int i);
};
