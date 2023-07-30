#version 430 core

in vec2 lineCoord;
in vec2 texCoord;

layout(location=0) out vec4 color;

layout(std140, binding=1) uniform lineParamUBO
{
	vec4 colorBase;
	vec4 colorGradient[4];
	vec4 distCoeff;
	vec4 lineWidths;
} lineParam;

layout(location=0, binding=0) uniform sampler2D texBackground;

void main()
{
	float d = length(lineCoord);

	if (d > 1.0) {
		discard;
	}
	gl_FragDepth = d;

	float ndHere = texelFetch(texBackground, ivec2(gl_FragCoord.xy), 0).r;
	float ndLine = textureLod(texBackground, texCoord, 0).r;
	float nd = 2.0 * clamp(max(ndHere, ndLine), 0.0, 1.999999);
	//float nd = 2.0 * clamp(ndLine, 0.0, 1.999999);
	int sel = int(nd);
	vec3 col = mix(lineParam.colorGradient[sel].rgb, lineParam.colorGradient[sel+1].rgb, fract(nd));
	color = vec4(col, 1-d);
}
