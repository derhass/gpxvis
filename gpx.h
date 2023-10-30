#ifndef GPXVIS_GPX_H
#define GPXVIS_GPX_H

#include <glad/gl.h>

#include "util.h"

#include <string>
#include <vector>
#include <time.h>

namespace gpx {

extern void projectMercator(double lon, double lat, double& x, double&y);
extern void unprojectMercator(double x, double y, double& lon, double& lat);
extern double getProjectionScale(double lat);

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
		const gpxutil::CAABB& GetAABBLonLat() const {return aabbLonLat;}
		double GetLength() const {return totalLen;}
		double GetDuration() const {return totalDuration;}

		const std::vector<TPoint>& GetPoints() const {return points;}
		float  GetPointByIndex(double idx) const;
		float  GetPointByDistance(double distance) const;
		float  GetPointByDuration(double duration) const;

		double GetDistanceAt(float animPos) const;
		double GetDurationAt(float animPos) const;

		const std::string& GetFilenameStr() const {return fullFilename;}
		const char* GetFilename() const {return fullFilename.c_str();}
		const char* GetInfo() const {return info.c_str();}
		const char* GetDurationString() const {return durationStr.c_str();}

		time_t GetStartTimestamp() const;

		void   SetInternalID(size_t id) {internalID = id;}
		size_t GetIntenalID() const {return internalID;}

	private:
		std::vector<TPoint> points;
		gpxutil::CAABB      aabb;
		gpxutil::CAABB      aabbLonLat;
		double              totalLen;
		double              totalDuration;
		double              projectionScale;
		size_t              internalID;
		std::string fullFilename;
		std::string info;
		std::string durationStr;

		friend bool IsEqual(const CTrack& a, const CTrack& b);
};

bool EarlierThan(const CTrack& a, const CTrack& b);
bool EarlierFilenameThan(const CTrack& a, const CTrack& b);
bool ShorterDurationThan(const CTrack& a, const CTrack& b);
bool ShorterDistanceThan(const CTrack& a, const CTrack& b);
bool IsEqual(const TPoint& a, const TPoint& b);
bool IsEqual(const CTrack& a, const CTrack& b);

} // namespace gpx

#endif // GPXVIS_GPX_H
