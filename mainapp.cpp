#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "gpx.h"
#include "util.h"
#include "vis.h"

#ifdef GPXVIS_WITH_IMGUI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "filedialog.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * DATA STRUCTURES                                                          *
 ****************************************************************************/

#define APP_TITLE "gpxvis"

/* OpenGL debug output error level */
typedef enum {
	DEBUG_OUTPUT_DISABLED=0,
	DEBUG_OUTPUT_ERRORS_ONLY,
	DEBUG_OUTPUT_ALL
} DebugOutputLevel;

/* AppConfig: application configuration, controllable via command line arguments*/
struct AppConfig {
	int posx;
	int posy;
	int width;
	int height;
	bool decorated;
	bool fullscreen;
	unsigned int frameCount;
	DebugOutputLevel debugOutputLevel;
	bool debugOutputSynchronous;
	bool withGUI;
	bool exitAfterOutputFrames;
	int switchTo;
	char *outputFrames;

	AppConfig() :
		posx(100),
		posy(100),
		width(1920),
		height(1080),
		decorated(true),
		fullscreen(false),
		frameCount(0),
		debugOutputLevel(DEBUG_OUTPUT_DISABLED),
		debugOutputSynchronous(false),
#ifdef GPXVIS_WITH_IMGUI
		withGUI(true),
#else
		withGUI(false),
#endif
		exitAfterOutputFrames(true),
		switchTo(0),
		outputFrames(NULL)
	{
#ifndef NDEBUG
		debugOutputLevel = DEBUG_OUTPUT_ERRORS_ONLY;
#endif
	}

};

/* MainApp: We encapsulate all of our application state in this struct.
 * We use a single instance of this object (in main), and set a pointer to
 * this as the user-defined pointer for GLFW windows. That way, we have access
 * to this data structure without ever using global variables.
 */
typedef struct {
	/* the window and related state */
	GLFWwindow *win;
	AppConfig* cfg;
	int width, height;
	int winWidth, winHeight;
	bool resized;
	double winToPixel[2];
	unsigned int flags;

	/* timing */
	double timeCur, timeDelta;
	double avg_frametime;
	double avg_fps;
	unsigned int frame;

	/* position of the main framebuffer in the final framebuffer */
	int mainSizeDynamic;
	GLsizei mainWidth;
	GLsizei mainHeight;
	GLsizei mainWidthOffset;
	GLsizei mainHeightOffset;

	/* input state */
	double mousePosWin[2];
	GLfloat mousePosMain[2];
	bool isDragging;
        GLfloat mousePosDragStart[2];

	// some gl limits
	int maxGlTextureSize;
	int maxGlSize;
	// actual visualizer
	gpxvis::CAnimController animCtrl;
#ifdef GPXVIS_WITH_IMGUI
	filedialog::CFileDialogTracks *fileDialog;
#endif
} MainApp;

/* flags */
#define APP_HAVE_GLFW	0x1	/* we have called glfwInit() and should terminate it */
#define APP_HAVE_GL	0x2	/* we have a valid GL context */
#define APP_HAVE_IMGUI	0x4	/* we have Dear ImGui initialized */

/****************************************************************************
 * SETTING UP THE GL STATE                                                  *
 ****************************************************************************/

/* debug callback of the GL */
static void APIENTRY
debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
			  GLsizei length, const GLchar *message, const GLvoid* userParam)
{
	/* we pass a pointer to our application config to the callback as userParam */
	const AppConfig *cfg=(const AppConfig*)userParam;

	switch(type) {
		case GL_DEBUG_TYPE_ERROR:
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
			if (cfg->debugOutputLevel >= DEBUG_OUTPUT_ERRORS_ONLY) {
				gpxutil::warn("GLDEBUG: %s %s %s [0x%x]: %s",
					gpxutil::translateDebugSourceEnum(source),
					gpxutil::translateDebugTypeEnum(type),
					gpxutil::translateDebugSeverityEnum(severity),
					id, message);
			}
			break;
		default:
			if (cfg->debugOutputLevel >= DEBUG_OUTPUT_ALL) {
				gpxutil::warn("GLDEBUG: %s %s %s [0x%x]: %s",
					gpxutil::translateDebugSourceEnum(source),
					gpxutil::translateDebugTypeEnum(type),
					gpxutil::translateDebugSeverityEnum(severity),
					id, message);
			}
	}
}

/* Initialize the global OpenGL state. This is called once after the context
 * is created. */
static void initGLState(MainApp* app, const AppConfig& cfg)
{
	gpxutil::printGLInfo();
	//listGLExtensions();

	if (cfg.debugOutputLevel > DEBUG_OUTPUT_DISABLED) {
		if (GLAD_GL_VERSION_4_3) {
			gpxutil::info("enabling GL debug output [via OpenGL >= 4.3]");
			glDebugMessageCallback(debugCallback,&cfg);
			glEnable(GL_DEBUG_OUTPUT);
			if (cfg.debugOutputSynchronous) {
				glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			} else {
				glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			}
		} else if (GLAD_GL_KHR_debug) {
			gpxutil::info("enabling GL debug output [via GL_KHR_debug]");
			glDebugMessageCallback(debugCallback,&cfg);
			glEnable(GL_DEBUG_OUTPUT);
			if (cfg.debugOutputSynchronous) {
				glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			} else {
				glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			}
		} else if (GLAD_GL_ARB_debug_output) {
			gpxutil::info("enabling GL debug output [via GL_ARB_debug_output]");
			glDebugMessageCallbackARB(debugCallback,&cfg);
			if (cfg.debugOutputSynchronous) {
				glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
			} else {
				glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
			}
		} else {
			gpxutil::warn("GL debug output requested, but not supported by the context");
		}
	}

	glDepthFunc(GL_LESS);
	glClearDepth(1.0f);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	app->maxGlTextureSize = 4096;
	GLint maxViewport[2] = { 4096, 4096};
	GLint maxFB[2] = {4096, 4096};
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &app->maxGlTextureSize);
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
	glGetIntegerv(GL_MAX_FRAMEBUFFER_WIDTH, &maxFB[0]);
	glGetIntegerv(GL_MAX_FRAMEBUFFER_HEIGHT, &maxFB[1]);
	gpxutil::info("GL limits: tex size: %d, viewport: %dx%d, framebuffer: %dx%d",
			app->maxGlTextureSize, maxViewport[0], maxViewport[1], maxFB[0], maxFB[1]);
	app->maxGlSize = app->maxGlTextureSize;
	if (maxViewport[0] < app->maxGlSize) {
		app->maxGlSize = maxViewport[0];
	}
	if (maxViewport[1] < app->maxGlSize) {
		app->maxGlSize = maxViewport[1];
	}
	if (maxFB[0] < app->maxGlSize) {
		app->maxGlSize = maxFB[0];
	}
	if (maxFB[1] < app->maxGlSize) {
		app->maxGlSize = maxFB[1];
	}
	gpxutil::info("GL limits: tex size: %d, viewport: %dx%d, framebuffer: %dx%d, using limt: %d",
			app->maxGlTextureSize, maxViewport[0], maxViewport[1], maxFB[0], maxFB[1], app->maxGlSize);
}

/****************************************************************************
 * coordinate space transformations and setups                              *
 ****************************************************************************/

/* calculate the position of the main framebuffer in the final window framebuffer */
static void updateMainFramebufferCoords(MainApp* app)
{
	const gpxvis::CAnimController& animCtrl = app->animCtrl;
	const gpxvis::CVis& vis = animCtrl.GetVis();

	GLsizei w = vis.GetWidth();
	GLsizei h = vis.GetHeight();

	app->mainWidthOffset=0;
	app->mainHeightOffset=0;
	app->mainWidth = app->width;
	app->mainHeight = app->height;

	if (animCtrl.IsPrepared()) {
		float winAspect = (float)app->width / (float)app->height;
		float imgAspect = (float)w/(float)h;
		if (winAspect > imgAspect) {
			float scale = (float)app->height / (float)h;
			app->mainWidth = (GLsizei)(scale * w + 0.5f);
			app->mainWidthOffset = (app->width - app->mainWidth);
		} else {
			float scale = (float)app->width / (float)w;
			app->mainHeight = (GLsizei)(scale * h + 0.5f);
			app->mainHeightOffset = (app->height- app->mainHeight) / 2;
		}
	}
}

/* transform a window coordinate into the default framebuffer coord of the window */
static void windowToMainFramebufferNormalized(const MainApp* app, const double pWin[2], GLfloat posView[2], GLfloat posTrack[2])
{
	const gpxvis::CVis& vis = app->animCtrl.GetVis();

	/* transform to default framebuffer */
	posView[0] = (GLfloat)(pWin[0] * app->winToPixel[0]);
	posView[1] = (GLfloat)(((double)app->height - 1.0 - pWin[1]) * app->winToPixel[1]);

	/* transform to main framebuffer */
	posView[0] = (posView[0] - (GLfloat) app->mainWidthOffset)  / (GLfloat)app->mainWidth;
	posView[1] = (posView[1] - (GLfloat) app->mainHeightOffset) / (GLfloat)app->mainHeight;

	/* transform to actual position considering the current transformation */
	vis.TransformToPos(posView, posTrack);
}

/****************************************************************************
 * scene transformation                                                     *
 ****************************************************************************/

/* update the transformations */
static void transformUpdate(MainApp* app, gpxvis::CAnimController& animCtrl, gpxvis::CVis& vis)
{
	(void)app;
	vis.UpdateTransform();
	animCtrl.RestoreHistory(true, true);
	animCtrl.RefreshCurrentTrack(true);
}

/* change the zoom factor */
static void doZoom(MainApp* app, float factor, float offset)
{
	GLfloat oldPos[2];
	GLfloat newPos[2];

	gpxvis::CAnimController& animCtrl = app->animCtrl;
	gpxvis::CVis& vis = animCtrl.GetVis();
	gpxvis::CVis::TConfig& visCfg = vis.GetConfig();

	vis.TransformFromPos(app->mousePosMain, oldPos);
	visCfg.zoomFactor = factor * visCfg.zoomFactor + offset;
	if ( visCfg.zoomFactor > 0.99f && visCfg.zoomFactor < 1.01f) {
		visCfg.zoomFactor = 1.0f;
	}
	vis.TransformToPos(oldPos, newPos);
	visCfg.centerNormalized[0] -= (newPos[0] - app->mousePosMain[0]);
	visCfg.centerNormalized[1] -= (newPos[1] - app->mousePosMain[1]);

	transformUpdate(app, animCtrl, vis);
}

/****************************************************************************
 * WINDOW-RELATED CALLBACKS                                                 *
 ****************************************************************************/

static bool isOurInput(MainApp *app, bool mouse)
{
	bool inputAllowed = (app->cfg->outputFrames == NULL);

#ifdef GPXVIS_WITH_IMGUI
	if (app->cfg->withGUI) {
		ImGuiIO& io = ImGui::GetIO();
		if (mouse) {
			if (io.WantCaptureMouse) {
				inputAllowed = false;
			}
		} else {
			if (io.WantCaptureKeyboard) {
				inputAllowed = false;
			}
		}
	}
#endif
	return inputAllowed;
}


/* This function is registered as the framebuffer size callback for GLFW,
 * so GLFW will call this whenever the window is resized. */
static void callback_Resize(GLFWwindow *win, int w, int h)
{
	MainApp *app=(MainApp*)glfwGetWindowUserPointer(win);
	gpxutil::info("new framebuffer size: %dx%d pixels",w,h);

	/* store curent size for later use in the main loop */
	if (w != app->width || h != app->height) {
		app->width = w;
		app->height = h;
		app->resized = true;
	}

	app->winToPixel[0] = (double)app->width  / (double)app->winWidth;
	app->winToPixel[1] = (double)app->height / (double)app->winHeight;
}

/* This function is registered as the window size callback for GLFW,
 * so GLFW will call this whenever the window is resized. */
static void callback_WinResize(GLFWwindow *win, int w, int h)
{
	MainApp *app=(MainApp*)glfwGetWindowUserPointer(win);
	gpxutil::info("new window size: %dx%d units",w,h);

	/* store curent size for later use in the main loop */
	app->winWidth=w;
	app->winHeight=h;

	app->winToPixel[0] = (double)app->width  / (double)app->winWidth;
	app->winToPixel[1] = (double)app->height / (double)app->winHeight;
}

/* This function is registered as the keayboard callback for GLFW, so GLFW
 * will call this whenever a key is pressed. */
static void callback_Keyboard(GLFWwindow *win, int key, int scancode, int action, int mods)
{
	MainApp *app=(MainApp*)glfwGetWindowUserPointer(win);
	if (isOurInput(app, false)) {
		AppConfig *cfg = app->cfg;
		if (action == GLFW_PRESS) {
			switch(key) {
				case GLFW_KEY_ESCAPE:
					glfwSetWindowShouldClose(win, 1);
					break;
			}
			if (!cfg->outputFrames) {
				gpxvis::CAnimController::TAnimConfig& animCfg = app->animCtrl.GetAnimConfig();
				switch(key) {
					case GLFW_KEY_SPACE:
						animCfg.paused = !animCfg.paused;
						break;
				}
			}
		}
	}
}

/* registered scroll wheel callback */
static void callback_scroll(GLFWwindow *win, double x, double y)
{
	MainApp *app=(MainApp*)glfwGetWindowUserPointer(win);
	if (isOurInput(app, true)) {
		if (y > 0.1) {
			doZoom(app, (float)(y * sqrt(2.0)), 0.0f);
		} else if ( y < -0.1) {
			doZoom(app, (float)(-1.0 / (sqrt(2.0) * y)), 0.0f);
		}
	}
}

/* called by mainLoop(): process further inputs */
static void processInputs(MainApp *app)
{
	GLfloat posView[2];
	glfwGetCursorPos(app->win, &app->mousePosWin[0], &app->mousePosWin[1]);
	windowToMainFramebufferNormalized(app, app->mousePosWin, posView, app->mousePosMain);
	if (!isOurInput(app, true)) {
		return;
	}

	gpxvis::CAnimController& animCtrl = app->animCtrl;
	gpxvis::CVis& vis = animCtrl.GetVis();
	gpxvis::CVis::TConfig& visCfg = vis.GetConfig();
	int leftButton = glfwGetMouseButton(app->win, GLFW_MOUSE_BUTTON_LEFT);
	if  (leftButton == GLFW_PRESS) {
		if (app->isDragging) {
			GLfloat delta[2];
			delta[0] = app->mousePosMain[0] - app->mousePosDragStart[0];
			delta[1] = app->mousePosMain[1] - app->mousePosDragStart[1];
			if ( (delta[0] != 0.0f) && delta[1] != 0.0f) {
				visCfg.centerNormalized[0] -= delta[0];
				visCfg.centerNormalized[1] -= delta[1];
				transformUpdate(app, animCtrl, vis);
			}
		} else {
			app->isDragging = true;
			app->mousePosDragStart[0] = app->mousePosMain[0];
			app->mousePosDragStart[1] = app->mousePosMain[1];
		}
	} else {
		app->isDragging = false;
	}
}

/****************************************************************************
 * GLOBAL INITIALIZATION AND CLEANUP                                        *
 ****************************************************************************/

/* Initialize the Application.
 * This will initialize the app object, create a windows and OpenGL context
 * (via GLFW), initialize the GL function pointers via GLEW and initialize
 * the cube.
 * Returns true if successfull or false if an error occured. */
bool initMainApp(MainApp *app, AppConfig& cfg)
{
	int w, h, x, y;
	bool debugCtx=(cfg.debugOutputLevel > DEBUG_OUTPUT_DISABLED);

	/* Initialize the app structure */
	app->win=NULL;
	app->cfg=&cfg;
	app->flags=0;
	app->avg_frametime=-1.0;
	app->avg_fps=-1.0;
	app->frame = 0;
	app->resized = false;
#ifdef GPXVIS_WITH_IMGUI
	app->fileDialog = NULL;
#endif

	/* initialize GLFW library */
	gpxutil::info("initializing GLFW");
	if (!glfwInit()) {
		gpxutil::warn("Failed to initialze GLFW");
		return false;
	}

	app->flags |= APP_HAVE_GLFW;

	/* request a OpenGL 4.6 core profile context */
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, (debugCtx)?GL_TRUE:GL_FALSE);

	GLFWmonitor *monitor = NULL;
	x = cfg.posx;
	y = cfg.posy;
	w = cfg.width;
	h = cfg.height;

	if (cfg.fullscreen) {
		monitor = glfwGetPrimaryMonitor();
	}
	if (monitor) {
		glfwGetMonitorPos(monitor, &x, &y);
		const GLFWvidmode *v = glfwGetVideoMode(monitor);
		if (v) {
			w = v->width;
			h = v->height;
			gpxutil::info("Primary monitor: %dx%d @(%d,%d)", w, h, x, y);
		}
		else {
			gpxutil::warn("Failed to query current video mode!");
		}
	}

	if (!cfg.decorated) {
		glfwWindowHint(GLFW_DECORATED, GL_FALSE);
	}

	/* create the window and the gl context */
	gpxutil::info("creating window and OpenGL context");
	app->win=glfwCreateWindow(w, h, APP_TITLE, monitor, NULL);
	if (!app->win) {
		gpxutil::warn("failed to get window with OpenGL 4.5 core context");
		return false;
	}

	app->width = w;
	app->height = h;
	app->winWidth = w;
	app->winHeight = h;
	app->winToPixel[0] = 1.0f;
	app->winToPixel[1] = 1.0f;
	app->mainSizeDynamic = 0;
	app->mainWidth = w;
	app->mainHeight = h;
	app->mainWidthOffset = 0;
	app->mainHeightOffset = 0;
	app->mousePosWin[0] = 0.0f;
	app->mousePosWin[1] = 0.0f;
	app->mousePosMain[0] = 0.0f;
	app->mousePosMain[1] = 1.0f;
	app->isDragging = false;
	app->mousePosDragStart[0] = 0.0f;
	app->mousePosDragStart[1] = 0.0f;

	if (!monitor) {
		glfwSetWindowPos(app->win, x, y);
	}

	/* store a pointer to our application context in GLFW's window data.
	 * This allows us to access our data from within the callbacks */
	glfwSetWindowUserPointer(app->win, app);
	/* register our callbacks */
	glfwSetFramebufferSizeCallback(app->win, callback_Resize);
	glfwSetWindowSizeCallback(app->win, callback_WinResize);
	glfwSetKeyCallback(app->win, callback_Keyboard);
	glfwSetScrollCallback(app->win, callback_scroll);

	/* make the context the current context (of the current thread) */
	glfwMakeContextCurrent(app->win);

	/* ask the driver to enable synchronizing the buffer swaps to the
	 * VBLANK of the display. Depending on the driver and the user's
	 * setting, this may have no effect. But we can try... */
	glfwSwapInterval((cfg.outputFrames)?0:1);

	/* initialize glad,
	 * this will load all OpenGL function pointers
	 */
	gpxutil::info("initializing glad");
	if (!gladLoadGL(glfwGetProcAddress)) {
		gpxutil::warn("failed to intialize glad GL extension loader");
		return false;
	}

	if (!GLAD_GL_VERSION_4_5) {
		gpxutil::warn("failed to load at least GL 4.5 functions via GLAD");
		return false;
	}

	app->flags |= APP_HAVE_GL;

	if (cfg.withGUI) {
#ifdef GPXVIS_WITH_IMGUI
		/* initialize imgui */
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = NULL;
		io.LogFilename = NULL;

		// Setup Platform/Renderer backends
		ImGui_ImplGlfw_InitForOpenGL(app->win, true);
		ImGui_ImplOpenGL3_Init();
		app->flags |= APP_HAVE_IMGUI;
#else
		gpxutil::warn("GUI requested but Dear ImGui not compiled in!");
#endif
	}

	/* initialize the GL context */
	initGLState(app, cfg);

	if (cfg.switchTo) {
		size_t cnt = app->animCtrl.GetTrackCount();
		size_t idx;
		if (cfg.switchTo > 0) {
			idx = (size_t)cfg.switchTo;
		} else {
			idx = (size_t)-cfg.switchTo;
			if (idx > cnt) {
				idx = cnt;
			}
			idx = cnt - idx;
		}
		app->animCtrl.SwitchToTrack(idx);
	}

	// TODO ...
	if (!app->animCtrl.Prepare(app->width,app->height)) {
		gpxutil::warn("failed to initialize animation controller");
		if (cfg.outputFrames) {
			return false;
		}
	}

	/* initialize the timer */
	app->timeCur=glfwGetTime();

	return true;
}

/* Clean up: destroy everything the cube app still holds */
static void destroyMainApp(MainApp *app)
{
	if (app->flags & APP_HAVE_GLFW) {
		if (app->win) {
			if (app->flags & APP_HAVE_GL) {
				app->animCtrl.DropGL();
				/* shut down imgui */
#ifdef GPXVIS_WITH_IMGUI
				if (app->flags & APP_HAVE_IMGUI) {
					ImGui_ImplOpenGL3_Shutdown();
					ImGui_ImplGlfw_Shutdown();
					ImGui::DestroyContext();				
				}
#endif
			}
			glfwDestroyWindow(app->win);
		}
		glfwTerminate();
	}
}

/****************************************************************************
 * DRAWING FUNCTION                                                         *
 ****************************************************************************/

static void saveCurrentFrame(gpxvis::CAnimController& animCtrl, const char *namePrefix, const char *additionalPrefix, unsigned long number)
{
	if (!namePrefix) {
		namePrefix = "gpxvis_";
	}
	if (!additionalPrefix) {
		additionalPrefix = "";
	}

	gpximg::CImg img;
	if (animCtrl.GetVis().GetImage(img)) {
		char buf[4096];
		mysnprintf(buf, sizeof(buf), "%s%s%06lu.tga", namePrefix, additionalPrefix, number);
		img.WriteTGA(buf);
	}
}

static void saveCurrentFrame(gpxvis::CAnimController& animCtrl, const char *namePrefix)
{
	saveCurrentFrame(animCtrl, namePrefix, NULL, animCtrl.GetFrame());
}

#ifdef GPXVIS_WITH_IMGUI
static void drawTrackStatus(gpxvis::CAnimController& animCtrl)
{
	ImGui::Begin("frameinfo", NULL,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
	if (animCtrl.GetTrackCount()) {
		const gpx::CTrack& track = animCtrl.GetCurrentTrack();
		ImGui::SetCursorPosY(2);
		ImGui::Text("#%d/%d", (int)animCtrl.GetCurrentTrackIndex()+1, (int)animCtrl.GetTrackCount());
		float ww = ImGui::GetWindowSize().x;
		float tw = ImGui::CalcTextSize(track.GetInfo()).x;
		ImGui::SetCursorPosX(ww - tw - 8.0f);
		ImGui::SetCursorPosY(2);
		ImGui::Text(track.GetInfo());
	}
	ImGui::End();
}

static void drawTrackManager(MainApp* app, gpxvis::CAnimController& animCtrl, gpxvis::CVis& vis, bool *isOpen)
{
	const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 600, main_viewport->WorkPos.y+100), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_FirstUseEver);
	std::vector<gpx::CTrack>& tracks=animCtrl.GetTracks();
	bool modified = false;
	bool disabled = (tracks.size() < 1);
	static size_t curIdx = 0;
	if (!ImGui::Begin("Track Manager", isOpen)) {
		ImGui::End();
	}
	ImGui::SeparatorText("Info:");
	ImGui::BeginDisabled(disabled);
	if (ImGui::BeginTable("managerinfosplit", 3)) {
		ImGui::TableNextColumn();
		ImGui::Text("%llu tracks loaded", (unsigned long long)tracks.size());
		ImGui::TableNextColumn();
		ImGui::Text("length: %.1fkm", animCtrl.GetAllTrackLength());
		ImGui::TableNextColumn();
		ImGui::Text("duration: %s", animCtrl.GetAllTrackDurationString());
		ImGui::EndTable();
	}
	ImGui::EndDisabled();
	ImGui::SeparatorText("Files:");
	if (ImGui::BeginListBox("##listbox 2", ImVec2(-FLT_MIN,  40 * ImGui::GetTextLineHeightWithSpacing()))) {
		for (size_t i=0; i<tracks.size(); i++) {
			char info[512];
			mysnprintf(info, sizeof(info), "%d. %s [%s] %.1fkm %s", (int)(i+1), tracks[i].GetFilename(), tracks[i].GetInfo(), tracks[i].GetLength(), tracks[i].GetDurationString());
			info[sizeof(info)-1]=0;
			bool isSelected = (curIdx == i);
			if (ImGui::Selectable(info, isSelected)) {
				curIdx = i;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndListBox();
	}
	std::vector<gpx::CTrack>::iterator it=tracks.begin();
	std::advance(it, curIdx);
	ImGui::BeginDisabled(disabled);
	if (ImGui::BeginTable("managerpertracksplit", 6)) {
		ImGui::TableNextColumn();
		if (ImGui::Button("Switch to", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 0) {
				animCtrl.SwitchToTrack(curIdx);
				// TODO: modifiedHistory = animCfg.clearAtCycle;
			}
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("To Front", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 1) {
				gpx::CTrack tmp = tracks[curIdx];
				tracks.erase(it);
				tracks.insert(tracks.begin(), tmp);
				curIdx = 0;
				modified = true;
			}
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Move Up", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 1 && curIdx > 0) {
				gpx::CTrack tmp = tracks[curIdx];
				tracks.erase(it);
				tracks.insert(tracks.begin()+(curIdx-1), tmp);
				curIdx--;
				modified = true;
			}
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Move Down", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 1 && curIdx + 1 < tracks.size()) {
				gpx::CTrack tmp = tracks[curIdx];
				tracks.erase(it);
				tracks.insert(tracks.begin()+(curIdx+1), tmp);
				curIdx++;
				modified = true;
			}
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("To End", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 1) {
				gpx::CTrack tmp = tracks[curIdx];
				tracks.erase(it);
				tracks.push_back(tmp);
				curIdx = tracks.size()-1;
				modified = true;
			}
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Remove", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 0) {
				tracks.erase(it);
				if (curIdx >= tracks.size()) {
					curIdx = tracks.size();
					if (curIdx > 0) {
						curIdx--;
					}
				}
				modified = true;
			}
		}
		ImGui::EndTable();
	}
	if (ImGui::BeginTable("managerpertracksplit3", 4)) {
		ImGui::TableNextColumn();
		if (ImGui::Button("Sort by Date", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.SortTracks(gpxvis::CAnimController::BY_TIME);
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Sort by Distance", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.SortTracks(gpxvis::CAnimController::BY_LENGTH);
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Sort by Duration", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.SortTracks(gpxvis::CAnimController::BY_DURATION);
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Sort by Name", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.SortTracks(gpxvis::CAnimController::BY_NAME);
		}
		ImGui::EndTable();
	}
	ImGui::EndDisabled();
	if (ImGui::BeginTable("managerpertracksplit2", 4)) {
		ImGui::TableNextColumn();
		if (ImGui::Button("Remove all Tracks", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			tracks.clear();
			curIdx = 0;
			modified = true;
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Remove all Others", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			if (tracks.size() > 1) {
				gpx::CTrack tmp = tracks[curIdx];
				tracks.clear();
				tracks.push_back(tmp);
				curIdx = 0;
				modified = true;
			}
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Remove Duplicates", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			modified = !animCtrl.RemoveDuplicateTracks();
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Reverse Order", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			modified = !animCtrl.ReverseTrackOrder();
		}
		ImGui::EndTable();
	}

	ImGui::BeginDisabled((app->fileDialog == NULL));
	if (ImGui::Button("Add Files", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		if (app->fileDialog) {
			app->fileDialog->Open();
		}
	}
	ImGui::EndDisabled();

	if (modified) {
		GLsizei w = vis.GetWidth();
		GLsizei h = vis.GetHeight();
		if (w < 1) {
			w = (GLsizei)app->width;
		}
		if (h < 1) {
			h = (GLsizei)app->height;
		}
		animCtrl.Prepare(w,h);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}
	ImGui::End();
}

static void drawMainWindow(MainApp* app, AppConfig& cfg, gpxvis::CAnimController& animCtrl, gpxvis::CVis& vis)
{
	bool modified = false;
	bool modifiedHistory = false;
	bool modifiedTransform = false;
	static bool first  = true;
	static char outputFilename[256];
	static bool showTrackManager = false;
	if (first) {
		strcpy(outputFilename, "gpxvis_");
	}

	const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x, main_viewport->WorkPos.y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(512, 0), ImGuiCond_FirstUseEver);

	ImGui::Begin("Main Controls");
	size_t cnt = animCtrl.GetTrackCount();
	bool disabled = (cnt < 1);

	if (first && disabled) {
		showTrackManager = true;
		if (app->fileDialog) {
			app->fileDialog->Open();
		}
	}

	char buf[16];
	if (cnt > 0) {
		mysnprintf(buf,sizeof(buf), "#%d/%d", (int)animCtrl.GetCurrentTrackIndex()+1, (int)cnt);
	} else {
		mysnprintf(buf,sizeof(buf), "(none)");
	}
	buf[sizeof(buf)-1]=0;



	gpxvis::CAnimController::TAnimConfig& animCfg = animCtrl.GetAnimConfig();
	gpxvis::CVis::TConfig& visCfg=vis.GetConfig();
	ImGui::BeginDisabled(disabled);
	if (ImGui::BeginTable("tracksplit", 3)) {
		ImGui::TableNextColumn();
		if (ImGui::Button("|<<", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0.0f))) {
			animCtrl.ResetAnimation();
			//modifiedHistory = true;
		}
		ImGui::SameLine();
		float startPos = ImGui::GetCursorPosX();
		ImGui::PushButtonRepeat(true);
		if (ImGui::Button("<", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.ChangeTrack(-1);
			modifiedHistory = true;
		}
		ImGui::PopButtonRepeat();
		float bWidth = ImGui::GetCursorPosX() - startPos;

		ImGui::TableNextColumn();
		//ImGui::SetCursorPosX(0.5f*(ImGui::GetContentRegionAvail().x));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX()+0.5f*(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(buf).x));
		ImGui::TextUnformatted(buf);
		ImGui::TableNextColumn();
		ImGui::PushButtonRepeat(true);
		if (ImGui::Button(">", ImVec2(bWidth, 0.0f))) {
			animCtrl.ChangeTrack(1);
			modifiedHistory = true;
		}
		ImGui::PopButtonRepeat();
		ImGui::SameLine();
		if (ImGui::Button(">>|", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.SwitchToTrack(cnt);
			modifiedHistory = true;
		}
		ImGui::EndTable();
	}
	ImGui::EndDisabled();
	if (ImGui::BeginTable("controls", 3)) {
		ImGui::BeginDisabled(disabled);
		ImGui::TableNextColumn();
		if (ImGui::Button(animCfg.paused?"Play":"Pause", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCfg.paused = !animCfg.paused;
		}
		ImGui::EndDisabled();
		ImGui::TableNextColumn();
		if (ImGui::Button("Manage Tracks", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			showTrackManager = !showTrackManager;
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Quit", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			glfwSetWindowShouldClose(app->win, 1);
		}
		ImGui::EndTable();
	}
	static const gpx::CTrack defaultTrack;
	static int timestepMode = 0;
	static float fixedTimestep = 1000.0f/60.0f;
	static float speedup = 1.0f;
	const gpx::CTrack *curTrack = &defaultTrack;
	if (cnt > 0) {
		curTrack = &animCtrl.GetCurrentTrack();
	}
	ImGui::Text("File: %s", curTrack->GetFilename());
	if (ImGui::BeginTable("infosplit", 3)) {
		ImGui::TableNextColumn();
		ImGui::Text("%s", curTrack->GetInfo());
		ImGui::TableNextColumn();
		ImGui::Text("Len: %.1fkm (%dpts)", curTrack->GetLength(), (int)curTrack->GetCount());
		ImGui::TableNextColumn();
		ImGui::Text("Dur: %s", curTrack->GetDurationString());
		ImGui::EndTable();
	}
	if (ImGui::TreeNodeEx("View Transformation", 0)) {
		double mPos[2];
		double tPos[2];
		double cPos[3];
		double lPos[2];
		mPos[0] = (double)app->mousePosMain[0];
		mPos[1] = (double)app->mousePosMain[1];
		animCtrl.GetAABB().InterpolateNormalized2D(mPos, tPos);
		animCtrl.GetAABB().GetCenter(cPos);
		gpx::unprojectMercator(tPos[0],tPos[1],lPos[0],lPos[1]);
		tPos[0] -= cPos[0];
		tPos[1] -= cPos[1];
		ImGui::Text("mouse position (normalized): (%f %f)", app->mousePosMain[0], app->mousePosMain[1]);
		ImGui::Text("mouse position (distance [km]): (%.3f %.3f) %.3f", tPos[0], tPos[1], sqrt(tPos[0]*tPos[0] + tPos[1]*tPos[1]));
		ImGui::Text("mouse position (lon/lat): (%.6f %.6f)", lPos[0], lPos[1]);
		ImGui::BeginDisabled(disabled);
		if (ImGui::SliderFloat("zoom factor", &visCfg.zoomFactor, 0.01f, 100.0f, "%.02fx", ImGuiSliderFlags_Logarithmic)) {
			modifiedHistory = true;
			modifiedTransform = true;
			modified = true;
		}
		if (ImGui::SliderFloat("position x", &visCfg.centerNormalized[0], 0.0f, 1.0f, "%.03f")) {
			modifiedHistory = true;
			modifiedTransform = true;
			modified = true;
		}
		if (ImGui::SliderFloat("position y", &visCfg.centerNormalized[1], 0.0f, 1.0f, "%.03f")) {
			modifiedHistory = true;
			modifiedTransform = true;
			modified = true;
		}
		if (ImGui::BeginTable("viewtransformsplit1", 3)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Reset Zoom", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				visCfg.zoomFactor = 1.0f;
				modifiedHistory = true;
				modifiedTransform = true;
				modified = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Reset Position", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				visCfg.centerNormalized[0] = 0.5f;
				visCfg.centerNormalized[1] = 0.5f;
				modifiedHistory = true;
				modifiedTransform = true;
				modified = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Reset View", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				visCfg.ResetTransform();
				modifiedHistory = true;
				modifiedTransform = true;
				modified = true;
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Histroy Manipulation", 0)) {
		ImGui::BeginDisabled(disabled);
		ImGui::SeparatorText("Manipulate History and Neighborhood");
		if (ImGui::BeginTable("histcontrolsplit1", 5)) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("History:");
			ImGui::TableNextColumn();
			if (ImGui::Button("Clear", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				vis.ClearHistory();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Current", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				vis.AddLineToBackground();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Up To", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.RestoreHistoryUpTo(animCtrl.GetCurrentTrackIndex(), true, false);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.RestoreHistoryUpTo(animCtrl.GetTrackCount(), true, false);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::EndTable();
		}
		if (ImGui::BeginTable("histcontrolsplit2", 5)) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Neighborhood:");
			ImGui::TableNextColumn();
			if (ImGui::Button("Clear", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				vis.ClearNeighborHood();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Current", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				vis.AddLineToNeighborhood();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Up To", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.RestoreHistoryUpTo(animCtrl.GetCurrentTrackIndex(), false, true);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.RestoreHistoryUpTo(animCtrl.GetTrackCount(), false, true);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::EndTable();
		}
		if (ImGui::BeginTable("histcontrolsplit3", 5)) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Both:");
			ImGui::TableNextColumn();
			if (ImGui::Button("Clear", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				vis.Clear();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Current", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				vis.AddToBackground();
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add Up To", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.RestoreHistoryUpTo(animCtrl.GetCurrentTrackIndex(), true, true);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Add All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.RestoreHistoryUpTo(animCtrl.GetTrackCount(), true, true);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Animation Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginDisabled(disabled);
		ImGui::SeparatorText("Animation Position");
		if (ImGui::BeginTable("animinfosplit", 3)) {
			ImGui::TableNextColumn();
			ImGui::Text("frame: %lu", animCtrl.GetFrame());
			ImGui::TableNextColumn();
			ImGui::Text("time: %.2fs", animCtrl.GetTime());
			ImGui::TableNextColumn();
			ImGui::Text("delta: %.1fms", animCtrl.GetAnimationDelta()*1000.0);
			ImGui::EndTable();
		}
		
		float trackUpTo = animCtrl.GetCurrentTrackUpTo();
		float trackTime = (float)animCtrl.GetCurrentTrackPos();
		float trackPos = (float)curTrack->GetDistanceAt(trackUpTo);
		if (trackUpTo < 0.0f) {
			trackUpTo = (float)curTrack->GetCount();
		}
		if (ImGui::SliderFloat("track time", &trackTime, 0.0f, (float)curTrack->GetDuration()-1.0f, "%.1fs")) {
			animCtrl.SetCurrentTrackPos((double)trackTime);
		}
		if (ImGui::SliderFloat("track position", &trackPos, 0.0f, (float)curTrack->GetLength(), "%.3fkm")) {
			trackUpTo = curTrack->GetPointByDistance((double)trackPos);
			trackTime = (float)curTrack->GetDurationAt(trackUpTo);
			animCtrl.SetCurrentTrackPos((double)trackTime);
		}
		if (ImGui::SliderFloat("track index", &trackUpTo, 0.0f, (float)curTrack->GetCount()-1.0f, "%.2f")) {
			trackTime = (float)curTrack->GetDurationAt(trackUpTo);
			animCtrl.SetCurrentTrackPos((double)trackTime);
		}
		float fadeRatio = animCtrl.GetCurrentFadeRatio();
		if (ImGui::SliderFloat("fade-out", &fadeRatio, 0.0f, 1.0f, "%.2f")) {
			animCtrl.SetCurrentFadeRatio(fadeRatio);
		}

		ImGui::SeparatorText("Animation Speed");
		bool timestepModified = false;
		ImGui::TextUnformatted("Timestep: ");
		ImGui::SameLine();
		if (ImGui::RadioButton("dynamic", &timestepMode, 0)) {
			timestepModified = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("fixed", &timestepMode, 1)) {
			timestepModified = true;
		}
		if (timestepMode) {
			if (ImGui::SliderFloat("fixed timestep", &fixedTimestep, 0.01f, 10000.0, "%.2fms", ImGuiSliderFlags_Logarithmic)) {
				timestepModified = true;
			}
		} else {
			float value = (float)app->timeDelta * 1000.0f;
			ImGui::SliderFloat("dynamic timestep", &value, 0.01f, 10000.0, "%.2fms", ImGuiSliderFlags_Logarithmic);
		}

		float trackSpeed = (float)animCfg.trackSpeed/3600.0f;
		if (ImGui::SliderFloat("track speed", &trackSpeed, 0.0f, 100.0, "%.3fhrs/s", ImGuiSliderFlags_Logarithmic)) {
			animCfg.trackSpeed = trackSpeed * 3600.0;
		}
		float fadeout = (float)animCfg.fadeoutTime;
		if (ImGui::SliderFloat("fade-out time", &fadeout, 0.0f, 10.0, "%.2fs", ImGuiSliderFlags_Logarithmic)) {
			animCfg.fadeoutTime = fadeout;
		}
		if (ImGui::SliderFloat("speedup factor", &speedup, 0.0f, 100.0f, "%.3fx", ImGuiSliderFlags_Logarithmic)) {
			timestepModified = true;
		}
		if (ImGui::Button("Reset Animation Speeds", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCfg.ResetSpeeds();
			speedup = 1.0f;
			timestepMode = 0;
			fixedTimestep = 1000.0f/60.0f;
			timestepModified = true;
		}
		if (timestepModified) {
			if (timestepMode == 1) {
				animCtrl.SetAnimSpeed(fixedTimestep/1000.0 * speedup);
			} else {
				animCtrl.SetAnimSpeed(-speedup);
			}
		}
		ImGui::SeparatorText("Animation Options");
		if (ImGui::BeginTable("animoptionssplit", 2)) {
			ImGui::TableNextColumn();
			ImGui::Checkbox("Pause at end", &animCfg.pauseAtCycle);
			ImGui::TableNextColumn();
			ImGui::Checkbox("Clear at end", &animCfg.clearAtCycle);
			ImGui::EndTable();
		}
		if (ImGui::BeginTable("animoptionshistorysplit", 5)) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("History:");
			ImGui::TableNextColumn();
			int bgMode =(int)animCfg.historyMode;
			int nhMode =(int)animCfg.neighborhoodMode;
			if (ImGui::RadioButton("none##1", &bgMode, gpxvis::CAnimController::BACKGROUND_NONE)) {
				animCfg.historyMode = (gpxvis::CAnimController::TBackgroundMode)bgMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("current##1", &bgMode, gpxvis::CAnimController::BACKGROUND_CURRENT)) {
				animCfg.historyMode = (gpxvis::CAnimController::TBackgroundMode)bgMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("up-to##1", &bgMode, gpxvis::CAnimController::BACKGROUND_UPTO)) {
				animCfg.historyMode = (gpxvis::CAnimController::TBackgroundMode)bgMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("all##1", &bgMode, gpxvis::CAnimController::BACKGROUND_ALL)) {
				animCfg.historyMode = (gpxvis::CAnimController::TBackgroundMode)bgMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Neighborh.:");
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("none##2", &nhMode, gpxvis::CAnimController::BACKGROUND_NONE)) {
				animCfg.neighborhoodMode = (gpxvis::CAnimController::TBackgroundMode)nhMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("current##2", &nhMode, gpxvis::CAnimController::BACKGROUND_CURRENT)) {
				animCfg.neighborhoodMode = (gpxvis::CAnimController::TBackgroundMode)nhMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("up-to##2", &nhMode, gpxvis::CAnimController::BACKGROUND_UPTO)) {
				animCfg.neighborhoodMode = (gpxvis::CAnimController::TBackgroundMode)nhMode;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("all##2", &nhMode, gpxvis::CAnimController::BACKGROUND_ALL)) {
				animCfg.neighborhoodMode = (gpxvis::CAnimController::TBackgroundMode)nhMode;
				modifiedHistory = true;
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Visualization Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginDisabled(disabled);
		ImGui::SeparatorText("Track Colors");
		int historyLineMode = (int)(visCfg.historyWideLine);
		if (ImGui::ColorEdit3("track history", visCfg.colorBase)) {
			modified = true;
			modifiedHistory = true;
		}
		ImGui::BeginDisabled(visCfg.historyAdditive < gpxvis::CVis::BACKGROUND_ADD_MIXED_COLORS);
		if (ImGui::ColorEdit3("track history add", visCfg.colorHistoryAdd)) {
			modified = true;
			modifiedHistory = true;
		}
		ImGui::EndDisabled();
		if (ImGui::ColorEdit3("gradient new", &visCfg.colorGradient[0][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("gradient mid", &visCfg.colorGradient[1][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("gradient old", &visCfg.colorGradient[2][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("current point", &visCfg.colorGradient[3][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("background", visCfg.colorBackground)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::Button("Reset Colors", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			visCfg.ResetColors();
			modified = true;
			modifiedHistory = true;
		}
		ImGui::SeparatorText("Line Parameters");
		if (ImGui::SliderFloat("track width", &visCfg.trackWidth, 0.0f, 32.0f)) {
			modified = true;
		}
		if (ImGui::SliderFloat("track sharpness", &visCfg.trackExp, 0.1f, 10.0f, "%0.2f", ImGuiSliderFlags_Logarithmic)) {
			modified = true;
		}
		if (ImGui::SliderFloat("point size", &visCfg.trackPointWidth, 0.0f, 32.0f)) {
			modified = true;
		}
		if (ImGui::SliderFloat("point sharpness", &visCfg.trackPointExp, 0.1f, 10.0f, "%0.2f", ImGuiSliderFlags_Logarithmic)) {
			modified = true;
		}
		if (ImGui::SliderFloat("neighborhood width", &visCfg.neighborhoodWidth, 0.0f, 32.0f)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::SliderFloat("neighborhood sharpness", &visCfg.neighborhoodExp, 0.1f, 10.0f, "%0.2f", ImGuiSliderFlags_Logarithmic)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::BeginTable("visoptionshistorylinesplit", 3)) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("History Line mode:");
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("thin", &historyLineMode, 0)) {
				visCfg.historyWideLine = false;
				modified = true;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("wide", &historyLineMode, 1)) {
				visCfg.historyWideLine = true;
				modified = true;
				modifiedHistory = true;
			}
			ImGui::EndTable();
		}
		if (ImGui::BeginTable("visoptionshistorysplit", 5)) {
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Additive:");
			ImGui::TableNextColumn();
			int histAddMode =(int)visCfg.historyAdditive;
			if (ImGui::RadioButton("off", &histAddMode, gpxvis::CVis::BACKGROUND_ADD_NONE)) {
				visCfg.historyAdditive = (gpxvis::CVis::TBackgroundAdditiveMode)histAddMode; 
				modified = true;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("simple", &histAddMode, gpxvis::CVis::BACKGROUND_ADD_SIMPLE)) {
				visCfg.historyAdditive = (gpxvis::CVis::TBackgroundAdditiveMode)histAddMode; 
				modified = true;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("mixed", &histAddMode, gpxvis::CVis::BACKGROUND_ADD_MIXED_COLORS)) {
				visCfg.historyAdditive = (gpxvis::CVis::TBackgroundAdditiveMode)histAddMode; 
				modified = true;
				modifiedHistory = true;
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("gradient", &histAddMode, gpxvis::CVis::BACKGROUND_ADD_GRADIENT)) {
				visCfg.historyAdditive = (gpxvis::CVis::TBackgroundAdditiveMode)histAddMode; 
				modified = true;
				modifiedHistory = true;
			}
			ImGui::EndTable();
		}
		ImGui::BeginDisabled(!visCfg.historyWideLine);
		if (ImGui::SliderFloat("history width", &visCfg.historyWidth, 0.0f, 32.0f)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::SliderFloat("history sharpness", &visCfg.historyExp, 0.1f, 10.0f, "%0.2f", ImGuiSliderFlags_Logarithmic)) {
			modified = true;
			modifiedHistory = true;
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(visCfg.historyAdditive == gpxvis::CVis::BACKGROUND_ADD_NONE);
		if (ImGui::SliderFloat("additive exponent", &visCfg.historyAddExp, 0.01f, 100.0f, "%0.3f", ImGuiSliderFlags_Logarithmic)) {
			modified = true;
			modifiedHistory = true;
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(visCfg.historyAdditive != gpxvis::CVis::BACKGROUND_ADD_GRADIENT);
		if (ImGui::SliderFloat("gradient slope", &visCfg.historyAddSaturationOffset, 1.0f, 100.0f, "%0.1f")) {
			modified = true;
			modifiedHistory = true;
		}
		ImGui::EndDisabled();
		if (ImGui::Button("Reset Line Parameters", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			visCfg.ResetWidths();
			modified = true;
			modifiedHistory = true;
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Rendering", 0)) {
		static int renderSize[2] = {-1, -1};
		int maxSize = 8192;
		bool adaptToWindow = false;
		ImGui::BeginDisabled(disabled);
		if (app->maxGlSize < maxSize) {
			maxSize = app->maxGlSize;
		}
		ImGui::SeparatorText("Render Settings");
		if (ImGui::BeginTable("renderinfosplit", 2)) {
			ImGui::TableNextColumn();
			ImGui::Text("Resolution: %dx%d", (int)vis.GetWidth(), (int)vis.GetHeight());
			ImGui::TableNextColumn();
			ImGui::Text("data aspect ratio: %.3f", vis.GetDataAspect());
			ImGui::EndTable();
		}
		if (renderSize[0] < 1) {
			renderSize[0] = (int)vis.GetWidth();
		}
		if (renderSize[1] < 1) {
			renderSize[1] = (int)vis.GetHeight();
		}
		ImGui::TextUnformatted("Framebuffer size: ");
		ImGui::SameLine();
		ImGui::RadioButton("static", &app->mainSizeDynamic, 0);
		ImGui::SameLine();
		if (ImGui::RadioButton("dynamic", &app->mainSizeDynamic, 1)) {
			adaptToWindow = true;
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(disabled || (app->mainSizeDynamic == 1));
		if (ImGui::SliderInt("render width", &renderSize[0], 256, maxSize, "%dpx")) {
			renderSize[0] = (int)gpxutil::roundNextMultiple((GLsizei)renderSize[0], animCfg.resolutionGranularity);
		}
		if (ImGui::SliderInt("render height", &renderSize[1], 256, maxSize, "%dpx")) {
			renderSize[1] = (int)gpxutil::roundNextMultiple((GLsizei)renderSize[1], animCfg.resolutionGranularity);
		}
		ImGui::SliderInt("resolution multiple of", (int*)&animCfg.resolutionGranularity, 1, 64, "%dpx");
		ImGui::Checkbox("Adjust framebuffer size to data aspect ratio", &animCfg.adjustToAspect);
		if (ImGui::BeginTable("renderbuttonssplit", 2)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.Prepare((GLsizei)renderSize[0], (GLsizei)renderSize[1]);
				modifiedHistory = true;
				modified = true;
				renderSize[0] = -1;
				renderSize[1] = -1;
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Cancel", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				renderSize[0] = -1;
				renderSize[1] = -1;
			}
			ImGui::EndTable();
		}
		if (ImGui::Button("Adapt to window", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			adaptToWindow = true;
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
		if  (app->mainSizeDynamic && app->resized) {
			adaptToWindow = true;
		}
		if (adaptToWindow) {
			int mask = 63;
			renderSize[0] = app->width;
			renderSize[1] = app->height;
			while (mask > 0) {
				if ( !(renderSize[0] & mask) && !(renderSize[1] & mask)) {
					break;
				}
				mask = mask>>1;
			}
			animCfg.resolutionGranularity = (GLsizei)(mask+1);
			animCfg.adjustToAspect = false;
			animCtrl.Prepare((GLsizei)renderSize[0], (GLsizei)renderSize[1]);
			modifiedHistory = true;
			modified = true;
			renderSize[0] = -1;
			renderSize[1] = -1;
		}
	}
	if (ImGui::TreeNodeEx("Output")) {
		ImGui::BeginDisabled(disabled);
		static bool forceFixedTimestep = true;
		static bool withLabel = false;
		static bool exitAfter = false;
		ImGui::SeparatorText("Output to Files");
		ImGui::InputText("filename prefix", outputFilename, sizeof(outputFilename));
		ImGui::Checkbox("force fixed timestep", &forceFixedTimestep);
		ImGui::Checkbox("render text labels into images", &withLabel);
		ImGui::Checkbox("exit application when finished", &exitAfter);

		if (ImGui::BeginTable("outputbuttonssplit", 3)) {
			ImGui::TableNextColumn();
			if (ImGui::Button("Render Animation", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.ResetAnimation();
				if (forceFixedTimestep) {
					animCtrl.SetAnimSpeed(fixedTimestep/1000.0 * speedup);
				}
				animCtrl.Play();
				cfg.outputFrames = outputFilename;
				cfg.exitAfterOutputFrames = exitAfter;
				cfg.withGUI = withLabel;
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Render From Here", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				animCtrl.ResetFrameCounter();
				if (forceFixedTimestep) {
					animCtrl.SetAnimSpeed(fixedTimestep/1000.0 * speedup);
				}
				animCtrl.Play();
				cfg.outputFrames = outputFilename;
				cfg.exitAfterOutputFrames = exitAfter;
				cfg.withGUI = withLabel;
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Save current frame", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
				static unsigned long currentFrameIdx = 0;
				saveCurrentFrame(animCtrl, outputFilename, "current_", currentFrameIdx++);
			}
			ImGui::EndTable();
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}
	ImGui::End();

	if (modifiedTransform) {
		vis.UpdateTransform();
	}
	if (modified) {
		vis.UpdateConfig();
	}
	if (modifiedHistory) {
		//size_t curTrackIdx = animCtrl.GetCurrentTrackIndex();
		animCtrl.RestoreHistory();
	}
	if (modified) {
		animCtrl.RefreshCurrentTrack(modifiedHistory);
	}

	if (showTrackManager) {
		drawTrackManager(app, animCtrl, vis, &showTrackManager);
	}
	if (app->fileDialog && app->fileDialog->Visible()) {
		if (app->fileDialog->Draw()) {
			GLsizei w = vis.GetWidth();
			GLsizei h = vis.GetHeight();
			if (w < 1) {
				w = (GLsizei)app->width;
			}
			if (h < 1) {
				h = (GLsizei)app->height;
			}
			animCtrl.Prepare(w,h);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}

	}
	//const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	first  = false;
}
#endif

/* This draws the complete scene for a single eye */
static void
drawScene(MainApp *app, AppConfig& cfg)
{
	gpxvis::CAnimController& animCtrl = app->animCtrl;
	gpxvis::CVis& vis = animCtrl.GetVis();
	const gpxvis::CVis::TConfig visCfg = vis.GetConfig();

#ifdef GPXVIS_WITH_IMGUI
	if ((app->flags & APP_HAVE_IMGUI ) && cfg.outputFrames && cfg.withGUI && animCtrl.IsPrepared()) {
		float scale = 2.0f;
		// Render some stuff to the image itself
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, vis.GetImageFBO());

		ImGui_ImplOpenGL3_NewFrame();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)vis.GetWidth()/scale, (float)vis.GetHeight()/scale);
		io.DisplayFramebufferScale = ImVec2(scale, scale);
		io.DeltaTime = 1.0e-10f;
		//ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0,0));
		ImGui::SetNextWindowSize(ImVec2(vis.GetWidth()/scale, vis.GetHeight()/scale));

		drawTrackStatus(animCtrl);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
#endif

	/* set the viewport (might have changed since last iteration) */
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, app->width, app->height);

	glClearColor(visCfg.colorBackground[0], visCfg.colorBackground[1], visCfg.colorBackground[2], visCfg.colorBackground[3]);
	glClear(GL_COLOR_BUFFER_BIT); /* clear the buffers */

	if (animCtrl.IsPrepared()) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, vis.GetImageFBO());
		glBlitFramebuffer(0,0, vis.GetWidth(), vis.GetHeight(), app->mainWidthOffset, app->mainHeightOffset, app->mainWidthOffset + app->mainWidth, app->mainHeightOffset + app->mainHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}

#ifdef GPXVIS_WITH_IMGUI
	if (!cfg.outputFrames && (app->flags & APP_HAVE_IMGUI)) {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		/*
		ImGui::SetNextWindowPos(ImVec2(widthOffset, heightOffset));
		ImGui::SetNextWindowSize(ImVec2(newWidth, newHeight));
		drawTrackStatus(animCtrl);
		*/
		drawMainWindow(app, cfg, animCtrl, vis);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
#endif
}


/* The main drawing function. This is responsible for drawing the next frame,
 * it is called in a loop as long as the application runs */
static bool
displayFunc(MainApp *app, AppConfig& cfg)
{
	// Render an animation frame
	bool cycleFinished = app->animCtrl.UpdateStep(app->timeDelta);
	drawScene(app, cfg);

	if (cfg.outputFrames) {
		gpximg::CImg img;
		saveCurrentFrame(app->animCtrl, cfg.outputFrames);
		if (cycleFinished) {
			cfg.outputFrames = NULL;
			if (cfg.exitAfterOutputFrames) {
				return false;
			}
		}
	}

	/* finished with drawing, swap FRONT and BACK buffers to show what we
	 * have rendered */
	glfwSwapBuffers(app->win);

	/* In DEBUG builds, we also check for GL errors in the display
	 * function, to make sure no GL error goes unnoticed. */
	GL_ERROR_DBG("display function");
	return true;
}

/****************************************************************************
 * MAIN LOOP                                                                *
 ****************************************************************************/

/* The main loop of the application. This will call the display function
 *  until the application is closed. This function also keeps timing
 *  statistics. */
static void mainLoop(MainApp *app, AppConfig& cfg)
{
	unsigned int frame=0;
	double start_time=glfwGetTime();
	double last_time=start_time;

	gpxutil::info("entering main loop");
	while (!glfwWindowShouldClose(app->win)) {
		/* update the current time and time delta to last frame */
		double now=glfwGetTime();
		app->timeDelta = now - app->timeCur;
		app->timeCur = now;

		/* update FPS estimate at most once every second */
		double elapsed = app->timeCur - last_time;
		if (elapsed >= 1.0) {
			char WinTitle[80];
			app->avg_frametime=1000.0 * elapsed/(double)frame;
			app->avg_fps=(double)frame/elapsed;
			last_time=app->timeCur;
			frame=0;
			/* update window title */
			mysnprintf(WinTitle, sizeof(WinTitle), APP_TITLE "   /// AVG: %4.2fms/frame (%.1ffps)", app->avg_frametime, app->avg_fps);
			glfwSetWindowTitle(app->win, WinTitle);
			gpxutil::info("frame time: %4.2fms/frame (%.1ffps)",app->avg_frametime, app->avg_fps);
		}

		/* This is needed for GLFW event handling. This function
		 * will call the registered callback functions to forward
		 * the events to us. */
		glfwPollEvents();

		/* calculate position of the main framebuffer */
		updateMainFramebufferCoords(app);

		/* process further inputs */
		processInputs(app);

		/* call the display function */
		if (!displayFunc(app, cfg)) {
			break;
		}
		app->resized = false;
		app->frame++;
		frame++;
		if (cfg.frameCount && app->frame >= cfg.frameCount) {
			break;
		}
	}
	gpxutil::info("left main loop\n%u frames rendered in %.1fs seconds == %.1ffps",
		app->frame,(app->timeCur-start_time),
		(double)app->frame/(app->timeCur-start_time) );
}

/****************************************************************************
 * SIMPLE COMMAND LINE PARSER                                               *
 ****************************************************************************/

void parseCommandlineArgs(AppConfig& cfg, MainApp& app, int argc, char**argv)
{
	gpxvis::CAnimController::TAnimConfig& animCfg = app.animCtrl.GetAnimConfig();
	//gpxvis::CVis::TConfig& visCfg = app.animCtrl.GetVis().GetConfig();

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fullscreen")) {
			cfg.fullscreen = true;
			cfg.decorated = false;
		} else if (!strcmp(argv[i], "--undecorated")) {
			cfg.decorated = false;
		} else if (!strcmp(argv[i], "--gl-debug-sync")) {
			cfg.debugOutputSynchronous = true;
		} else if (!strcmp(argv[i], "--no-gui")) {
			cfg.withGUI = false;
		} else if (!strcmp(argv[i], "--with-gui")) {
			cfg.withGUI = true;
		} else if (!strcmp(argv[i], "--paused")) {
			animCfg.paused = true;
		} else {
			bool unhandled = false;
			if (i + 1 < argc) {
				if (!strcmp(argv[i], "--width")) {
					cfg.width = (int)strtol(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--height")) {
					cfg.height = (int)strtol(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--x")) {
					cfg.posx = (int)strtol(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--y")) {
					cfg.posy = (int)strtol(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--frameCount")) {
					cfg.frameCount = (unsigned)strtoul(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--gl-debug-level")) {
					cfg.debugOutputLevel = (DebugOutputLevel)strtoul(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--output-frames")) {
					cfg.outputFrames = argv[++i];
					cfg.withGUI = false;
				} else if (!strcmp(argv[i], "--output-fps")) {
					double fps = strtod(argv[++i], NULL);
					app.animCtrl.SetAnimSpeed(1.0/fps);
				} else if (!strcmp(argv[i], "--track-speed")) {
					animCfg.trackSpeed = strtod(argv[++i], NULL) * 3600.0;
				} else if (!strcmp(argv[i], "--fade-time")) {
					animCfg.fadeoutTime = strtod(argv[++i], NULL);
				} else if (!strcmp(argv[i], "--history-mode")) {
					animCfg.historyMode = (gpxvis::CAnimController::TBackgroundMode)strtol(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--neighborhood-mode")) {
					animCfg.neighborhoodMode = (gpxvis::CAnimController::TBackgroundMode)strtol(argv[++i], NULL, 10);
				} else if (!strcmp(argv[i], "--switch-to")) {
					cfg.switchTo = (int)strtol(argv[++i], NULL, 10);
				} else {
					unhandled = true;
				}
			} else {
				unhandled = true;
			}
			if (unhandled) {
				app.animCtrl.AddTrack(argv[i]);
			}
		}
	}
}

/****************************************************************************
 * PROGRAM ENTRY POINT                                                      *
 ****************************************************************************/

#ifdef WIN32
#define REALMAIN main_utf8
#else
#define REALMAIN main
#endif

int REALMAIN (int argc, char **argv)
{
	AppConfig cfg;	/* the generic configuration */
	MainApp app;	/* the cube application stata stucture */
#ifdef GPXVIS_WITH_IMGUI
	filedialog::CFileDialogTracks fileDialog(app.animCtrl);
#endif

	parseCommandlineArgs(cfg, app, argc, argv);

	if (initMainApp(&app, cfg)) {
#ifdef GPXVIS_WITH_IMGUI
		app.fileDialog = &fileDialog;
#endif
		/* initialization succeeded, enter the main loop */
		mainLoop(&app, cfg);
	}
	/* clean everything up */
	destroyMainApp(&app);

	return 0;
}

#ifdef WIN32
int wmain(int argc, wchar_t **argv)
{
	std::vector<std::string> args_utf8;
	std::vector<char*> argv_utf8;
	int i;

	for (i = 0; i < argc; i++) {
		args_utf8.push_back(gpxutil::wideToUtf8(std::wstring(argv[i])));
	}
	for (i = 0; i < argc; i++) {
		argv_utf8.push_back((char*)args_utf8[i].c_str());
	}
	return REALMAIN(argc, argv_utf8.data());
}
#endif // WIN32
