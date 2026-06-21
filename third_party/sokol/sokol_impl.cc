// The single SOKOL_IMPL translation unit. All sokol implementations live here
// exactly once; v2play.cpp includes the same headers for declarations only.
//
// NOTE: this file is intentionally NOT built with the v2_flags determinism
// policy. sokol is display/audio-transport glue, not the synth core -- its
// float math never touches the deterministic render path, so the strict
// -ffp-contract=off / no-fast-math contract does not apply here.
#include "sokol_config.h"

#define SOKOL_IMPL
#include "sokol_log.h"
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_gl.h"
#include "sokol_audio.h"
