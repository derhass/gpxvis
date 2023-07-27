#ifndef GPXVIS_VIS_H
#define GPXVIS_VIS_H

#include <glad/gl.h>

namespace gpxvis {

class CVis {
	public:
		CVis();
		~CVis();

		CVis(const CVis& other) = delete;
		CVis(CVis&& other) = delete;
		CVis& operator=(const CVis& other) = delete;
		CVis& operator=(CVis&& other) = delete;

		bool InitializeGL();
		void DropGL();

		void Draw();

	private:
		GLuint vaoEmpty;
		GLuint ssboLine;
		GLuint programLine;
};

} // namespace gpxvis

#endif // GPXVIS_VIS_H
