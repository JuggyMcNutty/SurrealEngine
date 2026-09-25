
#include "Precomp.h"
#include "ULaserIterator.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Math/coords.h"
#include "Utils/Random.h"

// The original's behaviour: docs/re/deusex-dll.md, particles and lasers.
// The script's AddBeam spaces segments every 16 units, every 15 for a
// random beam, and Init counts MaxItems as the active beams' segments
// plus one: the last item, left at a segment chosen at random.

static vec3 RandomUnitVector()
{
	while (true)
	{
		vec3 v(FRandRange(-1.0f, 1.0f), FRandRange(-1.0f, 1.0f), FRandRange(-1.0f, 1.0f));
		float len2 = dot(v, v);
		if (len2 > 0.001f && len2 <= 1.0f)
			return v / std::sqrt(len2);
	}
}

UActor* ULaserIterator::CurrentItem()
{
	if (!Proxy())
		return nullptr;

	// Find the beam and segment of the current item
	auto beams = Beams();
	int item = Index();
	for (int b = 0; b < 8; b++)
	{
		const DXBeam& beam = beams[b];
		if (!beam.bActive)
			continue;
		if (item < beam.NumSegments)
			return PlaceProxy(beam, item);
		item -= beam.NumSegments;
	}

	// Past every beam's segments: the one extra item. Leave the proxy at a
	// segment chosen at random.
	int active[8];
	int count = 0;
	for (int b = 0; b < 8; b++)
	{
		if (beams[b].bActive && beams[b].NumSegments > 0)
			active[count++] = b;
	}
	if (count == 0)
		return nullptr;
	const DXBeam& beam = beams[active[std::min((int)(FRand() * count), count - 1)]];
	int segment = std::min((int)(FRand() * beam.NumSegments), beam.NumSegments - 1);
	return PlaceProxy(beam, segment);
}

UActor* ULaserIterator::PlaceProxy(const DXBeam& beam, int segment)
{
	UActor* proxy = Proxy();
	float step = bRandomBeam() ? 15.0f : 16.0f;
	vec3 dir = Coords::Rotation(beam.Rotation).XAxis;
	vec3 location = beam.Location + dir * (step * segment);
	Rotator rotation = beam.Rotation;

	if (bRandomBeam())
	{
		// Each segment's end is jittered by a random unit vector, and the
		// segment aims along the result; the ends chain
		if (segment == 0)
		{
			PrevLoc() = beam.Location;
			PrevRand() = vec3(0.0f);
		}
		vec3 start = PrevLoc();
		vec3 jitter = RandomUnitVector();
		vec3 end = beam.Location + dir * (step * (segment + 1)) + jitter;
		location = start;
		rotation = Rotator::FromVector(end - start);
		PrevLoc() = end;
		PrevRand() = jitter;
	}

	if (!proxy->SetLocation(location))
		return nullptr;
	proxy->Rotation() = rotation;
	return proxy;
}
