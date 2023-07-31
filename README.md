# gpxvis
Qucik and dirty GPS track visualization with OpenGL. This is only
a hack, not a real project. It is provided here in case anyone
may find it useful.

## Caveats:

* gpx reading doesn't use an XML parser, it just collects the value out of the raw text, works well enough with real world files, but may break with yours
* render resolution is fixed to inital window dimensions, as the main purpose is generating a series of images using the `--output-frames' parameter
* lots of parameters to tweak the output only controlled in source code
* used GL version is probably 4.5, but 4.6 is requested in any case
* only Linux Makefile is provided, building on other platforms should work without much hassle, but I don't care
* probably a lot more...


## Requirements

* OpenGL 4.6 core profile
* GLFW3 (http://www.glfw.org/) is used as a multi-platform library for creating windows and OpenGL contexts and for event handling
* Dear ImGui (https://github.com/ocornut/imgui) is included as a git submodule

# License

See [LICENSE file](./LICENSE)
