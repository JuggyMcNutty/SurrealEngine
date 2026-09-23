#pragma once

#include "Math/vec.h"
#include "Math/FrustumPlanes.h"

class BBox;

class ClipSpan
{
public:
	int16_t x0;
	int16_t x1;
};

class PortalSpan
{
public:
	int64_t y;
	int16_t x0;
	int16_t x1;
};

class BspClipper
{
public:
	BspClipper();
	~BspClipper();

	// The occlusion grid's size. Its cost is per row, so it follows the image
	// being drawn (VisibleFrame::Process) rather than a fixed 1080 rows:
	// capped at MaxWidth x MaxHeight, which is what it always was.
	void SetViewportSize(int width, int height);
	enum { MaxWidth = 2048, MaxHeight = 1080 };

	void Setup(const mat4& world_to_projection, const Array<PortalSpan>& portalSpans, const vec4& portalPlane);

	bool CheckSurface(const vec3* vertices, uint32_t count, bool solid);
	bool IsAABBVisible(const BBox& bbox);

	Array<PortalSpan> CheckPortal(const vec3* vertices, uint32_t count);

	int numDrawSpans;
	int numSurfs;
	int numTris;

private:
	bool IsVisible(int16_t y, int16_t x0, int16_t x1);

	struct ShadedVertex
	{
		vec4 position;
		float clipDistance;
	};

	bool DrawTriangle(const ShadedVertex* const* vert, bool solid, bool ccw);
	int ClipEdge(const ShadedVertex* const* verts);

	bool DrawClippedTriangle(const vec4* const* vertices, bool solid);
	bool DrawSpan(int16_t y, int16_t x0, int16_t x1, bool solid);

	static bool IsDegenerate(const vec4* const* vert);
	static bool IsFrontfacing(const vec4* const* vert);
	static void SortVertices(const vec4* const* vertices, const vec4** sortedVertices);

	Array<Array<ClipSpan>> Viewport;
	FrustumPlanes FrustumClip;
	mat4 WorldToProjection;
	vec4 PortalPlane = vec4(0.0f);

	Array<PortalSpan> VisibleSpans;
	bool CollectSpans = false;

	enum { max_additional_vertices = 16 };
	float weightsbuffer[max_additional_vertices * 3 * 2];
	float* weights = nullptr;

	int ViewportWidth = MaxWidth;
	int ViewportHeight = MaxHeight;
};
