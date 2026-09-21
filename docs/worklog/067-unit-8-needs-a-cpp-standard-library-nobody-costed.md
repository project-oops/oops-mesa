# 067. Unit 8 needs a C++ standard library, and nobody had costed it

**2026-09-21** - roadmap unit 8

## What was done

No code. Unit 8's roadmap row says "conformance: a bounded, named subset of the GL 3.3 CTS run on
the hardware", and the collection's framing has been that it is *mechanical by design* - pick the
subset, run it, record the table, and a failure is an upstream Mesa bug rather than something to
patch around here. That framing is right about what a CTS result *means*. It is wrong about how
much has to exist before one can be produced, and this entry is the check that found out.

## The finding

**The CTS cannot be built for this target today, and the blocker is not GL.** It is the C++
standard library, which this repository does not have and has a written reason not to have.

What the CTS requires, read from the source at `KhronosGroup/VK-GL-CTS` rather than recalled:

- `framework/common/tcuDefs.hpp` includes `<string>` and `<stdexcept>`, and defines the whole test
  exception hierarchy on top of the standard one: `tcu::Exception` derives from
  `std::runtime_error`, `TestException` from that, and `TestError`, `InternalError`,
  `ResourceError`, `NotSupportedError`, `QualityWarning` and `CapabilityWarning` from that again.
- The macros that drive every test's control flow - `TCU_THROW`, `TCU_FAIL`, `TCU_CHECK`,
  `TCU_CHECK_MSG`, `TCU_CHECK_INTERNAL`, `TCU_CHECK_AND_THROW` - all **throw**.
- `framework/common/tcuTestCase.hpp` includes `<string>` and `<vector>` and holds its node tree in
  them (`std::string m_name`, `std::vector<TestNode *> m_children`), and its own documentation says
  a test "can also signal error condition by throwing an exception", which the framework catches to
  set the result code.

So exceptions are not an optional build flavour of the CTS; they are how a test reports anything
other than plain success. `NotSupportedError` - the mechanism by which a test says it does not
apply to this implementation, which a *bounded subset* run leans on heavily - is a throw.

What this repository has, which is the other half of the problem:

- **No C++ standard library.** D006: `src/runtime/cxx_support.cpp` provides "the definitions Mesa
  actually references. Eight of them today. Nothing else of libc++ is built or linked."
- **No exception machinery, deliberately.** `toolchain/build-mesa.sh:476` compiles that file
  `-fno-exceptions`, and `cxx_support.cpp:167` says the exception constructors "are deliberately
  not here". Worklog 045 recorded that Mesa references neither `__cxa_begin_catch` nor
  `__gxx_personality_v0`, and that when it appeared to, the cause was this repository's own file
  being compiled with exceptions it says it does not use.
- **The platform's C++ library cannot serve.** D006 again: obSCEne's census found 666 mangled C++
  symbols in `libSceLibcInternal` and not one in libc++'s ABI namespace - they are Microsoft's STL
  internals, a third implementation with its own ABI.

Mesa never exposed this because ACO is written to need almost nothing: eight definitions covered
it. The CTS is an ordinary C++ application and needs an ordinary C++ standard library.

## The surprise inside the surprise

D006 says libc++ "is not built from the pinned checkout", and the reason is a version mismatch: the
checkout's libc++ is `_LIBCPP_VERSION 210108` - LLVM 21 - against the collection's clang 18, and
"43 of its 71 sources fail, on constructs in its own type traits that clang 18 does not
implement". It then names one way out and rejects it: moving the container to clang 21 "is not
this repository's call", because CLAUDE.md pins clang 18 to match oops-sdk and oops-apps.

**Those are the only two options D006 weighed, and there is a third it did not.** The failure is a
mismatch between *libc++ 21* and *clang 18*, not between libc++ and clang. A libc++ from LLVM 18 -
the version-matched pairing - is a different source tree from the one that was tried, and the
stated reason for the failure does not apply to it. It is not in the checkout D004 pins, so it
would be a new dependency with its own provenance question, and whether it actually builds for this
target is untested. But "libc++ cannot be built here" is a stronger claim than the evidence
supports, and unit 8 is the first thing that makes the difference matter.

Exceptions are a second prerequisite and not automatically satisfied by a standard library:
throwing needs the unwinder as well - `__cxa_throw`, `__gxx_personality_v0`, the `_Unwind_*`
family - which means libunwind built for the target, or the platform's own, measured. Nothing in
this collection has asked for either.

## What this changes

Unit 8 is not mechanical. It has a prerequisite that is larger than the unit as written, and the
prerequisite is a collection-level question rather than an oops-mesa one - the same shape as the
clang pin D006 declined to decide alone.

It does not change what a CTS result would mean, and it does not change unit 6 or 7, which are
where the remaining GL work is. It does mean the roadmap's ordering is optimistic: unit 8 reads
like the last small step and is not.

## What is next

**Decided the same day: the collection moves to clang 21.** The reasoning was the owner's and it
is short - most substantial homebrew is C++ or drags C++ in, so a C++ standard library is needed
whether or not unit 8 ever happens, and skipping it now only means meeting it again during a port
with less warning. Route 2 below wins over route 1 because the libc++ source is **already pinned
and staged** at `toolchain/libcxx-src`, taken from the same FreeBSD checkout D004 pins; route 1
would add an unpinned second copy to avoid a change that has to happen anyway.

**Sequenced after unit 6, deliberately.** A toolchain bump while unit 6 is half-finished makes any
regression ambiguous - toolchain or unfinished work - and unit 6 has hardware oracles
(`0x9dbfe189`, `0x5188ddb7`) that make the bump verifiable rather than hopeful once it is closed.

**And it is not oops-mesa's to execute.** CLAUDE.md pins clang 18 to match oops-sdk and oops-apps,
so the whole collection moves together; this entry records the choice, it does not make it for the
other repositories. `REQ-20260921T0953Z-e3f7` (does the platform export the Itanium unwinder, or
must libunwind be built too) bounds what else the route costs, and is filed.

The three routes as they stood, kept because the comparison is the reasoning:

- **Build a version-matched libc++ (LLVM 18) for the target**, plus an unwinder, and port the CTS
  platform layer on top of `oops_gl_create`. The largest piece of work in the repository to date,
  and the only route that produces a real CTS result.
- **Move the collection to clang 21**, which D006 already identified as fixing the checkout's own
  libc++, and which is a decision for the collection rather than for here.
- **Say plainly that unit 8 is out of scope for now** and let units 6 and 7 close without it,
  recording the reason where the roadmap makes the claim rather than leaving the row blank.

What should *not* happen is a substitute suite written here and called conformance. The value of a
CTS result is that the tests are not ours; a failure is an upstream Mesa bug to report. A suite
written in this repository to test this repository's stack has none of that property, and would be
the "plausible output" principle 4 exists to refuse.
