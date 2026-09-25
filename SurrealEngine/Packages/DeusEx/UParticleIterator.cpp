
#include "Precomp.h"
#include "UParticleIterator.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Utils/Random.h"

// The original's behaviour: docs/re/deusex-dll.md, particles and lasers.

void UParticleIterator::UpdateParticles(float deltaTime)
{
	auto particles = Particles();
	float fullLife = OwnerLifeSpan();
	bool anyAlive = false;

	for (int i = 0; i < 64; i++)
	{
		DXParticle& p = particles[i];
		if (!p.bActive)
			continue;

		// Ages, and one whose life has run out is deleted
		p.LifeSpan -= deltaTime;
		if (p.LifeSpan <= 0.0f)
		{
			DeleteParticle(i);
			continue;
		}
		anyAlive = true;

		// Drifts: the initial horizontal velocity plus a new random offset
		// each frame, from -3 to +2
		p.Velocity.x = p.InitVel.x + FRandRange(-3.0f, 2.0f);
		p.Velocity.y = p.InitVel.y + FRandRange(-3.0f, 2.0f);

		// Rises or falls: under zone gravity when the generator asks for
		// it, else faster with age at the generator's rise rate
		if (bOwnerUsesGravity())
			p.Velocity.z += OwnerZoneGravity() * deltaTime;
		else
			p.Velocity.z += OwnerRiseRate() * deltaTime;

		float lived = fullLife > 0.0f ? clamp(1.0f - p.LifeSpan / fullLife, 0.0f, 1.0f) : 1.0f;

		// Grows with age, from 0.01 to 3 times the draw scale, when asked
		if (bOwnerScales())
			p.DrawScale = OwnerDrawScale() * (0.01f + (3.0f - 0.01f) * lived);

		// Fades with its remaining life, when asked
		p.ScaleGlow = bOwnerFades() ? 1.0f - lived : 1.0f;

		// Moves by its velocity
		p.Location += p.Velocity * deltaTime;
	}

	// The proxy is hidden while no particle lives
	if (UActor* proxy = Proxy())
		proxy->bHidden() = !anyAlive;
}

UActor* UParticleIterator::CurrentItem()
{
	UActor* proxy = Proxy();
	int i = Index();
	if (!proxy || i < 0 || i >= 64)
		return nullptr;

	auto particles = Particles();
	DXParticle& p = particles[i];
	if (!p.bActive)
		return nullptr;

	vec3 location = p.Location;
	if (!proxy->SetLocation(location))
	{
		DeleteParticle(i);
		return nullptr;
	}
	proxy->DrawScale() = p.DrawScale;
	proxy->ScaleGlow() = p.ScaleGlow;
	return proxy;
}

void UParticleIterator::DeleteParticle(int i)
{
	auto particles = Particles();
	if (particles[i].bActive)
	{
		particles[i].bActive = 0;
		NextFreeParticle() = i;
	}
}
