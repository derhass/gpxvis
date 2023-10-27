#ifndef GPXVIS_FILEDIALOG_H
#define GPXVIS_FILEDIALOG_H

#ifdef GPXVIS_WITH_IMGUI

#include <string>
#include <vector>

#include "vis.h"

namespace filedialog {

/****************************************************************************
 * PATH AND FILENAME RELATED UTILITY FUNCTIONS                              *
 ****************************************************************************/

extern void removePathDelimitersAtEnd(std::string& path);
extern std::string makePath(const std::string& path, const std::string& file);
extern std::string makeAbsolutePath(const std::string& path);
extern bool extensionMatches(const std::string& file, const std::string& extension);
extern bool ListDirectory(const std::string& path, std::vector<std::string>& subdirs, std::vector<std::string>& files);

/****************************************************************************
 * BASE CLASS FOR FILE DIALOGS VIA IMGUI                                    *
 ****************************************************************************/

class CFileDialogBase {
	public:
		CFileDialogBase();
		CFileDialogBase(bool selectDirectoryOnly);

		bool ChangeDir(const std::string& newPath);
		bool Draw();
		void DropSelection();
		void SelectByExtension(bool updateFile = false);

		void Open() {isOpen = true;}
		void Close() {isOpen = false;}
		void ToggleOpen() {isOpen = !isOpen;}

		bool Visible() const {return isOpen;}

		const std::string& GetPath() const {return path;}
	
	protected:
		bool                     selectDirectory;
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
		virtual const char *GetDialogName();
};

/****************************************************************************
 * FILE DIALOG FOR GPX TRACK SELECTION                                      *
 ****************************************************************************/

class CFileDialogTracks : public CFileDialogBase {
	public:
		CFileDialogTracks(gpxvis::CAnimController& aniCtrl);

	protected:
		gpxvis::CAnimController& animCtrl;

		virtual void Apply(const std::string& fullFilename);
		virtual const char *GetDialogName();
};

/****************************************************************************
 * DIRECTORY SELECTION DIALOG                                               *
 ****************************************************************************/

class CFileDialogSelectDir : public CFileDialogBase {
	public:
		CFileDialogSelectDir();

	protected:
		virtual const char *GetDialogName();
};

} // namespace filedialog
#endif // GPXVIS_WITH_IMGUI

#endif // GPXVIS_FILEDIALOG_H

