# Bumping the vendored OCCT submodule from 7.9.3 to 8.0

**Question**: What would it take to bump the vendored OCCT kernel from V7_9_3 to OCCT 8.0, in the simplest, cleanest, most conventional way?
**Date**: 2026-08-26
**Repos**: `/Users/SFlanagan/Desktop/occt-js` (single repo; no `~/cv/` involvement)
**Verified against**: occt-js@`ad8ffb6`; upstream OCCT tags `V7_9_3` = `a016080b`, `V8_0_1` = `b8f597c6` (both `V8.0.1` and `V8_0_1` tags resolve to that same commit)

## Summary

The submodule is pinned at `a016080b` = upstream tag `V7_9_3`; the newest upstream stable is `V8.0.1`. The bump itself is one gitlink change. The real work is that OCCT 8.0 reorganized `src/` from flat `src/<Package>` to `src/<Module>/<Toolkit>/<Package>`, and `CMakeLists.txt:39-61` hand-globs OCCT sources from the flat layout across 347 hardcoded package names — every one of which resolves to nothing under 8.0.

The good news, established by diffing both trees: the new layout is perfectly regular (packages sit at exactly one depth, no nesting below them, **zero duplicate package basenames**), so a two-line change to the glob in each of the two helper functions relocates 338 of the 347 entries at once. The remaining 9 split into three trivial buckets. Separately, 8.0 split several packages this project already depends on into new sibling packages that must be added to the list, and exactly one header the project `#include`s was deleted (its typedef survives in a neighbouring header). The project's own C++ is otherwise clean: an audit of all 33 files in `src/` against 8.0's removed/deprecated API surface found **zero** hits in six of seven risk categories.

The real risk is not compilation — it is verification. CI never builds the wasm or runs tests (`.github/workflows/deploy-demo.yml:20`), `dist/occt-js.wasm` is an 11 MB committed artifact, and `test/orientation_reference_golden.json` pins kernel outputs to 13 significant figures.

## Sources of truth

- **Which OCCT commit is vendored** — the gitlink in the tree at path `occt` (`160000 commit a016080bf6738d6aeae020badee4e888ad1540a5`). `.gitmodules:1-3` declares only path + URL, with **no `branch =` key**, so the SHA is the entire pin. Currently **not initialized** in this working copy: `git submodule status` prints a leading `-`, and `occt/` is empty. Nothing can be built until `git submodule update --init --recursive occt` runs.
- **Which OCCT packages get compiled** — `CMakeLists.txt:67-466`, 332 `AddOcctModule(...)` + 15 `AddOcctHeaders(...)` calls. This is a hand-maintained allowlist, not derived from anything upstream.
- **How a package name becomes a path** — `CMakeLists.txt:40` (`AddOcctModule`) and `CMakeLists.txt:54` (`AddOcctHeaders`), both hardcoding `occt/src/${moduleName}`.
- **The OCCT version string baked into the build** — `occt/adm/cmake/version.cmake`, included at `CMakeLists.txt:15`, feeding `configure_file` at `CMakeLists.txt:24-28`. Copies of the version *for humans* live at `LICENSE:11-12`, `README.md:3`, `AGENTS.md:7`, `.planning/codebase/STACK.md:35`.
- **The shipped artifact** — `dist/occt-js.wasm` (11 MB) is **committed** (un-ignored at `.gitignore:7-9`), so it is a source of truth for consumers, not a build output. `demo/` and `packages/occt-core/test/live-root-integration.test.mjs` consume it directly.

## The 8.0 source layout (the core finding)

Measured from the upstream trees, not inferred:

| | 7.9.3 | 8.0.1 |
|---|---|---|
| `src/` entries | 478 flat package dirs | 8 module dirs |
| Package location | `src/<Package>` | `src/<Module>/<Toolkit>/<Package>` |
| Modules | — | `FoundationClasses`, `ModelingData`, `ModelingAlgorithms`, `Visualization`, `ApplicationFramework`, `DataExchange`, `Draw`, plus `Deprecated` |
| Per-package file list | `FILES` | `FILES.cmake` (537 of them) |
| Resources | `src/StdResource`, `src/XSTEPResource` | top-level `resources/` |

Three properties make the migration mechanical rather than a 347-line hand-edit:

1. **Directory depth under `src/` is exactly 1/2/3/4 fields and never 5.** Packages are flat leaf directories containing `.cxx`/`.hxx` directly — same shape as 7.9.3, just three levels deeper. So `occt/src/*/*/<Name>/*.c*` is a complete replacement for `occt/src/<Name>/*.c*`; no recursion needed.
2. **Zero duplicate package basenames** at package depth (the only repeated name is `GTests`, which is a *sibling* of packages at `src/<Module>/<Toolkit>/GTests`, never nested inside one — 54 of them). A fixed-depth glob is therefore unambiguous for every name in the list, and can never accidentally pick up a GTest directory unless someone literally writes `AddOcctModule(GTests)`.
3. **`src/Deprecated/NCollectionAliases/`** is one flat directory of ~800 deprecated typedef alias headers covering `TColgp` (58), `TColStd` (64), `TopTools` (29), `StepVisual` (36), and most other packages. It sits at *toolkit* depth (`src/Deprecated/NCollectionAliases`), so the package glob will not find it — it needs one explicit include-path entry, and it is **mandatory**: both this project and OCCT's own 8.0 sources still include those aliases.

## Mapping the 347 existing entries

**338 map 1:1 by directory name.** Examples: `Standard` → `src/FoundationClasses/TKernel/Standard`; `gp` → `src/FoundationClasses/TKMath/gp`; `BRepGProp` → `src/ModelingData/TKGeomBase/BRepGProp`; `STEPControl` → `src/DataExchange/TKDESTEP/STEPControl`. All 15 `AddOcctHeaders` entries (`CMakeLists.txt:369-383`, the GLTF/OBJ/PLY/STL/VRML header-only block) are in this group.

**9 do not resolve by name**, in three buckets:

| Bucket | Entries (with line) | Disposition |
|---|---|---|
| Already compile no-ops | `XSTEPResource` (`:465`), `StdResource` (`:466`) | Neither had a single `.cxx` or `.hxx` in 7.9.3 — contents are `FILES`, `Plugin`, `Standard`, `STEP`, `IGES` (extensionless resource data). `AddOcctModule` globs zero sources and adds an include dir nothing includes from. In 8.0 they exist only under top-level `resources/`. **Delete both lines; no functional change.** |
| Header-only, aliases relocated | `TColgp` (`:125`), `TColGeom` (`:126`), `TColGeom2d` (`:127`) | All three had 0 `.cxx` in 7.9.3. Their headers now live in `src/Deprecated/NCollectionAliases`. **Replace the three lines with one include-path entry for that directory** (which is needed anyway — see above). |
| Genuinely removed | `GeomEvaluator` (`:102`), `Geom2dEvaluator` (`:116`), `Geom2dLProp` (`:120`), `LProp3d` (`:145`) | These had real compiled sources in 7.9.3 (4, 2, 6, 4 `.cxx`). Case-insensitive grep over all 36,501 paths in the 8.0.1 tree finds **no file carrying these names** — they were absorbed by the `EvalD0/D1/D2/D3` evaluation redesign and the new `GeomProp`/`Geom2dProp`/`BRepProp` differential-properties packages. **Delete all four lines.** Nothing in `src/` references them (verified). |

## Packages that must be ADDED

Cross-referencing the project's allowlist against 8.0.1's package inventory per toolkit produces a striking result: of the 47 toolkits the project draws from, **35 are consumed 100% completely**. The gaps are concentrated and explain themselves.

24 packages sit in already-consumed toolkits but are absent from the allowlist. They split cleanly:

**18 are new in 8.0 and are delegation targets of APIs the project already calls — add these:**

- `FoundationClasses/TKMath`: `MathRoot`, `MathSys`, `MathLin`, `MathPoly`, `MathUtils`, `MathInteg`, `MathOpt` — the TKMath reorganization. The legacy `math_*` headers still compile but now delegate here. (TKMath is the one toolkit with a large gap: 14 of 21 selected, and all 7 missing are these.)
- `ModelingData/TKG3d`: `GeomEval`, `GeomGridEval`, `GeomHash`
- `ModelingData/TKG2d`: `Geom2dEval`, `Geom2dGridEval`, `Geom2dHash`
- `ModelingData/TKGeomBase`: `GeomBndLib` (`BndLib_Add3dCurve`/`AddSurface` are now thin wrappers forwarding to it), `ExtremaPC` (new point-to-curve extrema dispatch)
- `DataExchange/TKDESTEP`: `StepTidy` (STEP duplicate-entity removal)
- `ModelingData/TKBRep`: `BRepGraph`, `BRepGraphInc` — the new graph topology subsystem. Large and arguably opt-in; **suggest omitting on the first build attempt and adding only if the compile demands it**, since it is the one addition that would materially grow the wasm.

**6 pre-existed in 7.9.3 and were deliberately excluded — keep excluding:** `Cocoa`, `Xw`, `Shaders`, `Wasm` (Visualization/TKService platform + GL backends), `HLRAppli`, `BRepPreviewAPI`.

## The one source-level break in `src/`

`TopTools_ListIteratorOfListOfShape.hxx` is the **only one of the project's 96 distinct OCCT includes that does not exist in 8.0.1** (checked every one against the full 36,501-path tree; response was not truncated).

In 7.9.3 that header was a pure forwarding stub — its entire body is `#include <TopTools_ListOfShape.hxx>`. In 8.0.1 the stub is gone, but the typedef itself survives inside `TopTools_ListOfShape.hxx`:

```cpp
Standard_DEPRECATED("TopTools_ListIteratorOfListOfShape is deprecated, use "
                    "NCollection_List<TopoDS_Shape>::Iterator directly")
typedef NCollection_List<TopoDS_Shape>::Iterator TopTools_ListIteratorOfListOfShape;
```

So the minimal fix is swapping the include at three sites; the five usage sites then compile untouched:

| Include site | Usage sites |
|---|---|
| `src/extruded-shape.cpp:20` | `src/extruded-shape.cpp:743` |
| `src/revolved-tool.cpp:26` | `src/revolved-tool.cpp:1357` |
| `src/exact-query.cpp:24` | `src/exact-query.cpp:547`, `:761`, `:1397` |

All five are the same shape: `for (TopTools_ListIteratorOfListOfShape it(list); it.More(); it.Next())`. The tidier option is `NCollection_List<TopoDS_Shape>::Iterator` (or range-`for`, which 8.0 enables), which also avoids the deprecation warning — the header carries `Standard_HEADER_DEPRECATED` so *every* `TopTools_*` alias include now warns. Not a blocker: **there is no `-Werror` anywhere in `CMakeLists.txt`, `tools/`, or `.github/`.**

## What is NOT a problem

An audit of all 33 files / 16,332 lines in `src/` against 8.0's removed and deprecated API surface:

- **Removed exception APIs** (`Standard_Failure::Raise`/`Throw`/`Instance`, `Standard_ErrorHandler::Catches`, `LastCaughtError`, `OCC_CATCH_SIGNALS`) — **NONE**. The project has no `throw` statements of its own either.
- **Deprecated OCCT global math wrappers** (`ACos`, `Sqrt`, `Abs`, `Min`, `Max`, …) — **NONE**. The project already uses `std::` forms exclusively (160 call sites). The only capital-letter `Min`/`Max` tokens are enumerators of the project's own `OrientationBboxCoordinate` (`src/orientation.hpp:17,19,26`).
- **`.gxx` generic-template includes** — **NONE**.
- **Removed collection APIs** (`NCollection_Vector`, `NCollection_BasePointerVector`, `Standard_Mutex`, `TopTools_MutexForShapeProvider`, `OSD_MAllocHook`, `PLib_*`) — **NONE**. There is no `NCollection_` or `PLib` token of any kind in `src/`.
- **Mesh plugin system** (`BRepMesh_PluginMacro`, `DISCRETPLUGIN`, `DISCRETALGO`, `BRepMesh_FactoryError`) — **NONE**. The only meshing entry point is `BRepMesh_IncrementalMesh.hxx`, the direct non-plugin API, which survives.
- **BSpline/Bezier pole/weight accessors** (whose const-ref/always-populated-weights semantics changed) — **NONE**. The strings `BSpline` and `Bezier` do not appear anywhere in `src/`; geometry is accessed through adaptors and `GeomAbs_*` enums.

Build-config compatibility:

- `CMakeLists.txt:9` already sets `CMAKE_CXX_STANDARD 17`, which is exactly 8.0's new minimum. No bump needed.
- `adm/templates/Standard_Version.hxx.in` is **byte-identical** between the two tags (same blob SHA `5285c102`), so the `configure_file` at `CMakeLists.txt:24-28` keeps working unchanged and will emit `8.0.1` automatically. `version.cmake` differs only in the three version integers.
- `-DOCCT_NO_PLUGINS` (`CMakeLists.txt:505`) is still honoured in 8.0.1 (referenced by `Plugin_Macro.hxx`, `DE_PluginHolder.hxx`, `adm/cmake/occt_toolkit.cmake`).
- The comment at `CMakeLists.txt:384` — `XBRepMesh excluded: DISCRETALGO symbol conflicts with BRepMesh_IncrementalMesh` — is now **obsolete**: 8.0 removed the symbol-based plugin system in favour of `BRepMesh_DiscretAlgoFactory`, and upstream states TKMesh and TKXMesh can now coexist. `XBRepMesh` still exists at `src/ModelingAlgorithms/TKXMesh/XBRepMesh`. Keep excluding it, but the stated reason should be corrected or dropped.

## Conventions

- **Package selection is an explicit allowlist, deliberately fine-grained to keep the wasm small.** Follow it — the existing exclusions (`Draw`, `IVtk`, `OpenGl`, `Cocoa`, `Xw`, VTK, samples) are intentional. `CMakeLists.txt:369-383` shows the established pattern for "include path only, do not compile": `AddOcctHeaders`.
- **Missing modules warn rather than fail** (`CMakeLists.txt:41-44`, `:55-57`). This is the single most dangerous property for this migration: under 8.0 the current file would emit 347 warnings and produce an empty source list, then fail confusingly at link time rather than at configure time. **Consider inverting this to a hard `message(FATAL_ERROR)`** — a package named in the allowlist but absent from the tree is always a bug, and after this migration the warning has no legitimate use. That change is what makes the upgrade safe to repeat.
- **Version strings are duplicated by hand** across `LICENSE`, `README.md`, `AGENTS.md`, `.planning/codebase/STACK.md` — no test asserts on them, so nothing catches drift.
- **`.planning/milestones/**` and `docs/specs/**` are archives.** They cite `7.9.3` and `occt/src/<Package>/<File>.hxx` paths as `[VERIFIED: ...]` evidence snapshots. Rewriting them would falsify the archive and may disturb `test/planning_archive_contract.test.mjs`. Leave them alone.

### Rejected alternative: toolkit-level granularity

8.0 ships `src/MODULES.cmake`, per-module `TOOLKITS.cmake`, and per-toolkit `PACKAGES.cmake`. Since 35 of 47 toolkits are already consumed completely, replacing 347 package names with ~47 toolkit names and reading packages from upstream's own lists looks appealing and self-maintaining. **It is not simpler in practice**: it would pull in `Cocoa`, `Xw`, `Shaders`, and `Wasm` from `Visualization/TKService` — platform windowing and GL backends that will not build under Emscripten — so an exclusion list is required regardless, and the package-level allowlist already *is* that list in explicit form. Recommend keeping package granularity.

## Blast radius

**Changes required (small):**
- `CMakeLists.txt:40` and `:54` — the glob path in both helper functions.
- `CMakeLists.txt` — delete 6 lines (`:102`, `:116`, `:120`, `:145`, `:465`, `:466`), replace 3 (`:125-127`) with one `NCollectionAliases` include entry, add ~16 new package lines, fix the stale comment at `:384`.
- `src/extruded-shape.cpp:20`, `src/revolved-tool.cpp:26`, `src/exact-query.cpp:24` — one include swap each.
- The `occt` gitlink.
- Version strings: `LICENSE:11`, `LICENSE:12` (`tag V7_9_3` — the only `V7_9` literal in the repo outside `occt/`), `README.md:3`, `AGENTS.md:7`, `.planning/codebase/STACK.md:35`. Note `LICENSE` ships in the npm tarball (`package.json:27`, asserted at `test/package_tarball_contract.test.mjs:38`), so a stale string reaches consumers.
- `dist/occt-js.wasm` — an 11 MB committed binary diff.

**Tests that will likely need regeneration or re-verification (this is where the effort actually goes):**
- `test/orientation_reference_golden.json` — a full numeric golden of kernel outputs (13-significant-figure bbox extents, 16-entry transform matrices, detected axes), verified by `test/test_optimal_orientation_reference.mjs` at `ABS_TOL = 1e-6` / `REL_TOL = 1e-5` (`:6-7`), with `:83` at `1e-9` and `:108` at `1e-12`. **Expect regeneration.** 8.0 changed `BRepBndLib` (now forwarding to `GeomBndLib`), `BRepGProp` shared-subshape counting, and `BRepMesh` — all upstream of these numbers.
- Hardcoded topology counts from the STEP/XDE readers: `test/test_step_product_inspection.mjs:121-164`, `test/test_brep_root_mode.mjs:77` (`rootNodes.length === 15`). STEP product-structure interpretation is exactly what 8.0 reworked.
- Tightest exact-geometry tolerances: `test/exact_primitive_queries_contract.test.mjs:191` (`1e-9`), `test/exact_query_store_performance_contract.test.mjs:93-99` (`1e-8`), `test/exact_relation_contract.test.mjs:184` (axis-alignment gate `0.999999`), `test/generated_revolved_tool_contract.test.mjs:180` (mesh welding at `1e-6`, directly exposed to `BRepMesh`).
- `packages/occt-core/test/live-root-integration.test.mjs` loads `dist/occt-js.wasm` directly at 13 sites and has `1e-9` normal-length checks at `:770-771`. It is in the release gate via `package.json:34`.
- Three `BRepClass`/`BRepClass3d` classifier call sites (`src/revolved-tool.cpp:608`, `src/exact-query.cpp:1574`, `:469`) sit on top of the `TopClass` `.gxx`→`.pxx` port. Public API is unchanged, but tolerance/`State()` behaviour could shift — worth a targeted runtime check rather than a code change.

**Explicitly NOT affected:**
- No OCCT version string exists anywhere under `test/`, in any `package.json`, or in any `src/*.cpp`/`*.hpp`; `grep -rn "OCC_VERSION" src/` is empty. No version-conditional C++.
- No test asserts on wasm file size, hash, or exported-symbol list.
- `packages/occt-core`, `packages/occt-babylon-*`, and `demo/` contain **zero** references to the OCCT kernel version or the `occt/` submodule. Their only binding is the runtime artifact path.
- `dist/occt-js.d.ts` is hand-maintained, not generated (`README.md:399-402`), so it will not drift on its own.
- `test/release_governance_contract.test.mjs` is pure text/script-surface governance with nothing kernel-version-dependent.

**Verification gap — the biggest practical risk:**
- `.github/workflows/deploy-demo.yml:20` uses `actions/checkout@v4` with **no `submodules:` key**; there is no `emcmake`/`cmake`/`build_wasm` step and no `npm test` step anywhere in CI. The whole upgrade is verified only on a maintainer's local machine via `npm run test:release:root`.
- `tools/check_wasm_prereqs.mjs:9-11` checks exactly one path inside the submodule — `occt/src/Standard` — and 8.0 has no `src/Standard`. So the preflight **will now correctly fail** against an 8.0 checkout, which is accidentally helpful, but it must be repointed (e.g. to `occt/src/FoundationClasses/TKernel/Standard`), and `test/wasm_build_prereqs.test.mjs:15-19` and `:43` assert that exact string and must move with it. `README.md:409` documents the same path.
- Emscripten is pinned to **3.1.69** (`tools/setup_emscripten_win.bat:22,24`, documented `README.md:364`). Clang there supports C++17 fully, so 8.0's baseline is fine — but if emsdk is bumped alongside, `test/package_tarball_contract.test.mjs:76-78` matches **verbatim minified Emscripten glue** and will break.

## Suggested sequence

1. `git submodule update --init --recursive occt` (currently uninitialized — nothing builds without this), then check out `V8_0_1`.
2. Change the glob in `AddOcctModule`/`AddOcctHeaders` to `occt/src/*/*/${moduleName}`; turn the not-found warning into a fatal error.
3. Apply the 9-entry cleanup, add `src/Deprecated/NCollectionAliases` to the include path, add the ~16 new packages.
4. Swap the three `TopTools_ListIteratorOfListOfShape.hxx` includes.
5. Build (`npm run build:wasm`) and fix whatever header/signature diffs surface — tree-diffing cannot catch those, only a real compile can. This is the unbounded step.
6. Run `npm run test:release:root`, then triage numeric-golden failures deliberately: for each, decide *regenerate* vs *real regression*, and do not batch-regenerate.
7. Update the 5 version strings and `tools/check_wasm_prereqs.mjs` + its two test assertions. Leave `.planning/` and `docs/specs/` archives untouched.

## Open questions

- **Should this be 8.0.1 or wait?** 8.0.1 is the newest tag; 8.0.0 shipped after 3 betas and 5 RCs. There is no in-repo signal about tolerance for a `.1` release of a brand-new major.
- **BRepGraph/BRepGraphInc**: whether to compile the new graph subsystem is a wasm-size-vs-completeness call the code cannot answer. Recommend omitting first and letting the build decide.
- **Golden regeneration policy**: nothing in the repo states who may regenerate `test/orientation_reference_golden.json` or what evidence justifies it. `test/release_governance_contract.test.mjs:225-229` governs release-boundary wording but has no notion of a kernel-version bump — worth deciding whether an OCCT bump should become a documented release-policy event.
- **Should CI build the wasm?** Out of scope for this bump, but the upgrade is the moment the zero-CI-coverage gap costs the most.
