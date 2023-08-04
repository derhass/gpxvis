#version 430 core

in vec2 lineCoord;
layout(location=0) out vec4 color;

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
	float d = length(lineCoord);
	if (d > 1.0) {
		discard;
	}
	d = 1.0 - pow(d, lineParam.distExp.x);

	float c = lineParam.distCoeff.x * d + lineParam.distCoeff.y;
	float a = lineParam.distCoeff.z * d + lineParam.distCoeff.w;

	color = vec4(c * lineParam.colorBase.rgb, a * lineParam.colorBase.a);
}
