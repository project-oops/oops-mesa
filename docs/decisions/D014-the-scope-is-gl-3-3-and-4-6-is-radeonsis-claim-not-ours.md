# D014 - The GL version radeonsi reports is its claim, and the project claims what it measured

**Status:** decided
**Date:** 2026-09-26

The build does not clamp the GL version: radeonsi derives it from caps and reports
`4.6 (Compatibility Profile)` on this part. oops-mesa claims only what a conformance run
or a hardware test has measured, and the documents say so. The destination is OpenGL 4.6;
the order of work toward it is in [GL_SURFACE](../GL_SURFACE.md).

**Why:** a capability nobody has run is the failure this project exists not to ship, and
an unqualified version number is what a porter acts on. The source analysis finds no
structural obstacle to most of 4.6, but that is a reading of the code, not pixels.

**Rejected:**
- Promising 4.6 from the version string or the source analysis: unmeasured.
- Clamping the reported version to what has been measured: it hides what the driver
  concluded and needs a patch or an override on every bump.
