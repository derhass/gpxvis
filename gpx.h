#ifndef GPXVIS_GPX_H
#define GPXVIS_GPX_H

#include <glad/gl.h>

#include "util.h"

#include <vector>
#include <time.h>

namespace gpx {

struct TPoint {
	double x;
	double y;
	double h;
	double len;
	time_t timestamp;
};

class CTrack {
	public:
		CTrack();

		bool   Load(const char *filename);
		void   Reset();

		size_t GetCount() const  {return points.size();}
		void   GetVertices(bool withZ, const double *origin, const double *scale, std::vector<GLfloat>& data) const;
		const gpxutil::CAABB& GetAABB() const {return aabb;}
		double GetLength() const {return totalLen;}

	private:
		std::vector<TPoint> points;
		gpxutil::CAABB      aabb;
		double              totalLen;
};

} // namespace gpx

#endif // GPXVIS_GPX_H
