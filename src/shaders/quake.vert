#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
	mat4 mvp;
	vec3 color;
	int mode;
} pc;

layout(location = 0) out vec2 fragTexCoord;

void main()
{
	gl_Position = pc.mvp * vec4(inPosition, 1.0);
	gl_PointSize = 2.0;					// Used only by the points pipeline
	fragTexCoord = inTexCoord;
}
