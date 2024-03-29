#include "vis.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
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
	GLfloat zoomShift[4];
};

struct lineParam {
	GLfloat colorBase[4];
	GLfloat colorGradient[4][4];
	GLfloat distCoeff[4];
	GLfloat distExp[4];
	GLfloat lineWidths[4];
};

} // namespace ubo


/****************************************************************************
 * VISUALIZE A SINGLE POLYGON, MIX IT WITH THE HISTORY                      *
 ****************************************************************************/

CVis::TConfig::TConfig()
{
	Reset();
}

void CVis::TConfig::Reset()
{
	ResetColors();
	ResetWidths();
	ResetTransform();
}

void CVis::TConfig::ResetColors()
{
	colorBackground[0] = 0.0f;
	colorBackground[1] = 0.0f;
	colorBackground[2] = 0.0f;
	colorBackground[3] = 0.0f;

	colorBase[0] = 0.35f;
	colorBase[1] = 0.35f;
	colorBase[2] = 0.35f;
	colorBase[3] = 1.0f;

	colorHistoryAdd[0] = 0.85f;
	colorHistoryAdd[1] = 0.85f;
	colorHistoryAdd[2] = 0.85f;
	colorHistoryAdd[3] = 0.0f;

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
}

void CVis::TConfig::ResetWidths()
{
	trackWidth = 5.0f;
	trackExp = 1.0f;
	trackPointWidth = 10.0f;
	trackPointExp = 1.5f;
	historyWidth = 1.2f;
	historyExp = 1.0f;
	neighborhoodWidth = 3.0f;
	neighborhoodExp = 1.0f;
	historyWideLine = true;
	historyAdditive = BACKGROUND_ADD_GRADIENT;
	historyAddExp = 1.0f;
	historyAddSaturationOffset = 50.0f;
}

void CVis::TConfig::ResetTransform()
{
	zoomFactor = 1.0f;
	centerNormalized[0] = 0.5f;
	centerNormalized[1] = 0.5f;
}

void CVis::TConfig::ClampTransform()
{
	if (zoomFactor < 1.0e-6f) {
		zoomFactor = 1.0e-6f;
	} else if (zoomFactor > 1.0e6f) {
		zoomFactor = 1.0e6f;
	}
	if (centerNormalized[0] < 0.0f) {
		centerNormalized[0] = 0.0f;
	} else if (centerNormalized[0] > 1.0f) {
		centerNormalized[0] = 1.0f;
	}
	if (centerNormalized[1] < 0.0f) {
		centerNormalized[1] = 0.0f;
	} else if (centerNormalized[1] > 1.0f) {
		centerNormalized[1] = 1.0f;
	}
}

CVis::CVis() :
	bufferVertexCount(0),
	vertexCount(0),
	width(0),
	height(0),
	dataAspect(1.0f),
	vaoEmpty(0),
	texTrackDepth(0)
{
	scaleOffset[0] = 2.0f;
	scaleOffset[1] = 2.0f;
	scaleOffset[2] =-1.0f;
	scaleOffset[3] =-1.0f;

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

GLenum CVis::GetFramebufferTextureFormat(TFramebuffer fb) const
{
	GLenum format;

	switch(fb) {
		case FB_BACKGROUND:
		case FB_BACKGROUND_SCRATCH:
			format = GL_R32F;
			break;
		case FB_NEIGHBORHOOD:
			format = GL_R8;
			break;
		default:
			format = GL_RGBA8;
	}
	return format;
}

bool CVis::InitializeGL(GLsizei w, GLsizei h, float dataAspectRatio)
{
	dataAspect = dataAspectRatio;

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
			GLenum format = GetFramebufferTextureFormat((TFramebuffer)i);
			glGenTextures(1, &tex[i]);
			glBindTexture(GL_TEXTURE_2D, tex[i]);
			glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);
			gpxutil::info("created texture %u %ux%u fmt 0x%x (frambeuffer idx %d color attachment)", tex[i], (unsigned)w, (unsigned)h, (unsigned)format, i);
		}
		if (i == FB_TRACK) {
			if (!texTrackDepth) {
				GLenum format = GL_DEPTH_COMPONENT32F;
				glGenTextures(1, &texTrackDepth);
				glBindTexture(GL_TEXTURE_2D, texTrackDepth);
				glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glBindTexture(GL_TEXTURE_2D, 0);
				gpxutil::info("created texture %u %ux%u fmt 0x%x (frambeuffer idx %d depth attachment)", texTrackDepth, (unsigned)w, (unsigned)h, (unsigned)format, i);
			}
		}

		if (!fbo[i]) {
			glGenFramebuffers(1, &fbo[i]);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[i], 0);
			if (i == FB_TRACK) {
				glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texTrackDepth, 0);
			}
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

	static const char* programs[PROG_COUNT][2] = {
		{ "shaders/simple.vs", "shaders/simple.fs" },
		{ "shaders/track.vs", "shaders/track.fs"},
		{ "shaders/line.vs", "shaders/line.fs"},
		{ "shaders/point.vs", "shaders/point.fs"},
		{ "shaders/fullscreen.vs", "shaders/tex.fs"},
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
	Clear();
	return true;
}

bool CVis::InitializeUBO(int i)
{
	GLsizeiptr size=0;
	void *ptr = NULL;
	bool created = false;

	if (!ubo[i]) {
		created = true;
		glGenBuffers(1, &ubo[i]);
	}

	glBindBuffer(GL_UNIFORM_BUFFER, ubo[i]);

	ubo::transformParam transformParam;
	ubo::lineParam lineParam;

	float screenAspect;
	float tscale[2];
	float screenSize;

	switch(i) {
		case UBO_TRANSFORM:
			size = sizeof(ubo::transformParam);
			ptr = &transformParam;
			screenAspect = (float)width / (float)height;
			if (dataAspect > 1.0f) {
				tscale[0] = 1.0f;
				tscale[1] = dataAspect;
			} else {
				tscale[0] = 1.0f/dataAspect;
				tscale[1] = 1.0f;
			}
			if (screenAspect > dataAspect) {
				tscale[0] *= dataAspect / screenAspect;
			} else {
				tscale[1] *= screenAspect / dataAspect;
			}
			gpxutil::info("render aspect ratios %f %f, correction %f %f",
				screenAspect, dataAspect, tscale[0], tscale[1]);
			/*
			tscale[0]=1.0;
			tscale[1]=1.0;
			*/
			scaleOffset[0] = 2.0f * tscale[0];
			scaleOffset[1] = 2.0f * tscale[1];
			scaleOffset[2] =-1.0f * tscale[0];
			scaleOffset[3] =-1.0f * tscale[1];
			transformParam.scale_offset[0] = scaleOffset[0];
			transformParam.scale_offset[1] = scaleOffset[1];
			transformParam.scale_offset[2] = scaleOffset[2];
			transformParam.scale_offset[3] = scaleOffset[3];
			transformParam.size[0] = (GLfloat)width;
			transformParam.size[1] = (GLfloat)height;
			transformParam.size[2] = 1.0f/transformParam.size[0];
			transformParam.size[3] = 1.0f/transformParam.size[1];
			GetZoomShift(transformParam.zoomShift);
			break;
		case UBO_LINE_TRACK:
		case UBO_LINE_HISTORY:
		case UBO_LINE_HISTORY_FINAL:
		case UBO_LINE_NEIGHBORHOOD:
			size = sizeof(ubo::lineParam);
			ptr = &lineParam;
			if ((i == UBO_LINE_NEIGHBORHOOD) || (i == UBO_LINE_HISTORY)) {
				lineParam.colorBase[0] = 1.0f;
				lineParam.colorBase[1] = 1.0f;
				lineParam.colorBase[2] = 1.0f;
				lineParam.colorBase[3] = 1.0f;
			} else {
				memcpy(lineParam.colorBase, cfg.colorBase, 4*sizeof(GLfloat));
			}
			if (i == UBO_LINE_HISTORY_FINAL) {
				memcpy(&lineParam.colorGradient[0], cfg.colorBackground, 4*sizeof(GLfloat));
				memcpy(&lineParam.colorGradient[1], cfg.colorBase, 4*sizeof(GLfloat));
				if (cfg.historyAdditive >= BACKGROUND_ADD_MIXED_COLORS) {
					memcpy(&lineParam.colorGradient[2], cfg.colorHistoryAdd, 4*sizeof(GLfloat));
					memcpy(&lineParam.colorGradient[3], cfg.colorHistoryAdd, 4*sizeof(GLfloat));
				} else {
					memcpy(&lineParam.colorGradient[2], cfg.colorBase, 4*sizeof(GLfloat));
					memcpy(&lineParam.colorGradient[3], cfg.colorBase, 4*sizeof(GLfloat));
				}
				if (cfg.historyAdditive == BACKGROUND_ADD_GRADIENT) {
					lineParam.distCoeff[0] = 0.0f;
					lineParam.distCoeff[1] = 1.0f;
				} else {
					lineParam.distCoeff[0] = 1.0f;
					lineParam.distCoeff[1] = 0.0f;
				}
				lineParam.distCoeff[2] = 0.0f;
				lineParam.distCoeff[3] = 0.0f;
			} else {
				memcpy(lineParam.colorGradient, cfg.colorGradient, 4*4*sizeof(GLfloat));
				lineParam.distCoeff[0] = 1.0f;
				lineParam.distCoeff[1] = 0.0f;
				lineParam.distCoeff[2] = 1.0f;
				lineParam.distCoeff[3] = 0.0f;
			}
			if (i == UBO_LINE_HISTORY_FINAL) {
				if (cfg.historyAdditive > BACKGROUND_ADD_NONE) {
					lineParam.distExp[0] = cfg.historyAddExp;
				} else {
					lineParam.distExp[0] = 1.0f;
				}
				lineParam.distExp[1] = 1.0f / cfg.historyAddSaturationOffset;
			} else if (i == UBO_LINE_HISTORY) {
				lineParam.distExp[0] = cfg.historyExp;
				lineParam.distExp[1] = cfg.trackPointExp;
			} else if (i == UBO_LINE_NEIGHBORHOOD) {
				lineParam.distExp[0] = cfg.neighborhoodExp;
				lineParam.distExp[1] = cfg.trackPointExp;
			} else {
				lineParam.distExp[0] = cfg.trackExp;
				lineParam.distExp[1] = cfg.trackPointExp;
			}
			lineParam.distExp[2] = 1.0f;
			lineParam.distExp[3] = 1.0f;
			screenSize = (width < height)?(float)width:(float)height;
			if ((i == UBO_LINE_HISTORY) || (i == UBO_LINE_HISTORY_FINAL)) {
				lineParam.lineWidths[0] = (float)cfg.historyWidth / screenSize;
			} else {
				lineParam.lineWidths[0] = (float)cfg.neighborhoodWidth / screenSize;
			}
			lineParam.lineWidths[1] = (float)cfg.trackWidth / screenSize;
			lineParam.lineWidths[2] = (float)cfg.trackPointWidth / screenSize;
			lineParam.lineWidths[3] = (float)cfg.trackPointWidth / screenSize;
			break;
		default:
			gpxutil::warn("invalid UBO idx %d", i);
			return false;
	}
	if (created) {
		glBufferStorage(GL_UNIFORM_BUFFER, size, ptr, GL_DYNAMIC_STORAGE_BIT);
		gpxutil::info("created buffer %u (UBO idx %d) size %u", ubo[i], i, (unsigned)size);
	} else {
		glBufferSubData(GL_UNIFORM_BUFFER, 0, size, ptr);
		gpxutil::info("updated buffer %u (UBO idx %d) size %u", ubo[i], i, (unsigned)size);
	}
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
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
		if (i == FB_TRACK) {
			if (texTrackDepth) {
				gpxutil::info("destroying texture %u (frambeuffer idx %d depth attachment)", texTrackDepth, i);
				glDeleteTextures(1, &texTrackDepth);
				texTrackDepth = 0;
			}
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

void CVis::DrawTrackInternal(float upTo)
{
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

		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[SSBO_LINE]);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[UBO_TRANSFORM]);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE_TRACK]);

		glBindTextures(0, 1, &tex[FB_NEIGHBORHOOD]);
		glUniform1f(1, upTo);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(18*cnt));
		glDisable(GL_DEPTH_TEST);

		if (drawPoint) {
			glBlendEquation(GL_MAX);
			glBlendFunc(GL_ONE, GL_ONE);
			glEnable(GL_BLEND);
			glUseProgram(program[PROG_POINT_TRACK]);
			glUniform1f(1, upTo);
			//glBlendEquation(GL_FUNC_ADD);
			//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
	}
}

void CVis::DrawTrack(float upTo, bool clear)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_TRACK]);
	glViewport(0,0,width,height);
	if (clear) {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	DrawTrackInternal(upTo);
}

void CVis::DrawTrack(float upTo)
{
	DrawTrack(upTo, true);
}

void CVis::DrawHistory()
{
	glBindVertexArray(vaoEmpty);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[SSBO_LINE]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[UBO_TRANSFORM]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE_HISTORY]);

	if (cfg.historyWideLine) {
		glUseProgram(program[PROG_LINE_NEIGHBORHOOD]);
		glBlendEquation(GL_MAX);
		glBlendFunc(GL_ONE, GL_ONE);
		glEnable(GL_BLEND);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(18*(vertexCount-1)));
	} else {
		glUseProgram(program[PROG_LINE_SIMPLE]);

		if (cfg.historyAdditive > BACKGROUND_ADD_NONE) {
			glBlendEquation(GL_FUNC_ADD);
			glBlendFunc(GL_ONE, GL_ONE);
			glEnable(GL_BLEND);
		} else {
			glDisable(GL_BLEND);
		}

		glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)vertexCount);
	}
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
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE_NEIGHBORHOOD]);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(18*(vertexCount-1)));
}

void CVis::AddHistory()
{
	if (cfg.historyWideLine && (cfg.historyAdditive > BACKGROUND_ADD_NONE)) {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND_SCRATCH]);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		DrawHistory();
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND]);
		glUseProgram(program[PROG_FULLSCREEN_TEX]);
		glBindTextures(3, 1, &tex[FB_BACKGROUND_SCRATCH]);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		glEnable(GL_BLEND);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	} else {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND]);
		DrawHistory();
	}
}

void CVis::AddToBackground()
{
	glViewport(0,0,width,height);		
	AddHistory();
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_NEIGHBORHOOD]);
	DrawNeighborhood();
}

void CVis::AddLineToBackground()
{
	glViewport(0,0,width,height);
	AddHistory();
}

void CVis::AddLineToNeighborhood()
{
	glViewport(0,0,width,height);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_NEIGHBORHOOD]);
	DrawNeighborhood();
}

void CVis::MixTrackAndBackground(float factor)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_FINAL]);
	glViewport(0,0,width,height);
	glBindVertexArray(vaoEmpty);
	glUseProgram(program[PROG_FULLSCREEN_BLEND]);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE_HISTORY_FINAL]);
	GLuint texs[2] = {tex[FB_BACKGROUND], tex[FB_TRACK]};
	glBindTextures(1,2,texs);
	glDisable(GL_BLEND);
	glUniform1f(2, factor);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void CVis::ClearHistory()
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND]);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClearColor(cfg.colorBackground[0], cfg.colorBackground[1], cfg.colorBackground[2], cfg.colorBackground[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

void CVis::ClearNeighborHood()
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_NEIGHBORHOOD]);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void CVis::Clear()
{
	ClearHistory();
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	for (int i=0; i<FB_COUNT; i++) {
		if (i != FB_BACKGROUND) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
			if (i == FB_TRACK) { 
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			} else {
				glClear(GL_COLOR_BUFFER_BIT);
			}
		}
	}
}

bool CVis::GetImage(gpximg::CImg& img) const
{
	if (!tex[FB_FINAL]) {
		gpxutil::warn("no image available");
		return false;
	}
	if (!img.Allocate((int)width,(int)height,3)) {
		return false;
	}
	glGetTextureImage(tex[FB_FINAL],0,GL_RGB,GL_UNSIGNED_BYTE, (GLsizei)img.GetSize(), img.GetData());
	return true;
}

void CVis::UpdateConfig()
{
	InitializeUBO(UBO_LINE_TRACK);
	InitializeUBO(UBO_LINE_HISTORY);
	InitializeUBO(UBO_LINE_HISTORY_FINAL);
	InitializeUBO(UBO_LINE_NEIGHBORHOOD);
}

void CVis::UpdateTransform()
{
	cfg.ClampTransform();
	InitializeUBO(UBO_TRANSFORM);
}

void CVis::GetZoomShift(GLfloat zoomShift[4]) const
{
	zoomShift[0] = cfg.zoomFactor;
	zoomShift[1] = cfg.zoomFactor;
	zoomShift[2] = 0.5f - cfg.zoomFactor * cfg.centerNormalized[0];
	zoomShift[3] = 0.5f - cfg.zoomFactor * cfg.centerNormalized[1];
}

void CVis::TransformToPos(const GLfloat posNormalized[2], GLfloat pos[2]) const
{
	GLfloat zoomShift[4];
	GetZoomShift(zoomShift);
	pos[0] = ((2.0f * posNormalized[0] - 1.0f) - scaleOffset[2]) / scaleOffset[0];
	pos[1] = ((2.0f * posNormalized[1] - 1.0f) - scaleOffset[3]) / scaleOffset[1];
	pos[0] = (pos[0] - zoomShift[2]) / zoomShift[0];
	pos[1] = (pos[1] - zoomShift[3]) / zoomShift[1];
}

void CVis::TransformFromPos(const GLfloat pos[2], GLfloat posNormalized[2]) const
{
	GLfloat zoomShift[4];
	GetZoomShift(zoomShift);
	posNormalized[0] = pos[0] * zoomShift[0] + zoomShift[2];
	posNormalized[1] = pos[1] * zoomShift[1] + zoomShift[3];
	posNormalized[0] = posNormalized[0] * scaleOffset[0] + scaleOffset[2];
	posNormalized[1] = posNormalized[1] * scaleOffset[1] + scaleOffset[3];
	posNormalized[0] = 0.5f * posNormalized[0] + 0.5f;
	posNormalized[1] = 0.5f * posNormalized[1] + 0.5f;
}

/****************************************************************************
 * MANAGE ANIMATIONS AND MULTIPLE TRACKS                                    *
 ****************************************************************************/

CAnimController::TAnimConfig::TAnimConfig()
{
	Reset();
}

void CAnimController::TAnimConfig::Reset()
{
	mode = ANIM_MODE_TRACK;
	accuMode = ACCU_MONTH;
	accuCount = 1;
	accuWeekDayStart = 3; /* wednesday */
	ResetSpeeds();
	ResetAtCycle();
	ResetModes();
	ResetResolutionSettings();
	paused = false;
}

void CAnimController::TAnimConfig::ResetSpeeds()
{
	animDeltaPerFrame = -1.0;
	trackSpeed = 3.0 * 3600.0;
	fadeoutTime = 0.5;
	fadeinTime = 0.5;
	endTime = 3.0;
}

void CAnimController::TAnimConfig::ResetAtCycle()
{
	pauseAtCycle = true;
	clearAtCycle = false;
}

void CAnimController::TAnimConfig::ResetModes()
{
	historyMode = BACKGROUND_UPTO;
	neighborhoodMode = BACKGROUND_UPTO;
}

void CAnimController::TAnimConfig::ResetResolutionSettings()
{
	adjustToAspect = true;
	resolutionGranularity = 8;
}

void CAnimController::TAnimConfig::PresetSpeedsSlow()
{
	animDeltaPerFrame = -1.0;
	trackSpeed = 0.08 * 3600.0;
	fadeoutTime = 3.0;
	endTime = 3.0;
}

CAnimController::CAnimController() :
	curTrack(0),
	curFrame(0),
	curTime(0.0),
	curPhase(PHASE_INIT),
	prepared(false),
	newCycle(true),
	animEndReached(false),
	animationTime(0.0),
	allTrackLength(0.0),
	allTrackDuration(0.0)
{
	avgStart[0] = avgStart[1] = avgStart[2] = 0.0;
	frameInfoBuffer[0]=0;
	frameInfoBuffer[sizeof(frameInfoBuffer)-1]=0;
	accuInfoBuffer[0]=0;
	accuInfoBuffer[sizeof(accuInfoBuffer)-1]=0;
}

bool CAnimController::AddTrack(const char *filename)
{
	size_t idx = tracks.size();
	tracks.emplace_back();
	if (!tracks[idx].Load(filename)) {
		tracks.resize(idx);
		return false;
	}
	tracks[idx].SetInternalID(trackIDManager.GenerateID());
	prepared = false;
	return true;
}

bool CAnimController::Prepare(GLsizei width, GLsizei height)
{
	prepared = false;

	aabb.Reset();
	screenAABB.Reset();
	avgStart[0] = avgStart[1] = avgStart[2] = 0.0;
	if (tracks.size() < 1) {
		gpxutil::warn("anim controller without tracks");
		return false;
	}

	double totalLen = 0.0;
	double totalDur = 0.0;
	for(size_t i=0; i<tracks.size(); i++) {
		aabb.MergeWith(tracks[i].GetAABB());
		totalLen += tracks[i].GetLength();
		totalDur += tracks[i].GetDuration();
		const std::vector<gpx::TPoint>& points = tracks[i].GetPoints();
		if (points.size() > 0) {
			avgStart[0] += points[0].x;
			avgStart[1] += points[0].y;
			avgStart[2] += points[0].h;
		}
	}
	if (tracks.size() > 0) {
		avgStart[0] /= tracks.size();
		avgStart[1] /= tracks.size();
		avgStart[2] /= tracks.size();
	}
	gpxutil::info("have %llu tracks, total lenght: %f, total duration: %f",
		(unsigned long long)tracks.size(), totalLen, totalDur);
	allTrackLength = totalLen;
	allTrackDuration = totalDur;
	char tbuf[64];
	gpxutil::durationToString(totalDur, tbuf, sizeof(tbuf));
	allTrackDurationString = tbuf;

	screenAABB = aabb;
	screenAABB.Enhance(1.05,0.0);
	screenAABB.GetNormalizeScaleOffset(scale, offset);
	double dataAspect = screenAABB.GetAspect();
	if (dataAspect >= 1.0) {
		scale[1] = scale[0];
		offset[1] -= (0.5 - 0.5f / dataAspect) / scale[1];
	} else {
		scale[0] = scale[1];
		offset[0] -= (0.5 - 0.5 * dataAspect) / scale[0];
	}
	double screenAspect = (double)width/(double)height;
	GLsizei realWidth = width;
	GLsizei realHeight = height;
        if (animCfg.adjustToAspect) {
		if (screenAspect > dataAspect) {
			realWidth = (GLsizei)(width * (dataAspect/screenAspect) + 0.5);
		} else {
			realHeight = (GLsizei)(height * (screenAspect/dataAspect) + 0.5);
		}
		gpxutil::info("adjusted rendering resolution from %ux%u (%f) to %ux%u (%f) to match data aspect %f",
			(unsigned)width, (unsigned)height, screenAspect,
			(unsigned)realWidth, (unsigned)realHeight, (double)realWidth/(double)realHeight, dataAspect);
	}
	if (animCfg.resolutionGranularity > 1) {
		realWidth = gpxutil::roundNextMultiple(realWidth, animCfg.resolutionGranularity);
		realHeight = gpxutil::roundNextMultiple(realHeight, animCfg.resolutionGranularity);
		gpxutil::info("adjusted rendering resolution from %ux%u (%f) to %ux%u (%f) to match granularity %d, data aspect: %f",
			(unsigned)width, (unsigned)height, screenAspect,
			(unsigned)realWidth, (unsigned)realHeight, (double)realWidth/(double)realHeight,
			(int)animCfg.resolutionGranularity, dataAspect);
	}
	if (!vis.InitializeGL(realWidth, realHeight, (float)dataAspect)) {
		return false;
	}

	if (curTrack >= tracks.size()) {
		curTrack = tracks.size()-1;
	}
	UpdateTrack(curTrack);

	/*
	std::vector<GLfloat> vertices;
	gpxutil::CAABB xxx;
	for(size_t j=0; j<tracks.size(); j++) {
		vertices.clear();
		tracks[j].GetVertices(false, offset, scale, vertices);
		for (size_t k=0; k<vertices.size()/2; k++) {
			xxx.Add(vertices[k*2],vertices[k*2+1], 0.0);
		}
	}
	const double *q = xxx.Get();
	gpxutil::info("XXXX %f %f %f %f %f %f",q[0],q[1],q[2],q[3],q[4],q[5]);
	*/

	/*
	std::vector<GLfloat> vertices;
	tracks[0].GetVertices(false, offset, scale, vertices);
	vis.SetPolygon(vertices);
	vis.AddToBackground();

	vertices.clear();
	tracks[1].GetVertices(false, offset, scale, vertices);
	vis.SetPolygon(vertices);
	*/

	/*
	for (double j=0.0; j<=1.0; j+=(1.0/1024.0)) {
		gpxutil::info("XXX %f %f",j, tracks[0].GetPointByDistance(j * tracks[0].GetLength()));
	}
	*/

	/*
	std::vector<GLfloat> vertices;
	tracks[0].GetVertices(false, offset, scale, vertices);
	vis.SetPolygon(vertices);
	vis.AddToBackground();
	vertices.clear();
	vertices.push_back(0.0f);
	vertices.push_back(0.0f);
	vertices.push_back(0.75f);
	vertices.push_back(0.5f);
	vertices.push_back(1.0f);
	vertices.push_back(1.0f);
	vertices.push_back(0.05f);
	vertices.push_back(0.95f);
	vertices.push_back(0.95f);
	vertices.push_back(0.05f);
	vis.SetPolygon(vertices);
	*/
	prepared = true;
	return true;
}

void CAnimController::UpdateTrack(size_t idx)
{
	std::vector<GLfloat> vertices;
	tracks[idx].GetVertices(false, offset, scale, vertices);
	vis.SetPolygon(vertices);
}

void CAnimController::RestoreHistoryUpTo(size_t idx, bool history, bool neighborhood)
{
	size_t cnt = tracks.size();
	vis.Clear();

	if (cnt > 0) {
		if (idx > cnt) {
			idx = cnt;
		}
		for (size_t i=0; i<idx; i++) {
			UpdateTrack(i);
			if (history && neighborhood) {
				vis.AddToBackground();
			} else if (history) {
				vis.AddLineToBackground();
			} else if (neighborhood) {
				vis.AddLineToNeighborhood();
			}
		}
		UpdateTrack(curTrack);
	}
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void CAnimController::RestoreHistory(bool history, bool neighborhood)
{
	size_t cnt = tracks.size();
	size_t idx = curTrack;
	vis.Clear();

	if (cnt > 0) {
		if (idx > cnt) {
			idx = cnt;
		}
		if (history && neighborhood && (animCfg.historyMode == animCfg.neighborhoodMode) ) {
			switch (animCfg.historyMode) {
				case BACKGROUND_UPTO:
					for (size_t i=0; i<idx; i++) {
						UpdateTrack(i);
						vis.AddToBackground();
					}
					break;
				case BACKGROUND_ALL:
					for (size_t i=0; i<tracks.size(); i++) {
						UpdateTrack(i);
						vis.AddToBackground();
					}
					break;
				case BACKGROUND_CURRENT:
					UpdateTrack(curTrack);
					vis.AddToBackground();
					break;
				case BACKGROUND_NONE:
				default:
					(void)0;
			}
		} else {
		       	if (history) {
				switch (animCfg.historyMode) {
					case BACKGROUND_UPTO:
						for (size_t i=0; i<idx; i++) {
							UpdateTrack(i);
							vis.AddLineToBackground();
						}
						break;
					case BACKGROUND_ALL:
						for (size_t i=0; i<tracks.size(); i++) {
							UpdateTrack(i);
							vis.AddLineToBackground();
						}
						break;
					case BACKGROUND_CURRENT:
						UpdateTrack(curTrack);
						vis.AddLineToBackground();
						break;
					case BACKGROUND_NONE:
					default:
						(void)0;
				}
			}
			if (neighborhood) {
				switch (animCfg.neighborhoodMode) {
					case BACKGROUND_UPTO:
						for (size_t i=0; i<idx; i++) {
							UpdateTrack(i);
							vis.AddLineToNeighborhood();
						}
						break;
					case BACKGROUND_ALL:
						for (size_t i=0; i<tracks.size(); i++) {
							UpdateTrack(i);
							vis.AddLineToNeighborhood();
						}
						break;
					case BACKGROUND_CURRENT:
						UpdateTrack(curTrack);
						vis.AddLineToNeighborhood();
						break;
					case BACKGROUND_NONE:
					default:
						(void)0;
				}
			}
		}
		UpdateTrack(curTrack);
	}
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void CAnimController::DropGL()
{
	vis.DropGL();
}

bool CAnimController::UpdateStep(double timeDelta)
{
	bool cycleFinished;

	if (!prepared) {
		return false;
	}
	/*
	vis.DrawTrack(-1.0f);
	vis.MixTrackAndBackground(1.0f);

	return;
	*/
	if (!animCfg.paused) {
		curFrame++;
		curTime += timeDelta;
	}

	animationTimeDelta = GetAnimationTimeDelta(timeDelta);
	animationTime += animationTimeDelta;

	switch(animCfg.mode) {
		case ANIM_MODE_TRACK:
			cycleFinished = UpdateStepModeTrack();
			break;
		case ANIM_MODE_TRACK_ACCU:
			cycleFinished = UpdateStepModeTrackAccu();
			break;
		case ANIM_MODE_HISTORY:
			cycleFinished = UpdateStepModeHistory();
			break;
		default:
			cycleFinished = true;
	}
	return cycleFinished;
}

bool CAnimController::UpdateStepModeTrack()
{
	bool cycleFinished = false;

	TPhase nextPhase = curPhase;
	switch(curPhase) {
		case PHASE_INIT:
			if (newCycle) {
				animationTime = 0.0;
				curFrame = 0;
				newCycle = false;
				RestoreHistory();
			}
			if (animCfg.historyMode == BACKGROUND_CURRENT) {
				vis.ClearHistory();
				vis.AddLineToBackground();
			}
			if (animCfg.neighborhoodMode == BACKGROUND_CURRENT) {
				vis.ClearNeighborHood();
				vis.AddLineToNeighborhood();
			}
			vis.DrawTrack(0.0f);
			vis.MixTrackAndBackground(1.0f);
			nextPhase = PHASE_TRACK;
			curTrackPos = 0.0;
			curTrackUpTo = 0.0f;
			curFadeRatio = 0.0f;
			break;
		case PHASE_TRACK:
			curTrackUpTo = GetTrackAnimation(nextPhase);
			vis.DrawTrack(curTrackUpTo);
			vis.MixTrackAndBackground(1.0f - curFadeRatio);
			break;
		case PHASE_FADEOUT_INIT:
			vis.DrawTrack(-1.0f);
			if (animCfg.historyMode == BACKGROUND_UPTO) {
				vis.AddLineToBackground();
			}
			vis.MixTrackAndBackground(1.0f);
			nextPhase = PHASE_FADEOUT;
			curFadeTime = curFadeRatio * animCfg.fadeoutTime;
			curTrackUpTo = -1.0f;
			break;
		case PHASE_FADEOUT:
			vis.MixTrackAndBackground(GetFadeoutAnimation(nextPhase));
			break;
		case PHASE_SWITCH_TRACK:
			if (animCfg.neighborhoodMode == BACKGROUND_UPTO) {
				vis.AddLineToNeighborhood();
			}
			vis.MixTrackAndBackground(0.0f);
			if (++curTrack >= tracks.size()) {
				if (tracks.size() > 0) {
					curTrack = tracks.size() - 1;
				} else {
					curTrack = 0;
				}
				nextPhase = PHASE_END;
			} else {
				curFadeRatio = 0.0f;
				curFadeTime = 0.0;
				UpdateTrack(curTrack);
				nextPhase = PHASE_INIT;
			}
			break;
		case PHASE_END:
			if (animationTime >= phaseEntryTime + animCfg.endTime) {
				nextPhase = PHASE_CYCLE;
				if (animCfg.pauseAtCycle) {
					animCfg.paused = true;
				}
				if (animCfg.clearAtCycle) {
					vis.Clear();
				}
			}
			vis.MixTrackAndBackground(1.0f - curFadeRatio);
			break;
		case PHASE_CYCLE:
			if (!animCfg.paused) {
				curTrack = 0;
				newCycle = true;
				animationTime = 0.0;
				nextPhase = PHASE_INIT;
				curFadeRatio = 0.0f;
				curFadeTime = 0.0;
				UpdateTrack(curTrack);
			}
			vis.DrawTrack(curTrackUpTo);
			vis.MixTrackAndBackground(1.0f - curFadeRatio);
			cycleFinished = true;
			break;
		default:
			nextPhase = PHASE_INIT;
			gpxutil::warn("anim ctrl: invalid phase %d", (int)curPhase);
	}
	if (nextPhase != curPhase) {
		curPhase = nextPhase;
		phaseEntryTime = animationTime;
	}
	return cycleFinished;
}

bool CAnimController::UpdateStepModeTrackAccu()
{
	bool cycleFinished = false;

	if (newCycle) {
		vis.Clear();
		curFrame = 0;
		curPhase = PHASE_CYCLE;
		animEndReached = false;
		newCycle = false;
	}

	TPhase nextPhase = curPhase;
	switch(curPhase) {
		case PHASE_CYCLE:
			// first image keeps empty
			accuInfoBuffer[0] = 0;
			SwitchToTrackInternal(0);
			vis.MixTrackAndBackground(0.0f);
			nextPhase = PHASE_SWITCH_TRACK;
			break;
		case PHASE_INIT:
		case PHASE_SWITCH_TRACK:
			// ACCUMULATE
			animEndReached = AccumulateTracks(true);
			if (animCfg.fadeinTime <= 0.0) {
				AccumulateTrackHistory();
				vis.MixTrackAndBackground(1.0f);
				if (animCfg.fadeoutTime > 0.0) {
					curFadeRatio = 0.0f;
					curFadeTime = 0.0;
					nextPhase = PHASE_FADEOUT;
				}
			} else {
				vis.MixTrackAndBackground(0.0f);
				curFadeRatio = 0.0f;
				curFadeTime = 0.0;
				nextPhase = PHASE_TRACK;
			}
			break;
		case PHASE_TRACK:
			// FADE-IN accumulated
			curFadeTime += animationTimeDelta;
			curFadeRatio  = (float)(curFadeTime / animCfg.fadeinTime);
			if (curFadeRatio > 1.0f) {
				curFadeRatio = 1.0f;
			}
			vis.MixTrackAndBackground(curFadeRatio);
			if (curFadeRatio >= 1.0f) {
				AccumulateTrackHistory();
				if (animCfg.fadeoutTime <= 0.0) {
					nextPhase = PHASE_SWITCH_TRACK;
				} else {
					curFadeRatio = 0.0f;
					curFadeTime = 0.0;
					nextPhase = PHASE_FADEOUT;
				}
			}
			break;
		case PHASE_FADEOUT:
			// FADE-OUT accumulated
			curFadeTime += animationTimeDelta;
			curFadeRatio  = (float)(curFadeTime / animCfg.fadeoutTime);
			if (curFadeRatio > 1.0f) {
				curFadeRatio = 1.0f;
			}
			vis.MixTrackAndBackground(1.0f - curFadeRatio);
			if (curFadeRatio >= 1.0f) {
				nextPhase = PHASE_SWITCH_TRACK;
			}
			break;
		case PHASE_END:
			if (animationTime >= phaseEntryTime + animCfg.endTime) {
				nextPhase = PHASE_CYCLE;
				cycleFinished = true;
				if (animCfg.pauseAtCycle) {
					animCfg.paused = true;
				}
				if (animCfg.clearAtCycle) {
					vis.Clear();
				}
			}
			vis.MixTrackAndBackground(1.0f - curFadeRatio);
			break;
		default:
			(void)0;
	}

	if (curPhase != nextPhase) {
		if (nextPhase == PHASE_SWITCH_TRACK) {
			if (animEndReached) {
				nextPhase = PHASE_END;
			}
		}
		curPhase = nextPhase;
		phaseEntryTime = animationTime;
	}

	return cycleFinished;
}
bool CAnimController::UpdateStepModeHistory()
{
	bool cycleFinished = false;

	if (animCfg.paused) {
		return false;
	}

	if (newCycle) {
		vis.Clear();
		curFrame = 0;
		curPhase = PHASE_INIT;
		newCycle = false;
	}

	TPhase nextPhase = curPhase;
	switch(curPhase) {
		case PHASE_INIT:
			// first image keeps empty
			SwitchToTrack(0);
			nextPhase = PHASE_CYCLE;
			break;
		case PHASE_CYCLE:
			vis.AddLineToBackground();
			vis.MixTrackAndBackground(0.0f);
			nextPhase = PHASE_TRACK;
			break;
		case PHASE_TRACK:
			SwitchToTrackInternal(curTrack + 1);
			vis.AddLineToBackground();
			vis.MixTrackAndBackground(0.0f);
			if (curTrack + 1 >= tracks.size()) {
				nextPhase = PHASE_INIT;
				cycleFinished = true;
				if (animCfg.pauseAtCycle) {
					animCfg.paused = true;
				}
				if (animCfg.clearAtCycle) {
					vis.Clear();
				}
			}
			break;
		default:
			(void)0;
	}
	curPhase = nextPhase;

	return cycleFinished;
}

double CAnimController::GetAnimationTimeDelta(double deltaTime) const
{
	if (animCfg.paused) {
		return 0.0;
	}
	if (animCfg.animDeltaPerFrame < 0.0) {
		return (-animCfg.animDeltaPerFrame) * deltaTime;
	} else {
		return animCfg.animDeltaPerFrame;
	}
}

float CAnimController::GetTrackAnimation(TPhase& nextPhase)
{
	curTrackPos += animationTimeDelta * animCfg.trackSpeed;
	if (curTrackPos >= tracks[curTrack].GetDuration()) {
		nextPhase = PHASE_FADEOUT_INIT;
		curTrackPos = tracks[curTrack].GetDuration();
	}
	return tracks[curTrack].GetPointByDuration(curTrackPos);
}

float CAnimController::GetFadeoutAnimation(TPhase& nextPhase)
{
	curFadeTime += animationTimeDelta;
	if (animCfg.fadeoutTime > 0.0) {
		curFadeRatio  = (float)(curFadeTime / animCfg.fadeoutTime);
	} else {
		curFadeRatio = 1.01f;
	}
	if (curFadeRatio > 1.0f) {
		curFadeRatio = 1.0f;
		nextPhase = PHASE_SWITCH_TRACK;
	}
	return (1.0f-curFadeRatio);
}

void CAnimController::ChangeTrack(int delta)
{
	if (tracks.size() < 1 || !delta) {
		return;
	}

	if (delta < 0) {
		size_t off = (size_t) -delta;
		off = off % tracks.size();
		if (off > curTrack) {
			curTrack = (curTrack + tracks.size() - off) % tracks.size();
		} else {
			curTrack -= off;
		}
	} else {
		size_t off = (size_t) delta;
		curTrack = (curTrack + off) % tracks.size();
	}
	UpdateTrack(curTrack);
	curPhase = PHASE_INIT;
}

void CAnimController::SwitchToTrackInternal(size_t idx)
{
	if (tracks.size() < 1) {
		return;
	}

	if (idx >= tracks.size()) {
		idx = tracks.size() -1;
	}
	curTrack = idx;
	UpdateTrack(curTrack);
}

void CAnimController::SwitchToTrack(size_t idx)
{
	SwitchToTrackInternal(idx);
	curPhase = PHASE_INIT;
}

void CAnimController::SetCurrentTrackPos(double v)
{
	animationTimeDelta = 0.0;
	curTrackPos = v;
	TPhase ignored;
	float upTo = GetTrackAnimation(ignored);
	(void)ignored;
	SetCurrentTrackUpTo(upTo);
}

void CAnimController::SetCurrentTrackUpTo(float v)
{
	curTrackUpTo = v;
	RefreshCurrentTrack();
}

void CAnimController::RefreshCurrentTrack(bool needRestoreHistory)
{
	if (curPhase != PHASE_TRACK) {
		vis.DrawTrack(curTrackUpTo);
		if (needRestoreHistory && (curPhase >= PHASE_FADEOUT)) {
			if (animCfg.historyMode == BACKGROUND_UPTO) {
				vis.AddLineToBackground();
			}
		}
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}
}

void CAnimController::ResetAnimation()
{
	newCycle = true;
	SwitchToTrack(0);
	curPhase = PHASE_INIT;
	vis.Clear();
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void CAnimController::ResetFrameCounter()
{
	curFrame=0;
}

bool CAnimController::RestoreCurrentTrack(size_t curId)
{
	bool found = false;
	size_t cnt = tracks.size();
	for (size_t i=0; i<cnt; i++) {
		if (tracks[i].GetIntenalID() == curId) {
			curTrack = i;
			found = true;
			break;
		}
	}
	if (!found) {
		SwitchToTrack(0);
	}
	return found;
}

bool CAnimController::SortTracks(TSortMode sortMode)
{
	size_t cnt = tracks.size();
	if (cnt < 2) {
		return true;
	}
	size_t curId = (curTrack < cnt) ? tracks[curTrack].GetIntenalID() : 0;
	switch (sortMode) {
		case BY_TIME:
			std::sort(tracks.begin(),tracks.end(), gpx::EarlierThan);
			break;
		case BY_LENGTH:
			std::sort(tracks.begin(),tracks.end(), gpx::ShorterDistanceThan);
			break;
		case BY_DURATION:
			std::sort(tracks.begin(),tracks.end(), gpx::ShorterDurationThan);
			break;
		default:
			std::sort(tracks.begin(),tracks.end(), gpx::EarlierFilenameThan);
	}
	return RestoreCurrentTrack(curId);
}

bool CAnimController::ReverseTrackOrder()
{
	size_t cnt = tracks.size();
	if (cnt < 2) {
		return true;
	}
	size_t curId = (curTrack < cnt) ? tracks[curTrack].GetIntenalID() : 0;
	for (size_t i=0; i < (cnt>>1); i++) {
		std::swap(tracks[i], tracks[cnt-1-i]);
	}
	return RestoreCurrentTrack(curId);
}

bool CAnimController::RemoveDuplicateTracks()
{
	const size_t cnt = tracks.size();
	size_t newCnt = 0;

	if (cnt < 2) {
		return true;
	}

	size_t curId = (curTrack < cnt) ? tracks[curTrack].GetIntenalID() : 0;
	for (size_t i=0; i<cnt; i++) {
		const gpx::CTrack &t = tracks[i];
		bool keep = true;
		for (size_t j=0; j<i; j++) {
			if (IsEqual(t, tracks[j])) {
				keep = false;
				gpxutil::warn("'%s' is duplicate of '%s', removed", t.GetInfo(), tracks[j].GetInfo());
				break;
			}
		}
		if (keep) {
			if (newCnt < i) {
				tracks[newCnt] = tracks[i];
			}
			newCnt++;
		} else {
		}	
	}
	if (newCnt < cnt) {
		tracks.resize(newCnt);
	}
	return RestoreCurrentTrack(curId);
}

bool CAnimController::StatsToCSV(const char *filename) const
{
	char buf[4096];
	bool success = true;

	FILE *file = gpxutil::fopen_wrapper(filename, "wt");
	if (!file) {
		gpxutil::warn("failed to open \"%s\" for writing", filename);
		return false;
	}
	gpx::CTrack::GetStatLineHeader(buf, sizeof(buf));
	if (fputs(buf, file) == EOF) {
		success = false;
	}
	const size_t cnt = tracks.size();
	for (size_t i=0; i<cnt; i++) {
		tracks[i].GetStatLine(buf, sizeof(buf));
		if (fputs(buf, file) == EOF) {
			success = false;
		}
	}
	fclose(file);
	if (success) {
		gpxutil::info("wrote stats to \"%s\"", filename);
	} else {
		gpxutil::warn("I/O error writing stats to \"%s\"", filename);
	}
	return success;
}

void CAnimController::TransformToPos(const GLfloat posNormalized[2], double pos[2]) const
{
	pos[0] = ((double)posNormalized[0]) / scale[0] + offset[0];
	pos[1] = ((double)posNormalized[1]) / scale[1] + offset[1];
}

void CAnimController::TransformFromPos(const double pos[2], GLfloat posNormalized[2]) const
{
	posNormalized[0] = (GLfloat)((pos[0] - offset[0])* scale[0]);
	posNormalized[1] = (GLfloat)((pos[1] - offset[1])* scale[1]);
}

static bool CloserThan(const TTrackDist& a, const TTrackDist& b)
{
	return (a.d < b.d);
}

void CAnimController::GetTracksAt(double x, double y, double radius, std::vector<TTrackDist>& indices, TBackgroundMode mode) const
{
	const size_t cnt = tracks.size();
	const double r2 = radius*radius;
	size_t from = 0;
	size_t to = cnt;

	indices.clear();
	if (!prepared && cnt < 1) {
		return;
	}
	switch(mode) {
		case BACKGROUND_NONE:
			from = cnt+1;
			break;
		case BACKGROUND_UPTO:
			to = curTrack + 1;
			break;
		case BACKGROUND_CURRENT:
			from = curTrack;
			to = curTrack+1;
			break;
		default:
			(void)0; // already set up for "all"
	}

	for (size_t i=from; i<to; i++) {
		double d2 = tracks[i].GetDistanceSqrTo(x,y);
		if (d2 <= r2) {
			TTrackDist td;
			td.idx = i;
			td.d = sqrt(d2);
			indices.push_back(td);
		}
	}
	std::sort(indices.begin(),indices.end(), CloserThan);
}

void CAnimController::InitAccumulator(size_t startIdx)
{
	struct tm tA,tB;
	if (startIdx >= GetTrackCount()) {
		accuInfoBuffer[0] = 0;
		accumulateStart = 1;
		accumulateEnd = 0;
		return;
	}

	accumulateStartTime = tracks[startIdx].GetStartTimestamp();
#ifdef WIN32
	localtime_s(&tA, &accumulateStartTime);
#else
	localtime_r(&accumulateStartTime, &tA);
#endif
	tA.tm_isdst = -1;
	tA.tm_hour = 0;
	tA.tm_min = 0;
	tA.tm_sec = 0;
	tB = tA;

	accumulateStart = startIdx;
	accumulateEnd = startIdx;

	switch(animCfg.accuMode) {
		case ACCU_COUNT:
			if (animCfg.accuCount > 1) {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"#%llu - #%llu", (unsigned long long)startIdx+1, (unsigned long long)(startIdx+animCfg.accuCount));
			} else {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"#%llu", (unsigned long long)startIdx+1);
			}
			gpxutil::info("accumulation mode %d: %s", (int)animCfg.accuMode, accuInfoBuffer);
			return;
		case ACCU_DAY:
			tB.tm_mday += (int)animCfg.accuCount;
			break;
		case ACCU_WEEK:
			tA.tm_mday -= (tA.tm_wday - animCfg.accuWeekDayStart);
			if (tA.tm_wday < animCfg.accuWeekDayStart) {
				tA.tm_mday =- 7;
			}
			tB.tm_mday = tA.tm_mday + 7 * (int)animCfg.accuCount;
			break;
		case ACCU_MONTH:
			tA.tm_mday = 1;
			tB.tm_mday = 1;
			tB.tm_mon += (int)animCfg.accuCount;
			break;
		case ACCU_YEAR:
			tA.tm_mday = 1;
			tA.tm_mon = 0;
			tB.tm_mday = 1;
			tB.tm_mon = 0;
			tB.tm_year += (int)animCfg.accuCount;
			break;
		default:
			gpxutil::warn("invalid accuMode!");
			return;
	}

	accumulateStartTime = mktime(&tA);
	accumulateEndTime = mktime(&tB);
	tB.tm_min = -1;
	mktime(&tB);

	switch(animCfg.accuMode) {
		case ACCU_MONTH:
			if (animCfg.accuCount > 1) {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"%d-%02d - %d-%02d",
					tA.tm_year+1900, tA.tm_mon+1,
					tB.tm_year+1900, tB.tm_mon+1);
			} else {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"%d-%02d",
					tA.tm_year+1900, tA.tm_mon+1);
			}
			break;
		case ACCU_YEAR:
			if (animCfg.accuCount > 1) {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"%d - %d",
					tA.tm_year+1900,
					tB.tm_year+1900);
			} else {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"%d",
					tA.tm_year+1900);
			}
			break;
		default:
			if (animCfg.accuCount > 1 || animCfg.accuMode != ACCU_DAY) {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"%d-%02d-%02d - %d-%02d-%02d",
					tA.tm_year+1900, tA.tm_mon+1, tA.tm_mday,
					tB.tm_year+1900, tB.tm_mon+1, tB.tm_mday);


			} else {
				mysnprintf(accuInfoBuffer, sizeof(accuInfoBuffer),
					"%d-%02d-%02d",
					tA.tm_year+1900, tA.tm_mon+1, tA.tm_mday);
			}
			break;
	}
	gpxutil::info("accumulation mode %d: %s (%d-%02d-%02d - %d-%02d-%02d)",
		(int)animCfg.accuMode, accuInfoBuffer,
		tA.tm_year+1900, tA.tm_mon+1, tA.tm_mday,
		tB.tm_year+1900, tB.tm_mon+1, tB.tm_mday);
}

bool CAnimController::ShouldAccumulateTrack(size_t idx)
{
	if (accumulateStart > idx || accumulateStart > accumulateEnd) {
		return false;
	}
	if (idx == accumulateStart) {
		return true;
	}

	if (animCfg.accuMode == ACCU_COUNT) {
		return (idx < accumulateStart + animCfg.accuCount);
	}

	time_t t = tracks[idx].GetStartTimestamp();
	return (t >= accumulateStartTime && t < accumulateEndTime);
}

bool CAnimController::AccumulateTracks(bool clearAccu)
{
	size_t cnt = tracks.size();
	size_t i;

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, vis.fbo[CVis::FB_TRACK]);
	glViewport(0,0,vis.width,vis.height);
	if (clearAccu) {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	if (cnt < 1) {
		return true;
	}

	InitAccumulator(curTrack);

	for (i=accumulateStart; i<cnt; i++) {
		if (!ShouldAccumulateTrack(i)) {
			break;
		}
		SwitchToTrackInternal(i);
		vis.DrawTrackInternal(-1.0f);
		gpxutil::info("  accumulation: selected track %llu: %s", (unsigned long long)(i+1), tracks[i].GetInfo());
	}
	accumulateEnd = i;
	gpxutil::info("accumulation: selected %llu tracks: %llu - %llu",
		(unsigned long long)(accumulateEnd-accumulateStart),
		(unsigned long long)(accumulateStart + 1),
		(unsigned long long)(accumulateEnd));

	if (accumulateEnd < cnt) {
		SwitchToTrackInternal(accumulateEnd);
		return false;
	}

	return true;
}

void CAnimController::AccumulateTrackHistory()
{
	size_t cnt = tracks.size();
	size_t c = curTrack;
	size_t i;

	if (cnt < 1 || accumulateStart >= accumulateEnd || accumulateEnd > cnt) {
		return;
	}

	for (i=accumulateStart; i<accumulateEnd; i++) {
		SwitchToTrackInternal(i);
		vis.AddLineToBackground();
		vis.AddLineToNeighborhood();
	}

	SwitchToTrackInternal(c);
}

const char* CAnimController::GetFrameInfo(TFrameInfoType t)
{
	if (GetTrackCount() < 1) {
		return NULL;
	}
	if (animCfg.mode == ANIM_MODE_TRACK_ACCU) {
		switch(t) {
			case FRAME_INFO_LEFT:
				return accuInfoBuffer;
				break;
			default:
				return NULL;
		}
		
	} else {
		switch(t) {
			case FRAME_INFO_LEFT:
				mysnprintf(frameInfoBuffer, sizeof(frameInfoBuffer),
					"#%llu/%llu", (unsigned long long)GetCurrentTrackIndex()+1, (unsigned long long)GetTrackCount());
				break;
			case FRAME_INFO_RIGHT:
				return GetCurrentTrack().GetInfo();
			default:
				return NULL;
		}
	}
	frameInfoBuffer[sizeof(frameInfoBuffer)-1] = 0;
	return frameInfoBuffer;
}


} // namespace gpxvis

