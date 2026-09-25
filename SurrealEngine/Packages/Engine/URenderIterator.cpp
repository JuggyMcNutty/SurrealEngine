
#include "Precomp.h"
#include "URenderIterator.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "VM/ScriptCall.h"

void URenderIterator::Init(UPlayerPawn* camera)
{
	Observer() = camera;
	CallEvent(this, "Init", { ExpressionValue::ObjectValue(camera) });
}

void URenderIterator::First()
{
	Index() = 0;
}

bool URenderIterator::IsDone()
{
	return Index() >= MaxItems();
}

UActor* URenderIterator::CurrentItem()
{
	return UObject::TryCast<UActor>(CallEvent(this, "CurrentItem").ToObject());
}

void URenderIterator::Next()
{
	Index()++;
}
