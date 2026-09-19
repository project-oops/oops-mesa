# Work log

Append-only running record of what was done, in order. Its job is to let a session that
has lost its conversation history pick the work back up without guessing.

**Read this file and [DECISIONS.md](DECISIONS.md) at the start of any working session.**
Append an entry at the end of every completed unit of work, not at the end of a session,
which may not arrive cleanly. Entry format: what changed, what it unblocks, what is next,
and anything surprising found on the way. Surprises are the most valuable field.

Entries live one per file under `worklog/`; this table indexes them and is kept by hand.

| | entry |
|---|---|
| - | [001. The repository opens with its three founding choices written down](worklog/001-the-repository-opens-with-its-three-founding-choices.md) |
| - | [002. The route measurement passes: radeonsi's preamble runs on our queue, both ways](worklog/002-the-route-measurement-passes-both-preambles.md) |
| - | [003. Unit 4's thread surface is closed, and nothing in it is missing](worklog/003-unit-4s-thread-surface-is-closed-and-nothing-is-missing.md) |
| - | [004. The container build reaches Mesa's first real demand, and it is libdrm](worklog/004-the-container-build-reaches-mesas-first-real-demand.md) |
| - | [005. Mesa compiles for the console, and the build answers its own questions from measurements](worklog/005-mesa-compiles-for-the-console.md) |
| - | [006. The six commands that lead to a frame](worklog/006-the-six-commands-that-lead-to-a-frame.md) |
| - | [007. Contexts and buffer lists do the work that is actually available to them](worklog/007-contexts-and-buffer-lists-do-the-work-available-to-them.md) |
| - | [008. The buffer commands finish, and the host grows a stand-in for the platform](worklog/008-the-buffer-commands-finish-and-the-host-grows-a-stand-in.md) |
| - | [009. libdrm now calls the shim, and the repository has its first patch](worklog/009-libdrm-now-calls-the-shim.md) |
| - | [010. What radeonsi demands between opening a device and having a screen](worklog/010-what-radeonsi-demands-between-open-and-a-screen.md) |
| - | [011. The thread shim, and the descriptor that cannot be duplicated](worklog/011-the-thread-shim-and-the-descriptor-that-cannot-be-duplicated.md) |
| - | [012. Asking the build what it still needs, instead of guessing](worklog/012-asking-the-build-what-it-still-needs.md) |
| - | [013. The C++ runtime question, closed](worklog/013-the-cpp-runtime-question-closed.md) |
| - | [014. Everything in this repository's hands is done](worklog/014-everything-in-this-repositorys-hands-is-done.md) |
| - | [015. A title links all of Mesa](worklog/015-a-title-links-all-of-mesa.md) |
| - | [016. Memory from the kernel, and a sweep that did not say what its log says](worklog/016-memory-from-the-kernel-and-a-sweep-that-did-not-say-what-its-log-says.md) |
| - | [017. The register answered, and the gate nobody had read](worklog/017-the-register-answered-and-the-gate-nobody-had-read.md) |
| - | [018. The imports close, and three builds that reported success falsely](worklog/018-the-imports-close-and-three-builds-that-reported-success-falsely.md) |
| - | [019. The symbol was `getenv`](worklog/019-the-symbol-was-getenv.md) |
| - | [020. The next wall is syncobj, and it comes before everything](worklog/020-the-next-wall-is-syncobj-and-it-comes-first.md) |
| - | [021. The wall was earlier again, and this time the whole path is written down](worklog/021-the-wall-was-earlier-again-and-this-time-the-whole-path-is-written-down.md) |
| - | [022. A screen can be created without touching the GPU](worklog/022-a-screen-can-be-created-without-touching-the-gpu.md) |
| - | [023. The second null in the function that already killed this title](worklog/023-the-second-null-in-the-function-that-already-killed-this-title.md) |
| - | [024. The rest of the sweep is clean, and the chip check now runs Mesa's own macro](worklog/024-the-rest-of-the-sweep-is-clean-and-the-chip-check-now-runs-mesas-own-macro.md) |
| - | [025. The field that was zero by memset rather than by choice](worklog/025-the-field-that-was-zero-by-memset-rather-than-by-choice.md) |
| - | [026. Twenty-one more fields that were zero without saying so](worklog/026-twenty-one-more-fields-that-were-zero-without-saying-so.md) |
| - | [027. The refused commands, audited - and a premise that was wrong](worklog/027-the-refused-commands-audited-and-a-premise-that-was-wrong.md) |
| - | [028. The fence radeonsi actually reads is not the syncobj](worklog/028-the-fence-radeonsi-actually-reads-is-not-the-syncobj.md) |
| - | [029. Context creation is covered, and sparse is the one that is not](worklog/029-context-creation-is-covered-and-sparse-is-the-one-that-is-not.md) |
| - | [030. Presentation, and an extrapolation worth catching](worklog/030-presentation-and-an-extrapolation-worth-catching.md) |
| - | [031. The extrapolation checked out, and now it is checked](worklog/031-the-extrapolation-checked-out-and-now-it-is-checked.md) |
| - | [032. The import direction is closed at the libdrm level](worklog/032-the-import-direction-is-closed-at-the-libdrm-level.md) |
| - | [033. The format word is B8G8R8A8, and one half of that is measured](worklog/033-the-format-word-is-b8g8r8a8-and-one-half-of-that-is-measured.md) |
| - | [034. Nothing currently gives a title a GL context](worklog/034-nothing-currently-gives-a-title-a-gl-context.md) |
| - | [035. The loader ABI is the one Mesa header safe to show a title](worklog/035-the-loader-abi-is-the-one-mesa-header-safe-to-show-a-title.md) |
| - | [036. The answer was no, and counting the sources was the wrong count](worklog/036-the-answer-was-no-and-counting-the-sources-was-the-wrong-count.md) |
| - | [037. The first platform code, and the one value that is generated](worklog/037-the-first-platform-code-and-one-open-value.md) |
| - | [038. The identifier was the blocker, and Mesa has two routes to an ioctl](worklog/038-the-identifier-was-the-blocker-and-mesa-has-two-routes-to-an-ioctl.md) |
| - | [039. The platform shim compiles, and linking it costs fifty-five names](worklog/039-the-platform-shim-compiles-and-linking-it-costs-fifty-five-names.md) |
| - | [040. Thread-local storage has exactly one workable model here](worklog/040-thread-local-storage-has-exactly-one-workable-model-here.md) |
| - | [041. The DRI frontend links, and the last symbol was a platform rule](worklog/041-the-dri-frontend-links-and-the-last-symbol-was-a-platform-rule.md) |
| - | [042. The screen has a citation, and the arithmetic has a library](worklog/042-the-screen-has-a-citation-and-the-arithmetic-has-a-library.md) |
| - | [043. The first GL call found a library nobody built](worklog/043-the-first-gl-call-found-a-library-nobody-built.md) |
| - | [044. A title finishes by not finishing](worklog/044-a-title-finishes-by-not-finishing.md) |
| - | [045. The census was answering about a different machine](worklog/045-the-census-was-answering-about-a-different-machine.md) |
| - | [046. The tool that says what is left was under-reporting it](worklog/046-the-tool-that-says-what-is-left-was-under-reporting.md) |
| - | [047. Every import is now a measurement](worklog/047-every-import-is-now-a-measurement.md) |
| - | [048. The full stack ran, and the parking ending met its catch](worklog/048-the-full-stack-ran-and-the-parking-ending-met-its-catch.md) |
