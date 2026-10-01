# Third-party references and dependencies

The SKSE adapter compiles against the official SKSE public headers from
https://github.com/ianpatt/skse64 at the revision recorded in
`integrations/skse/upstream.json`. `api.h` reproduces the small public
`TaskDelegate` ABI declaration from `skse64/GameThreads.h` to avoid importing
engine internals. See the upstream `skse64_license.txt` for its terms.

Shipwright and its dependencies retain their upstream notices and licenses in
`external/Shipwright`. Its exact source/submodule revisions are recorded in
`integrations/shipwright/upstream.json`. Game assets and generated executables
are local build inputs/outputs and are excluded from this repository by `.gitignore`.

SkyCraft and the OOT decompilation are read-only architecture/gameplay references;
no game implementation or game assets from those references are included here.
