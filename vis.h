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

		void Draw();

	private:
		typedef enum {
			FB_FIRST = 0,
			FB_BACKGROUND = FB_FIRST,
			FB_NEIGHBORHOOD,

			FB_COUNT
		} TFramebuffer;
		size_t bufferVertexCount;
		size_t vertexCount;
		GLsizei width;
		GLsizei height;

		GLuint vaoEmpty;
		GLuint ssboLine;
		GLuint programLine;
		GLuint fbo[FB_COUNT];
		GLuint tex[FB_COUNT];
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
