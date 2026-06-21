// Backend selection shared by every translation unit that includes sokol.
// Picked once here so the public headers and the SOKOL_IMPL unit agree on the
// conditional struct layouts (glue/environment/swapchain depend on it).
#ifndef V2PLAY_SOKOL_CONFIG_H_
#define V2PLAY_SOKOL_CONFIG_H_

// Respect an externally forced backend (e.g. -DSOKOL_GLCORE for a GL build on
// Windows, which runs under Wine via WGL); otherwise pick the platform default.
#if !defined(SOKOL_GLCORE) && !defined(SOKOL_D3D11) && \
    !defined(SOKOL_METAL)  && !defined(SOKOL_GLES3)
#if defined(_WIN32)
#define SOKOL_D3D11
#elif defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif
#endif

#endif // V2PLAY_SOKOL_CONFIG_H_
