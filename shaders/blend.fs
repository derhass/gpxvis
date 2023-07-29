#version 430 core

in vec2 texCoord;
layout(location=0) out vec4 color;

layout(location=0, binding=1) uniform sampler2D texBackground;
layout(location=1, binding=2) uniform sampler2D texOverlay;
layout(location=2) uniform float baseAlpha;

void main()
{
	vec4 bg = texture(texBackground, texCoord);
	vec4 fg = texture(texOverlay, texCoord);
	float alpha = baseAlpha * fg.a;
	color = vec4(mix(bg.rgb, fg.rgb, alpha), alpha);
}
