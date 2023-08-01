#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "gpx.h"
#include "util.h"
#include "vis.h"

#ifdef GPXVIS_WITH_IMGUI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
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
		outputFrames(NULL)
	{
#ifndef NDEBUG
		debugOutputLevel = DEBUG_OUTPUT_ALL;
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
	int width, height;
	unsigned int flags;

	/* timing */
	double timeCur, timeDelta;
	double avg_frametime;
	double avg_fps;
	unsigned int frame;

	// actual visualizer
	gpxvis::CAnimController animCtrl;
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
static void initGLState(const AppConfig&cfg)
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
}

/****************************************************************************
 * WINDOW-RELATED CALLBACKS                                                 *
 ****************************************************************************/

/* This function is registered as the framebuffer size callback for GLFW,
 * so GLFW will call this whenever the window is resized. */
static void callback_Resize(GLFWwindow *win, int w, int h)
{
	MainApp *app=(MainApp*)glfwGetWindowUserPointer(win);
	gpxutil::info("new framebuffer size: %dx%d pixels",w,h);

	/* store curent size for later use in the main loop */
	app->width=w;
	app->height=h;

	/* we _could_ directly set the viewport here ... */
}

/* This function is registered as the keayboard callback for GLFW, so GLFW
 * will call this whenever a key is pressed. */
static void callback_Keyboard(GLFWwindow *win, int key, int scancode, int action, int mods)
{
	//MainApp *app=(MainApp*)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) {
		switch(key) {
			case GLFW_KEY_ESCAPE:
				glfwSetWindowShouldClose(win, 1);
				break;
		}
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
bool initMainApp(MainApp *app, const AppConfig& cfg)
{
	int w, h, x, y;
	bool debugCtx=(cfg.debugOutputLevel > DEBUG_OUTPUT_DISABLED);

	/* Initialize the app structure */
	app->win=NULL;
	app->flags=0;
	app->avg_frametime=-1.0;
	app->avg_fps=-1.0;
	app->frame = 0;

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
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
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
		gpxutil::warn("failed to get window with OpenGL 4.6 core context");
		return false;
	}

	app->width = w;
	app->height = h;

	if (!monitor) {
		glfwSetWindowPos(app->win, x, y);
	}

	/* store a pointer to our application context in GLFW's window data.
	 * This allows us to access our data from within the callbacks */
	glfwSetWindowUserPointer(app->win, app);
	/* register our callbacks */
	glfwSetFramebufferSizeCallback(app->win, callback_Resize);
	glfwSetKeyCallback(app->win, callback_Keyboard);

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

	if (!GLAD_GL_VERSION_4_6) {
		gpxutil::warn("failed to load at least GL 4.6 functions via GLAD");
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
	initGLState(cfg);

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

static void drawMainWindow(MainApp* app, gpxvis::CAnimController& animCtrl, gpxvis::CVis& vis)
{
	bool modified = false;
	bool modifiedHistory = false;

	const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x, main_viewport->WorkPos.y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_FirstUseEver);

	ImGui::Begin("gpxvis");
	size_t cnt = animCtrl.GetTrackCount();
	char buf[16];
	if (cnt > 0) {
		mysnprintf(buf,sizeof(buf), "#%d/%d", (int)animCtrl.GetCurrentTrackIndex()+1, (int)cnt);
	} else {
		mysnprintf(buf,sizeof(buf), "(none)");
	}
	buf[sizeof(buf)-1]=0;


	gpxvis::CAnimController::TAnimConfig& animCfg = animCtrl.GetAnimConfig();
	if (ImGui::BeginTable("tracksplit", 3)) {
		ImGui::TableNextColumn();
		if (ImGui::Button("|<<", ImVec2(ImGui::GetContentRegionAvail().x * 0.5, 0.0f))) {
			animCtrl.SwitchToTrack(0);
			modifiedHistory = animCfg.clearAtCycle;
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
		ImGui::SetCursorPosX(ImGui::GetCursorPosX()+0.5*(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(buf).x));
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
	if (ImGui::BeginTable("controls", 3)) {
		ImGui::TableNextColumn();
		if (ImGui::Button(animCfg.paused?"Play":"Pause", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCfg.paused = !animCfg.paused;
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Clear All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			vis.Clear();
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}
		ImGui::TableNextColumn();
		if (ImGui::Button("Add All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			animCtrl.RestoreHistoryUpTo(cnt);
		}
		ImGui::EndTable();
	}
	static const gpx::CTrack defaultTrack;
	const gpx::CTrack *curTrack = &defaultTrack;
	if (cnt > 0) {
		curTrack = &animCtrl.GetCurrentTrack();
	}
	ImGui::Text("File: %s", curTrack->GetFilename());
	if (ImGui::BeginTable("infosplit", 3)) {
		ImGui::TableNextColumn();
		ImGui::Text("%s", curTrack->GetInfo());
		ImGui::TableNextColumn();
		ImGui::Text("Len: %.1f (%dpts)", curTrack->GetLength(), (int)curTrack->GetCount());
		ImGui::TableNextColumn();
		int thrs = (int)floor(curTrack->GetDuration() / 3600.0);
		int tmin = (int)floor((curTrack->GetDuration() - 3600.0*thrs) / 60.0);
		ImGui::Text("Dur: %02d:%02d", thrs, tmin);
		ImGui::EndTable();
	}

	if (ImGui::TreeNodeEx("Animation Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SeparatorText("Animation Position");
		float trackUpTo = animCtrl.GetCurrentTrackUpTo();
		float trackTime = (float)animCtrl.GetCurrentTrackPos();
		float trackPos = (float)curTrack->GetDistanceAt(trackUpTo);
		if (trackUpTo < 0.0f) {
			trackUpTo = (float)curTrack->GetCount();
		}
		if (ImGui::SliderFloat("track time", &trackTime, 0.0f, curTrack->GetDuration()-1.0f, "%.1f")) {
			animCtrl.SetCurrentTrackPos((double)trackTime);
		}
		if (ImGui::SliderFloat("track position", &trackPos, 0.0f, curTrack->GetLength(), "%.001f")) {
			trackUpTo = curTrack->GetPointByDistance((double)trackPos);
			trackTime = curTrack->GetDurationAt(trackUpTo);
			animCtrl.SetCurrentTrackPos((double)trackTime);
		}
		if (ImGui::SliderFloat("track index", &trackUpTo, 0.0f, (float)curTrack->GetCount(), "%.01f")) {
			trackTime = curTrack->GetDurationAt(trackUpTo);
			animCtrl.SetCurrentTrackPos((double)trackTime);
		}
		float fadeRatio = animCtrl.GetCurrentFadeRatio();
		if (ImGui::SliderFloat("fade-out", &fadeRatio, 0.0f, 1.0f, "%.2f")) {
			animCtrl.SetCurrentFadeRatio(fadeRatio);
		}

		ImGui::SeparatorText("Animation Speed");
		static int timestepMode = 0;
		static float fixedTimestep = 1000.0f/60.0f;
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

		float trackSpeed = animCfg.trackSpeed/3600.0f;
		if (ImGui::SliderFloat("track speed", &trackSpeed, 0.0f, 100.0, "%.3fhrs/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoInput)) {
			animCfg.trackSpeed = trackSpeed * 3600.0;
		}
		float fadeout = animCfg.fadeoutTime;
		if (ImGui::SliderFloat("fade-out time", &fadeout, 0.0f, 10.0, "%.2fs", ImGuiSliderFlags_Logarithmic)) {
			animCfg.fadeoutTime = fadeout;
		}
		static float speedup = 1.0f;
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
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Visualization Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SeparatorText("Track Colors");
		gpxvis::CVis::TConfig& cfg=vis.GetConfig();
		if (ImGui::ColorEdit3("track history", cfg.colorBase)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::ColorEdit3("gradient new", &cfg.colorGradient[0][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("gradient mid", &cfg.colorGradient[1][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("gradient old", &cfg.colorGradient[2][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("current point", &cfg.colorGradient[3][0])) {
			modified = true;
		}
		if (ImGui::ColorEdit3("background", cfg.colorBackground)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::Button("Reset Colors", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			cfg.ResetColors();
			modified = true;
			modifiedHistory = true;
		}
		ImGui::SeparatorText("Line Parameters");
		if (ImGui::SliderFloat("track width", &cfg.trackWidth, 0.0f, 32.0f)) {
			modified = true;
		}
		if (ImGui::SliderFloat("point size", &cfg.trackPointWidth, 0.0f, 32.0f)) {
			modified = true;
		}
		if (ImGui::SliderFloat("neighborhood width", &cfg.neighborhoodWidth, 0.0f, 32.0f)) {
			modified = true;
			modifiedHistory = true;
		}
		if (ImGui::Button("Reset Line Parameters", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			cfg.ResetWidths();
			modified = true;
			modifiedHistory = true;
		}
		ImGui::TreePop();
	}

	if (modified) {
		vis.UpdateConfig();
		animCtrl.RefreshCurrentTrack();
	}
	if (modifiedHistory) {
		size_t curTrackIdx = animCtrl.GetCurrentTrackIndex();
		animCtrl.RestoreHistoryUpTo(curTrackIdx);
	}
	ImGui::End();
	//const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
}
#endif

/* This draws the complete scene for a single eye */
static void
drawScene(MainApp *app, const AppConfig& cfg)
{
	gpxvis::CAnimController& animCtrl = app->animCtrl;
	gpxvis::CVis& vis = animCtrl.GetVis();

	GLsizei w = vis.GetWidth();
	GLsizei h = vis.GetHeight();

#ifdef GPXVIS_WITH_IMGUI
	if ((app->flags & APP_HAVE_IMGUI ) && cfg.outputFrames && animCtrl.IsPrepared()) {
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

	const gpxvis::CVis::TConfig& visCfg = vis.GetConfig();
	glClearColor(visCfg.colorBackground[0], visCfg.colorBackground[1], visCfg.colorBackground[2], visCfg.colorBackground[3]);
	glClear(GL_COLOR_BUFFER_BIT); /* clear the buffers */

	GLsizei widthOffset=0;
	GLsizei heightOffset=0;
	GLsizei newWidth = app->width;
	GLsizei newHeight = app->height;

	if (animCtrl.IsPrepared()) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, vis.GetImageFBO());

		float winAspect = (float)app->width / (float)app->height;
		float imgAspect = (float)w/(float)h;
		if (winAspect > imgAspect) {
			float scale = (float)app->height / (float)h;
			newWidth = (GLsizei)(scale * w + 0.5f);
			widthOffset = (app->width - newWidth);
		} else {
			float scale = (float)app->width / (float)w;
			newHeight = (GLsizei)(scale * h + 0.5f);
			heightOffset = (app->height- newHeight) / 2;
		}
		glBlitFramebuffer(0,0,w,h, widthOffset,heightOffset, widthOffset+newWidth, heightOffset+newHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

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
		
		drawMainWindow(app, animCtrl, vis);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
#endif
}


/* The main drawing function. This is responsible for drawing the next frame,
 * it is called in a loop as long as the application runs */
static bool
displayFunc(MainApp *app, const AppConfig& cfg)
{
	// Render an animation frame
	bool cycleFinished = app->animCtrl.UpdateStep(app->timeDelta);
	drawScene(app, cfg);

	if (cfg.outputFrames) {
		gpximg::CImg img;
		if (app->animCtrl.GetVis().GetImage(img)) {
			char buf[4096];
			mysnprintf(buf, sizeof(buf), "%s%06lu.tga", cfg.outputFrames, app->animCtrl.GetFrame());
			img.WriteTGA(buf);
		}
		if (cycleFinished) {
			return false;
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
static void mainLoop(MainApp *app, const AppConfig& cfg)
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

		/* call the display function */
		if (!displayFunc(app, cfg)) {
			break;
		}
		app->frame++;
		frame++;
		if (cfg.frameCount && app->frame >= cfg.frameCount) {
			break;
		}
		/* This is needed for GLFW event handling. This function
		 * will call the registered callback functions to forward
		 * the events to us. */
		glfwPollEvents();
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

int main (int argc, char **argv)
{
	AppConfig cfg;	/* the generic configuration */
	MainApp app;	/* the cube application stata stucture */

	parseCommandlineArgs(cfg, app, argc, argv);

	if (initMainApp(&app, cfg)) {
		/* initialization succeeded, enter the main loop */
		mainLoop(&app, cfg);
	}
	/* clean everything up */
	destroyMainApp(&app);

	return 0;
}

