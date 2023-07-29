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

layout(std140, binding=1) uniform lineParamUBO
{
	vec4 colorBase;
	vec4 colorGradient[3];
	vec4 distCoeff;
	vec2 lineWidths;
} lineParam;

out vec2 lineCoord;

void main()
{
	int lineIdx = gl_VertexID / 18;
	int partIdx = gl_VertexID % 18;
	int segmentIdx = partIdx / 6;
	int vertexIdx = partIdx % 6;
	vec2 vertices[6]=vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1, 1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
	ivec2 pointSelector[3] = ivec2[3](ivec2(0,0), ivec2(1,1), ivec2(0,1));

	vec2 line[2] = vec2[2](points.point[lineIdx],points.point[lineIdx+1]);

	vec2 delta = line[1] - line[0];
	float len = length(delta);
	vec2 t;
	if (len > 0.0000001) {
		t = normalize(delta);
	} else {
		t = vec2(1,0);
	}
	vec2 n = vec2(-t.y, t.x);
	float width = 0.1;

	ivec2 selector = pointSelector[segmentIdx];
	vec2 vertex = vertices[vertexIdx];

	int side = (vertex.x < 0.0) ? 0 : 1;
	vec2 basePoint = line[selector[side]];
	if (segmentIdx == selector && side == (1-selector)) {
		vertex.x = 0.0;
	} else {
		vertex.x *= float(1 - selector.y + selector.x);
	}
	lineCoord = vertex;

	vertex = lineParam.lineWidths.y * vertex;

	basePoint = basePoint + vertex.x * t + vertex.y * n;

	gl_Position = vec4(transformParam.scale_offset.xy * basePoint + transformParam.scale_offset.zw, 0, 1);


}
