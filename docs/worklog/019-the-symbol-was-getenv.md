# 019. The symbol was `getenv`

**2026-09-17** - roadmap unit 5

## What changed

The import that killed a Mesa title on hardware has a name: `getenv`. `libSceLibcInternal` does
not export it on firmware 12.40. It is now defined in `src/runtime/libc_absent.c` and the title
builds with it resolved locally. Nothing was deployed.

## How it was narrowed

Worklog 018 left this as "a static question and the next thing to answer", with eighteen
candidates. Three steps closed it, and none of them was a guess.

**The window.** The title's own log brackets it: `winsys device opened` printed,
`screen options built` did not. That is `driParseOptionInfo` and `driParseConfigFiles`.

**What those functions actually call**, from disassembling `xmlconfig.c.o` rather than reading the
source and assuming the compiler agreed:

- `driParseConfigFiles` calls `os_get_option` before anything else, and `os_get_option` is one
  call deep: `getenv`. First platform call on the path.
- It also reaches `util_get_process_name` (`call_once`) and `util_get_process_exec_path`
  (`sysctl`), and `os_read_file` (`__error`, `close`, `fstat`, `open`, `read`).
- `driParseOptionInfo` reaches `__stderrp`, `abort` and `fprintf`, but only on its out-of-memory
  branch.

`call_once` was eliminated by checking rather than assuming: it resolves statically from Mesa's
own C11 threads archive and is not an import at all.

**Which of the survivors the platform exports.** `REQ-20260917T0025Z-1f6d` asked for eleven names
with handle-status separate from resolution and a positive control in the same check. Sweep
`20260917-013336` answered on both legs, and the two legs are why the controls mattered:

| | payload | eboot |
|---|---|---|
| `libc-controls` resolved | 3 of 3 | **0 of 3** |
| `kernel-controls` resolved | 3 of 3 | **0 of 3** |
| `getenv` | **0x0** | 0x0 |
| the other ten | 0x1 | 0x0 |

On the eboot leg everything reads zero including the controls - the isolation artifact `-2e08`
first recorded and `-8c3b` was misread on. The payload leg resolves, and there `getenv` is the
single absent name among eleven.

## Why this stub is not like the other six

The rest of `libc_absent.c` fails loudly, because a stub that returns a plausible value for
something it cannot do is the lying stub this collection refuses. `getenv` is the exception:
**null is the correct answer**, not a failure. `getenv` returns null for a name that is not set,
and on a platform with no environment no name is set. Mesa reads only optional debug switches
through it - `MESA_DRICONF_EXECUTABLE_OVERRIDE`, `AMD_DEBUG`, `R600_DEBUG` - and every caller
already treats null as "not configured".

So it says so once and is quiet afterwards. A correct answer repeated on every call is noise, and
klog drops lines past about 128 bytes (orbistoun worklog 539).

## A check that was wrong, and how it looked right

After adding it, `grep -w getenv build/imports.txt` still showed
`libSceLibcInternal getenv`, which read as the fix having failed. It had not: the generator copies
`oops-apps/common/symbols.txt` into the manifest wholesale, so a name in the shared list appears
there whether or not the binary imports it. The binary is the thing to ask, and
`nm --dynamic --undefined-only` on the linked ELF shows no `getenv` at all.

Worth writing down because the manifest looks like the authority and is not; it is a lookup table
for whatever the ELF happens to need.

## State

Host suite 70 of 70. `make check` passes. The import manifest is complete and
`dist/mesa-probe-title-prospero.zip` builds with every known unresolvable import either placed,
removed or defined locally.

radeonsi has still never issued an ioctl to the winsys. Every answer in `drm_device.c` - the
derived `GB_ADDR_CONFIG`, DRM 3.54, `HW_IP_INFO`, `FW_VERSION` - remains unexercised on hardware.
What is different is that the one failure standing in front of them is now fixed rather than
merely understood, and the next thing the title does is not predicted anywhere.
