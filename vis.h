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
		struct TConfig {
			GLfloat colorBackground[4];
			GLfloat colorBase[4];
			GLfloat colorGradient[4][4];
			GLfloat trackWidth;
			GLfloat trackExp;
			GLfloat trackPointWidth;
			GLfloat trackPointExp;
			GLfloat historyWidth;
			GLfloat historyExp;
			GLfloat neighborhoodWidth;
			GLfloat neighborhoodExp;
			bool historyWideLine;
			bool historyAdditive;

			TConfig();
			void Reset();
			void ResetColors();
			void ResetWidths();
		};

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
		void DrawHistory();
		void DrawNeighborhood();

		void AddToBackground();
		void AddLineToBackground();
		void AddLineToNeighborhood();
		void MixTrackAndBackground(float factor);

		void ClearHistory();
		void ClearNeighborHood();
		void Clear();

		GLsizei GetWidth() const {return width;}
		GLsizei GetHeight() const {return height;}
		GLuint  GetImageFBO() const {return fbo[FB_FINAL];}
		bool	GetImage(gpximg::CImg& img) const;

		TConfig& GetConfig() {return cfg;} // use UpdateConfig after you modified something!
		void UpdateConfig();

		float  GetDataAspect() const {return dataAspect;}

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
			UBO_LINE_TRACK,
			UBO_LINE_HISTORY,
			UBO_LINE_NEIGHBORHOOD,
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

		TConfig cfg;

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
		typedef enum : int {
			BACKGROUND_NONE,
			BACKGROUND_UPTO,
			BACKGROUND_CURRENT,
			BACKGROUND_ALL
		} TBackgroundMode;

		struct TAnimConfig {
			double	      animDeltaPerFrame; // negative is a factor for dynamic scale with render time, postive is fixed increment 
			double        trackSpeed;        // 1.0 is realtime
			double        fadeoutTime;	 // seconds
			bool          paused;
			bool          pauseAtCycle;
			bool          clearAtCycle;
			TBackgroundMode historyMode;
			TBackgroundMode neighborhoodMode;

			TAnimConfig();
			void Reset();
			void ResetSpeeds();
			void ResetAtCycle();
			void ResetModes();
		};

		CAnimController();

		bool AddTrack(const char *filename);
		bool Prepare(GLsizei width, GLsizei height);
		void DropGL();

		bool UpdateStep(double timeDelta); // return true if cycle is finished

		const CVis& GetVis() const {return vis;}
		CVis& GetVis() {return vis;}

		unsigned long GetFrame() const {return curFrame;}
		double GetTime() const {return animationTime;}
		double GetAnimationDelta() const {return animationTimeDelta;}
		bool IsPrepared() const {return prepared;}

		void SetAnimSpeed(double s) {animCfg.animDeltaPerFrame = s;}
		void SetTrackSpeec(double s) {animCfg.trackSpeed = s;}
		void SetFadoutTime(double s) {animCfg.fadeoutTime = s;}
		void Play() {animCfg.paused = false;}
		void Pause() {animCfg.paused = true;}
		TAnimConfig& GetAnimConfig() {return animCfg;}

		size_t GetTrackCount() const {return tracks.size();}
		size_t GetCurrentTrackIndex() const {return curTrack;}
		const gpx::CTrack& GetCurrentTrack() const {return tracks[curTrack];}
		const gpx::CTrack& GetiTrack(size_t idx) const {return tracks[idx];}
		double GetCurrentTrackPos() const {return curTrackPos;}
		float GetCurrentTrackUpTo() const {return curTrackUpTo;}
		void SetCurrentTrackPos(double v);
		void SetCurrentTrackUpTo(float v);
		void RefreshCurrentTrack();
		void ChangeTrack(int delta);
		void SwitchToTrack(size_t idx);
		std::vector<gpx::CTrack>& GetTracks() {return tracks;} // call Prepare after you modified these...

		void RestoreHistory(bool history=true, bool neighborhood=true);
		void RestoreHistoryUpTo(size_t idx, bool history=true, bool neighborhood=true);
		void ResetAnimation();

		float GetCurrentFadeRatio() const {return curFadeRatio;}
		void SetCurrentFadeRatio(float v) {curFadeRatio = v; curFadeTime = curFadeRatio * animCfg.fadeoutTime; }

	private:
		typedef enum {
			PHASE_INIT,
			PHASE_TRACK,
			PHASE_FADEOUT_INIT,
			PHASE_FADEOUT,
			PHASE_SWITCH_TRACK,
		} TPhase;

		TAnimConfig   animCfg;

		size_t        curTrack;
		unsigned long curFrame;
		double        curTime;
		TPhase        curPhase;
		bool          prepared;
		bool          newCycle;

		double        animationTime;
		double        animationTimeDelta;
		double        phaseEntryTime;
		double        curTrackPos;
		float         curTrackUpTo;
		float         curFadeRatio;
		double        curFadeTime;

		double        offset[3];
		double        scale[3];

		CVis  vis;
		gpxutil::CAABB aabb;
		std::vector<gpx::CTrack> tracks;

		void   UpdateTrack(size_t idx);

		double GetAnimationTimeDelta(double deltaTime) const;
		float  GetTrackAnimation(TPhase& nextPhase);
		float  GetFadeoutAnimation(TPhase& nextPhase);
};

} // namespace gpxvis

#endif // GPXVIS_VIS_H
