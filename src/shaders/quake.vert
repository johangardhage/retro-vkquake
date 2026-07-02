#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec2 inLightCoord;

layout(push_constant) uniform PushConstants {
	mat4 mvp;
	vec3 color;
	int mode;
	float time;
	int surfaceFlags;
	float cameraOriginX;
	float cameraOriginY;
	float cameraOriginZ;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec2 fragLightCoord;
layout(location = 2) out vec3 fragSkyDir;

void main()
{
	vec3 position = inPosition;
	if ((pc.surfaceFlags & 2) != 0) {
		position += vec3(pc.cameraOriginX, pc.cameraOriginY, pc.cameraOriginZ);
	}

	gl_Position = pc.mvp * vec4(position, 1.0);
	gl_PointSize = 2.0;					// Used only by the points pipeline
	fragTexCoord = inTexCoord;
	if ((pc.surfaceFlags & 1) != 0) {
		fragTexCoord.x += sin(inTexCoord.y * 6.28 + pc.time * 2.0) * 0.0625;
		fragTexCoord.y += sin(inTexCoord.x * 6.28 + pc.time * 2.0) * 0.0625;
	}
	fragLightCoord = inLightCoord;
	fragSkyDir = normalize(inPosition);
}
