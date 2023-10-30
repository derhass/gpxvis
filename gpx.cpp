#include "gpx.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

namespace gpx {

// mercator projection with km as units, for a point on the equator
// we apply a latitude-based scale factor when calculating the individual lenghts
static const double mercatorScaleX = 40075.0167; // equator length in km
static const double mercatorScaleY = 39940.65274158; // meridian length accrdoing to WGS84 flattening factor

extern void projectMercator(double lon, double lat, double& x, double&y)
{
	lon = lon * M_PI / 180.0;
	lat = lat * M_PI / 180.0;
	x = (mercatorScaleX * (lon + M_PI)) / (2.0 * M_PI);
	y = (mercatorScaleY * (M_PI + log(tan(M_PI/4.0 + lat * 0.5)))) / (2.0 * M_PI);
}

extern void unprojectMercator(double x, double y, double& lon, double& lat)
{
	lon = ((x * 2.0 * M_PI) / mercatorScaleX) - M_PI;
	lat = 2.0 * (atan(exp(((y * 2.0 * M_PI) / mercatorScaleY) - M_PI)) - M_PI/4.0);
	lon = lon * 180.0 / M_PI;
	lat = lat * 180.0 / M_PI;
}

extern double getProjectionScale(double lat)
{
	return cos(lat * M_PI / 180.0);
}

CTrack::CTrack() :
	internalID(0)
{
	Reset();
}

static double getDbl(const char *str)
{
	if (!str) {
		return 0.0;
	}
	while(!isdigit(*str) && *str!='-' && *str!='+') {
		if (!*str) {
			return 0.0;
		}
		str++;
	}
	return strtod(str, NULL);
}

static int getInt(const char *str, const char*& next)
{
	if (!str) {
		next = NULL;
	}
	while(!isdigit(*str)) {
		if (!*str) {
			next = str;
			return 0;
		}
		str++;
	}
	char *n=NULL;
	unsigned long int val = strtoul(str, &n, 10);
	next = n;
	return (int)val;
}

static time_t getTime(const char *str)
{
	time_t val = (time_t)0;
	if (str) {
		struct tm ts = { 0 };
		ts.tm_year = getInt(str, str) - 1900;
		ts.tm_mon = getInt(str, str)-1;
		ts.tm_mday = getInt(str, str);
		ts.tm_hour = getInt(str, str);
		ts.tm_min = getInt(str, str);
		ts.tm_sec = getInt(str, str);
		val = mktime(&ts);
		if (val == (time_t)-1) {
			val = (time_t)0;
		}
	}
	return val;
}

bool CTrack::Load(const char *filename)
{
#ifdef WIN32
	std::wstring filename_wide = gpxutil::utf8ToWide(std::string(filename));
	FILE *file = NULL;
	_wfopen_s(&file, filename_wide.c_str(), L"rt");
#else
	FILE *file = fopen(filename, "rt");
#endif
	if(!file) {
		gpxutil::warn("gpx file '%s' can't be opened", filename);
		return false;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	if (size < 4 || size > 100*1024*1024) {
		fclose(file);
		gpxutil::warn("gpx file '%s' has invalid file size: %ld", filename, size);
		return false;
	}
	char *source = (char*)malloc(size+1);
	if (!source) {
		fclose(file);
		gpxutil::warn("gpx file '%s': out of memory when importing", filename);
		return false;
	}
	fseek(file, 0, SEEK_SET);
	source[fread(source, 1, size, file)] = 0;
	fclose(file);

	Reset();
	long pos = 0;
	while (pos < size) {
		const char *start = strstr(source+pos, "<trkpt");
		if (!start) {
			break;
		}
		const char *end = strstr(start, "</trkpt>");
		if (!end) {
			break;
		}
		pos = (long)(end - source);
		source[pos++] = 0;
		const char *lat=strstr(start, "lat=");
		const char *lon=strstr(start, "lon=");
                const char *ele=strstr(start, "<ele>");
		const char *time=strstr(start, "<time>");

		if (lat && lon) {
			TPoint pt;
			pt.lon = getDbl(lon);
			pt.lat = getDbl(lat);
			projectMercator(pt.lon,pt.lat,pt.x,pt.y);
			pt.h = getDbl(ele);
			pt.timestamp = getTime(time);

			aabb.Add(pt.x,pt.y,pt.h);
			aabbLonLat.Add(pt.lon, pt.lat, pt.h); 

			pt.len = 0.0;
			pt.duration = 0.0;
			pt.posOnTrack = 0.0;
			pt.timeOnTrack = 0.0;
			points.push_back(pt);
		} else {
			gpxutil::warn("gpx file '%s': invalid trkpt occured", filename);
		}
	}
	free(source);

	if (points.size() < 2) {
		gpxutil::warn("gpx file '%s': contains no track, only %u points found", filename, (unsigned)points.size());
		return false;
	}

	double geoCenter[3];
	projectionScale = 0.0;
	if (aabbLonLat.GetCenter(geoCenter)) {
		projectionScale = getProjectionScale(geoCenter[1]);
	}

	for (size_t i=1;  i < points.size(); i++) {
		TPoint& A = points[i-1];
		TPoint& B = points[i];
		double dx = B.x - A.x;
		double dy = B.y - A.y;
		// estimate scale for each line segment separately
		double pScale = getProjectionScale(0.5 * A.lat + 0.5 * B.lat);
		A.len = sqrt(dx*dx + dy*dy) * pScale;
		projectionScale += pScale;
		totalLen += A.len;
		B.posOnTrack = totalLen;

		double dur = (double)difftime(B.timestamp, A.timestamp);
		if (dur < 0.0) {
			gpxutil::warn("gpx file '%s': time warp deteced at point %llu", filename, (unsigned long long)i);
			B.timestamp = A.timestamp;
			dur = 0.0;
		}
		A.duration = dur;
		totalDuration += dur;
		B.timeOnTrack = totalDuration;
	}
	projectionScale /= (double)points.size(); // average projection scale

	const double *a = aabb.Get();
	gpxutil::info("gpx file '%s': %llu points, total len: %f, duration: %f, aabb: (%f %f %f) - (%f %f %f), projection scale: %f",
			filename, (unsigned long long)GetCount(), totalLen, totalDuration,
			a[0], a[1], a[2], a[3], a[4], a[5], projectionScale);
	fullFilename = filename;
	if (points.size() > 0) {
		struct tm *tm;
		char buf[64];
		tm = localtime(&points[0].timestamp);
		mysnprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
		buf[sizeof(buf)-1] = 0;
		info = buf;
		gpxutil::durationToString(totalDuration, buf, sizeof(buf));
		durationStr = buf;
	}

	return true;
}

void CTrack::Reset()
{
	points.clear();
	aabb.Reset();
	aabbLonLat.Reset();
	totalLen = 0.0;
	totalDuration = 0.0;
	projectionScale = 1.0;
	fullFilename.clear();
	info = "(empty track)";
	durationStr.clear();
}

void CTrack::GetVertices(bool withZ, const double *origin, const double *scale, std::vector<GLfloat>& data) const
{
	for (size_t i=0; i<points.size(); i++) {
		data.push_back((float)((points[i].x - origin[0])*scale[0]));
		data.push_back((float)((points[i].y - origin[1])*scale[1]));
		if (withZ) {
			data.push_back((float)((points[i].h - origin[2])*scale[2]));
		}
	}
}

float CTrack::GetPointByIndex(double idx) const
{
	size_t cnt = GetCount();
	double maxIdx = (double)cnt - 1.0;

	if (cnt < 2) {
		return 0.0f;
	}

	if (idx <= 0.0) {
		return 0.0f;
	}
	if (idx >= maxIdx) {
		return (float)maxIdx;
	}
	return (float)idx;

}

float CTrack::GetPointByDistance(double distance) const
{
	size_t cnt = GetCount();

	if (cnt < 2) {
		return 0.0f;
	}

	if (distance <= 0.0) {
		return 0.0f;
	}
	if (distance >= totalLen) {
		return (float)(cnt-1);
	}

	size_t window[2] = {0, cnt-1};
	//gpxutil::info("searching: %f",distance);
	while (window[0]+1  < window[1]) {
		size_t center = window[0] + (window[1] - window[0])/2;
		//gpxutil::info("XXX %u %u %u %f %f %f",(unsigned)window[0],(unsigned)window[1],(unsigned)center,points[window[0]].posOnTrack,points[window[1]].posOnTrack,points[center].posOnTrack);
		if (points[center].posOnTrack < distance) {
			window[0] = center;
		} else {
			window[1] = center;
		}
	}
	//gpxutil::info("XXX %u %u %f %f",(unsigned)window[0],(unsigned)window[1],points[window[0]].posOnTrack,points[window[1]].posOnTrack);

	if (points[window[0]].posOnTrack > distance || points[window[1]].posOnTrack < distance) {
		return (float)(cnt-1);
	}
	assert(points[window[0]].posOnTrack <= distance);
	assert(points[window[1]].posOnTrack >= distance);

	distance -= points[window[0]].posOnTrack;
	float rel;
	if (points[window[0]].len > 0.0) {
		rel = (float)(distance / points[window[0]].len);
		if (rel < 0.0f) {
			rel = 0.0f;
		} else if (rel > 0.999999f) {
			rel = 0.999999f;
		}
	} else {
		rel = 0.0f;
	}
	return (float)window[0] + rel;
}

float CTrack::GetPointByDuration(double duration) const
{
	size_t cnt = GetCount();

	if (cnt < 2) {
		return 0.0f;
	}

	if (duration <= 0.0) {
		return 0.0f;
	}
	if (duration >= totalDuration) {
		return (float)(cnt-1);
	}

	size_t window[2] = {0, cnt-1};
	//gpxutil::info("searching: %f",duration);
	while (window[0]+1  < window[1]) {
		size_t center = window[0] + (window[1] - window[0])/2;
		//gpxutil::info("XXX %u %u %u %f %f %f",(unsigned)window[0],(unsigned)window[1],(unsigned)center,points[window[0]].timeOnTrack,points[window[1]].timeOnTrack,points[center].timeOnTrack);
		if (points[center].timeOnTrack < duration) {
			window[0] = center;
		} else {
			window[1] = center;
		}
	}
	//gpxutil::info("XXX %u %u %f %f",(unsigned)window[0],(unsigned)window[1],points[window[0]].timeOnTrack,points[window[1]].timeOnTrack);

	if (points[window[0]].timeOnTrack > duration || points[window[1]].timeOnTrack < duration) {
		return (float)(cnt-1);
	}
	assert(points[window[0]].timeOnTrack <= duration);
	assert(points[window[1]].timeOnTrack >= duration);

	duration -= points[window[0]].timeOnTrack;
	float rel;
	if (points[window[0]].duration > 0.0) {
		rel = (float)(duration / points[window[0]].duration);
		if (rel < 0.0f) {
			rel = 0.0f;
		} else if (rel > 0.999999f) {
			rel = 0.999999f;
		}
	} else {
		rel = 0.0f;
	}
	return (float)window[0] + rel;
}

double CTrack::GetDistanceAt(float animPos) const
{
	if (points.size() < 2) {
		return 0.0;
	}
	size_t ptIdx = (size_t)animPos;
	float  rel = animPos - floorf(animPos);
	if (animPos < 0.0 || ptIdx >= points.size()) {
		return GetLength();
	}
	return points[ptIdx].posOnTrack + rel * points[ptIdx].len;
}

double CTrack::GetDurationAt(float animPos) const
{
	if (points.size() < 2) {
		return 0.0;
	}
	size_t ptIdx = (size_t)animPos;
	float  rel = animPos - floorf(animPos);
	if (animPos < 0.0 || ptIdx >= points.size()) {
		return GetDuration();
	}
	return points[ptIdx].timeOnTrack + rel * points[ptIdx].duration;
	if (animPos < 0.0) {
		return GetDuration();
	}
}

time_t CTrack::GetStartTimestamp() const
{
	if (points.size() > 0) {
		return points[0].timestamp;
	}
	return (time_t)0;
}

bool EarlierThan(const CTrack& a, const CTrack& b)
{
	return (a.GetStartTimestamp() < b.GetStartTimestamp());
}

bool EarlierFilenameThan(const CTrack& a, const CTrack& b)
{
	return (a.GetFilenameStr() < b.GetFilenameStr());
}

bool ShorterDurationThan(const CTrack& a, const CTrack& b)
{
	return (a.GetDuration() < b.GetDuration());
}

bool ShorterDistanceThan(const CTrack& a, const CTrack& b)
{
	return (a.GetLength() < b.GetLength());
}

bool IsEqual(const TPoint& a, const TPoint& b)
{
	return (a.lon == b.lon && a.lat == b.lat && a.timestamp == b.timestamp);
}

bool IsEqual(const CTrack& a, const CTrack& b)
{
	if (a.points.size() != b.points.size()) {
		return false;
	}
	for (size_t i=0; i<a.points.size(); i++) {
		if (!IsEqual(a.points[i], b.points[i])) {
			return false;
		}
	}
	return true;
}

} // namespace gpx

