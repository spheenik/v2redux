// tablecheck -- the "lab-build assert" of task 4.1: verifies the portable
// loader's transcribed format tables (v2load.h) against the lab's
// sounddef.h source-of-truth annotations, replicating sdInit()'s size
// computation. Read-only include of the lab header; nothing in v2/ changes.
//
// Build/run: a ctest test (tablecheck); exits nonzero on any mismatch.

#include "../src/v2load.h"

#include <stdio.h>

// sounddef.h is an editor-side header (MSVC-isms, lab types); neutralize
// just enough to use its const data tables.
#define __declspec(x)
typedef int sBool;
typedef unsigned char sU8;
#include "../src/sounddef.h"

using namespace v2redux;

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
  } while (0)

int main()
{
  // table shapes
  CHECK(v2nparms == kNumPatchParms, "v2nparms %d != %d", v2nparms, kNumPatchParms);
  CHECK(v2ngparms == kNumGlobalParms, "v2ngparms %d != %d", v2ngparms, kNumGlobalParms);

  // max format version (sdInit derives v2version as the max annotation)
  int maxver = 0;
  for (int i = 0; i < v2nparms; i++)
    if (v2parms[i].version > maxver) maxver = v2parms[i].version;
  for (int i = 0; i < v2ngparms; i++)
    if (v2gparms[i].version > maxver) maxver = v2gparms[i].version;
  CHECK(maxver == kMaxFormatVer, "max version %d != %d", maxver, kMaxFormatVer);

  // per-param version annotations
  for (int i = 0; i < v2nparms && i < kNumPatchParms; i++)
    CHECK(v2parms[i].version == kPatchParmVer[i],
          "patch parm %d (%s): ver %d != %d",
          i, v2parms[i].name, v2parms[i].version, kPatchParmVer[i]);
  for (int i = 0; i < v2ngparms && i < kNumGlobalParms; i++)
    CHECK(v2gparms[i].version == kGlobalParmVer[i],
          "global parm %d (%s): ver %d != %d",
          i, v2gparms[i].name, v2gparms[i].version, kGlobalParmVer[i]);

  // canonical defaults (params section of v2initsnd; whole v2initglobs)
  for (int i = 0; i < v2nparms && i < kNumPatchParms; i++)
    CHECK(v2initsnd[i] == kPatchDefaults[i],
          "patch default %d (%s): %d != %d",
          i, v2parms[i].name, v2initsnd[i], kPatchDefaults[i]);
  for (int i = 0; i < v2ngparms && i < kNumGlobalParms; i++)
    CHECK(v2initglobs[i] == kGlobalDefaults[i],
          "global default %d (%s): %d != %d",
          i, v2gparms[i].name, v2initglobs[i], kGlobalDefaults[i]);

  // per-version sizes, recomputed sdInit-style from the lab annotations
  for (int v = 0; v <= kMaxFormatVer; v++)
  {
    int vs = 0, gs = 0;
    for (int i = 0; i < v2nparms; i++)
      if (v2parms[i].version <= v) vs++;
    for (int i = 0; i < v2ngparms; i++)
      if (v2gparms[i].version <= v) gs++;
    CHECK(vs == patchParmCount(v), "v%d patch parm count %d != %d", v, vs, patchParmCount(v));
    CHECK(vs + 1 + 255*3 == patchSize(v), "v%d patch size %d != %d", v, vs + 1 + 255*3, patchSize(v));
    CHECK(gs == globalSize(v), "v%d global size %d != %d", v, gs, globalSize(v));
  }

  // v2soundsize is the v6 max patch size
  CHECK(v2soundsize == patchSize(6), "v2soundsize %d != %d", v2soundsize, patchSize(6));

  if (fails) { printf("tablecheck: %d FAILURES\n", fails); return 1; }
  printf("PASS: v2load.h tables match sounddef.h (sdInit semantics)\n");
  return 0;
}
