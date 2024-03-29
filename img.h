#ifndef GPXVIS_IMG_H
#define GPXVIS_IMG_H

#include <stdlib.h>

namespace gpximg {

const int getFileTypeIndex(const char *filetype, int defaultValue = 0);
const char *getFileTypeName(int index);

class CImg {
	public:
		CImg();
		~CImg();

		CImg(const CImg& other) = delete;
		CImg(CImg&& other) = delete;
		CImg& operator=(const CImg& other) = delete;
		CImg& operator=(CImg&& other) = delete;

		bool Allocate(int w, int h, int c);
		void Destroy();

		bool Write(const char *filename, const char *filetype) const;

		int GetWidth() const {return width;}
		int GetHeight() const {return height;}
		int GetChannels() const {return channels;}
		size_t GetSize() const {return size;}
		const unsigned char* GetData() const {return data;}
		unsigned char* GetData() {return data;}
	private:
		unsigned char *data;
		size_t size;
		int  width;
		int  height;
		int  channels;
};

} // namespace gpximg

#endif // GPXVIS_IMG_H
