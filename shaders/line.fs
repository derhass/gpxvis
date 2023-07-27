#version 430 core

in vec2 lineCoord;
out vec4 color;

void main()
{
	float d = 1.0 - length(lineCoord);
	if (d < 0.0) {
		discard;
	}


	color = vec4(d,d,d, 1);
}
