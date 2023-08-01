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

CVis::TConfig::TConfig()
{
	Reset();
}

void CVis::TConfig::Reset()
{
	ResetColors();
	ResetWidths();
}

void CVis::TConfig::ResetColors()
{
	colorBackground[0] = 0.0f;
	colorBackground[1] = 0.0f;
	colorBackground[2] = 0.0f;
	colorBackground[3] = 0.0f;

	colorBase[0] = 0.6f;
	colorBase[1] = 0.6f;
	colorBase[2] = 0.6f;
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
}

void CVis::TConfig::ResetWidths()
{
	trackWidth = 5.0f;
	trackPointWidth = 10.0f;
	neighborhoodWidth = 3.0f;
}

CVis::CVis() :
	bufferVertexCount(0),
	vertexCount(0),
	width(0),
	height(0),
	dataAspect(0),
	vaoEmpty(0),
	texTrackDepth(0)
{
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
			GLenum format = (i == FB_NEIGHBORHOOD)? GL_R8:GL_RGBA8;
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

	if (ubo[i]) {
		gpxutil::info("destroying buffer %u (UBO idx %d)", ubo[i], i);
		glDeleteBuffers(1, &ubo[i]);
		ubo[i] = 0;
	}

	glGenBuffers(1, &ubo[i]);
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
				tscale[0] = 1.0/dataAspect;
				tscale[1] = 1.0f;
			}
			if (screenAspect > dataAspect) {
				tscale[0] *= dataAspect / screenAspect;
			} else {
				tscale[1] *= screenAspect / dataAspect;
			}
			gpxutil::info("render aspect ratios %f %f, correction %f %f",
				screenAspect, dataAspect, tscale[0], tscale[1]);
			transformParam.scale_offset[0] = 2.0f * tscale[0];
			transformParam.scale_offset[1] = 2.0f * tscale[1];
			transformParam.scale_offset[2] =-1.0f * tscale[0];
			transformParam.scale_offset[3] =-1.0f * tscale[1];
			transformParam.size[0] = (GLfloat)width;
			transformParam.size[1] = (GLfloat)height;
			transformParam.size[2] = 1.0f/transformParam.size[0];
			transformParam.size[3] = 1.0f/transformParam.size[1];
			break;
		case UBO_LINE:
			size = sizeof(ubo::lineParam);
			ptr = &lineParam;
			memcpy(lineParam.colorBase, cfg.colorBase, 4*sizeof(GLfloat));
			memcpy(lineParam.colorGradient, cfg.colorGradient, 4*4*sizeof(GLfloat));
			lineParam.distCoeff[0] = 1.0f;
			lineParam.distCoeff[1] = 0.0f;
			lineParam.distCoeff[2] = 1.0f;
			lineParam.distCoeff[3] = 0.0f;
			screenSize = (width < height)?(float)width:(float)height;
			lineParam.lineWidths[0] = (float)cfg.neighborhoodWidth / screenSize;
			lineParam.lineWidths[1] = (float)cfg.trackWidth / screenSize;
			lineParam.lineWidths[2] = (float)cfg.trackPointWidth / screenSize;
			lineParam.lineWidths[3] = (float)cfg.trackPointWidth / screenSize;
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

void CVis::DrawTrack(float upTo)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_TRACK]);
	glViewport(0,0,width,height);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo[UBO_LINE]);

		glBindTextures(0, 1, &tex[FB_NEIGHBORHOOD]);
		glUniform1f(1, upTo);
		glDrawArrays(GL_TRIANGLES, 0, 18*cnt);
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
}

void CVis::AddLineToBackground()
{
	glViewport(0,0,width,height);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND]);
	DrawSimple();
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
	GLuint texs[2] = {tex[FB_BACKGROUND], tex[FB_TRACK]};
	glBindTextures(1,2,texs);
	glDisable(GL_BLEND);
	glUniform1f(2, factor);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void CVis::Clear()
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[FB_BACKGROUND]);
	glClearColor(cfg.colorBackground[0], cfg.colorBackground[1], cfg.colorBackground[2], cfg.colorBackground[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	for (int i=0; i<FB_COUNT; i++) {
		if (i != FB_BACKGROUND) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
			glClear(GL_COLOR_BUFFER_BIT);
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
	glGetTextureImage(tex[FB_FINAL],0,GL_RGB,GL_UNSIGNED_BYTE, img.GetSize(), img.GetData());
	return true;
}

void CVis::UpdateConfig()
{
	InitializeUBO(UBO_LINE);
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
	ResetSpeeds();
	ResetAtCycle();
	paused = false;
}

void CAnimController::TAnimConfig::ResetSpeeds()
{
	animDeltaPerFrame = -1.0;
	trackSpeed = 3.0 * 3600.0;
	fadeoutTime = 0.5;
}

void CAnimController::TAnimConfig::ResetAtCycle()
{
	pauseAtCycle = true;
	clearAtCycle = false;
}

CAnimController::CAnimController() :
	curTrack(0),
	curFrame(0),
	curTime(0.0),
	curPhase(PHASE_INIT),
	prepared(false),
	animationTime(0.0)
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
	prepared = false;
	return true;
}

bool CAnimController::Prepare(GLsizei width, GLsizei height)
{
	if (tracks.size() < 1) {
		gpxutil::warn("anim controller without tracks");
		return false;
	}

	double totalLen = 0.0;
	double totalDur = 0.0;
	for(size_t i=0; i<tracks.size(); i++) {
		totalLen += tracks[i].GetLength();
		totalDur += tracks[i].GetDuration();
	}
	gpxutil::info("have %llu tracks, total lenght: %f, total duration: %f",
		(unsigned long long)tracks.size(), totalLen, totalDur);

	gpxutil::CAABB screenAABB = aabb;
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
	if (screenAspect > dataAspect) {
		realWidth = (GLsizei)(width * (dataAspect/screenAspect) + 0.5);
	} else {
		realHeight = (GLsizei)(height * (screenAspect/dataAspect) + 0.5);
	}
	realWidth = gpxutil::roundNextMultiple(realWidth, 8);
	realHeight = gpxutil::roundNextMultiple(realHeight, 8);
	gpxutil::info("adjusted rendering resolution from %ux%u (%f) to %ux%u (%f) to match data aspect %f",
		(unsigned)width, (unsigned)height, screenAspect,
		(unsigned)realWidth, (unsigned)realHeight, (double)realWidth/(double)realHeight, dataAspect);

	if (!vis.InitializeGL(realWidth, realHeight, (float)dataAspect)) {
		return false;
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

void CAnimController::RestoreHistoryUpTo(size_t idx)
{
	size_t cnt = tracks.size();
	vis.Clear();

	if (cnt > 0) {
		if (idx > cnt) {
			idx = cnt;
		}
		for (size_t i=0; i<idx; i++) {
			UpdateTrack(i);
			vis.AddToBackground();
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
	if (!prepared) {
		return false;
	}
	bool cycleFinished = false;
	/*
	vis.DrawTrack(-1.0f);
	vis.MixTrackAndBackground(1.0f);

	return;
	*/
	curFrame++;
	curTime += timeDelta;

	animationTimeDelta = GetAnimationTimeDelta(timeDelta);
	animationTime += animationTimeDelta;

	TPhase nextPhase = curPhase;

	switch(curPhase) {
		case PHASE_INIT:
			vis.DrawTrack(0.0f);
			vis.MixTrackAndBackground(1.0f);
			nextPhase = PHASE_TRACK;
			curTrackPos = 0.0;
			curTrackUpTo = 0.0f;
			curFadeRatio = 0.0f;
			break;
		case PHASE_TRACK:
			curTrackUpTo = GetTrackAnimation(nextPhase);
			vis.DrawTrack(GetTrackAnimation(nextPhase));
			vis.MixTrackAndBackground(1.0f - curFadeRatio);
			break;
		case PHASE_FADEOUT_INIT:
			vis.DrawTrack(-1.0f);
			vis.AddLineToBackground();
			vis.MixTrackAndBackground(1.0f);
			nextPhase = PHASE_FADEOUT;
			curFadeTime = curFadeRatio * animCfg.fadeoutTime;
			curTrackUpTo = -1.0f;
			break;
		case PHASE_FADEOUT:
			vis.MixTrackAndBackground(GetFadeoutAnimation(nextPhase));
			break;
		case PHASE_SWITCH_TRACK:
			vis.AddLineToNeighborhood();
			if (++curTrack >= tracks.size()) {
				cycleFinished = true;
				animationTime = 0.0;
				if (animCfg.pauseAtCycle) {
					animCfg.paused = true;
				}
				curTrack = 0;
				if (animCfg.clearAtCycle) {
					vis.Clear();
				}
			}
			curFadeRatio = 0.0f;
			curFadeTime = 0.0;
			UpdateTrack(curTrack);
			nextPhase = PHASE_INIT;
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

void CAnimController::SwitchToTrack(size_t idx)
{
	if (tracks.size() < 1) {
		return;
	}

	if (idx >= tracks.size()) {
		idx = tracks.size() -1;
	}
	curTrack = idx;
	UpdateTrack(curTrack);
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

void CAnimController::RefreshCurrentTrack()
{
	if (curPhase != PHASE_TRACK) {
		vis.DrawTrack(curTrackUpTo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}
}

} // namespace gpxvis

