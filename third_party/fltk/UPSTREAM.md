# FLTK upstream record

- Upstream: https://github.com/fltk/fltk
- Release: 1.4.5 (`release-1.4.5`)
- Source archive: https://github.com/fltk/fltk/archive/refs/tags/release-1.4.5.tar.gz
- SHA-256: `7715e69ce081fa9ce6da48bb0dd3b07a4cf2cf937813814c04272f36fff593ea`
- Retrieved: 2026-07-15
- License: FLTK License (LGPL 2.0 with the static-linking exception), in `COPYING`

The source tree is the release archive with its top-level directory stripped. Project code does not
modify FLTK. CMake disables shared libraries, tools, examples, tests, documentation, OpenGL, SVG,
Wayland, and print support for the manager build. FLTK links only into the manager executable.
