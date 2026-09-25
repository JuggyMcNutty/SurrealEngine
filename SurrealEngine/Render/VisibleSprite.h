#pragma once

#include "Math/vec.h"

class UActor;
class VisibleFrame;

class VisibleSprite
{
public:
	void Draw(VisibleFrame* frame, UActor* actor);

	// A render iterator's item draws where the proxy was as the item was
	// listed, not where the actor is now
	void Draw(VisibleFrame* frame, UActor* actor, const vec3& location, float drawscale, float scaleglow);
};
