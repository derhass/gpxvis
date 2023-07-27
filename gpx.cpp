#include "gpx.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace gpx {

CTrack::CTrack()
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

bool CTrack::Load(const char *filename)
{
	FILE *file = fopen(filename, "rt");
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
	fseeko(file, 0, SEEK_SET);
	source[fread(source, 1, size, file)] = 0;
	fclose(file);

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
		pos = end - source;
		source[pos++] = 0;
		const char *lat=strstr(start, "lat=");
		const char *lon=strstr(start, "lon=");
                const char *ele=strstr(start, "<ele>");
		const char *time=strstr(start, "<time>");

		if (lat && lon) {
			TPoint pt;
			pt.x = getDbl(lon);
			pt.y = getDbl(lat);
			pt.h = getDbl(ele);

			aabb.Add(pt.x,pt.y,pt.h);

			size_t cnt = points.size();
			if (cnt > 0) {
				double dx = pt.x - points[cnt-1].x;
				double dy = pt.y - points[cnt-1].y;
				double len = sqrt(dx*dx + dy*dy);
				pt.len = len;
				totalLen += len;
			}

			points.push_back(pt);
		} else {
			gpxutil::warn("gpx file '%s': invalid trkpt occured", filename);
		}
	}
	free(source);
	const double *a = aabb.Get();
	gpxutil::info("gpx file '%s': %llu points, total len: %f, aabb: (%f %f %f) - (%f %f %f)",
			filename, (unsigned long long)GetCount(), totalLen,
			a[0], a[1], a[2], a[3], a[4], a[5], a[6]);


	return true;
}

void CTrack::Reset()
{
	points.clear();
	aabb.Reset();
	totalLen = 0.0;
}

void CTrack::GetVertices(bool withZ, const double *origin, const double *scale, std::vector<GLfloat>& data) const
{
	for (size_t i=0; i<points.size(); i++) {
		data.push_back((points[i].x - origin[0])*scale[0]);
		data.push_back((points[i].y - origin[1])*scale[1]);
		if (withZ) {
			data.push_back((points[i].h - origin[2])*scale[2]);
		}
	}
}

} // namespace gpx

