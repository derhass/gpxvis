#version 430 core

in vec2 lineCoord;
layout(location=0) out vec4 color;

layout(std140, binding=1) uniform lineParamUBO
{
	vec4 colorBase;
	vec4 colorGradient[3];
	vec4 distCoeff;
	vec2 lineWidths;
} lineParam;

void main()
{
	float d = 1.0 - length(lineCoord);
	if (d < 0.0) {
		discard;
	}

	float c = lineParam.distCoeff.x * d + lineParam.distCoeff.y;
	float a = lineParam.distCoeff.z * d + lineParam.distCoeff.w;

	color = lineParam.colorBase * vec4(c,c,c,a);
}
