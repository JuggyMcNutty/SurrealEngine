
#include "Precomp.h"
#include "VisibleIteratorItem.h"
#include "VisibleFrame.h"
#include "VisibleMesh.h"
#include "VisibleSprite.h"

void VisibleIteratorItem::DrawTranslucent(VisibleFrame* frame)
{
	if (Type == DT_Mesh)
	{
		// The proxy is put back to what it was as the item was listed, for
		// the draw, and restored after
		vec3 location = Actor->Location();
		Rotator rotation = Actor->Rotation();
		float drawScale = Actor->DrawScale();
		float scaleGlow = Actor->ScaleGlow();
		Actor->Location() = Location;
		Actor->Rotation() = Rotation;
		Actor->DrawScale() = DrawScale;
		Actor->ScaleGlow() = ScaleGlow;

		VisibleMesh vismesh;
		if (vismesh.DrawMesh(frame, Actor, false, false))
			vismesh.DrawMesh(frame, Actor, false, true);

		Actor->Location() = location;
		Actor->Rotation() = rotation;
		Actor->DrawScale() = drawScale;
		Actor->ScaleGlow() = scaleGlow;
	}
	else if (Type == DT_Sprite || Type == DT_SpriteAnimOnce)
	{
		VisibleSprite vissprite;
		vissprite.Draw(frame, Actor, Location, DrawScale, ScaleGlow);
	}
}
