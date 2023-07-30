#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "gpx.h"
#include "util.h"
#include "vis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* define mysnprintf to be either snprintf (POSIX) or sprintf_s (MS Windows) */
#ifdef WIN32
#define mysnprintf sprintf_s
#else
#define mysnprintf snprintf
#endif

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

	AppConfig() :
		posx(100),
		posy(100),
		width(1920),
		height(1200),
		decorated(true),
		fullscreen(false),
		frameCount(0),
		debugOutputLevel(DEBUG_OUTPUT_DISABLED),
		debugOutputSynchronous(false)
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
	glfwSwapInterval(1);

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

	/* initialize the GL context */
	initGLState(cfg);

	// TODO ...
	if (!app->animCtrl.Prepare(app->width,app->height)) {
		gpxutil::warn("failed to initialize animation controller");
		return false;
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
			}
			glfwDestroyWindow(app->win);
		}
		glfwTerminate();
	}
}

/****************************************************************************
 * DRAWING FUNCTION                                                         *
 ****************************************************************************/

/* This draws the complete scene for a single eye */
static void
drawScene(MainApp *app)
{
	// TODO ...
	app->animCtrl.UpdateStep(app->timeDelta);

	/* set the viewport (might have changed since last iteration) */
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	//glViewport(0, 0, app->width, app->height);

	glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT); /* clear the buffers */

	const gpxvis::CVis& vis = app->animCtrl.GetVis();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, vis.GetImageFBO());

	float winAspect = (float)app->width / (float)app->height;
	GLsizei w = vis.GetWidth();
	GLsizei h = vis.GetHeight();
	float imgAspect = (float)w/(float)h;
	if (winAspect > imgAspect) {
		float scale = (float)app->height / (float)h;
		GLsizei newWidth = (GLsizei)(scale * w + 0.5f);
		GLsizei offset = (app->width - newWidth) / 2;

		glBlitFramebuffer(0,0,w,h, offset,0,offset+newWidth, app->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	} else {
		float scale = (float)app->width / (float)w;
		GLsizei newHeight = (GLsizei)(scale * h + 0.5f);
		GLsizei offset = (app->height- newHeight) / 2;

		glBlitFramebuffer(0,0,w,h, 0,offset,app->width, offset+newHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}


/* The main drawing function. This is responsible for drawing the next frame,
 * it is called in a loop as long as the application runs */
static void
displayFunc(MainApp *app, const AppConfig& cfg)
{
	drawScene(app);

	/* finished with drawing, swap FRONT and BACK buffers to show what we
	 * have rendered */
	glfwSwapBuffers(app->win);

	/* In DEBUG builds, we also check for GL errors in the display
	 * function, to make sure no GL error goes unnoticed. */
	GL_ERROR_DBG("display function");
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
		displayFunc(app, cfg);
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

