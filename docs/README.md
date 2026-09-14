# oops-mesa documentation

A shim under upstream Mesa for Prospero-generation hardware, so titles built on oops-sdk get
OpenGL 3.3 without anyone in the collection writing OpenGL.

Not one of the four. Like oops-sdk, this is infrastructure a title links, and a repository
rather than a project: it publishes a static SDK, and its own code is three shims and a patch
set. The [root README](../README.md) has the layout and how a title consumes it.

| document | what it holds |
|---|---|
| [DECISIONS.md](DECISIONS.md) | every decision, numbered, with reasoning; the table is generated from `decisions/` |
| [ROADMAP.md](ROADMAP.md) | the units of work in order, and the measurement that gates the driver route |
| [WORKLOG.md](WORKLOG.md) | what was done, in order, with the surprises; the table indexes `worklog/` |

Decisions are cited as `(D002)` inside this repository and as `oops-mesa#D002` from anywhere
else. Hardware facts cite the oops-sdk oracle record or the obSCEne measurement they rest on.
