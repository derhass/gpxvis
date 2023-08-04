#version 430 core

in vec2 texCoord;
layout(location=0) out vec4 color;

layout(location=0, binding=3) uniform sampler2D tex;

void main()
{
	color = texture(tex, texCoord);
}
