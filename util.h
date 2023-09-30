#ifndef GPXVIS_UTIL_H
#define GPXVIS_UTIL_H

#include <glad/gl.h>
#include <stddef.h>

/* define mysnprintf to be either snprintf (POSIX) or sprintf_s (MS Windows) */
#ifdef WIN32
#include <string>
#define mysnprintf sprintf_s
#else
#define mysnprintf snprintf
#endif

namespace gpxutil {

/****************************************************************************
 * SIMPLE MESSAGES                                                          *
 ****************************************************************************/

/* Print a info message to stdout, use printf syntax. */
extern void info (const char *format, ...);

/* Print a warning message to stderr, use printf syntax. */
extern void warn (const char *format, ...);

/****************************************************************************
 * GL ERRORS                                                                *
 ****************************************************************************/

/* Check for GL errors. If ignore is not set, print a warning if an error was
 * encountered.
 * Returns GL_NO_ERROR if no errors were set. */
extern GLenum getGLError(const char *action, bool ignore=false, const char *file=NULL, const int line=0);

/* helper macros:
 * define GL_ERROR_DBG() to be present only in DEBUG builds. This way, you can
 * add error checks at strategic places without influencing the performance
 * of the RELEASE build */
#ifdef NDEBUG
#define GL_ERROR_DBG(action) (void)0
#else
#define GL_ERROR_DBG(action) gpxutil::getGLError(action, false, __FILE__, __LINE__)
#endif

/* define BUFFER_OFFSET to specify offsets inside VBOs */
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

/****************************************************************************
 * GL DEBUG MESSAGES                                                        *
 ****************************************************************************/

/* Newer versions of the GL support the generation of human-readable messages
   for GL errors, performance warnings and hints. These messages are
   forwarded to a debug callback which has to be registered with the GL
   context. Debug output may only be available in special debug context... */

/* translate the debug message "source" enum to human-readable string */
extern  const char *
translateDebugSourceEnum(GLenum source);

/* translate the debug message "type" enum to human-readable string */
extern const char *
translateDebugTypeEnum(GLenum type);

/* translate the debug message "xeverity" enum to human-readable string */
extern const char *
translateDebugSeverityEnum(GLenum severity);

/****************************************************************************
 * UTILITY FUNCTIONS: print information about the GL context                *
 ****************************************************************************/

/* Print info about the OpenGL context */
extern void printGLInfo();

/* List all supported GL extensions */
extern  void listGLExtensions();

/****************************************************************************
 * SHADER COMPILATION AND LINKING                                           *
 ****************************************************************************/

/* Print the gpxutil::info log of the shader compiler/linker.
 * If program is true, obj is assumed to be a program object, otherwise, it
 * is assumed to be a shader object.
 */
extern void printInfoLog(GLuint obj, bool program);

/* Create a new shader object, attach "source" as source string,
 * and compile it.
 * Returns the name of the newly created shader object, or 0 in case of an
 * error.
 */
extern GLuint shaderCreateAndCompile(GLenum type, const GLchar *source);

/* Create a new shader object by loading a file, and compile it.
 * Returns the name of the newly created shader object, or 0 in case of an
 * error.
 */
extern GLuint shaderCreateFromFileAndCompile(GLenum type, const char *filename);

/* Create a program by linking a vertex and fragment shader object. The shader
 * objects should already be compiled.
 * Returns the name of the newly created program object, or 0 in case of an
 * error.
 */
extern GLuint programCreate(GLuint vertex_shader, GLuint fragment_shader);

/* Create a program object directly from vertex and fragment shader source
 * files.
 * Returns the name of the newly created program object, or 0 in case of an
 * error.
 */
extern GLuint programCreateFromFiles(const char *vs, const char *fs);

/****************************************************************************
 * AABBs                                                                    *
 ****************************************************************************/

class CAABB {
	public:
		CAABB();
		void Reset();

		void Add(double x, double y, double z);
	
		const double *Get() const {return aabb;}
		bool IsValid() const {return (aabb[0] <= aabb[3]);}
		void GetNormalizeScaleOffset(double scale[3], double offset[3]) const;
		void Enhance(double relative, double absolute);
		void MergeWith(const CAABB& other);
		double GetAspect() const;
		bool GetCenter(double center[3]) const;
		void InterpolateNormalized2D(const double normalized[2], double result[2]) const;

	private:
		double aabb[6];
};

/****************************************************************************
 * MANAGER FOR INTERNAL IDs                                                 *
 ****************************************************************************/

template <typename T>
class CInternalIDGenerator {
	public:
		CInternalIDGenerator() :
			internalIDCounter(1)
		{}

		T GenerateID() {return internalIDCounter++;}

	private:
		T internalIDCounter;
};

/****************************************************************************
 * MISC UTILITIES                                                           *
 ****************************************************************************/

/* round GLsizei to next multiple of base */
extern GLsizei roundNextMultiple(GLsizei value, GLsizei base);

/* get duration in human-readable string format */
extern bool durationToString(double seconds, char *buffer, size_t bufSize);

#ifdef WIN32
/****************************************************************************
 * WINDOWS WIDE STRING <-> UTF8                                             *
 ****************************************************************************/

extern std::string wideToUtf8(const std::wstring& data);
extern std::wstring utf8ToWide(const std::string& data);
#endif // WIN32

} // namespace gpxutil
#endif // GPXVIS_UTIL_H
