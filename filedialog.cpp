#include "filedialog.h"
#include "util.h"


#ifdef GPXVIS_WITH_IMGUI

#ifdef WIN32
#include <Windows.h>
#include <string.h>
#else
//#define _POSIX_C_SOURCE 1
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace filedialog {

/****************************************************************************
 * PATH AND FILENAME RELATED UTILITY FUNCTIONS                              *
 ****************************************************************************/

extern void removePathDelimitersAtEnd(std::string& path)
{
	size_t len = path.length();
	while(len > 1) {
		char c = path[len-1];
#ifdef WIN32
		if (c != '/' && c != '\\') {
#else
		if (c != '/') {
#endif
			break;
		}
		len--;
	}
	if (len < path.length()) {
		path.resize(len);
	}
}

extern std::string makePath(const std::string& path, const std::string& file)
{
	std::string result = path;
	if (result.empty()) {
		result = ".";
	}
	removePathDelimitersAtEnd(result);
#ifdef WIN32
	result = result + std::string("\\") + file;
#else
	result = result + std::string("/") + file;
#endif
	return result;
}

extern std::string makeAbsolutePath(const std::string& path)
{
#ifdef WIN32
	std::wstring path_wide = gpxutil::utf8ToWide(path);
	std::wstring result;
	result.resize(4096, 0);
	DWORD res = GetFullPathNameW(path_wide.c_str(), (DWORD)result.size(), &result[0], NULL);
	if (res > 0) {
		result.resize(res);
		return gpxutil::wideToUtf8(result);
	}
	return path;
#else
	char newPath[PATH_MAX];
	realpath(path.c_str(), newPath);
	return std::string(newPath);
#endif
}

extern bool extensionMatches(const std::string& file, const std::string& extension)
{
	size_t fl = file.length();
	size_t el = extension.length();
	if (el > 0 && fl > el) {
		const char *e = extension.c_str();
		const char *f = file.c_str() + fl - el;
#ifdef WIN32
		return (_stricmp(e,f) == 0);
#else
		return (strcasecmp(e,f) == 0);
#endif
	}
	return false;
}

#ifdef WIN32
static void processDirectoryEntry(WIN32_FIND_DATAW& ffd, const std::string& path, std::vector<std::string>& subdirs, std::vector<std::string>& files)
{
	std::string name(gpxutil::wideToUtf8(ffd.cFileName));
	if (name == ".") {
		return;
	}
	if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		subdirs.push_back(name);
	} else {
		files.push_back(name);
	}
}
#endif

extern bool ListDirectory(const std::string& path, std::vector<std::string>& subdirs, std::vector<std::string>& files)
{
#ifdef WIN32
	HANDLE h = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAW ffd;
	int r = 0;

	std::wstring filter = gpxutil::utf8ToWide(path) + std::wstring(L"\\*");
	h = FindFirstFileW(filter.c_str(),  &ffd);
	if (h == INVALID_HANDLE_VALUE) {
		gpxutil::warn("failed to open directory '%s'", path.c_str());
		return false;
	}
	processDirectoryEntry(ffd, path, subdirs, files);
	while (FindNextFileW(h, &ffd) != 0) {
		processDirectoryEntry(ffd, path, subdirs, files);
	}
	FindClose(h);
	return true;
#else
	DIR* d=opendir(path.c_str());
	struct dirent *e;

	if (!d) {
		gpxutil::warn("failed to open directory '%s'", path.c_str());
		return false;
	}
	while( (e = readdir(d)) != NULL ) {
		struct stat s;
		std::string fullname;
		std::string name;

		if (!e->d_name || (e->d_name[0] == '.' && e->d_name[1] == 0)) {
			continue;
		}
		name = std::string(e->d_name);
		fullname = makePath(path, name);
		if (stat(fullname.c_str(), &s)) {
			gpxutil::warn("failed to stat file '%s'", fullname.c_str());
		} else {
			if (S_ISDIR(s.st_mode)) {
				subdirs.push_back(name);
			} else if (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode)) {
				files.push_back(name);
			}
		}
	}
	closedir(d);
	return true;
#endif
}

/****************************************************************************
 * BASE CLASS FOR FILE DIALOGS VIA IMGUI                                    *
 ****************************************************************************/

CFileDialogBase::CFileDialogBase() :
	selectDirectory(false),
	isOpen(false),
	extension(".gpx")
{
}

CFileDialogBase::CFileDialogBase(bool selectDirectoryOnly) :
	selectDirectory(selectDirectoryOnly),
	isOpen(false),
	extension(".gpx")
{
}

bool CFileDialogBase::ChangeDir(const std::string& newPath)
{
	std::string targetPath = makeAbsolutePath(newPath);
	if (targetPath != path) {
		path = targetPath;
		pathDialog = targetPath;
		subdirs.clear();
		files.clear();
		file.clear();
		selection.clear();

		if (ListDirectory(path, subdirs, files)) {
			std::sort(subdirs.begin(), subdirs.end());
			std::sort(files.begin(), files.end());
			selection.resize(files.size(), false);
			SelectByExtension(true);
		} else {
			return false;
		}
	}
	return true;
}

bool CFileDialogBase::Draw()
{
	if (path.empty()) {
		ChangeDir(".");
	}
	size_t dirCnt = subdirs.size();
	size_t fileCnt = files.size();
	size_t cnt = dirCnt + fileCnt;
	bool filesAdded = false;
	std::string changePath;
	char buf[4096];

	const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 700, main_viewport->WorkPos.y+20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(GetDialogName(), &isOpen)) {
		ImGui::End();
	}


	ImGui::SeparatorText("Path");
	ImGui::InputText("Path", &pathDialog);
	if (ImGui::Button("Switch to this Path", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		changePath = pathDialog;
	}
	ImGui::SeparatorText("Contents:");
	ImGui::Text("current path: %s", path.c_str());
	if (ImGui::BeginListBox("##listboxfile", ImVec2(-FLT_MIN,  40 * ImGui::GetTextLineHeightWithSpacing()))) {
		for (size_t i=0; i<cnt; i++) {
			if (i < dirCnt) {
				mysnprintf(buf, sizeof(buf), "<%s>", subdirs[i].c_str());
				if (ImGui::Selectable(buf, false)) {
					changePath = makePath(path, subdirs[i]);
				}
			} else {
				size_t idx = i - dirCnt;
				if (ImGui::Selectable(files[idx].c_str(), selection[idx])) {
					if (!selectDirectory) {
						selection[idx] = !selection[idx];
						file = files[idx];
						ImGui::SetItemDefaultFocus();
					}
				}
			}
		}
		ImGui::EndListBox();
	}
	if (selectDirectory) {
		ImGui::SeparatorText("Actions:");
		if (ImGui::Button("Use this Directory", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			filesAdded = true;
			Close();
		}
	} else {
		ImGui::SeparatorText("Selection and Actions:");
		ImGui::InputText("File", &file);
		ImGui::InputText("Extension", &extension);
		if (ImGui::BeginTable("filedialogsplit0", 4)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Select All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				for (size_t i=0; i<fileCnt; i++) {
					selection[i] = true;
				}
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Select None", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				DropSelection();
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Select by Extension", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				SelectByExtension();
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Invert Selection", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				for (size_t i=0; i<fileCnt; i++) {
					selection[i] = !selection[i];
				}

			}
			ImGui::EndTable();
		}
		if (ImGui::BeginTable("filedialogsplit1", 4)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Selected", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				for (size_t i=0; i<fileCnt; i++) {
					if (selection[i]) {
						DoApplyFile(i);
						filesAdded = true;
					}
				}
				DropSelection();
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Single", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				if (!file.empty()) {
					DoApplyFile(file);
					filesAdded = true;
					DropSelection();
				}
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				for (size_t i=0; i<fileCnt; i++) {
					DoApplyFile(i);
					filesAdded = true;
				}
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Close", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				Close();
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
	if (!changePath.empty()) {
		ChangeDir(changePath);
	}

	if (filesAdded) {
		Update();
	}

	return filesAdded;
}

void CFileDialogBase::DropSelection()
{
	for (size_t i=0; i<selection.size(); i++) {
		selection[i] = false;
	}
}

void CFileDialogBase::SelectByExtension(bool updateFile)
{
	if (extension.empty() || selectDirectory) {
		return;
	}
	for (size_t i=0; i<files.size(); i++) {
		selection[i] = extensionMatches(files[i], extension);
		if (selection[i] && updateFile && file.empty()) {
			file = files[i];
		}
	}
}

void CFileDialogBase::DoApplyFile(size_t idx)
{
	DoApplyFile(files[idx]);
}

void CFileDialogBase::DoApplyFile(const std::string&  file)
{
	std::string fullname = makePath(path, file);
	Apply(fullname);
}

void CFileDialogBase::Apply(const std::string& fullFilename)
{
	(void)fullFilename;
}

void CFileDialogBase::Update()
{
}

const char *CFileDialogBase::GetDialogName()
{
	return "File Selection";
}

/****************************************************************************
 * FILE DIALOG FOR GPX TRACK SELECTION                                      *
 ****************************************************************************/

CFileDialogTracks::CFileDialogTracks(gpxvis::CAnimController& aniCtrl) :
	CFileDialogBase(),
	animCtrl(aniCtrl)
{
}

void CFileDialogTracks::Apply(const std::string& fullFilename)
{
	animCtrl.AddTrack(fullFilename.c_str());
}

const char *CFileDialogTracks::GetDialogName()
{
	return "GPX Track File Selection";
}

/****************************************************************************
 * DIRECTORY SELECTION DIALOG                                               *
 ****************************************************************************/

CFileDialogSelectDir::CFileDialogSelectDir() :
	CFileDialogBase(true)
{
}

const char *CFileDialogSelectDir::GetDialogName()
{
	return "Select Directory";
}

} // namespace filedialog
#endif // GPXVIS_WITH_IMGUI
