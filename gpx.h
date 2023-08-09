#ifndef GPXVIS_GPX_H
#define GPXVIS_GPX_H

#include <glad/gl.h>

#include "util.h"

#include <string>
#include <vector>
#include <time.h>

namespace gpx {

struct TPoint {
	double lon;
	double lat;
	double x;
	double y;
	double h;
	double len;
	double duration;
	double posOnTrack;
	double timeOnTrack;
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
		double GetDuration() const {return totalDuration;}

		float  GetPointByIndex(double idx) const;
		float  GetPointByDistance(double distance) const;
		float  GetPointByDuration(double duration) const;

		double GetDistanceAt(float animPos) const;
		double GetDurationAt(float animPos) const;

		const char* GetFilename() const {return fullFilename.c_str();}
		const char* GetInfo() const {return info.c_str();}

	private:
		std::vector<TPoint> points;
		gpxutil::CAABB      aabb;
		gpxutil::CAABB      aabbLonLat;
		double              totalLen;
		double              totalDuration;
		double              projectionScale;
		std::string fullFilename;
		std::string info;
};

} // namespace gpx

#endif // GPXVIS_GPX_H
