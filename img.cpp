#include "img.h"

#include "util.h"

#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gpximg {

const int getFileTypeIndex(const char *filetype, int defaultValue)
{
	const char *name;
	int i=0;

	if (!filetype) {
		return defaultValue;
	}

	while( (name = getFileTypeName(i)) != NULL) {
		if (!strcmp(name, filetype)) {
			return i;
		}
		i++;
	}
	return defaultValue;
}

const char *getFileTypeName(int index)
{
	const char* names[] = {
		"tga",
		"png",
		"bmp",
		"jpg"
	};

	if (index < 0 || index >= (int)(sizeof(names)/sizeof(names[0]))) {
		return NULL;
	}
	return names[index];
}

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

bool CImg::Write(const char *filename, const char *filetype) const
{
	int ft = getFileTypeIndex(filetype, -1);
	int res = 0;
	if (!data || size < 1) {
		gpxutil::warn("invalid image, can't save");
		return false;
	}
	if (ft < 0) {
		ft = 0;
		gpxutil::warn("invalid file type '%s', will use '%s' instead", filetype, getFileTypeName(ft));
	}

	stbi_flip_vertically_on_write(1);
	switch(ft) {
		case 1: /* PNG */
			stbi_write_png_compression_level = 9;
			res = stbi_write_png(filename, width, height, channels, data, width*channels);
			break;
		case 2: /* BMP */
			res = stbi_write_bmp(filename, width, height, channels, data);
			break;
		case 3: /* JPG */
			res = stbi_write_jpg(filename, width, height, channels, data, 90);
			break;
		default: /* TGA */
			res = stbi_write_tga(filename, width, height, channels, data);

	}
	if (!res) {
		gpxutil::warn("failed to write image '%s'", filename);
	}
	return true;
}

} // namespace gpximg
