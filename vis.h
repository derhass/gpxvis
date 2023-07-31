#ifndef GPXVIS_VIS_H
#define GPXVIS_VIS_H

#include <glad/gl.h>

#include "gpx.h"
#include "img.h"

#include <vector>

namespace gpxvis {

/****************************************************************************
 * VISUALIZE A SINGLE POLYGON, MIX IT WITH THE HISTORY                      *
 ****************************************************************************/

class CVis {
	public:
		CVis();
		~CVis();

		CVis(const CVis& other) = delete;
		CVis(CVis&& other) = delete;
		CVis& operator=(const CVis& other) = delete;
		CVis& operator=(CVis&& other) = delete;

		bool InitializeGL(GLsizei w, GLsizei h, float dataAspectRatio);
		void DropGL();

		void SetPolygon(const std::vector<GLfloat>& vertices2D);

		void DrawTrack(float upTo);
		void DrawSimple();
		void DrawNeighborhood();

		void AddToBackground();
		void MixTrackAndBackground(float factor);

		void Clear();

		GLsizei GetWidth() const {return width;}
		GLsizei GetHeight() const {return height;}
		GLuint  GetImageFBO() const {return fbo[FB_FINAL];}
		bool	GetImage(gpximg::CImg& img) const;

	private:
		typedef enum {
			SSBO_LINE,
			SSBO_COUNT // end marker
		} TSSBO;

		typedef enum {
			FB_BACKGROUND,
			FB_NEIGHBORHOOD,
			FB_TRACK,
			FB_FINAL,
			FB_COUNT // end marker
		} TFramebuffer;

		typedef enum {
			UBO_TRANSFORM,
			UBO_LINE,
			UBO_COUNT // end marker
		} TUBO;

		typedef enum {
			PROG_LINE_SIMPLE,
			PROG_LINE_TRACK,
			PROG_LINE_NEIGHBORHOOD,
			PROG_POINT_TRACK,
			PROG_FULLSCREEN_BLEND,
			PROG_COUNT // end marker
		} TProgram;

		size_t bufferVertexCount;
		size_t vertexCount;
		GLsizei width;
		GLsizei height;
		float   dataAspect;

		GLfloat colorBackground[4];
		GLfloat colorBase[4];
		GLfloat colorGradient[4][4];
		GLfloat trackWidth;
		GLfloat trackPointWidth;
		GLfloat neighborhoodWidth;

		GLuint vaoEmpty;
		GLuint texTrackDepth;
		GLuint ssbo[SSBO_COUNT];
		GLuint fbo[FB_COUNT];
		GLuint tex[FB_COUNT];
		GLuint ubo[UBO_COUNT];
		GLuint program[PROG_COUNT];

		bool InitializeUBO(int i);
};

/****************************************************************************
 * MANAGE ANIMATIONS AND MULTIPLE TRACKS                                    *
 ****************************************************************************/

class CAnimController {
	public:
		CAnimController();

		bool AddTrack(const char *filename);
		bool Prepare(GLsizei width, GLsizei height);
		void DropGL();

		bool UpdateStep(double timeDelta); // return true if cycle is finished

		const CVis& GetVis() const {return vis;}
		CVis& GetVis() {return vis;}

		unsigned long GetFrame() const {return curFrame;}
		bool IsPrepared() const {return prepared;}

		void SetAnimSpeed(double s) {animDeltaPerFrame = s;}
		void SetTrackSpeec(double s) {trackSpeed = s;}
		void SetFadoutTime(double s) {fadeoutTime = s;}
		void Play() {paused = false;}
		void Pause() {paused = true;}

		size_t GetTrackCount() const {return tracks.size();}
		size_t GetCurrentTrackIndex() const {return curTrack;}
		const gpx::CTrack& GetCurrentTrack() const {return tracks[curTrack];}
		void ChangeTrack(int delta);


	private:
		typedef enum {
			PHASE_INIT,
			PHASE_TRACK,
			PHASE_FADEOUT_INIT,
			PHASE_FADEOUT,
			PHASE_SWITCH_TRACK,
		} TPhase;

		double	      animDeltaPerFrame; // negative is a factor for dynamic scale with render time, postive is fixed increment 
		double        trackSpeed;        // 1.0 is realtime
		double        fadeoutTime;	 // seconds
		bool          paused;

		size_t        curTrack;
		unsigned long curFrame;
		double        curTime;
		TPhase        curPhase;
		bool          prepared;

		double        animationTime;
		double        phaseEntryTime;

		double        offset[3];
		double        scale[3];

		CVis  vis;
		gpxutil::CAABB aabb;
		std::vector<gpx::CTrack> tracks;

		void   UpdateTrack(size_t idx);

		double GetAnimationTime(double deltaTime) const;
		float  GetTrackAnimation(TPhase& nextPhase);
		float  GetFadeoutAnimation(TPhase& nextPhase);
};

} // namespace gpxvis

#endif // GPXVIS_VIS_H
