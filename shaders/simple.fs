#version 430 core

out vec4 color;

layout(std140, binding=1) uniform lineParamUBO
{
	vec4 colorBase;
	vec4 colorGradient[3];
	vec4 distCoeff;
	vec2 lineWidths;
} lineParam;

void main()
{
	color = lineParam.colorBase;
}
