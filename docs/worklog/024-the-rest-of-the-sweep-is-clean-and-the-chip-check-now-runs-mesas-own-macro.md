# 024. The rest of the sweep is clean, and the chip check now runs Mesa's own macro

**2026-09-17** - roadmap unit 5

## What this entry is

Worklog 023 found a null dereference by reading one function carefully. The obvious follow-up is
whether there are more of the same shape on the startup path. This is that sweep, and its result
is negative: there is one more instance and it is the one 023 already recorded.

A negative result is worth writing down when the search was systematic, because otherwise the next
person repeats it. So this says what was searched and what it found, and then does the one piece of
real work the sweep suggested.

## The search

The shape is: Mesa calls something that can fail or return null *differently on this platform than
on a Linux host*, and uses the result without checking. Mechanically, that is a string function
whose argument is a call rather than a variable. Across radeonsi, the amdgpu winsys, `ac_*` and
`xmlconfig`, restricted to functions that can return null here:

| Site | Verdict |
|---|---|
| `amdgpu_winsys.c:58-69`, seven `strstr(debug_get_option("...", ""), ...)` | safe - the default argument is `""`, not null, so `debug_get_option` never returns null to them |
| `si_gfx_screen.c:842`, `strlen(ac_get_llvm_processor_name(...))` | not compiled - `AMD_LLVM_AVAILABLE` is 0 in this build |
| `si_state.c:4825`, `strstr(util_get_process_name(), name)` | **real**, and already recorded in worklog 023: context path, currently unreachable because `cache_db_gl2` reads false from an incomplete option cache |
| `ac_gpu_info.c:1586`, `ac_drm_get_marketing_name` | safe - guarded with an explicit `marketing_name ? ... : "AMD Unknown"` |
| `xmlconfig.c`, `driParseConfigFiles` -> `parseAppAttr` | fixed in worklog 023 by injecting the name |

`debug_get_flags_option` was checked separately because it takes no string default: it passes the
possibly-null value to `debug_parse_flags_option`, which opens with `if (!str) result = dfault;`.
Safe.

So the class is closed for screen creation. What remains is in context creation, is documented, and
is dormant for a reason that is itself written down.

## Four dependencies that turn out not to exist

Checking the build's own defines rather than assuming, several things that would have been
platform risks are simply not compiled:

| | | |
|---|---|---|
| `HAVE_DLADDR` | 0 | `disk_cache_get_function_identifier` is the stub that returns false |
| `ENABLE_SHADER_CACHE` | 0 | the on-disk shader cache does not exist at all |
| `HAVE_BUILD_ID` | 0 | no ELF note walking |
| `AMD_LLVM_AVAILABLE` | 0 | no `ac_init_llvm_once`, no LLVM processor-name lookup |
| `HAVE_PTHREAD_SETAFFINITY` | 0 | already recorded in worklog 022 |

The first three together matter more than they look: **the disk cache was the only thing on the
screen-creation path that wanted a filesystem**, and it is compiled out before it can ask. Combined
with `-Dxmlconfig=disabled` - which makes `driParseConfigFiles` take `parseStaticConfig` and read no
files - screen creation touches no filesystem, no environment, and no GPU. That is a much smaller
surface than a Linux host's radeonsi startup, and all of it by configuration rather than by luck.

## The chip check now evaluates Mesa's logic instead of a copy of it

`test_device_is_identifiable_as_gfx1013` carried this:

```c
/* Mesa's constants, quoted from src/amd/addrlib/src/amdgpu_asic_addr.h at the pin. If either
 * side moves, this test says so rather than the driver misidentifying the chip at run time. */
#define FAMILY_NV           0x8F
#define GFX1013_RANGE_LOW   0x82
#define GFX1013_RANGE_HIGH  0x86
```

The comment states an intention the code cannot carry out. Copied constants do not notice when the
original moves; they were right when they were copied and go on looking right afterwards.

`amdgpu_asic_addr.h` has **no includes at all** - it is macros end to end - so the suite can just
use it. The test now calls `ASICREV_IS(dev.external_rev, GFX1013)`, Mesa's own macro, against
Mesa's own `FAMILY_NV`. `mesa/src` joins the host suite's include path, which is the same directory
`oops-mesa.mk` already puts on a title's.

The macro was checked to actually discriminate rather than expand to something constant:

```
FAMILY_NV        = 0x8F
rev 0x81 GFX1013 -> 0        rev 0x86 GFX1013 -> 0     (half-open range, as the header says)
rev 0x82 GFX1013 -> 1        rev 0x82 NAVI10  -> 0
rev 0x85 GFX1013 -> 1
```

So the transcribed constants were correct - this changes no verdict. What changes is that a pin
bump moving the GFX1013 range now fails the suite, which is what the old comment wanted and could
not deliver. This is the same check `ac_identify_chip` makes, and failing it there returns
`AC_QUERY_GPU_INFO_UNIMPLEMENTED_HW` (worklog 021, entry 20).

## State

90 host checks, 0 failed - the count is unchanged because one check was replaced rather than added.
All gates pass. Nothing deployed. `-5c9d` is still the outstanding question, and it is still the
one that decides how much the next run can tell us.
