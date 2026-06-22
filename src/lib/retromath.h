//
// Retro graphics library
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//

#ifndef _RETROMATH_H_
#define _RETROMATH_H_

#include <math.h>

// Dot product of two 3D vectors
inline float DotProduct(const float a[3], const float b[3])
{
	return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
}

// Normalize a 3D vector in place (leaves a zero-length vector untouched)
inline void Normalize(float v[3])
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (length == 0.0f) {
		return;
	}
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
}

// Cross product: result = a x b
inline void Cross(const float a[3], const float b[3], float result[3])
{
	result[0] = a[1] * b[2] - a[2] * b[1];
	result[1] = a[2] * b[0] - a[0] * b[2];
	result[2] = a[0] * b[1] - a[1] * b[0];
}

// Signed area of edge a->b versus point (x,y). Its sign says which side of the edge
// the point is on; used for triangle coverage tests and barycentric weights.
inline float EdgeFunction(float ax, float ay, float bx, float by, float x, float y)
{
	return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
}

// Wrap a texel coordinate into [0, size), handling negative values
inline int WrapTexel(int value, int size)
{
	value %= size;
	if (value < 0) {
		value += size;
	}
	return value;
}

// Floor of value/16; lightmap luxels are spaced one per 16 texels
inline int FloorDiv16(float value)
{
	return (int)floorf(value / 16.0f);
}

// Ceil of value/16; lightmap luxels are spaced one per 16 texels
inline int CeilDiv16(float value)
{
	return (int)ceilf(value / 16.0f);
}

// 4x4 matrix, stored column-major to match GLSL's mat4 memory layout.
// Element (row r, column c) lives at index c * 4 + r.
typedef float mat4_t[16];

// Build a perspective projection for Vulkan clip space. Unlike OpenGL,
// Vulkan's NDC has Y pointing down and depth in [0, 1]; the Y row is negated
// and the depth terms map near..far to 0..1 so the result is upright.
inline void Mat4Perspective(float fovYdeg, float aspect, float znear, float zfar, mat4_t out)
{
	float f = 1.0f / tanf(fovYdeg * (float)M_PI / 360.0f); // cot(fovY/2)
	for (int i = 0; i < 16; i++) {
		out[i] = 0.0f;
	}
	out[0] = f / aspect;
	out[5] = -f;								// Y-flip for Vulkan
	out[10] = zfar / (znear - zfar);			// depth 0..1
	out[11] = -1.0f;
	out[14] = (zfar * znear) / (znear - zfar);
}

// Build a right-handed view matrix looking from eye towards center.
inline void Mat4LookAt(const float eye[3], const float center[3], const float up[3], mat4_t out)
{
	float forward[3] = { center[0] - eye[0], center[1] - eye[1], center[2] - eye[2] };
	Normalize(forward);

	float side[3];
	Cross(forward, up, side);
	Normalize(side);

	float trueUp[3];
	Cross(side, forward, trueUp);

	out[0] = side[0];   out[4] = side[1];    out[8]  = side[2];    out[12] = -DotProduct(side, eye);
	out[1] = trueUp[0]; out[5] = trueUp[1];  out[9]  = trueUp[2];  out[13] = -DotProduct(trueUp, eye);
	out[2] = -forward[0]; out[6] = -forward[1]; out[10] = -forward[2]; out[14] = DotProduct(forward, eye);
	out[3] = 0.0f;      out[7] = 0.0f;       out[11] = 0.0f;       out[15] = 1.0f;
}

// Matrix product out = a * b (column-major, so out column j = a * b's column j).
inline void Mat4Multiply(const mat4_t a, const mat4_t b, mat4_t out)
{
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] +
							 a[1 * 4 + r] * b[c * 4 + 1] +
							 a[2 * 4 + r] * b[c * 4 + 2] +
							 a[3 * 4 + r] * b[c * 4 + 3];
		}
	}
}

#endif
