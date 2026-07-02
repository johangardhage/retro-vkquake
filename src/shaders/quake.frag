#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
	mat4 mvp;
	vec3 color;	// Flat surface colour (solid mode)
	int mode;	// 0 = textured, 1 = solid, 2+ = single colour (wireframe/points)
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
	if (pc.mode == 0) {
		// Textured
		outColor = texture(texSampler, fragTexCoord);
	} else if (pc.mode == 1) {
		// Solid: the surface's flat colour, no shading
		outColor = vec4(pc.color, 1.0);
	} else {
		// Wireframe / points: single colour
		outColor = vec4(0.85, 0.85, 0.85, 1.0);
	}
}
