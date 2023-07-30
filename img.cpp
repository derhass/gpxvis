#include "img.h"

#include "util.h"

#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gpximg {

CImg::CImg() :
	data(NULL),
	size(0),
	width(0),
	height(0),
	channels(0)
{
}

CImg::~CImg()
{
	Destroy();
}

bool CImg::Allocate(int w, int h, int c)
{
	Destroy();
	if (w <= 0 || h <= 0 || c <= 0) {
		gpxutil::warn("invalid image dims %dx%dx%d",w,h,c);
		return false;
	}
	size = (size_t)w * (size_t)h * (size_t)c;
	data = (unsigned char*)malloc(size);
	if (!data) {
		gpxutil::warn("out of memory for image %dx%dx%d",w,h,c);
		size = 0;
		return false;
	}
	width = w;
	height = h;
	channels = c;
	return true;
}

void CImg::Destroy()
{
	if (data) {
		free(data);
	}
	data = NULL;
	size = 0;
	width = height = channels = 0;
}

bool CImg::WriteTGA(const char *filename) const
{
	if (!data || size < 1) {
		gpxutil::warn("invalid image, can't save");
		return false;
	}
	stbi_flip_vertically_on_write(1);
	int res = stbi_write_tga(filename, width, height, channels, data);
	if (!res) {
		gpxutil::warn("failed to write image '%s'", filename);
	}
	return true;
}

bool CImg::WritePNG(const char *filename) const
{
	if (!data || size < 1) {
		gpxutil::warn("invalid image, can't save");
		return false;
	}
	stbi_flip_vertically_on_write(1);
	int res = stbi_write_png(filename, width, height, channels, data, width*channels);
	if (!res) {
		gpxutil::warn("failed to write image '%s'", filename);
	}
	return true;
}


} // namespace gpximg
