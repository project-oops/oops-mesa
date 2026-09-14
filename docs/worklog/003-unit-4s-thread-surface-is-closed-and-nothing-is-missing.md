# 003. Unit 4's thread surface is closed, and nothing in it is missing

**2026-09-14** - between units 2 and 3, prompted by the owner asking what orbistoun's POSIX crate
already knows

## What changed

No code. Two documents and a corrected premise, from reading two corpora this collection already
owns instead of waiting on a hardware run.

- **[The thread surface table](../hardware/mesa-thread-surface-fw1240.md)**: Mesa's C11 threads
  layer is the only thing in Mesa that touches `pthread_*`, so the runtime shim's thread surface
  is that one file's requirements. It is 26 functions and six other names, and it is closed.
  Every one of the 26 has a vendor twin on this platform: 16 already bound and working in
  oops-sdk, 7 named `present` in obSCEne's libkernel census, 3 in orbistoun's libkernel symbol
  inventory. None is missing.
- **D002 and the roadmap corrected.** Both said the thread names were mined candidates that might
  not be there. The names are real and the obstacle is different, which is written down now.
- **`REQ-20260914T1443Z-3ea7` rewritten twice and then demoted.** It was filed asking whether the
  core POSIX thread names exist at all. They do. It was rewritten to ask the narrow question that
  is actually open. It is now an optimisation rather than unit 4's gate, because unit 4 can be
  built from the table above whatever the answer is.

## What it unblocks

Unit 4 stops being a risk and becomes a known quantity: a mapping table of 26 rows, 16 of which
already exist in oops-sdk. It can be written before the obSCEne request is answered, and the
answer can only make it smaller. Unit 3 is now the only thing between here and a Mesa build, and
nothing blocks it.

## Surprises

- **The obstacle was never the names. It was which library holds them and where that library
  loads.** Retail titles import `pthread_create` from `libScePosix`, and orbistoun's declaration
  of that library is built from four titles' own import tables (its D349). obSCEne separately
  measured that `libScePosix` does not load in the PS5 app sandbox: `OBS|module|libScePosix|0x0`
  with every import from it unresolvable, while the `libkernel` imports on the lines either side
  resolve. obSCEne's `017-posix` section skips all five of its checks on that leg for this reason
  and says so in a source comment. Two projects held halves of this and neither had joined them.
- **A mined census reads as an export dump if you let it.** obSCEne's libkernel symbol list has 46
  `pthread_*` names and every one is a `_np` extension, a spinlock, `pthread_atfork`,
  `pthread_kill` or `pthread_timedjoin_np`. That looked like strong evidence the portable names
  were absent. It is a list of candidates that were asked about, so absence from it means nobody
  asked. Three names this table needs are absent from it and present in orbistoun's inventory,
  which is the same lesson twice.
- **orbistoun is the better first source for a name-and-module question**, and was not being used
  that way. It has done the archaeology for the whole POSIX and C-library surface, with the
  library each name belongs to, because an emulator cannot resolve an import without knowing
  both. A question of the form "does this name exist and who exports it" should start there and
  reach obSCEne second, for the binding rather than the naming.
- **Mesa's thread dependency is one file.** The worry that a hosted Mesa drags in an unbounded
  C-runtime surface is not true for threads: `src/c11/impl/threads_posix.c` is the whole of it,
  by construction, because Mesa abstracts threads for Windows' sake. The same is not yet known
  for the rest of the C runtime, which is unit 3's job.

## Next

Unit 3, unchanged and unblocked: the container build up to Mesa's first compile error against the
runtime shim's headers. That error list is the non-thread half of what this entry did for threads.
