#ifndef GPXVIS_VIS_H
#define GPXVIS_VIS_H

#include <glad/gl.h>

#include "gpx.h"

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

		bool InitializeGL(GLsizei w, GLsizei h);
		void DropGL();

		void SetPolygon(const std::vector<GLfloat>& vertices2D);

		void DrawTrack(size_t upTo, bool withFinish);
		void DrawSimple();
		void DrawNeighborhood();

		void AddToBackground();

	private:
		typedef enum {
			SSBO_LINE,
			SSBO_FINISH,
			SSBO_COUNT // end marker
		} TSSBO;

		typedef enum {
			FB_BACKGROUND,
			FB_NEIGHBORHOOD,
			FB_TRACK,
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
			PROG_FULLSCREEN_BLEND,
			PROG_COUNT // end marker
		} TProgram;

		size_t bufferVertexCount;
		size_t vertexCount;
		GLsizei width;
		GLsizei height;

		GLfloat colorBackground[4];
		GLfloat colorBase[4];
		GLfloat colorGradient[3][4];
		GLfloat neighborhoodWidth;
		GLfloat trackWidth;

		GLuint vaoEmpty;
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

		void Draw();


	private:
		size_t curTrack;

		CVis  vis;
		gpxutil::CAABB aabb;
		std::vector<gpx::CTrack> tracks;

};

} // namespace gpxvis

#endif // GPXVIS_VIS_H
