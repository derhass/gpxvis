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
	GLfloat size[4];
};

struct lineParam {
	GLfloat colorBase[4];
	GLfloat colorGradient[4][4];
	GLfloat distCoeff[4];
	GLfloat lineWidths[4];
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
	vaoEmpty(0)
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

	colorGradient[3][0] = 1.0f;
	colorGradient[3][1] = 1.0f;
	colorGradient[3][2] = 1.0f;
	colorGradient[3][3] = 1.0f;

	trackWidth = 1.0f;
	trackPointWidth = 1.0f;
	neighborhoodWidth = 3.0f;

	for (int i=0; i<SSBO_COUNT; i++) {
		ssbo[i] = 0;
	}
	for (int i=0; i<FB_COUNT; i++) {
		fbo[i] = 0;
		tex[i] = 0;
	}
	for (int i=0; i<UBO_COUNT; i++) {
		ubo[i] = 0;
	}
	for (int i=0; i<PROG_COUNT; i++) {
		program[i] = 0;
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

	for (int i=0; i<FB_COUNT; i++) {
		if (!tex[i]) {
			GLenum format = (i == FB_NEIGHBORHOOD)? GL_R8:GL_RGBA8;
			glGenTextures(1, &tex[i]);
			glBindTexture(GL_TEXTURE_2D, tex[i]);
			glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glBindTexture(GL_TEXTURE_2D, 0);
			gpxutil::info("created texture %u %ux%u fmt 0x%x (frambeuffer idx %d color attachment)", tex[i], (unsigned)w, (unsigned)h, (unsigned)format, i);
		}

		if (fbo[i]) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
		} else {
			glGenFramebuffers(1, &fbo[i]);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[i], 0);
			GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				gpxutil::warn("framebuffer idx %d setup failed with status 0x%x", i, (unsigned)status);
				return false;
			}
			gpxutil::info("created FBO %u (frambeuffer idx %d)", fbo[i], i);
		}
		GLfloat clear[4];
		if (i == FB_BACKGROUND) {
			memcpy(clear, colorBackground, sizeof(GLfloat)*4);
		} else {
			clear[0] = clear[1] = clear[2] = clear[3] = 0.0f;
		}
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}

	width = w;
	height = h;
	
	for (int i=0; i<UBO_COUNT; i++) {
		if (!InitializeUBO(i)) {
			return false;
		}
	}

	static const char* programs[PROG_COUNT][2] = {
		{ "shaders/simple.vs", "shaders/simple.fs" },
		{ "shaders/track.vs", "shaders/track.fs"},
		{ "shaders/line.vs", "shaders/line.fs"},
		{ "shaders/point.vs", "shaders/point.fs"},
		{ "shaders/fullscreen.vs", "shaders/blend.fs"},
	};

	for (int i=0; i<PROG_COUNT; i++) {
		if (!program[i]) {
			program[i] = gpxutil::programCreateFromFiles(programs[i][0], programs[i][1]);
			if (!program[i]) {
				gpxutil::warn("program idx %d (%s, %s) failed", i, programs[i][0], programs[i][1]);
				return false;
			}
			gpxutil::info("created program %u (idx %d)", program[i], i);
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
			transformParam.size[0] = (GLfloat)width;
			transformParam.size[1] = (GLfloat)height;
			transformParam.size[2] = 1.0f/transformParam.size[0];
			transformParam.size[3] = 1.0f/transformParam.size[1];
			break;
		case UBO_LINE:
			size = sizeof(ubo::lineParam);
			ptr = &lineParam;
			memcpy(lineParam.colorBase, colorBase, 4*sizeof(GLfloat));
			memcpy(lineParam.colorGradient, colorGradient, 4*4*sizeof(GLfloat));
			lineParam.distCoeff[0] = 1.0f;
			lineParam.distCoeff[1] = 0.0f;
			lineParam.distCoeff[2] = 1.0f;
			lineParam.distCoeff[3] = 0.0f;
			// TODO XXXXXXXXXX
			lineParam.lineWidths[0] = 0.05f;
			lineParam.lineWidths[1] = 0.02f;
			lineParam.lineWidths[2] = 0.02f;
			lineParam.lineWidths[3] = 0.02f;
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
	for (int i=0; i<SSBO_COUNT; i++) {
		if (ssbo[i]) {
			gpxutil::info("destroying buffer %u (SSBO %d)", ssbo[i], i);
			glDeleteBuffers(1, &ssbo[i]);
			ssbo[i] = 0;
		}
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
	for (int i=0; i<PROG_COUNT; i++) {
		if (program[i]) {
			gpxutil::info("destroying program %u (idx %d)", program[i], i);
			glDeleteProgram(program[i]);
			program[i] = 0;
		}
	}
	width = 0;
	height = 0;
}

void CVis::SetPolygon(const std::vector<GLfloat>& vertices2D)
{
	if (ssbo[SSBO_LINE]) {
		gpxutil::info("destroying buffer %u (SSBO %d line)", ssbo[SSBO_LINE], (int)SSBO_LINE);
		glDeleteBuffers(1, &ssbo[SSBO_LINE]);
		ssbo[SSBO_LINE] = 0;
	}
	glGenBuffers(1, &ssbo[SSBO_LINE]);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[SSBO_LINE]);
	glBufferStorage(GL_SHADER_STORAGE_BUFFER, sizeof(GLfloat) * vertices2D.size(), vertices2D.data(), 0);
	bufferVertexCount = vertexCount = vertices2D.size() / 2;
	gpxutil::info("created buffer %u (SSBO %d line) for %u vertices", ssbo[SSBO_LINE], (int)SSBO_LINE, (unsigned)bufferVertexCount);
}

void CVis::DrawTrack(float upTo)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_TRACK]);
	glViewport(0,0,width,height);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindVertexArray(vaoEmpty);

	if (vertexCount > 0) {
		size_t cnt;
		bool drawPoint;
		if (upTo < 0.0f) {
			upTo = (float)vertexCount;
			cnt = vertexCount-1;
			drawPoint = false;
		} else {
			cnt = (size_t)(upTo+1);
			if (cnt >= vertexCount) {
				cnt = vertexCount-1;
			}
			drawPoint = true;
		}

		glUseProgram(program[PROG_LINE_TRACK]);

		glBlendEquation(GL_MAX);
		glBlendFunc(GL_ONE, GL_ONE);
		glEnable(GL_BLEND);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[SSBO_LINE]);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[UBO_TRANSFORM]);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE]);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex[FB_NEIGHBORHOOD]);
		glUniform1f(1, upTo);
		glDrawArrays(GL_TRIANGLES, 0, 18*cnt);

		if (drawPoint) {
			glUseProgram(program[PROG_POINT_TRACK]);
			glUniform1f(1, upTo);
			//glBlendEquation(GL_FUNC_ADD);
			//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glUseProgram(program[PROG_FULLSCREEN_BLEND]);
	GLuint texs[2] = {tex[FB_BACKGROUND], tex[FB_TRACK]};
	glBindTextures(1,2,texs);
	glDisable(GL_BLEND);
	glUniform1f(2, 1.0f);
	glDrawArrays(GL_TRIANGLES, 0, 3);


}

void CVis::DrawSimple()
{
	glUseProgram(program[PROG_LINE_SIMPLE]);
	glBindVertexArray(vaoEmpty);

	glDisable(GL_BLEND);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[SSBO_LINE]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[UBO_TRANSFORM]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE]);
	glDrawArrays(GL_LINE_STRIP, 0, vertexCount);
}

void CVis::DrawNeighborhood()
{
	glUseProgram(program[PROG_LINE_NEIGHBORHOOD]);
	glBindVertexArray(vaoEmpty);

	glBlendEquation(GL_MAX);
	glBlendFunc(GL_ONE, GL_ONE);
	glEnable(GL_BLEND);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[SSBO_LINE]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[UBO_TRANSFORM]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE]);
	glDrawArrays(GL_TRIANGLES, 0, 18*(vertexCount-1));
}

void CVis::AddToBackground()
{
	glViewport(0,0,width,height);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND]);
	DrawSimple();

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_NEIGHBORHOOD]);
	DrawNeighborhood();

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
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
	vis.AddToBackground();

	vertices.clear();
	tracks[1].GetVertices(false, offset, scale, vertices);
	vis.SetPolygon(vertices);

	/*
	vertices.clear();
	vertices.push_back(0.0f);
	vertices.push_back(0.0f);
	vertices.push_back(0.75f);
	vertices.push_back(0.5f);
	vertices.push_back(1.0f);
	vertices.push_back(1.0f);
	vis.SetPolygon(vertices);
	*/

	return true;
}

void CAnimController::DropGL()
{
	vis.DropGL();
}

void CAnimController::Draw()
{
	static float xxx = 0.0f;
	vis.DrawTrack(xxx);
	xxx+= 1.5f;
}

} // namespace gpxvis

