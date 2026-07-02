#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec2 fragLightCoord;
layout(location = 2) in vec3 fragSkyDir;

layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 0) uniform sampler2D lightSampler;
layout(set = 2, binding = 0) uniform sampler2D lumaSampler;

layout(push_constant) uniform PushConstants {
	mat4 mvp;
	vec3 color;	// Flat surface colour (solid mode)
	int mode;	// 0 = textured, 1 = solid, 2+ = single colour (wireframe/points)
	float time;
	int surfaceFlags;
	float cameraOriginX;
	float cameraOriginY;
	float cameraOriginZ;
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
	if (pc.mode == 0) {
		// Textured
		if ((pc.surfaceFlags & 2) != 0) {
			vec3 skyDir = vec3(fragSkyDir.xy, fragSkyDir.z * 3.0);
			float scale = (6.0 * 63.0) / max(length(skyDir), 0.0001);
			vec2 sky = skyDir.xy * scale / 128.0;
			vec2 backUv = fract(sky + pc.time * (8.0 / 128.0));
			vec2 frontUv = fract(sky + pc.time * (16.0 / 128.0));
			vec2 backCoord = vec2(0.5 + backUv.x * 0.5, backUv.y);
			vec2 frontCoord = vec2(frontUv.x * 0.5, frontUv.y);
			vec4 back = texture(texSampler, backCoord);
			vec4 front = texture(texSampler, frontCoord);
			outColor = vec4(mix(back.rgb, front.rgb, front.a), 1.0);
		} else {
			vec4 base = texture(texSampler, fragTexCoord);
			float light = texture(lightSampler, fragLightCoord).r;
			vec4 luma = texture(lumaSampler, fragTexCoord);
			outColor = vec4(base.rgb * light * 2.0 + luma.rgb * luma.a, base.a);
		}
	} else if (pc.mode == 1) {
		// Solid: the surface's flat colour, no shading
		outColor = vec4(pc.color, 1.0);
	} else {
		// Wireframe / points: single colour
		outColor = vec4(0.85, 0.85, 0.85, 1.0);
	}
}
