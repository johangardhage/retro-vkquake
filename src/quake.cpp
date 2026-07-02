//
// Quake map viewer
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
#define RETRO_WIDTH 320
#define RETRO_HEIGHT 200
#include "lib/retromain.h"
#include "lib/retrobsp.h"
#include "lib/retromath.h"
#include "lib/retrocamera.h"
#include "quake_vert_spv.h"
#include "quake_frag_spv.h"
#include <float.h>
#include <math.h>

#define MOVEMENT_SPEED 5.0
#define SURFACE_FLAG_TURBULENT 1
#define SURFACE_FLAG_SKY 2
#define SKY_DOME_SLICES 64
#define SKY_DOME_STACKS 32
#define SKY_DOME_RADIUS 4096.0f

// Where a surface's triangles live in the mesh vertex buffer. One entry
// per BSP surface.
struct SurfaceDraw
{
	int firstVertex;
	int vertexCount;
};

struct LightmapRect
{
	int x;
	int y;
	int width;
	int height;
	int minS;
	int minT;
};

struct TextureAnim
{
	int total = 0;
	int frames[10];
};

struct World
{
	RETRO_BSP map;							// The loaded map (BSP, palette and colormap), owned by value

	primdesc_t *surfacePrimitives = NULL;	// Array of surface primitives, contains vertex information for every surface
	int *visibleSurfaces = NULL;			// Array of visible surfaces, contains an index to the surfaces
	int numMaxEdgesPerSurface = 0;			// Max edges per surface
	SurfaceDraw *surfaceDraws = NULL;		// Per-surface slice into the mesh vertex buffer
	SurfaceDraw skyDomeDraw;				// Slice containing the continuous sky dome
	TextureAnim *textureAnimations = NULL;	// Per-texture animation sequence table
	bool *textureSky = NULL;				// Per-texture sky classification
	bool *textureTurbulent = NULL;			// Per-texture liquid classification
	int skyTextureIndex = -1;				// Texture used by the sky dome
	int numTextures = 0;
	double textureTime = 0.0;				// Time accumulator for animated texture sequences
	LightmapRect *lightmaps = NULL;			// Per-surface rectangle inside the lightmap atlas
	int lightmapAtlasWidth = 0;
	int lightmapAtlasHeight = 0;
	int lightStyleFrame = -1;				// Current frame index of the 10Hz light animations
	int lightStyles[64];					// Current values of the 64 light styles
	double lightStyleTime = 0.0;			// Time accumulator for light styles
};

World world;
RETROVK_Mesh mesh;				// Textured mesh resources, owned and driven through retrovk.h
RETROVK_RenderMode renderMode = RETROVK_RENDER_TEXTURED;	// Selected with keys 1-4
RETRO_Camera camera;

bool IsSkyTextureName(const char *name)
{
	if (!name) {
		return false;
	}
	char c0 = name[0];
	char c1 = name[1];
	char c2 = name[2];
	if (c0 >= 'A' && c0 <= 'Z') c0 += 'a' - 'A';
	if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
	if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
	return c0 == 's' && c1 == 'k' && c2 == 'y';
}

bool IsTurbulentTextureName(const char *name)
{
	return name && name[0] == '*';
}

//
// Expand the palettised mip textures into RGBA images and upload them to the
// mesh, one slot per BSP texture.
//
bool UploadTextures(World *world)
{
	world->numTextures = world->map.getNumTextures();
	world->textureSky = new bool [world->numTextures];
	world->textureTurbulent = new bool [world->numTextures];

	// NEAREST magnification (chunky texels, matching the old GL_NEAREST) and
	// trilinear minification across mip levels.
	RETROVK_CreateMeshTextures(&mesh, world->numTextures, VK_FILTER_NEAREST, VK_FILTER_LINEAR);

	// Build an RGBA image for every texture slot. Missing slots get a 1x1 filler
	// so that every surface has a valid texture to bind.
	for (int i = 0; i < world->numTextures; i++) {
		world->textureSky[i] = false;
		world->textureTurbulent[i] = false;

		miptex_t *mipTexture = world->map.getMipTexture(i);
		if (!mipTexture || !mipTexture->name[0] || mipTexture->offsets[0] == 0) {
			unsigned int filler = 0xFF000000;	// Opaque black
			unsigned int transparent = 0x00000000;
			RETROVK_UploadMeshTexture(&mesh, i, 1, 1, &filler);
			RETROVK_UploadMeshLumaTexture(&mesh, i, 1, 1, &transparent);
			continue;
		}

		int width = mipTexture->width;
		int height = mipTexture->height;
		world->textureSky[i] = IsSkyTextureName(mipTexture->name);
		world->textureTurbulent[i] = IsTurbulentTextureName(mipTexture->name);
		if (world->textureSky[i] && world->skyTextureIndex < 0) {
			world->skyTextureIndex = i;
		}

		// Expand the 8-bit indexed full-resolution mip into RGBA (force opaque alpha)
		unsigned int *texture = new unsigned int[width * height];
		unsigned int *lumaTexture = new unsigned int[width * height];
		unsigned char *rawTexture = (unsigned char *)mipTexture + mipTexture->offsets[0];
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				unsigned char colorIndex = rawTexture[x + y * width];
				unsigned int alpha = 0xFF000000;
				if (world->textureSky[i] && x < width / 2 && colorIndex == 0) {
					alpha = 0x00000000;
				}
				texture[x + y * width] = world->map.palette[colorIndex] | alpha;
				lumaTexture[x + y * width] = (colorIndex >= 224) ? (world->map.palette[colorIndex] | 0xFF000000) : 0x00000000;
			}
		}

		RETROVK_UploadMeshTexture(&mesh, i, width, height, texture);
		RETROVK_UploadMeshLumaTexture(&mesh, i, width, height, lumaTexture);
		delete[] texture;
		delete[] lumaTexture;
	}

	return true;
}

int TextureAnimationFrame(const char *name)
{
	if (!name || name[0] != '+') return -1;
	if (name[1] >= '0' && name[1] <= '9') return name[1] - '0';
	return -1;
}

bool IsSameTextureAnimation(const char *a, const char *b)
{
	if (!a || !b || a[0] != '+' || b[0] != '+') {
		return false;
	}

	for (int i = 2; i < 16; i++) {
		if (a[i] != b[i]) return false;
		if (a[i] == '\0') return true;
	}

	return true;
}

bool BuildTextureAnimations(World *world)
{
	world->textureAnimations = new TextureAnim[world->numTextures];
	for (int i = 0; i < world->numTextures; i++) {
		world->textureAnimations[i].total = 0;
		for (int frame = 0; frame < 10; frame++) {
			world->textureAnimations[i].frames[frame] = -1;
		}
	}

	for (int i = 0; i < world->numTextures; i++) {
		miptex_t *baseTexture = world->map.getMipTexture(i);
		if (!baseTexture || TextureAnimationFrame(baseTexture->name) != 0) {
			continue;
		}

		TextureAnim *anim = &world->textureAnimations[i];
		for (int j = 0; j < world->numTextures; j++) {
			miptex_t *frameTexture = world->map.getMipTexture(j);
			if (!frameTexture || !IsSameTextureAnimation(baseTexture->name, frameTexture->name)) {
				continue;
			}

			int frame = TextureAnimationFrame(frameTexture->name);
			if (frame >= 0 && frame < 10) {
				anim->frames[frame] = j;
				if (anim->total < frame + 1) {
					anim->total = frame + 1;
				}
			}
		}
	}

	return true;
}

int ResolveTextureAnimation(World *world, int textureIndex)
{
	if (textureIndex < 0 || textureIndex >= world->numTextures || !world->textureAnimations) {
		return textureIndex;
	}

	TextureAnim *anim = &world->textureAnimations[textureIndex];
	if (anim->total <= 1) {
		return textureIndex;
	}

	int frame = ((int)(world->textureTime * 10.0)) % anim->total;
	int resolved = anim->frames[frame];
	if (resolved < 0) {
		resolved = anim->frames[0];
	}
	if (resolved < 0 || resolved >= world->numTextures) {
		return textureIndex;
	}
	return resolved;
}

//
// Classic Quake light-style brightness patterns. Each character 'a'..'z' is a
// brightness level ('a' = dark, 'm' = normal, 'z' = bright), cycled at 10 Hz.
//
const char *LightStylePattern(int style)
{
	static const char *patterns[] = {
		"m",
		"mmnmmommommnonmmonqnmmo",
		"abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
		"mmmmmaaaaammmmmaaaaaabcdefgabcdefg",
		"mamamamamama",
		"jklmnopqrstuvwxyzyxwvutsrqponmlkj",
		"nmonqnmomnmomomno",
		"mmmaaaabcdefgmmmmaaaammmaamm",
		"mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",
		"aaaaaaaazzzzzzzz",
		"mmamammmmammamamaaamammma",
		"abcdefghijklmnopqrrqponmlkjihgfedcba",
		"mmnnmmnnnmmnn",
		"kmjmlnklkj",
		"mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmaaaaaaaazzzzzzzz",
	};

	if (style >= 0 && style < (int)(sizeof(patterns) / sizeof(patterns[0]))) {
		return patterns[style];
	}
	return "m";
}

//
// Advance classic Quake animated light style values. Returns true when the
// 10Hz style frame changed and dynamic lightmaps should be refreshed.
//
bool UpdateLightStyles(World *world, double deltaTime)
{
	world->lightStyleTime += deltaTime;
	int frame = (int)(world->lightStyleTime * 10.0);	// light styles animate at 10 Hz
	if (frame == world->lightStyleFrame) {
		return false;
	}
	world->lightStyleFrame = frame;

	for (int style = 0; style < 64; style++) {
		const char *pattern = LightStylePattern(style);
		int length = 0;
		while (pattern[length]) {
			length++;
		}

		// Map the current pattern letter 'a'..'z' onto a brightness scale where
		// 'm' (== 264) is normal full-strength lighting.
		char value = pattern[length > 0 ? frame % length : 0];
		if (value < 'a') value = 'a';
		if (value > 'z') value = 'z';
		world->lightStyles[style] = ((int)value - (int)'a') * 22;
	}

	return true;
}

//
// Fill the atlas rectangle for one surface. Sky and liquid surfaces
// (TEX_SPECIAL), and faces with no stored lighting, get solid white luxels so
// the modulate pass leaves the base texture at full brightness.
//
void BuildLightmap(World *world, int surface, unsigned int *atlas)
{
	LightmapRect *rect = &world->lightmaps[surface];
	dface_t *face = world->map.getSurface(surface);
	texinfo_t *textureInfo = world->map.getTextureInfo(surface);
	unsigned char *samples = world->map.getLightmap(face->lightofs);

	if ((textureInfo->flags & TEX_SPECIAL) || !samples) {
		for (int y = 0; y < rect->height; y++) {
			for (int x = 0; x < rect->width; x++) {
				atlas[(rect->x + x) + (rect->y + y) * world->lightmapAtlasWidth] = 0xFFFFFFFF;
			}
		}
		return;
	}

	// Combine every light style affecting this surface into one grayscale map.
	// Each active style contributes one width*height block of samples.
	int size = rect->width * rect->height;
	for (int y = 0; y < rect->height; y++) {
		for (int x = 0; x < rect->width; x++) {
			int src = x + y * rect->width;
			float intensity = 0.0f;
			for (int style = 0; style < MAXLIGHTMAPS && face->styles[style] != 255; style++) {
				int styleIndex = face->styles[style];
				float scale = 1.0f;
				if (styleIndex < 64) {
					scale = (float)world->lightStyles[styleIndex] / 264.0f;
				}
				intensity += (float)samples[style * size + src] * scale;
			}
			int intVal = (int)(intensity + 0.5f);
			unsigned int light = (intVal > 255) ? 255 : (unsigned int)intVal;
			atlas[(rect->x + x) + (rect->y + y) * world->lightmapAtlasWidth] = 0xFF000000 | (light << 16) | (light << 8) | light;
		}
	}
}

void FillLightmapAtlas(World *world, unsigned int *atlas)
{
	for (int i = 0; i < world->lightmapAtlasWidth * world->lightmapAtlasHeight; i++) {
		atlas[i] = 0xFFFFFFFF;
	}

	for (int i = 0; i < world->map.getNumSurfaces(); i++) {
		BuildLightmap(world, i, atlas);
	}
}

void UpdateLightmapAtlas(World *world)
{
	unsigned int *atlas = new unsigned int [world->lightmapAtlasWidth * world->lightmapAtlasHeight];
	FillLightmapAtlas(world, atlas);
	RETROVK_UpdateMeshLightmap(&mesh, world->lightmapAtlasWidth, world->lightmapAtlasHeight, atlas);
	delete[] atlas;
}

//
// Build per-surface primitive vertices
//
bool BuildSurfacePrimitives(World *world)
{
	int numSurfaces = world->map.getNumSurfaces();

	// Allocate memory for the visible surfaces array
	world->visibleSurfaces = new int [world->map.getNumSurfaceLists()];

	// Calculate max number of edges per surface
	world->numMaxEdgesPerSurface = 0;
	for (int i = 0; i < numSurfaces; i++) {
		if (world->numMaxEdgesPerSurface < world->map.getNumEdges(i)) {
			world->numMaxEdgesPerSurface = world->map.getNumEdges(i);
		}
	}

	// Allocate memory for the surface primitive array
	world->surfacePrimitives = new primdesc_t [numSurfaces * world->numMaxEdgesPerSurface];
	world->lightmaps = new LightmapRect [numSurfaces];
	world->lightmapAtlasWidth = 1024;

	int atlasX = 0;
	int atlasY = 0;
	int atlasRowHeight = 0;

	// Loop through all the surfaces to fetch the vertices and calculate their texture and lightmap coords
	for (int i = 0; i < numSurfaces; i++) {
		int numEdges = world->map.getNumEdges(i);

		// Get a pointer to texinfo for this surface
		texinfo_t *textureInfo = world->map.getTextureInfo(i);
		// Get a pointer to the surface's miptextures (missing texture slots return NULL)
		miptex_t *mipTexture = world->map.getMipTexture(textureInfo->miptex);
		// Fall back to a unit size when the texture slot has no data (missing texture)
		float texWidth = (mipTexture && mipTexture->width) ? (float)mipTexture->width : 1.0f;
		float texHeight = (mipTexture && mipTexture->height) ? (float)mipTexture->height : 1.0f;

		// Point to a surface primitive array
		primdesc_t *primitives = &world->surfacePrimitives[i * world->numMaxEdgesPerSurface];

		// Track the surface's texture-space bounds to size its lightmap
		float minS = FLT_MAX, minT = FLT_MAX, maxS = -FLT_MAX, maxT = -FLT_MAX;

		for (int j = 0; j < numEdges; j++, primitives++) {
			// Get an edge id from the surface. Fetch the correct edge by using the id in the Edge List.
			// The winding is backwards!
			int edgeId = world->map.getEdgeList(world->map.getSurface(i)->firstedge + (numEdges - 1 - j));
			// Positive surfedge -> edge used forwards (start vertex); otherwise reversed (end vertex)
			int vertexId = ((edgeId >= 0) ? world->map.getEdge(edgeId)->v[0] : world->map.getEdge(-edgeId)->v[1]);

			// Store the vertex in the primitive array
			vec3_t *vertex = world->map.getVertex(vertexId);
			primitives->v[0] = ((float *)vertex)[0];
			primitives->v[1] = ((float *)vertex)[1];
			primitives->v[2] = ((float *)vertex)[2];

			// Project the vertex into texture space
			float s = DotProduct(textureInfo->vecs[0], primitives->v) + textureInfo->vecs[0][3];
			float t = DotProduct(textureInfo->vecs[1], primitives->v) + textureInfo->vecs[1][3];

			// Store normalized texture coords, and stash raw texture-space coords
			// in the lightmap slot until the bounds are known.
			primitives->t[0] = s / texWidth;
			primitives->t[1] = t / texHeight;
			primitives->l[0] = s;
			primitives->l[1] = t;

			if (s < minS) minS = s;
			if (t < minT) minT = t;
			if (s > maxS) maxS = s;
			if (t > maxT) maxT = t;
		}

		// Size the lightmap from the texture-space bounds (one luxel per 16 texels)
		int lightMinS = FloorDiv16(minS);
		int lightMinT = FloorDiv16(minT);
		int lightWidth = CeilDiv16(maxS) - lightMinS + 1;
		int lightHeight = CeilDiv16(maxT) - lightMinT + 1;

		if (atlasX + lightWidth > world->lightmapAtlasWidth) {
			atlasX = 0;
			atlasY += atlasRowHeight;
			atlasRowHeight = 0;
		}

		world->lightmaps[i].x = atlasX;
		world->lightmaps[i].y = atlasY;
		world->lightmaps[i].width = lightWidth;
		world->lightmaps[i].height = lightHeight;
		world->lightmaps[i].minS = lightMinS;
		world->lightmaps[i].minT = lightMinT;

		atlasX += lightWidth;
		if (lightHeight > atlasRowHeight) {
			atlasRowHeight = lightHeight;
		}
	}

	world->lightmapAtlasHeight = atlasY + atlasRowHeight;
	if (world->lightmapAtlasHeight < 1) {
		world->lightmapAtlasHeight = 1;
	}

	unsigned int *atlas = new unsigned int [world->lightmapAtlasWidth * world->lightmapAtlasHeight];
	FillLightmapAtlas(world, atlas);
	RETROVK_UploadMeshLightmap(&mesh, 0, world->lightmapAtlasWidth, world->lightmapAtlasHeight, atlas);
	delete[] atlas;

	// Convert the stashed texture-space coords into normalized atlas coords,
	// centred on the luxel (the +8 is half of the 16-texel luxel spacing).
	for (int i = 0; i < numSurfaces; i++) {
		int numEdges = world->map.getNumEdges(i);
		LightmapRect *rect = &world->lightmaps[i];
		primdesc_t *primitives = &world->surfacePrimitives[i * world->numMaxEdgesPerSurface];
		for (int j = 0; j < numEdges; j++, primitives++) {
			float luxelS = (primitives->l[0] - rect->minS * 16 + 8) / 16.0f;
			float luxelT = (primitives->l[1] - rect->minT * 16 + 8) / 16.0f;
			primitives->l[0] = (rect->x + luxelS) / (float)world->lightmapAtlasWidth;
			primitives->l[1] = (rect->y + luxelT) / (float)world->lightmapAtlasHeight;
		}
	}

	return true;
}

//
// Triangulate every surface (BSP faces are convex fans) into one device-local
// vertex buffer, recording each surface's slice so it can be drawn on its own.
//
bool BuildMeshVertices(World *world)
{
	int numSurfaces = world->map.getNumSurfaces();
	world->surfaceDraws = new SurfaceDraw[numSurfaces];
	world->skyDomeDraw.firstVertex = 0;
	world->skyDomeDraw.vertexCount = 0;

	// Count the triangulated vertices: an N-edge fan yields (N-2) triangles
	int totalVertices = 0;
	for (int i = 0; i < numSurfaces; i++) {
		int numEdges = world->map.getNumEdges(i);
		if (numEdges >= 3) {
			totalVertices += (numEdges - 2) * 3;
		}
	}
	int skyDomeVertices = SKY_DOME_SLICES * SKY_DOME_STACKS * 6;
	totalVertices += skyDomeVertices;

	RETROVK_Vertex *vertices = new RETROVK_Vertex[totalVertices];
	int written = 0;

	for (int i = 0; i < numSurfaces; i++) {
		int numEdges = world->map.getNumEdges(i);
		primdesc_t *prims = &world->surfacePrimitives[i * world->numMaxEdgesPerSurface];

		world->surfaceDraws[i].firstVertex = written;
		world->surfaceDraws[i].vertexCount = (numEdges >= 3) ? (numEdges - 2) * 3 : 0;

		// Fan triangulation around vertex 0: (0, k, k+1), preserving the winding
		// the primitive array was built with.
		for (int k = 1; k + 1 < numEdges; k++) {
			const int idx[3] = { 0, k, k + 1 };
			for (int t = 0; t < 3; t++) {
				primdesc_t *p = &prims[idx[t]];
				vertices[written].pos[0] = p->v[0];
				vertices[written].pos[1] = p->v[1];
				vertices[written].pos[2] = p->v[2];
				vertices[written].uv[0] = p->t[0];
				vertices[written].uv[1] = p->t[1];
				vertices[written].lightUv[0] = p->l[0];
				vertices[written].lightUv[1] = p->l[1];
				written++;
			}
		}
	}

	world->skyDomeDraw.firstVertex = written;
	world->skyDomeDraw.vertexCount = skyDomeVertices;

	for (int stack = 0; stack < SKY_DOME_STACKS; stack++) {
		float phi0 = (-0.5f + (float)stack / (float)SKY_DOME_STACKS) * 3.14159265358979323846f;
		float phi1 = (-0.5f + (float)(stack + 1) / (float)SKY_DOME_STACKS) * 3.14159265358979323846f;
		for (int slice = 0; slice < SKY_DOME_SLICES; slice++) {
			float theta0 = ((float)slice / (float)SKY_DOME_SLICES) * 2.0f * 3.14159265358979323846f;
			float theta1 = ((float)(slice + 1) / (float)SKY_DOME_SLICES) * 2.0f * 3.14159265358979323846f;
			float dirs[4][3] = {
				{ cosf(phi0) * cosf(theta0), cosf(phi0) * sinf(theta0), sinf(phi0) },
				{ cosf(phi1) * cosf(theta0), cosf(phi1) * sinf(theta0), sinf(phi1) },
				{ cosf(phi1) * cosf(theta1), cosf(phi1) * sinf(theta1), sinf(phi1) },
				{ cosf(phi0) * cosf(theta1), cosf(phi0) * sinf(theta1), sinf(phi0) },
			};
			const int indices[6] = { 0, 1, 2, 0, 2, 3 };
			for (int i = 0; i < 6; i++) {
				float *dir = dirs[indices[i]];
				vertices[written].pos[0] = dir[0] * SKY_DOME_RADIUS;
				vertices[written].pos[1] = dir[1] * SKY_DOME_RADIUS;
				vertices[written].pos[2] = dir[2] * SKY_DOME_RADIUS;
				vertices[written].uv[0] = 0.0f;
				vertices[written].uv[1] = 0.0f;
				vertices[written].lightUv[0] = 0.0f;
				vertices[written].lightUv[1] = 0.0f;
				written++;
			}
		}
	}

	// Hand the vertices to the mesh (uploaded to a device-local buffer internally)
	RETROVK_UploadMeshVertices(&mesh, vertices, totalVertices);
	delete[] vertices;

	return true;
}

//
// A flat colour for a surface in solid render mode: a deterministic colour
// derived from the surface id.
//
void GetSurfaceColor(int surface, float color[3])
{
	unsigned int hash = (unsigned int)surface * 1103515245u + 12345u;
	color[0] = (hash & 0xff) / 255.0f;
	color[1] = ((hash >> 8) & 0xff) / 255.0f;
	color[2] = ((hash >> 16) & 0xff) / 255.0f;
}

//
// Draw the visible surfaces.
//
void DrawSurfaces(World *world, int *visibleSurfaces, int numVisibleSurfaces)
{
	for (int i = 0; i < numVisibleSurfaces; i++) {
		int surface = visibleSurfaces[i];
		// The texture this surface uses, and where its triangles live in the mesh
		texinfo_t *textureInfo = world->map.getTextureInfo(surface);
		int textureIndex = ResolveTextureAnimation(world, textureInfo->miptex);
		int surfaceFlags = 0;
		if (textureIndex >= 0 && textureIndex < world->numTextures) {
			if (world->textureTurbulent[textureIndex]) surfaceFlags |= SURFACE_FLAG_TURBULENT;
			if (world->textureSky[textureIndex]) surfaceFlags |= SURFACE_FLAG_SKY;
		}
		if (renderMode == RETROVK_RENDER_TEXTURED && (surfaceFlags & SURFACE_FLAG_SKY)) {
			continue;
		}
		const SurfaceDraw *draw = &world->surfaceDraws[surface];
		float color[3];
		GetSurfaceColor(surface, color);
		RETROVK_DrawMeshRange(&mesh, textureIndex, surface, draw->firstVertex, draw->vertexCount, color, surfaceFlags);
	}
}

void DrawSkyDome(World *world)
{
	if (renderMode != RETROVK_RENDER_TEXTURED || world->skyTextureIndex < 0 || world->skyDomeDraw.vertexCount == 0) {
		return;
	}

	float color[3] = { 1.0f, 1.0f, 1.0f };
	RETROVK_DrawMeshRange(&mesh, world->skyTextureIndex, 0, world->skyDomeDraw.firstVertex, world->skyDomeDraw.vertexCount, color, SURFACE_FLAG_SKY);
}

//
// Calculate which other leaves are visible from the specified leaf, fetch the associated surfaces and draw them.
//
void DrawVisibleSet(World *world, dleaf_t *pLeaf)
{
	int numVisibleSurfaces = 0;
	// Leaves are numbered 1..numLeaves; bit (i-1) of the PVS maps to leaf i.
	int numLeaves = world->map.getNumLeaves();

	if (pLeaf->visofs < 0) {
		// No visibility information for this leaf: treat every leaf as potentially visible.
		for (int i = 1; i <= numLeaves; i++) {
			dleaf_t *visibleLeaf = world->map.getLeaf(i);
			int firstSurface = visibleLeaf->firstmarksurface;
			int lastSurface = firstSurface + visibleLeaf->nummarksurfaces;
			for (int k = firstSurface; k < lastSurface; k++) {
				if (numVisibleSurfaces < world->map.getNumSurfaceLists()) {
					world->visibleSurfaces[numVisibleSurfaces++] = world->map.getSurfaceList(k);
				}
			}
		}
	} else {
		// Decompress the run-length encoded PVS. A zero byte means "skip the next
		// (8 * following byte) leaves"; any other byte holds 8 visibility bits,
		// least-significant bit first.
		unsigned char *visibilityList = world->map.getVisibilityList(pLeaf->visofs);
		for (int i = 1; i <= numLeaves; ) {
			if (*visibilityList == 0) {
				i += 8 * visibilityList[1];
				visibilityList += 2;
			} else {
				for (int bit = 1; bit < 256 && i <= numLeaves; bit <<= 1, i++) {
					if (*visibilityList & bit) {
						// Fetch the leaf that is seen and copy its surfaces
						dleaf_t *visibleLeaf = world->map.getLeaf(i);
						int firstSurface = visibleLeaf->firstmarksurface;
						int lastSurface = firstSurface + visibleLeaf->nummarksurfaces;
						for (int k = firstSurface; k < lastSurface; k++) {
							if (numVisibleSurfaces < world->map.getNumSurfaceLists()) {
								world->visibleSurfaces[numVisibleSurfaces++] = world->map.getSurfaceList(k);
							}
						}
					}
				}
				visibilityList++;
			}
		}
	}

	// Draw the copied surfaces
	DrawSurfaces(world, world->visibleSurfaces, numVisibleSurfaces);
}

//
// Traverse the BSP tree to find the leaf containing the camera.
//
dleaf_t *FindCameraLeaf(World *world, RETRO_Camera *camera)
{
	dleaf_t *leaf = NULL;

	// Fetch the start node
	dnode_t *node = world->map.getStartNode();

	while (!leaf) {
		short nextNodeId;

		// Get a pointer to the plane which intersects the node
		dplane_t *plane = world->map.getPlane(node->planenum);

		// Calculate distance to the intersecting plane
		float distance = DotProduct(plane->normal, camera->origin);

		// If the camera is in front of the plane, traverse the right (front) node, otherwise traverse the left (back) node
		if (distance > plane->dist) {
			nextNodeId = node->children[0];
		} else {
			nextNodeId = node->children[1];
		}

		// If next node >= 0, traverse the node, otherwise use the inverse of the node as the index to the leaf (and we are done!)
		if (nextNodeId >= 0) {
			node = world->map.getNode(nextNodeId);
		} else {
			leaf = world->map.getLeaf(~nextNodeId);
		}
	}

	return leaf;
}

void DEMO_Startup(void)
{
	RETRO.title = "Quake!";
	RETRO.usagekeys = "WASD/arrows move, mouse look, PageUp/PageDown pitch, F toggle flycam, Space/Ctrl up/down";
	RETRO.fov = 75.0;
	RETRO.znear = 1.0;
	RETRO.zfar = 5000.0;
}

void DEMO_Initialize(void)
{
	// Load the map (BSP, palette and colormap)
	world.map = RETRO_LoadBSP("assets/start.bsp", "assets/palette.lmp", "assets/colormap.lmp");
	if (!world.map.bsp) {
		RETRO_RageQuit("Unable to load BSP\n");
	}

	// Build the world from the map
	if (!UploadTextures(&world)) {
		RETRO_RageQuit("Unable to initialize world textures\n");
	}
	if (!BuildTextureAnimations(&world)) {
		RETRO_RageQuit("Unable to initialize texture animations\n");
	}
	UpdateLightStyles(&world, 0.0);
	RETROVK_CreateMeshLightmaps(&mesh, world.map.getNumSurfaces(), VK_FILTER_LINEAR, VK_FILTER_LINEAR);
	if (!BuildSurfacePrimitives(&world)) {
		RETRO_RageQuit("Unable to initialize world surfaces\n");
	}
	if (!BuildMeshVertices(&world)) {
		RETRO_RageQuit("Unable to build vertex buffer\n");
	}

	RETROVK_CreateMeshPipelines(&mesh, quake_vert_spv, sizeof(quake_vert_spv), quake_frag_spv, sizeof(quake_frag_spv));

	// Set the camera's starting position
	camera.SetPosition(540.0f, 260.0f, 100.0f);
	camera.SetOrientation(90.0f, 0.0f);
	camera.SetMovementSpeed(MOVEMENT_SPEED);
	camera.SetFlycam(true);
}

void DEMO_Deinitialize(void)
{
	// Destroy every GPU resource (waits for the device to go idle first)
	RETROVK_DestroyMesh(&mesh);

	if (world.surfaceDraws) delete[] world.surfaceDraws;
	if (world.surfacePrimitives) delete[] world.surfacePrimitives;
	if (world.visibleSurfaces) delete[] world.visibleSurfaces;
	if (world.lightmaps) delete[] world.lightmaps;
	if (world.textureAnimations) delete[] world.textureAnimations;
	if (world.textureSky) delete[] world.textureSky;
	if (world.textureTurbulent) delete[] world.textureTurbulent;
	RETRO_FreeBSP(&world.map);
}

void DEMO_Input(double deltatime)
{
	// Select the render mode with keys 1-4.
	if (RETRO_KeyPressed(SDL_SCANCODE_1)) {
		renderMode = RETROVK_RENDER_TEXTURED;
	} else if (RETRO_KeyPressed(SDL_SCANCODE_2)) {
		renderMode = RETROVK_RENDER_SOLID;
	} else if (RETRO_KeyPressed(SDL_SCANCODE_3)) {
		renderMode = RETROVK_RENDER_WIREFRAME;
	} else if (RETRO_KeyPressed(SDL_SCANCODE_4)) {
		renderMode = RETROVK_RENDER_POINTS;
	}

	// Keyboard handling
	float scale = (float)(deltatime * RETRO_INPUT_FRAMERATE);

	if (RETRO_KeyState(SDL_SCANCODE_W) || RETRO_KeyState(SDL_SCANCODE_UP)) {
		camera.MoveForward(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_S) || RETRO_KeyState(SDL_SCANCODE_DOWN)) {
		camera.MoveBackward(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_D)) {
		camera.StrafeRight(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_A)) {
		camera.StrafeLeft(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_RIGHT)) {
		camera.TurnRight(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_LEFT)) {
		camera.TurnLeft(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_PAGEUP)) {
		camera.PitchUp(scale);
	}
	if (RETRO_KeyState(SDL_SCANCODE_PAGEDOWN)) {
		camera.PitchDown(scale);
	}
	if (RETRO_KeyPressed(SDL_SCANCODE_F)) {
		camera.SetFlycam(!camera.flycam);
	}
	if (camera.flycam && RETRO_KeyState(SDL_SCANCODE_SPACE)) {
		camera.MoveUp(scale);
	}
	if (camera.flycam && (RETRO_KeyState(SDL_SCANCODE_LCTRL) || RETRO_KeyState(SDL_SCANCODE_RCTRL))) {
		camera.MoveDown(scale);
	}

	// Mouse handling
	if (!RETRO.showcursor) {
		float xrel = 0.0f;
		float yrel = 0.0f;
		RETRO_MouseMotion(&xrel, &yrel);
		camera.MouseLook(xrel, yrel);
	}

	camera.Update();
}

void DEMO_Render(double deltatime)
{
	world.textureTime += deltatime;

	if (UpdateLightStyles(&world, deltatime)) {
		UpdateLightmapAtlas(&world);
	}

	// Build the model-view-projection matrix (Vulkan equivalent of gluPerspective/gluLookAt)
	float aspect = (float)RETROVK.swapchainExtent.width / (float)RETROVK.swapchainExtent.height;
	mat4_t projection, view, mvp;
	Mat4Perspective((float)RETRO.fov, aspect, (float)RETRO.znear, (float)RETRO.zfar, projection);
	float center[3] = {
		camera.origin[0] + camera.forward[0],
		camera.origin[1] + camera.forward[1],
		camera.origin[2] + camera.forward[2],
	};
	Mat4LookAt(camera.origin, center, camera.up, view);
	Mat4Multiply(projection, view, mvp);

	// Find the leaf the camera is in, then draw everything visible from it.
	dleaf_t *leaf = FindCameraLeaf(&world, &camera);

	// Render visible BSP surfaces.
	RETROVK_PrepareMeshDraws(&mesh, mvp, renderMode, (float)world.textureTime, camera.origin);
	DrawSkyDome(&world);
	DrawVisibleSet(&world, leaf);
}
