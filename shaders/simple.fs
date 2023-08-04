#version 430 core

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
	color = lineParam.colorBase;
}
