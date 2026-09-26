# Roadmap

The intended order of work toward a measured OpenGL 4.6 (D014). Open work and defects are
GitHub issues.

1. Run `mesa-demos` on the hardware; each program exercises one feature of [the GL surface](GL_SURFACE.md).
2. Record a bounded Khronos CTS subset and its pass table under `docs/hardware/`, once the CTS log file is written.
3. Run `KHR-GL46` through the conformance title.
4. Sparse VA: answer `AMDGPU_VA_OP_REPLACE` and `_CLEAR` in the winsys, for `ARB_sparse_buffer` and `ARB_sparse_texture`.
5. Asynchronous submission, shaped by whether a `WAIT_REG_MEM` packet inside a DCB stalls and resumes the graphics ME.
6. The refused extension commands (`GEM_USERPTR`, `FENCE_TO_HANDLE`, `GEM_METADATA`, `SCHED`), each when a title asks for it.

Out of scope: GL entry points, shading-language work or a gallium driver written here (D001),
and a compute ring (Mesa refuses `AMD_IP_COMPUTE` on GFX1013, see [GL_SURFACE](GL_SURFACE.md)).
