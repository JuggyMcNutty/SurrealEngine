#pragma once

#include "Packages/Engine/Resources/Level/ULevel.h"

class TraceRayModel
{
public:
	CollisionHitList Trace(UModel* model, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly);
	bool TraceAnyHit(UModel* model, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly);

private:
	void Trace(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly, BspNode* node, CollisionHitList& hits, double t0, double t1);
	bool TraceAnyHit(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly, BspNode* node, double t0, double t1);

	double NodeRayIntersect(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, BspNode* node, double t0, double t1);
	double TriangleRayIntersect(const dvec3& origin, const dvec3& dirNormalized, double tmax, const dvec3* points);

	UModel* Model = nullptr;
};
