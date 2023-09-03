#ifndef GPXVIS_FILEDIALOG_H
#define GPXVIS_FILEDIALOG_H

#ifdef GPXVIS_WITH_IMGUI

#include <string>
#include <vector>

#include "vis.h"

namespace filedialog {

class CFileDialogBase {
	public:
		CFileDialogBase();

		bool ChangeDir(const std::string& newPath);
		bool Draw();
		void DropSelection();
		void SelectByExtension(bool updateFile = false);

		void Open() {isOpen = true;}
		void Close() {isOpen = false;}
		void ToggleOpen() {isOpen = !isOpen;}

		bool Visible() const {return isOpen;}
	
	protected:
		bool                     isOpen;
		std::string              path;
		std::string              pathDialog;
		std::string		 file;
		std::string              extension;
		std::vector<std::string> subdirs;
		std::vector<std::string> files;
		std::vector<bool>        selection;

		void DoApplyFile(size_t idx);
		void DoApplyFile(const std::string& file);
		virtual void Apply(const std::string& fullFilename);
		virtual void Update();
};

class CFileDialogTracks : public CFileDialogBase {
	public:
		CFileDialogTracks(gpxvis::CAnimController& aniCtrl);

	protected:
		gpxvis::CAnimController& animCtrl;

		virtual void Apply(const std::string& fullFilename);
};

} // namespace filedialog
#endif // GPXVIS_WITH_IMGUI

#endif // GPXVIS_FILEDIALOG_H

