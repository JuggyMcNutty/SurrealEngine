#pragma once

#include "Packages/Engine/Actors/UActor.h"

class VisibleFrame;

// One item a render iterator listed: the proxy actor, kept as it was when
// the item was listed -- the iterators move one proxy from item to item, so
// each item's glow, draw scale, location and rotation are captured with it
// (docs/re/render-dll.md, render iterators).
class VisibleIteratorItem
{
public:
	void DrawTranslucent(VisibleFrame* frame);

	UActor* Actor = nullptr;
	EDrawType Type = DT_None;
	vec3 Location = vec3(0.0f);
	Rotator Rotation = Rotator(0, 0, 0);
	float DrawScale = 1.0f;
	float ScaleGlow = 1.0f;
};
