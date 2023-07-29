#include "vis.h"

#include <string.h>

#include "util.h"

namespace gpxvis {

namespace ubo {

/****************************************************************************
 * UBO DEFINITION                                                           *
 ****************************************************************************/

struct transformParam {
	GLfloat scale_offset[4];
};

struct lineParam {
	GLfloat colorBase[4];
	GLfloat colorGradient[3][4];
	GLfloat distCoeff[4];
	GLfloat lineWidths[2];
};

} // namespace ubo


/****************************************************************************
 * VISUALIZE A SINGLE POLYGON, MIX IT WITH THE HISTORY                      *
 ****************************************************************************/

CVis::CVis() :
	bufferVertexCount(0),
	vertexCount(0),
	width(0),
	height(0),
	vaoEmpty(0),
	ssboLine(0),
	programLine(0)
{
	colorBackground[0] = 0.0f;
	colorBackground[1] = 0.0f;
	colorBackground[2] = 0.0f;
	colorBackground[3] = 0.0f;

	colorBase[0] = 1.0f;
	colorBase[1] = 1.0f;
	colorBase[2] = 1.0f;
	colorBase[3] = 1.0f;

	colorGradient[0][0] = 1.0f;
	colorGradient[0][1] = 0.0f;
	colorGradient[0][2] = 0.0f;
	colorGradient[0][3] = 1.0f;

	colorGradient[1][0] = 1.0f;
	colorGradient[1][1] = 1.0f;
	colorGradient[1][2] = 0.0f;
	colorGradient[1][3] = 1.0f;

	colorGradient[2][0] = 0.0f;
	colorGradient[2][1] = 1.0f;
	colorGradient[2][2] = 0.0f;
	colorGradient[2][3] = 1.0f;

	trackWidth = 1.0f;
	neighborhoodWidth = 3.0f;

	for (int i=0; i<FB_COUNT; i++) {
		fbo[i] = 0;
		tex[i] = 0;
	}

	for (int i=0; i<UBO_COUNT; i++) {
		ubo[i] = 0;
	}
}

CVis::~CVis()
{
	DropGL();
}

bool CVis::InitializeGL(GLsizei w, GLsizei h)
{
	if (w != width || h != height) {
		DropGL();
	}

	if (!vaoEmpty) {
		glGenVertexArrays(1, &vaoEmpty);
		glBindVertexArray(vaoEmpty);
		glBindVertexArray(0);
		gpxutil::info("created VAO %u (empty)", vaoEmpty);
	}

	if (!programLine) {
		programLine = gpxutil::programCreateFromFiles("shaders/line.vs", "shaders/line.fs"); 
		if (!programLine) {
			gpxutil::warn("line shader failed");
			return false;
		}
		gpxutil::info("created program %u (line shader)", programLine);
	}

	for (int i=0; i<FB_COUNT; i++) {
		if (!tex[i]) {
			GLenum format = (i == FB_NEIGHBORHOOD)? GL_R8:GL_RGBA8;
			glGenTextures(1, &tex[i]);
			glBindTexture(GL_TEXTURE_2D, tex[i]);
			glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
			glBindTexture(GL_TEXTURE_2D, 0);
			gpxutil::info("created texture %u %ux%u fmt 0x%x (frambeuffer idx %d color attachment)", tex[i], (unsigned)w, (unsigned)h, (unsigned)format, i);
		}

		if (!fbo[i]) {
			glGenFramebuffers(1, &fbo[i]);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[i], 0);
			GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				gpxutil::warn("framebuffer idx %d setup failed with status 0x%x", i, (unsigned)status);
				return false;
			}
			gpxutil::info("created FBO %u (frambeuffer idx %d)", fbo[i], i);
		}
	}

	width = w;
	height = h;
	
	for (int i=0; i<UBO_COUNT; i++) {
		if (!InitializeUBO(i)) {
			return false;
		}
	}
	return true;
}

bool CVis::InitializeUBO(int i)
{
	GLsizeiptr size=0;
	void *ptr = NULL;

	if (ubo[i]) {
		gpxutil::info("destroying buffer %u (UBO idx %d)", ubo[i], i);
		glDeleteBuffers(1, &ubo[i]);
		ubo[i] = 0;
	}

	glGenBuffers(1, &ubo[i]);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo[i]);

	ubo::transformParam transformParam;
	ubo::lineParam lineParam;

	switch(i) {
		case UBO_TRANSFORM:
			size = sizeof(ubo::transformParam);
			ptr = &transformParam;
			transformParam.scale_offset[0] = 2.0f;
			transformParam.scale_offset[1] = 2.0f;
			transformParam.scale_offset[2] =-1.0f;
			transformParam.scale_offset[3] =-1.0f;
			break;
		case UBO_LINE:
			size = sizeof(ubo::lineParam);
			ptr = &lineParam;
			memcpy(lineParam.colorBase, colorBase, 4*sizeof(GLfloat));
			memcpy(lineParam.colorGradient, colorGradient, 3*4*sizeof(GLfloat));
			lineParam.distCoeff[0] = 1.0f;
			lineParam.distCoeff[1] = 0.0f;
			lineParam.distCoeff[2] = 1.0f;
			lineParam.distCoeff[3] = 0.0f;
			// TODO XXXXXXXXXX
			lineParam.lineWidths[0] = 0.1f;
			lineParam.lineWidths[1] = 0.1f;
			break;
		default:
			gpxutil::warn("invalid UBO idx %d", i);
			return false;
	}
	glBufferStorage(GL_UNIFORM_BUFFER, size, ptr, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	gpxutil::info("created buffer %u (UBO idx %d) size %u", ubo[i], i, (unsigned)size);
	return true;
}

void CVis::DropGL()
{
	if (vaoEmpty) {
		gpxutil::info("destroying VAO %u (empty)", vaoEmpty);
		glDeleteVertexArrays(1, &vaoEmpty);
		vaoEmpty = 0;
	}
	if (ssboLine) {
		gpxutil::info("destroying buffer %u (SSBO line)", ssboLine);
		glDeleteBuffers(1, &ssboLine);
		ssboLine = 0;
	}
	if (programLine) {
		gpxutil::info("destroying program %u (line shader)", programLine);
		glDeleteProgram(programLine);
		programLine = 0;
	}
	for (int i=0; i<FB_COUNT; i++) {
		if (fbo[i]) {
			gpxutil::info("destroying FBO %u (frambeuffer idx %d)", fbo[i], i);
			glDeleteFramebuffers(1, &fbo[i]);
			fbo[i] = 0;
		}
		if (tex[i]) {
			gpxutil::info("destroying texture %u (frambeuffer idx %d color attachment)", tex[i], i);
			glDeleteTextures(1, &tex[i]);
			tex[i] = 0;
		}
	}
	for (int i=0; i<UBO_COUNT; i++) {
		if (ubo[i]) {
			gpxutil::info("destroying buffer %u (UBO idx %d)", ubo[i], i);
			glDeleteBuffers(1, &ubo[i]);
			ubo[i] = 0;
		}
	}
	width = 0;
	height = 0;
}

void CVis::SetPolygon(const std::vector<GLfloat>& vertices2D)
{
	if (ssboLine) {
		gpxutil::info("destroying buffer %u (SSBO line)", ssboLine);
		glDeleteBuffers(1, &ssboLine);
		ssboLine = 0;
	}
	glGenBuffers(1, &ssboLine);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLine);
	glBufferStorage(GL_SHADER_STORAGE_BUFFER, sizeof(GLfloat) * vertices2D.size(), vertices2D.data(), 0);
	bufferVertexCount = vertexCount = vertices2D.size() / 2;
	gpxutil::info("created buffer %u (SSBO line) for %u vertices", ssboLine, (unsigned)bufferVertexCount);
}

void  CVis::Draw()
{
	glUseProgram(programLine);
	glBindVertexArray(vaoEmpty);

	glBlendEquation(GL_MAX);
	glBlendFunc(GL_ONE, GL_ONE);
	glEnable(GL_BLEND);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboLine);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[UBO_TRANSFORM]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE]);
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
	if (!vis.InitializeGL(width, height)) {
		return false;
	}

	if (tracks.size() < 1) {
		gpxutil::warn("anim controller without tracks");
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

