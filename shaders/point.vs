#version 430 core

layout(std430, binding=0) readonly buffer pointsBuffer
{
	vec2 point[];
} points;

layout(std140, binding=0) uniform transformParamUBO
{
	vec4 scale_offset;
	vec4 size;
	vec4 zoomShift;
} transformParam;

layout(std140, binding=1) uniform lineParamUBO
{
	vec4 colorBase;
	vec4 colorGradient[4];
	vec4 distCoeff;
	vec4 distExp;
	vec4 lineWidths;
} lineParam;

layout(location=1) uniform float upTo;

out vec2 lineCoord;
out vec2 texCoord;

void main()
{
	int idx = int(upTo);
	int maxIdx = points.point.length()-1;
	vec2 line[2] = vec2[2](points.point[min(idx,maxIdx)],points.point[min(idx+1,maxIdx)]);
	int vertexIdx = gl_VertexID % 6;
	vec2 vertices[6]=vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1, 1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
	vec2 point = transformParam.zoomShift.xy * mix(line[0], line[1], fract(upTo)) + transformParam.zoomShift.zw;

	vec2 vertex = vertices[vertexIdx];
	lineCoord = vertex;
	vec2 basePoint = point + lineParam.lineWidths.zw * vertex;
	gl_Position = vec4(transformParam.scale_offset.xy * basePoint + transformParam.scale_offset.zw, 0, 1);
	texCoord = 0.5 * (transformParam.scale_offset.xy * point + transformParam.scale_offset.zw) + 0.5;
}
