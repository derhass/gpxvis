#version 430 core

in vec2 texCoord;
out vec4 color;

layout(binding=1) uniform sampler2D texBackground;
layout(binding=2) uniform sampler2D texOverlay;


void main()
{
	vec4 bg = texture(texBackground, texCoord);
	vec4 fg = texture(texOverlay, texCoord);
	color = vec4(mix(bg.rgb, fg.rgb, fg.a), fg.a);
}
