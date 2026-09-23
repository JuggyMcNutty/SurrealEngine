
#include "Precomp.h"
#include "TraceRayModel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

CollisionHitList TraceRayModel::Trace(UModel* model, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly)
{
	Model = model;
	CollisionHitList hits;
	Trace(origin, tmin, dirNormalized, tmax, visibilityOnly, &Model->Nodes.front(), hits, 0.0, tmax);
	hits.SortByFraction();
	return hits;
}

bool TraceRayModel::TraceAnyHit(UModel* model, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly)
{
	Model = model;
	return TraceAnyHit(origin, tmin, dirNormalized, tmax, visibilityOnly, &Model->Nodes.front(), 0.0, tmax);
}

// The traversal handed both children of a plane the whole segment whenever it
// touched their side, and a long sight line then touched both sides of
// plane after plane further down. A child now gets only the part of the
// segment [t0, t1] in its half-space, widened by SplitMargin units so that a
// polygon whose edge lies on the plane is still reached from either side.
// The old test stays too, so the nodes visited are a subset of those it
// visited, in the same order.
static const double SplitMargin = 1.0;

static bool SegmentPart(double dA, double dB, double t0, double t1, double sign, double& p0, double& p1)
{
	// The part where sign * distance >= -SplitMargin
	double a = sign * dA + SplitMargin, b = sign * dB + SplitMargin;
	if (a < 0.0 && b < 0.0)
		return false;
	if (a >= 0.0 && b >= 0.0)
	{
		p0 = t0;
		p1 = t1;
	}
	else
	{
		double ts = t0 + (t1 - t0) * (a / (a - b));
		if (a >= 0.0)
		{
			p0 = t0;
			p1 = ts;
		}
		else
		{
			p0 = ts;
			p1 = t1;
		}
	}
	return true;
}

void TraceRayModel::Trace(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly, BspNode* node, CollisionHitList& hits, double t0, double t1)
{
	BspNode* polynode = node;
	while (true)
	{
		if (!visibilityOnly || (polynode->NodeFlags & NF_NotVisBlocking) == 0)
		{
			double t = NodeRayIntersect(origin, tmin, dirNormalized, tmax, polynode, t0, t1);
			if (t >= tmin && t < tmax)
			{
				CollisionHit hit = { (float)t, vec3(node->PlaneX, node->PlaneY, node->PlaneZ), nullptr, polynode, node };
				if (dot(to_dvec3(hit.Normal), dirNormalized) > 0.0)
					hit.Normal = -hit.Normal;
				hits.push_back(hit);
			}
		}

		if (polynode->Plane < 0) break;
		polynode = &Model->Nodes[polynode->Plane];
	}

	dvec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
	double fromSide = dot(dvec4(origin, 1.0), plane);
	double toSide = dot(dvec4(origin + dirNormalized * tmax, 1.0), plane);

	double dA = dot(dvec4(origin + dirNormalized * t0, 1.0), plane);
	double dB = dot(dvec4(origin + dirNormalized * t1, 1.0), plane);
	double p0, p1;
	if (node->Front >= 0 && (fromSide >= 0.0 || toSide >= 0.0) && SegmentPart(dA, dB, t0, t1, 1.0, p0, p1))
		Trace(origin, tmin, dirNormalized, tmax, visibilityOnly, &Model->Nodes[node->Front], hits, p0, p1);
	if (node->Back >= 0 && (fromSide <= 0.0 || toSide <= 0.0) && SegmentPart(dA, dB, t0, t1, -1.0, p0, p1))
		Trace(origin, tmin, dirNormalized, tmax, visibilityOnly, &Model->Nodes[node->Back], hits, p0, p1);
}

bool TraceRayModel::TraceAnyHit(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, bool visibilityOnly, BspNode* node, double t0, double t1)
{
	BspNode* polynode = node;
	while (true)
	{
		if (!visibilityOnly || (polynode->NodeFlags & NF_NotVisBlocking) == 0)
		{
			double t = NodeRayIntersect(origin, tmin, dirNormalized, tmax, polynode, t0, t1);
			if (t >= tmin && t < tmax)
				return true;
		}

		if (polynode->Plane < 0) break;
		polynode = &Model->Nodes[polynode->Plane];
	}

	dvec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
	double fromSide = dot(dvec4(origin, 1.0), plane);
	double toSide = dot(dvec4(origin + dirNormalized * tmax, 1.0), plane);

	double dA = dot(dvec4(origin + dirNormalized * t0, 1.0), plane);
	double dB = dot(dvec4(origin + dirNormalized * t1, 1.0), plane);
	double p0, p1;
	if (node->Front >= 0 && (fromSide >= 0.0 || toSide >= 0.0) && SegmentPart(dA, dB, t0, t1, 1.0, p0, p1) && TraceAnyHit(origin, tmin, dirNormalized, tmax, visibilityOnly, &Model->Nodes[node->Front], p0, p1))
		return true;
	else if (node->Back >= 0 && (fromSide <= 0.0 || toSide <= 0.0) && SegmentPart(dA, dB, t0, t1, -1.0, p0, p1) && TraceAnyHit(origin, tmin, dirNormalized, tmax, visibilityOnly, &Model->Nodes[node->Back], p0, p1))
		return true;
	else
		return false;
}

double TraceRayModel::NodeRayIntersect(const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, BspNode* node, double t0, double t1)
{
	// The plane tests come first: they read only the node's plane, the part of
	// it the traversal has just read, and turn away most nodes. Its vertex
	// count lies on another cache line and its surface's flags in another
	// array, so on the handheld those two tests were most of the time spent
	// here. Every test returns tmax, so their order changes nothing else.

	// Test if plane is actually crossed.
	dvec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
	double fromSide = dot(dvec4(origin, 1.0), plane);
	double toSide = dot(dvec4(origin + dirNormalized * tmax, 1.0), plane);
	if ((fromSide > 0.0 && toSide > 0.0) || (fromSide < 0.0 && toSide < 0.0))
		return tmax;

	// A hit lies in the node's own part of the segment, [t0, t1]: when that
	// stays clear of the plane (by more than SplitMargin), there is none.
	double partA = dot(dvec4(origin + dirNormalized * t0, 1.0), plane);
	double partB = dot(dvec4(origin + dirNormalized * t1, 1.0), plane);
	if ((partA > SplitMargin && partB > SplitMargin) || (partA < -SplitMargin && partB < -SplitMargin))
		return tmax;

	if (node->NumVertices < 3 || (node->Surf >= 0 && Model->Surfaces[node->Surf].PolyFlags & PF_NotSolid))
		return tmax;

	BspVert* v = &Model->Vertices[node->VertPool];
	vec3* points = Model->Points.data();

	dvec3 p[3];
	p[0] = to_dvec3(points[v[0].Vertex]);
	p[1] = to_dvec3(points[v[1].Vertex]);

	double t = tmax;
	int count = node->NumVertices;
	for (int i = 2; i < count; i++)
	{
		p[2] = to_dvec3(points[v[i].Vertex]);
		double tval = TriangleRayIntersect(origin, dirNormalized, tmax, p);
		if (tval >= tmin)
			t = std::min(tval, t);
		p[1] = p[2];
	}
	return t;
}

double TraceRayModel::TriangleRayIntersect(const dvec3& origin, const dvec3& dirNormalized, double tmax, const dvec3* p)
{
	// Moeller-Trumbore ray-triangle intersection algorithm:

	// Find vectors for two edges sharing p[0]
	dvec3 e1 = p[1] - p[0];
	dvec3 e2 = p[2] - p[0];

	// Begin calculating determinant - also used to calculate u parameter
	dvec3 P = cross(dirNormalized, e2);
	double det = dot(e1, P);

	// Backface check
	//if (det < 0.0)
	//	return tmax;

	// If determinant is near zero, ray lies in plane of triangle
	if (det > -FLT_EPSILON && det < FLT_EPSILON)
		return tmax;

	double inv_det = 1.0 / det;

	// Calculate distance from p[0] to ray origin
	dvec3 T = origin - p[0];

	// Calculate u parameter and test bound
	double u = dot(T, P) * inv_det;

	// Check if the intersection lies outside of the triangle
	if (u < 0.f || u > 1.f)
		return tmax;

	// Prepare to test v parameter
	dvec3 Q = cross(T, e1);

	// Calculate V parameter and test bound
	double v = dot(dirNormalized, Q) * inv_det;

	// The intersection lies outside of the triangle
	if (v < 0.f || u + v  > 1.f)
		return tmax;

	double t = dot(e2, Q) * inv_det;
	if (t <= FLT_EPSILON)
		return tmax;

	// Return hit location on triangle in barycentric coordinates
	// barycentricB = u;
	// barycentricC = v;

	return std::min(t, tmax);
}
