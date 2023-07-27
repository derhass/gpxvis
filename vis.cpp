#include "vis.h"

#include "util.h"

namespace gpxvis {

CVis::CVis() :
	vaoEmpty(0),
	ssboLine(0),
	programLine(0)
{
}

CVis::~CVis()
{
	DropGL();
}

bool CVis::InitializeGL()
{
	if (!vaoEmpty) {
		glGenVertexArrays(1, &vaoEmpty);
		glBindVertexArray(vaoEmpty);
	}

	if (!programLine) {
		programLine = gpxutil::programCreateFromFiles("shaders/line.vs", "shaders/line.fs"); 
	}

	return (programLine && vaoEmpty);
}

void CVis::DropGL()
{
	if (vaoEmpty) {
		glDeleteVertexArrays(1, &vaoEmpty);
		vaoEmpty = 0;
	}
	if (ssboLine) {
		glDeleteBuffers(1, &ssboLine);
		ssboLine = 0;
	}
	if (programLine) {
		glDeleteProgram(programLine);
		programLine = 0;
	}
}

void  CVis::Draw()
{
	if (!ssboLine) {
		glGenBuffers(1, &ssboLine);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLine);
		GLfloat data[] = {-0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
		glBufferStorage(GL_SHADER_STORAGE_BUFFER, sizeof(data), data, 0);
	}
	glUseProgram(programLine);
	glBindVertexArray(vaoEmpty);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboLine);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

} // namespace gpxvis

