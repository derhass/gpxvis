#version 430 core

in vec2 texCoord;
layout(location=0) out vec4 color;

layout(location=0, binding=1) uniform sampler2D texBackground;
layout(location=1, binding=2) uniform sampler2D texOverlay;
layout(location=2) uniform float baseAlpha;

layout(std140, binding=1) uniform lineParamUBO
{
	vec4 colorBase;
	vec4 colorGradient[4];
	vec4 distCoeff;
	vec4 distExp;
	vec4 lineWidths;
} lineParam;

void main()
{
	float history = texture(texBackground, texCoord).r;
	float standardHistory = min(history, 1.0);
	float historyExp = pow(history,lineParam.distExp.x);
	float extraHistory = max(historyExp - 1.0, 0.0);
	float gradient = 2.0 * historyExp / (historyExp + lineParam.distExp.y);
	vec4 bgBase = mix(lineParam.colorGradient[0], lineParam.colorGradient[1], standardHistory);
	vec4 bgA = min(bgBase + extraHistory * lineParam.colorGradient[2], 1.0);
	int sel = int(gradient);
	vec4 bgB = min(mix(lineParam.colorGradient[sel],lineParam.colorGradient[sel+1],fract(gradient)), 1.0);
	vec4 bg = lineParam.distCoeff[0] * bgA + lineParam.distCoeff[1]* bgB;
	vec4 fg = texture(texOverlay, texCoord);
	float alpha = baseAlpha * fg.a;
	color = vec4(mix(bg.rgb, fg.rgb, alpha), alpha);
}
