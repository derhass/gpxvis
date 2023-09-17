#include "util.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef WIN32
#include <Windows.h>
#endif

namespace gpxutil {

/****************************************************************************
 * SIMPLE MESSAGES                                                          *
 ****************************************************************************/

/* Print a info message to stdout, use printf syntax. */
extern void info (const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stdout,format, args);
	va_end(args);
	fputc('\n', stdout);
}

/* Print a warning message to stderr, use printf syntax. */
extern void warn (const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr,format, args);
	va_end(args);
	fputc('\n', stderr);
}

/****************************************************************************
 * GL ERRORS                                                                *
 ****************************************************************************/

/* Check for GL errors. If ignore is not set, print a warning if an error was
 * encountered.
 * Returns GL_NO_ERROR if no errors were set. */
extern GLenum getGLError(const char *action, bool ignore, const char *file, const int line)
{
	GLenum e,err=GL_NO_ERROR;

	do {
		e=glGetError();
		if ( (e != GL_NO_ERROR) && (!ignore) ) {
			err=e;
			if (file)
				fprintf(stderr,"%s:",file);
			if (line)
				fprintf(stderr,"%d:",line);
			warn("GL error 0x%x at %s",(unsigned)err,action);
		}
	} while (e != GL_NO_ERROR);
	return err;
}

/****************************************************************************
 * GL DEBUG MESSAGES                                                        *
 ****************************************************************************/

/* Newer versions of the GL support the generation of human-readable messages
   for GL errors, performance warnings and hints. These messages are
   forwarded to a debug callback which has to be registered with the GL
   context. Debug output may only be available in special debug context... */

/* translate the debug message "source" enum to human-readable string */
extern const char *
translateDebugSourceEnum(GLenum source)
{
	const char *s;

	switch (source) {
		case GL_DEBUG_SOURCE_API:
			s="API";
			break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
			s="window system";
			break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER:
			s="shader compiler";
			break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:
			s="3rd party";
			break;
		case GL_DEBUG_SOURCE_APPLICATION:
			s="application";
			break;
		case GL_DEBUG_SOURCE_OTHER:
			s="other";
			break;
		default:
			s="[UNKNOWN SOURCE]";
	}

	return s;
}

/* translate the debug message "type" enum to human-readable string */
extern const char *
translateDebugTypeEnum(GLenum type)
{
	const char *s;

	switch (type) {
		case GL_DEBUG_TYPE_ERROR:
			s="error";
			break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
			s="deprecated";
			break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
			s="undefined behavior";
			break;
		case GL_DEBUG_TYPE_PORTABILITY:
			s="portability";
			break;
		case GL_DEBUG_TYPE_PERFORMANCE:
			s="performance";
			break;
		case GL_DEBUG_TYPE_OTHER:
			s="other";
			break;
		default:
			s="[UNKNOWN TYPE]";
	}
	return s;
}

/* translate the debug message "xeverity" enum to human-readable string */
extern const char *
translateDebugSeverityEnum(GLenum severity)
{
	const char *s;

	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH:
			s="high";
			break;
		case GL_DEBUG_SEVERITY_MEDIUM:
			s="medium";
			break;
		case GL_DEBUG_SEVERITY_LOW:
			s="low";
			break;
		case GL_DEBUG_SEVERITY_NOTIFICATION:
			s="notification";
			break;
		default:
			s="[UNKNOWN SEVERITY]";
	}

	return s;
}

/****************************************************************************
 * UTILITY FUNCTIONS: print information about the GL context                *
 ****************************************************************************/

/* Print info about the OpenGL context */
extern void printGLInfo()
{
	/* get infos about the GL implementation */
	info("OpenGL: %s %s %s",
			glGetString(GL_VENDOR),
			glGetString(GL_RENDERER),
			glGetString(GL_VERSION));
	info("OpenGL Shading language: %s",
			glGetString(GL_SHADING_LANGUAGE_VERSION));
}

/* List all supported GL extensions */
extern void listGLExtensions()
{
	GLint num=0;
	GLuint i;
	glGetIntegerv(GL_NUM_EXTENSIONS, &num);
	info("GL extensions supported: %d", num);
	if (num < 1) {
		return;
	}

	for (i=0; i<(GLuint)num; i++) {
		const GLubyte *ext=glGetStringi(GL_EXTENSIONS,i);
		if (ext) {
			info("  %s",ext);
		}
	}
}

/****************************************************************************
 * SHADER COMPILATION AND LINKING                                           *
 ****************************************************************************/

/* Print the gpxutil::info log of the shader compiler/linker.
 * If program is true, obj is assumed to be a program object, otherwise, it
 * is assumed to be a shader object.
 */
extern void printInfoLog(GLuint obj, bool program)
{
	char log[16384];
	if (program) {
		glGetProgramInfoLog(obj, sizeof(log), 0, log);
	} else {
		glGetShaderInfoLog(obj, sizeof(log), 0, log);
	}
	/* technically, this is not strictly necessary as the GL implementation
	 * is required to properly terminate the string, but we never trust
	 * other code and make sure the string is terminated before running out
	 * of the buffer. */
	log[sizeof(log)-1]=0;
	fprintf(stderr,"%s\n",log);
}

/* Create a new shader object, attach "source" as source string,
 * and compile it.
 * Returns the name of the newly created shader object, or 0 in case of an
 * error.
 */
extern GLuint shaderCreateAndCompile(GLenum type, const GLchar *source)
{
	GLuint shader=0;
	GLint status;

	shader=glCreateShader(type);
	gpxutil::info("created shader object %u",shader);
	glShaderSource(shader, 1, (const GLchar**)&source, NULL);
	gpxutil::info("compiling shader object %u",shader);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		gpxutil::warn("Failed to compile shader");
		printInfoLog(shader,false);
		glDeleteShader(shader);
		shader=0;
	}

	return shader;
}

/* Create a new shader object by loading a file, and compile it.
 * Returns the name of the newly created shader object, or 0 in case of an
 * error.
 */
extern GLuint shaderCreateFromFileAndCompile(GLenum type, const char *filename)
{

	gpxutil::info("loading shader file '%s'",filename);
	FILE *file = fopen(filename, "rt");
	if(!file) {
		gpxutil::warn("Failed to open shader file '%s'", filename);
		return 0;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	GLchar *source = (GLchar*)malloc(size+1);
	if (!source) {
		gpxutil::warn("Failed to allocate memory for shader file '%s'", filename);
		fclose(file);
		return 0;
	}
	fseek(file, 0, SEEK_SET);
	source[fread(source, 1, size, file)] = 0;
	fclose(file);

	GLuint shader=shaderCreateAndCompile(type, source);
	free(source);
	if (!shader) {
		gpxutil::warn("Failed to compile shader '%s'", filename);
	}
	return shader;
}

/* Create a program by linking a vertex and fragment shader object. The shader
 * objects should already be compiled.
 * Returns the name of the newly created program object, or 0 in case of an
 * error.
 */
extern GLuint programCreate(GLuint vertex_shader, GLuint fragment_shader)
{
	GLuint program=0;
	GLint status;

	program=glCreateProgram();
	gpxutil::info("created program %u",program);

	if (vertex_shader)
		glAttachShader(program, vertex_shader);
	if (fragment_shader)
		glAttachShader(program, fragment_shader);

	/* hard-code the attribute indices for the attributeds we use */
	glBindAttribLocation(program, 0, "pos");
	glBindAttribLocation(program, 1, "nrm");
	glBindAttribLocation(program, 2, "clr");
	glBindAttribLocation(program, 3, "tex");

	/* hard-code the color number of the fragment shader output */
	glBindFragDataLocation(program, 0, "color");

	/* finally link the program */
	gpxutil::info("linking program %u",program);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		gpxutil::warn("Failed to link program!");
		printInfoLog(program,true);
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

/* Create a program object directly from vertex and fragment shader source
 * files.
 * Returns the name of the newly created program object, or 0 in case of an
 * error.
 */
extern GLuint programCreateFromFiles(const char *vs, const char *fs)
{
	GLuint id_vs=shaderCreateFromFileAndCompile(GL_VERTEX_SHADER, vs);
	GLuint id_fs=shaderCreateFromFileAndCompile(GL_FRAGMENT_SHADER, fs);
	GLuint program = 0;
	if (id_vs && id_fs) {
		program=programCreate(id_vs,id_fs);
	}
	/* Delete the shader objects. Since they are still in use in the
	 * program object, OpenGL will not destroy them internally until
	 * the program object is destroyed. The caller of this function
	 * does not need to care about the shader objects at all. */
	gpxutil::info("destroying shader object %u",id_vs);
	glDeleteShader(id_vs);
	gpxutil::info("destroying shader object %u",id_fs);
	glDeleteShader(id_fs);
	return program;
}

/****************************************************************************
 * AABBs                                                                    *
 ****************************************************************************/

CAABB::CAABB()
{
	Reset();
}

void CAABB::Reset()
{
	aabb[0] = 0.0f;
	aabb[3] =-1.0f;
}

void CAABB::Add(double x, double y, double z)
{
	if (IsValid()) {
		if (x < aabb[0]) {
			aabb[0] = x;
		} else if (x > aabb[3]) {
			aabb[3] = x;
		}
		if (y < aabb[1]) {
			aabb[1] = y;
		} else if (y > aabb[4]) {
			aabb[4] = y;
		}
		if (z < aabb[5]) {
			aabb[5] = z;
		} else if (z > aabb[5]) {
			aabb[5] = z;
		}
	} else {
		aabb[0] = aabb[3] = x;
		aabb[1] = aabb[4] = y;
		aabb[2] = aabb[5] = z;
	}
}

void CAABB::GetNormalizeScaleOffset(double scale[3], double offset[3]) const
{
	for (int i=0; i<3; i++) {
		double x = aabb[i+3] - aabb[i];
		if (x > 0.0) {
			scale[i] = 1.0 / x;
		} else {
			scale[i] = 1.0;
		}
		offset[i] = aabb[i];
	}
}

void CAABB::Enhance(double relative, double absolute)
{
	for (int i=0; i<3; i++) {
		double x = aabb[i+3] - aabb[i];
		if (x > 0.0) {
			double y = x * relative + absolute;
			double d = 0.5 * (y-x);
			aabb[i] -= d;
			aabb[i+3] += d;
		} else {
			aabb[i] = 0;
			aabb[i+3] = 1.0 * relative + absolute;
		}
	}
}

void CAABB::MergeWith(const CAABB& other)
{
	if (other.IsValid()) {
		const double *x = other.Get();
		Add(x[0],x[1],x[2]);
		Add(x[3],x[4],x[5]);
	}
}

double CAABB::GetAspect() const
{
	double w = aabb[3]-aabb[0];
	double h = aabb[4]-aabb[1];
	if (w > 0.0 || h > 0.0) {
		return w/h;
	}
	return 1.0;
}

bool CAABB::GetCenter(double center[3]) const
{
	if (IsValid()) {
		center[0] = 0.5 * aabb[0] + 0.5 * aabb[3];
		center[1] = 0.5 * aabb[1] + 0.5 * aabb[4];
		center[2] = 0.5 * aabb[2] + 0.5 * aabb[5];
		return true;
	}
	center[0] = center[1] = center[2] = 0.0;
	return false;
}

/****************************************************************************
 * MISC UTILITIES                                                           *
 ****************************************************************************/

/* round GLsizei to next multiple of base */
GLsizei roundNextMultiple(GLsizei value, GLsizei base)
{
	GLsizei rem = value % base;
	if (rem) {
		value += base - rem;
	}
	return value;
}

/* get duration in human-readable string format */
bool durationToString(double seconds, char *buffer, size_t bufSize)
{
	if (!buffer && bufSize < 1) {
		return false;
	}

	double minutes = floor(seconds / 60.0);
	if (minutes <= 0.5) {
		mysnprintf(buffer, bufSize, "%02.0fs", seconds);
	} else {
		seconds -= 60.0 * minutes;
		double hours = floor(minutes / 60.0);
		minutes -= 60.0 * hours;
		double days = floor(hours / 24.0);
		hours -= 24.0 * days;
		if (days <= 0.5) {
			mysnprintf(buffer, bufSize, "%02.0f:%02.0f:%02.0f", hours, minutes, seconds);
		} else {
			mysnprintf(buffer, bufSize, "%.0fdays %02.0f:%02.0f:%02.0f", days, hours, minutes, seconds);
		}
	}
	buffer[bufSize-1] = 0;
	return true;
}

#ifdef WIN32
/****************************************************************************
 * WINDOWS WIDE STRING <-> UTF8                                             *
 ****************************************************************************/

extern std::string wideToUtf8(const std::wstring& data)
{
	std::string result;
	if (data.length() > 0) {
		int len = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), (int)data.length(), NULL, 0, NULL, NULL);
		if (len > 0) {
			result.resize(len);
			WideCharToMultiByte(CP_UTF8, 0, data.c_str(), (int)data.length(), &result[0], len, NULL, NULL);
		}
	}
	return result;
}

extern std::wstring utf8ToWide(const std::string& data)
{
	std::wstring result;
	if (data.length() > 0) {
		int len = MultiByteToWideChar(CP_UTF8, 0, data.c_str(), (int)data.length(), NULL, 0);
		if (len > 0) {
			result.resize(len);
			MultiByteToWideChar(CP_UTF8, 0, data.c_str(), (int)data.length(), &result[0], len);
		}
	}
	return result;
}

#endif // WIN32


} // namespace gpxutil
