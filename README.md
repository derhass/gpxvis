# gpxvis
Quick and dirty GPS track visualization with OpenGL. This is only
a hack, not a real project. It is provided here in case anyone
may find it useful.

## Caveats:

* gpx reading doesn't use an XML parser, it just collects the value out of the raw text, works well enough with real world files, but may break with yours
* Linux Makefile is provided, building on other platforms should work without much hassle, but I don't care much
  * A VisualStudio project file for Windows builds is included, you have to manually download the GLFW binaries (or bild them yourself). By default, it uses the `GLFW\include` subdirectory for the headers, and `GLFW\lib-vc2017` for the libraries (for both 32bit and 64bit builds, choose just the one you need), you may want to modify the project settings for your liking. And no, I will not provide CMake project files.
* probably a lot more...


## Requirements

* OpenGL 4.5 core profile
* GLFW3 (http://www.glfw.org/) is used as a multi-platform library for creating windows and OpenGL contexts and for event handling
* Dear ImGui (https://github.com/ocornut/imgui) is included as a git submodule, but can be compiled without it

# License

See [LICENSE file](./LICENSE)
