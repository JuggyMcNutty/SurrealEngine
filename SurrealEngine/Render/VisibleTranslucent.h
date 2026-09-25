#pragma once

#include "VisibleNode.h"
#include "VisibleActor.h"
#include "VisibleIteratorItem.h"
#include "Engine.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/UActor.h"

class VisibleFrame;

class VisibleTranslucent
{
public:
	VisibleTranslucent(const VisibleNode& node, float distSqr) : Node(node), DistSqr(distSqr) {}
	VisibleTranslucent(const VisibleActor& actor, float distSqr) : Actor(actor), DistSqr(distSqr) {}
	VisibleTranslucent(const VisibleIteratorItem& item, float distSqr) : Item(item), DistSqr(distSqr) {}

	VisibleActor Actor;
	VisibleIteratorItem Item;
	VisibleNode Node;
	float DistSqr = 0.0f;

	void Draw(VisibleFrame* frame)
	{
		if (Actor.Actor)
			Actor.DrawTranslucent(frame);
		else if (Item.Actor)
			Item.DrawTranslucent(frame);
		else
			Node.Draw(frame);
	}
};
