# Bump the vendored OCCT kernel to 8.0.1 and unblock large-STEP rendering

**Status**: draft
**Created**: 2026-08-26
**Research**: .claude/research/occt-8-upgrade.md
**Repos**: `/Users/SFlanagan/Desktop/occt-js` (single repo)

## Goal

`occt/` is pinned at upstream tag `V8_0_1`, the wasm builds and the full root release suite passes, and `ExtractMeshFromShape` no longer degrades quadratically with face count — so a real multi-thousand-face STEP file tessellates in the browser instead of hanging.

## Background

The submodule is pinned at `a016080b` = `V7_9_3`; `V8_0_1^{}` = `b8f597c6` is the newest stable. The submodule is **not initialized** in this working copy (`git submodule status` shows a leading `-`), so nothing builds until it is.

OCCT 8.0 moved `src/<Package>` → `src/<Module>/<Toolkit>/<Package>`. `CMakeLists.txt:40,54` hardcode the flat path, so all 347 allowlist entries would resolve to nothing. The research doc's structural findings were re-verified against the upstream tree at `b8f597c6` (35,413 blobs, 1,088 trees) and all hold:

- 462 packages, all at exactly depth `src/*/*/<Name>`, **no directory deeper than 4 fields** under `src/`, and `GTests` is the only duplicate basename (always a *sibling* of packages, never nested).
- Of the 347 allowlist entries, exactly **9** do not exist in 8.0.1 — the same 9 the research doc names.
- Of the 96 distinct OCCT headers `src/` includes, exactly **one** is gone: `TopTools_ListIteratorOfListOfShape.hxx`. Its typedef survives verbatim in `src/Deprecated/NCollectionAliases/TopTools_ListOfShape.hxx`.
- `src/Deprecated/NCollectionAliases/` holds 972 alias headers with **zero basename collisions** against real packages, so putting it on the include path is unambiguous. It is mandatory: `TopTools_ListOfShape.hxx`, `TopTools_IndexedMapOfShape.hxx`, `TopTools_IndexedDataMapOfShapeListOfShape.hxx`, `TDF_LabelSequence.hxx` and `TColgp_Array1OfPnt.hxx` now live only there.
- 24 packages sit in already-consumed toolkits but are absent from the allowlist — the exact 18-new / 6-deliberately-excluded split the research doc describes.

**Separate from the bump**, and the reason this is worth doing together: `src/importer-utils.cpp:189-201` builds each edge's owner-face list by looping every face and exploring that face's edges, with a `TopTools_IndexedMapOfShape::FindIndex` hash lookup per edge visited. That is O(edges × faces) with a shape hash in the inner loop. On a 5,000-face / 15,000-edge solid it is ~450M hashed comparisons — tens of seconds, on the main thread, per unique solid. This is almost certainly the wall the STEP-in-browser case hits, and the repo already uses the correct one-pass idiom (`TopExp::MapShapesAndAncestors`) at six sites in `src/exact-query.cpp`.

The real risk in this work is verification, not compilation: CI never builds the wasm or runs tests (`.github/workflows/deploy-demo.yml:20` has no `submodules:` key and no build step), `dist/occt-js.wasm` is an 11 MB committed artifact, and `test/orientation_reference_golden.json` pins kernel outputs to 13 significant figures with no regeneration script.

## Changes

Do these in order. Steps 1-6 are the bump; 7-8 are perf; 9 is the version/docs sweep. Build after 6 and again after 8.

### Bump

1. `git submodule update --init --recursive occt`, then in `occt/`: `git fetch --tags && git checkout V8_0_1`. Stage the gitlink from the repo root (`git add occt`). Expect `160000 commit b8f597c677811d1f9f4d8a97f5ae2825c0353a42`.

2. `CMakeLists.txt:39-49` (`AddOcctModule`) — resolve the package by fixed-depth glob and make a miss fatal:
   ```cmake
   function(AddOcctModule moduleName)
       file(GLOB _modDirs "${CMAKE_CURRENT_SOURCE_DIR}/occt/src/*/*/${moduleName}")
       list(LENGTH _modDirs _count)
       if(NOT _count EQUAL 1)
           message(FATAL_ERROR "OCCT package '${moduleName}' resolved to ${_count} directories under occt/src/*/*/ (expected 1).")
       endif()
       list(GET _modDirs 0 _modDir)
       list(APPEND OcctSourceFolders "${_modDir}/*.c*")
       list(APPEND OcctIncludeDirs   "${_modDir}")
       ...
   ```
   Apply the same glob + `FATAL_ERROR` to `AddOcctHeaders` (`:52-61`), which currently returns silently on a miss. The warn-and-skip behaviour must go: under 8.0 it would emit 347 warnings and then fail incomprehensibly at link time. Update the `AddOcctModule` doc comment at `:31-35` to say `occt/src/<Module>/<Toolkit>/<Package>`.

3. `CMakeLists.txt`, immediately after the two function definitions — add the deprecated-alias include directory with a one-line comment on why it is required:
   ```cmake
   # 8.0 relocated the TColgp/TColStd/TopTools/TDF collection typedefs here.
   list(APPEND OcctIncludeDirs "${CMAKE_CURRENT_SOURCE_DIR}/occt/src/Deprecated/NCollectionAliases")
   ```

4. `CMakeLists.txt` — the 9-entry cleanup. Delete `GeomEvaluator` (`:102`), `Geom2dEvaluator` (`:116`), `Geom2dLProp` (`:120`), `LProp3d` (`:145`) — absorbed by the `EvalD0..D3` redesign, and nothing in `src/` names them. Delete `TColgp` (`:125`), `TColGeom` (`:126`), `TColGeom2d` (`:127`) — header-only in 7.9.3, now covered by step 3. Delete `XSTEPResource` (`:465`), `StdResource` (`:466`) — never had a single `.cxx`/`.hxx`, so they were already compile no-ops; 8.0 keeps their data under top-level `resources/`, which this build has never embedded.

5. `CMakeLists.txt` — add the 16 new delegation-target packages, each in the existing section for its area, following the surrounding `AddOcctModule(...)` one-per-line style:
   - TKMath reorg (the legacy `math_*` headers now forward here): `MathRoot`, `MathSys`, `MathLin`, `MathPoly`, `MathUtils`, `MathInteg`, `MathOpt`
   - `GeomEval`, `GeomGridEval`, `GeomHash` (TKG3d); `Geom2dEval`, `Geom2dGridEval`, `Geom2dHash` (TKG2d)
   - `GeomBndLib`, `ExtremaPC` (TKGeomBase — `BndLib_Add3dCurve`/`AddSurface` are now wrappers over `GeomBndLib`)
   - `StepTidy` (TKDESTEP)

   Do **not** add `BRepGraph`/`BRepGraphInc`; add them only if the link demands them (see Decisions). Keep excluding `Cocoa`, `Xw`, `Shaders`, `Wasm`, `HLRAppli`, `BRepPreviewAPI`.

6. `src/extruded-shape.cpp:20`, `src/revolved-tool.cpp:26`, `src/exact-query.cpp:24` — replace `#include <TopTools_ListIteratorOfListOfShape.hxx>` with `#include <TopTools_ListOfShape.hxx>` (none of the three already has it; checked). The five `for (TopTools_ListIteratorOfListOfShape it(...); ...)` usage sites compile unchanged.

   Then in `CMakeLists.txt:505`, alongside the existing `-DOCCT_NO_PLUGINS`, add `-DOCCT_NO_DEPRECATED`. Verified upstream: that macro appears only in `Standard_Macro.hxx` and `adm/cmake/occt_toolkit.cmake` and purely gates `Standard_DEPRECATED_WARNING` — it removes no API. Without it every one of the 972 alias headers warns on include, across ~3,000 translation units, and the build log becomes unreadable.

   **Build here** (`npm run build:wasm`) and fix whatever header/signature diffs surface. This is the one unbounded step; tree-diffing cannot predict it.

### Efficiency

7. `src/importer-utils.cpp:158-201` — replace the quadratic edge→face search with the single-pass ancestor map, following `src/exact-query.cpp:538` and its five siblings (`src/exact-query.cpp:23` is the include precedent). Add `#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>` and `#include <TopTools_ListOfShape.hxx>`, build the map once next to the three `TopExp::MapShapes` calls:
   ```cpp
   TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
   TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
   ```
   then in the Phase-2 loop replace lines 189-201 with a walk of `edgeToFaces.FindFromKey(edgeMap(ei))`, mapping each ancestor back through `faceMap.FindIndex(...)` into `edgeData.ownerFaceIds`.

   Two details that keep the output byte-identical, both verified against the 8.0.1 `TopExp.cxx` implementation: `MapShapesAndAncestors` does **not** dedupe, so a seam edge occurring twice in one face appends that face twice — `std::sort` + `std::erase(std::unique(...))` on `ownerFaceIds` after filling it, which also pins the ascending order today's `for (fi = 1..F)` loop produces. Every edge is present as a key (the function's second pass adds face-less edges with an empty list), so no `Contains` guard is needed.

   Leave Phase 3 (`:258-270`) alone — it is O(total face-edge incidences), not quadratic.

8. `CMakeLists.txt:487-497` — three link/compile-flag corrections, then one link-model change:
   - Delete `target_compile_options(occt-js PUBLIC -fexceptions)` (`:490`). It contradicts the `-fwasm-exceptions` on the next line; only one EH model can apply, and the intended one is already set at both compile and link.
   - Delete `--no-heap-copy` from `:494`. It is a file-packager option, not a linker option, and does nothing here.
   - Change `add_executable(occt-js ${OcctJSSourceFiles} ${OcctSourceFiles})` to build the OCCT sources as a static library the executable links:
     ```cmake
     add_library(occt-kernel STATIC ${OcctSourceFiles})
     add_executable(occt-js ${OcctJSSourceFiles})
     target_link_libraries(occt-js PRIVATE occt-kernel)
     ```
     with the compile options and include directories applied to both targets. Today every one of the ~3,000 OCCT objects is handed to the linker directly, so all of them are linked in unconditionally; from an archive the linker pulls only members that resolve an undefined symbol. This is the largest single lever on the 11 MB artifact and therefore on browser load time. It is safe from the one failure mode that would bite — dropped static-initializer registration — because `src/importer-xde.cpp:870-900` drives `STEPCAFControl_Reader`/`IGESCAFControl_Reader` directly rather than through `DE_Wrapper` plugin dispatch, and `-DOCCT_NO_PLUGINS` is already set. **Revert criterion**: if any format-import or XCAF test that passed after step 7 fails only after this change, revert this bullet and keep the other two.

### Version and prerequisite strings

9. Repoint the submodule marker and update the human-facing version strings:
   - `tools/check_wasm_prereqs.mjs:10` — `path.join(repoRoot, "occt", "src", "Standard")` → `path.join(repoRoot, "occt", "src", "FoundationClasses", "TKernel", "Standard")`.
   - `test/wasm_build_prereqs.test.mjs:17` (the `/occt\/src\/Standard/` assertion and its sample path), `:18` region, and the `mkdirSync(path.join(repoRoot, "occt", "src", "Standard"))` at `:43` — move both to the new path.
   - `README.md:409` — same path in the documented prerequisite list.
   - `LICENSE:11` (`OCCT version: 8.0.1`), `LICENSE:12` (`tag V8_0_1`), `README.md:3`, `AGENTS.md:7`, `.planning/codebase/STACK.md:35` — `7.9.3` → `8.0.1`. `LICENSE` ships in the npm tarball (`package.json:27`, asserted by `test/package_tarball_contract.test.mjs:38`), so a stale string reaches consumers.

## Out of scope

- `.planning/milestones/**` and `docs/specs/**` — archived evidence snapshots citing `7.9.3` and flat `occt/src/<Package>/` paths as `[VERIFIED: ...]`. Rewriting them falsifies the archive. Leave them.
- Adding a wasm build or test step to CI. This bump is where the zero-CI-coverage gap costs most, but fixing it is its own change.
- Bumping Emscripten off 3.1.69. Clang there covers 8.0's C++17 baseline, and `test/package_tarball_contract.test.mjs:76-78` matches verbatim minified Emscripten glue — an emsdk bump breaks it and belongs in a separate change.
- Moving import/tessellation off the main thread into a Worker, streaming meshes, or `-sINITIAL_MEMORY` tuning. Real wins for the browser, but not needed to establish whether large STEP files render at all once step 7 lands.
- Toolkit-level CMake granularity via upstream's `PACKAGES.cmake`. See Decisions.

## Verification

```bash
npm run build:wasm            # or build:wasm:win — must configure clean, zero FATAL_ERROR
npm run test:wasm:preflight   # covers the repointed check_wasm_prereqs marker
npm run test:release:root     # the full gate, incl. packages/occt-core
```

Also, and this is the point of the exercise: load a real multi-thousand-face `.stp` in `demo/` (`npm --prefix demo run dev`) and time import before and after step 7. Record face/edge counts and wall-clock. Type-passing and green tests do not tell you whether it renders.

Record `ls -l dist/occt-js.wasm` before step 8 and after, so the static-library change has a number attached.

Triage numeric failures one at a time — do **not** batch-regenerate:
- `test/orientation_reference_golden.json` (verified by `test/test_optimal_orientation_reference.mjs`, `ABS_TOL=1e-6`/`REL_TOL=1e-5`, with `:83` at `1e-9` and `:108` at `1e-12`). Expect movement: 8.0 changed `BRepBndLib` (now forwarding to `GeomBndLib`), `BRepGProp` shared-subshape counting, and `BRepMesh`. There is no generator script — edit the JSON by hand from the reported actual values. A last-digit shift in an extent or a transform entry is a kernel-precision change; a flipped `strategy`, `detectedAxis`, or `usedCylinderSupport` is a real regression and must be understood, not overwritten.
- Hardcoded topology counts: `test/test_step_product_inspection.mjs:121-164`, `test/test_brep_root_mode.mjs:77` (`rootNodes.length === 15`). STEP product-structure interpretation is exactly what 8.0 reworked. A count change here is a behaviour change, not a tolerance issue.
- Tightest tolerances: `test/exact_primitive_queries_contract.test.mjs:191` (`1e-9`), `test/exact_query_store_performance_contract.test.mjs:93-99` (`1e-8`), `test/exact_relation_contract.test.mjs:184` (`0.999999`), `test/generated_revolved_tool_contract.test.mjs:180` (mesh welding at `1e-6`, directly exposed to `BRepMesh`), `packages/occt-core/test/live-root-integration.test.mjs:770-771` (`1e-9`).
- `src/revolved-tool.cpp:608`, `src/exact-query.cpp:469`, `:1574` sit on the `BRepClass`/`TopClass` `.gxx`→`.pxx` port. Public API is unchanged; if a classifier-dependent test drifts, check `State()` behaviour rather than editing a tolerance.

Also confirm step 7 changed nothing observable: `test/import_appearance_contract.test.mjs` and the `*_root_mode` tests exercise `ownerFaceIds`/`edges` output.

## Decisions made

- **`V8_0_1`, not `V8_0_0`.** Newest stable; both `V8.0.1` and `V8_0_1` resolve to `b8f597c6`, confirmed via `git ls-remote`.
- **Package-level allowlist kept.** Upstream's `MODULES.cmake`/`TOOLKITS.cmake`/`PACKAGES.cmake` look self-maintaining, but 35 of 47 consumed toolkits are already consumed completely, and switching would drag in `Cocoa`, `Xw`, `Shaders`, `Wasm` from `Visualization/TKService` — platform windowing and GL backends that will not build under Emscripten. An exclusion list is required either way, and the existing allowlist already *is* that list, explicitly.
- **Warn-and-skip → `FATAL_ERROR`.** A package named in the allowlist and absent from the tree is always a bug. This is what makes the upgrade safe to repeat.
- **`BRepGraph`/`BRepGraphInc` omitted initially.** New graph-topology subsystem, large, and nothing in `src/` names it. If the link needs it, add it — but that is a real wasm-size cost, so let the compiler ask.
- **Marker repointed to `occt/src/FoundationClasses/TKernel/Standard`, not to `occt/adm/cmake/version.cmake`.** The cmake file is layout-independent and is what `CMakeLists.txt:15` actually needs, but repointing to the relocated `Standard` package is the smaller diff and keeps `resolveOcctSourceMarker` meaning what its name says.
- **`std::sort`/`std::unique` on `ownerFaceIds` rather than an `if not already present` check.** Cheap, and it guarantees the new code's output is identical to the old code's, which is what keeps the golden files honest as evidence about the *kernel* bump.
- **`-Oz` → `-O3` deferred.** The kernel is compute-bound, so `-O3` would help tessellation measurably, but it grows an artifact that is already 11 MB. Two variables at once makes the numbers uninterpretable. Rule: after step 8 lands with a measured size, run the single-flag experiment once and keep `-O3` only if the artifact stays at or under today's 11 MB.
- **Perf work bundled with the bump rather than split out.** Step 7 touches one function in one file and shares its entire verification surface with the bump. Splitting it would mean running `test:release:root` twice for no added signal.

## Open questions

- **Golden regeneration authority.** Nothing in the repo states who may edit `test/orientation_reference_golden.json` or what evidence justifies it; `test/release_governance_contract.test.mjs:225-229` governs release-boundary wording but has no notion of a kernel bump. Decide whether an OCCT major bump should become a documented release-policy event, and whether the regenerated golden should carry a provenance note. This does not block steps 1-9.

## How to use this doc

Open a fresh Claude session in the project root and run:

> Read `.claude/plans/occt-8-upgrade.md` and execute it. Stop and ask before any decision this doc has not already made.
