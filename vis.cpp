#include "vis.h"

#include "util.h"

namespace gpxvis {

/****************************************************************************
 * VISUALIZE A SINGLE POLYGON, MIX IT WITH THE HISTORY                      *
 ****************************************************************************/

CVis::CVis() :
	bufferVertexCount(0),
	vertexCount(0),
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

void CVis::SetPolygon(const std::vector<GLfloat>& vertices2D)
{
	if (ssboLine) {
		glDeleteBuffers(1, &ssboLine);
		ssboLine = 0;
	}
	glGenBuffers(1, &ssboLine);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLine);
	glBufferStorage(GL_SHADER_STORAGE_BUFFER, sizeof(GLfloat) * vertices2D.size(), vertices2D.data(), 0);
	bufferVertexCount = vertexCount = vertices2D.size() / 2;
}

void  CVis::Draw()
{
	glUseProgram(programLine);
	glBindVertexArray(vaoEmpty);

	glBlendEquation(GL_MAX);
	glBlendFunc(GL_ONE, GL_ONE);
	glEnable(GL_BLEND);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboLine);
	glDrawArrays(GL_TRIANGLES, 0, 18*(vertexCount-1));
}

/****************************************************************************
 * MANAGE ANIMATIONS AND MULTIPLE TRACKS                                    *
 ****************************************************************************/

CAnimController::CAnimController() :
	curTrack(0)
{
}

bool CAnimController::AddTrack(const char *filename)
{
	size_t idx = tracks.size();
	tracks.emplace_back();
	if (!tracks[idx].Load(filename)) {
		tracks.resize(idx);
		return false;
	}
	aabb.MergeWith(tracks[idx].GetAABB());
	return true;
}

bool CAnimController::Prepare(GLsizei width, GLsizei height)
{
	if (!vis.InitializeGL()) {
		return false;
	}

	if (tracks.size() < 1) {
		return false;
	}

	double scale[3];
	double offset[3];

	gpxutil::CAABB screenAABB = aabb;
	screenAABB.Enhance(1.05,0.0);
	screenAABB.GetNormalizeScaleOffset(scale, offset);
	if (scale[1] < scale[0]) {
		scale[0] = scale[1];
	} else {
		scale[1] = scale[0];
	}

	std::vector<GLfloat> vertices;
	tracks[0].GetVertices(false, offset, scale, vertices);
	vis.SetPolygon(vertices);

	return true;
}

void CAnimController::DropGL()
{
	vis.DropGL();
}

void CAnimController::Draw()
{
	vis.Draw();
}

} // namespace gpxvis

