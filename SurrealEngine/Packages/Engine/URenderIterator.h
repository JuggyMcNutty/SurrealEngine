#pragma once

#include "Packages/Core/UObject.h"

class UActor;
class UPlayerPawn;

// An actor with a RenderIteratorClass is drawn as the items its iterator
// lists, and gets no sprite of its own. The renderer makes the iterator (an
// object of that class with the actor as its outer) and, each scene frame,
// runs: Init with the viewer, First, then until IsDone an item for the actor
// CurrentItem gives, and Next (docs/re/render-dll.md, render iterators).
// A native subclass overrides the steps; the defaults call the script.
class URenderIterator : public UObject
{
public:
	using UObject::UObject;

	virtual void Init(UPlayerPawn* camera);
	virtual void First();
	virtual bool IsDone();
	virtual UActor* CurrentItem();
	virtual void Next();

	int& MaxItems() { return Value<int>(PropOffsets_RenderIterator.MaxItems); }
	int& Index() { return Value<int>(PropOffsets_RenderIterator.Index); }
	UPlayerPawn*& Observer() { return Value<UPlayerPawn*>(PropOffsets_RenderIterator.Observer); }
};
