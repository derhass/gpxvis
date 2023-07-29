#version 430 core

layout(std430, binding=0) readonly buffer pointsBuffer
{
	vec2 point[];
} points;

layout(std140, binding=0) uniform transformParamUBO
{
	vec4 scale_offset;
	vec4 size;
} transformParam;

out vec2 lineCoord;

void main()
{
	vec2 basePoint = points.point[gl_VertexID];
	lineCoord = vec2(0, gl_VertexID & 1);
	gl_Position = vec4(transformParam.scale_offset.xy * basePoint + transformParam.scale_offset.zw, 0, 1);
}
