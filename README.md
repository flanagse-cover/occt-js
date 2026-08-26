# occt-js

WebAssembly build of [OpenCASCADE Technology (OCCT)](https://dev.opencascade.org/) v8.0.1 for CAD import and triangulation. The canonical downstream path is the packaged Wasm carrier `@tx-code/occt-js`, optionally wrapped by the engine-agnostic adapter `@tx-code/occt-core`.

**[Live Demo](https://tx-code.github.io/occt-js/)** — drag and drop STEP/IGES/BREP files, face/edge/vertex picking, hover preview

## Features

- Import STEP / IGES / BREP files from memory (`Uint8Array`)
- Full B-Rep topology output (Face/Edge/Vertex with stable IDs and adjacency)
- XDE assembly tree traversal with names and per-face colors
- BRepMesh triangulation with configurable deflection
- Manufacturing-oriented single-part orientation analysis for STEP / IGES / BREP
- Embind-based API returning a structured scene graph
- Babylon.js demo with interactive face/edge/vertex selection

## Downstream Packages

- `@tx-code/occt-js`
  The root Wasm carrier. It publishes `dist/occt-js.js`, `dist/occt-js.wasm`, and `dist/occt-js.d.ts`.
- `@tx-code/occt-core`
  The engine-agnostic adapter layer on top of `@tx-code/occt-js` for normalized import flow and package-first downstream use.
- `@tx-code/occt-babylon-*`, `demo/`, and `demo/src-tauri/`
  Optional secondary surfaces for Babylon and local app development. They are not required to consume the root Wasm runtime.

## Revolved Shape Boundary

The generated revolved-shape surface in `occt-js` is intentionally narrow. The root Wasm carrier accepts one normalized `RevolvedShapeSpec` and returns validation, build, or exact-open results. It does not own tool-library schemas, vendor adapters, or app-level tool semantics.

The boundary contract is documented in [docs/specs/2026-04-21-revolved-shape-runtime-boundary-design.md](./docs/specs/2026-04-21-revolved-shape-runtime-boundary-design.md).

## Shared Profile and Extruded Shape Contract

The profile-solid runtime now also exposes one shared `Profile2D` validator plus additive linear extruded-shape entrypoints. The ownership boundary stays the same as the revolved family: upstream apps own tool-library schemas, vendor adapters, and viewer policy; `occt-js` only consumes normalized geometry specs.

```js
const profile = {
  version: 1,
  start: [0, 0],
  segments: [
    { kind: "line", id: "base", tag: "base", end: [6, 0] },
    { kind: "line", id: "right-wall", tag: "wall", end: [6, 10] },
    { kind: "line", id: "top", tag: "cap", end: [0, 10] },
    { kind: "line", id: "left-wall", tag: "wall", end: [0, 0] },
  ],
};

const profileValidation = occt.ValidateProfile2DSpec(profile);

const extrudedSpec = {
  version: 1,
  units: "mm",
  profile,
  extrusion: { depth: 24 },
};

const extrudedValidation = occt.ValidateExtrudedShapeSpec(extrudedSpec);
const built = occt.BuildExtrudedShape(extrudedSpec, {
  linearDeflectionType: "bounding_box_ratio",
  linearDeflection: 0.001,
  angularDeflection: 0.5,
});
const exact = occt.OpenExactExtrudedShape(extrudedSpec);
```

Contract rules:

- `ValidateProfile2DSpec(spec)` is the generic geometry-only validation lane reused by profile-driven solid families.
- `ValidateExtrudedShapeSpec(spec)` applies strict extrusion-family checks on top of the shared `Profile2D` contract.
- `BuildExtrudedShape(...)` returns `sourceFormat: "generated-extruded-shape"` with additive `extrudedShape` metadata, stable wall/cap face bindings, and deterministic runtime-owned appearance grouping.
- `OpenExactExtrudedShape(...)` reuses the same generated solid and returns retained exact handles for downstream exact queries.
- `@tx-code/occt-core` mirrors this surface package-first through `validateProfile2DSpec`, `validateExtrudedShapeSpec`, `buildExtrudedShape`, and `openExactExtrudedShape`.

The root runtime also exposes a generic helical sweep lane for non-tool-coupled spiral bodies:

```js
const helicalSpec = {
  version: 1,
  units: "mm",
  helix: {
    radius: 8,
    pitch: 3,
    turns: 4,
    handedness: "right",
  },
  section: {
    kind: "circle",
    radius: 0.8,
    segments: 24,
  },
};

const helicalValidation = occt.ValidateHelicalSweepSpec(helicalSpec);
const helicalBuilt = occt.BuildHelicalSweep(helicalSpec, {
  linearDeflectionType: "bounding_box_ratio",
  linearDeflection: 0.001,
  angularDeflection: 0.5,
});
const helicalExact = occt.OpenExactHelicalSweep(helicalSpec);
```

Helical sweep rules:

- `ValidateHelicalSweepSpec(spec)` validates one generic circular-section helix contract.
- `BuildHelicalSweep(...)` returns `sourceFormat: "generated-helical-sweep"` with additive `helicalSweep` metadata and stable runtime face bindings.
- `OpenExactHelicalSweep(...)` reuses the same generated body and returns retained exact handles for downstream exact queries.
- `@tx-code/occt-core` mirrors this surface package-first through `validateHelicalSweepSpec`, `buildHelicalSweep`, and `openExactHelicalSweep`.

The runtime also exposes a generic composite lane for boolean pipelines over generated operands (still tool-schema agnostic):

```js
const compositeSpec = {
  version: 1,
  units: "mm",
  seed: {
    family: "revolved",
    spec: revolvedSpec,
  },
  steps: [
    {
      op: "cut",
      operand: {
        family: "helical-sweep",
        spec: helicalSpec,
      },
    },
  ],
};

const compositeValidation = occt.ValidateCompositeShapeSpec(compositeSpec);
const compositeBuilt = occt.BuildCompositeShape(compositeSpec, {
  linearDeflectionType: "bounding_box_ratio",
  linearDeflection: 0.001,
  angularDeflection: 0.5,
});
const compositeExact = occt.OpenExactCompositeShape(compositeSpec);
```

Composite-shape rules:

- `ValidateCompositeShapeSpec(spec)` validates a generic seed + boolean-step pipeline over `revolved`, `extruded`, and `helical-sweep` operands.
- `BuildCompositeShape(...)` returns `sourceFormat: "generated-composite-shape"` with additive `compositeShape` metadata and shape-validation evidence.
- `OpenExactCompositeShape(...)` reuses the same generated composite body and returns retained exact handles for downstream exact queries.
- `@tx-code/occt-core` mirrors this surface package-first through `validateCompositeShapeSpec`, `buildCompositeShape`, and `openExactCompositeShape`.

## Package Usage

Install the packaged Wasm carrier:

```bash
npm install @tx-code/occt-js
```

The packaged contract supports two downstream Wasm resolution patterns:

1. Keep `occt-js.js` and `occt-js.wasm` adjacent in the published package layout.
2. Pass either `locateFile` or `wasmBinary` when initializing the module.

```js
import OcctJS from "@tx-code/occt-js";
import occtWasmUrl from "@tx-code/occt-js/dist/occt-js.wasm?url";

const occt = await OcctJS({
  locateFile(filename, scriptDirectory) {
    if (filename === "occt-js.wasm") {
      return occtWasmUrl;
    }
    return new URL(filename, scriptDirectory).toString();
  },
});

const occtWithBinary = await OcctJS({
  wasmBinary: await fetch(occtWasmUrl).then((response) => response.arrayBuffer()),
});
```

Use `@tx-code/occt-core` when you want a package-first adapter over the root Wasm carrier:

```bash
npm install @tx-code/occt-js @tx-code/occt-core
```

```js
import OcctJS from "@tx-code/occt-js";
import occtWasmUrl from "@tx-code/occt-js/dist/occt-js.wasm?url";
import { createOcctCore } from "@tx-code/occt-core";

const core = createOcctCore({
  factory: OcctJS,
  wasmBinaryLoader: () => fetch(occtWasmUrl).then((response) => response.arrayBuffer()),
});
```

## Import Appearance Contract

Import-time appearance is part of the packaged runtime contract. Downstream apps can either preserve source colors or request a normalized default CAD appearance during import:

```js
const result = occt.ReadFile("step", buffer, {
  appearancePreset: "cad-ghosted",
  colorMode: "default",
  defaultColor: { r: 0.2, g: 0.4, b: 0.6 },
  defaultOpacity: 0.5,
});
```

Contract rules:

- `appearancePreset: "cad-solid"` resolves to the built-in CAD base appearance: `[0.9, 0.91, 0.93]` with opaque materials.
- `appearancePreset: "cad-ghosted"` resolves to the same built-in CAD base appearance with the built-in ghost opacity `0.35`.
- Presets resolve before explicit `defaultColor` / `defaultOpacity` overrides and are ignored when `colorMode: "source"` is selected.
- `colorMode: "source"` preserves imported source colors.
- `colorMode: "default"` ignores source colors and uses one default CAD color for the imported result.
- If `defaultColor` is omitted in default mode, the built-in fallback is `[0.9, 0.91, 0.93]`.
- If `defaultOpacity` is omitted in default mode, imports keep the preset-derived opacity when present; otherwise the default appearance stays opaque.
- `readColors` is a legacy toggle and is only authoritative when `colorMode` is omitted.

Apps own settings persistence. `occt-js` only consumes the selected import-time appearance options and returns the resulting colors/materials.
Viewer overrides remain downstream concerns; post-import repaint, theme switching, and display policy are outside the root Wasm carrier scope.

## Exact Measurement and Helper SDK

Most downstream JS consumers should start with the package-first adapter surface in `@tx-code/occt-core`. A fuller walkthrough lives in [docs/sdk/measurement.md](./docs/sdk/measurement.md); the root README stays the lower-level reference.

### Package-first workflow with `@tx-code/occt-core`

`@tx-code/occt-core` wraps retained exact-model handles, occurrence transforms, pairwise measurement, placement helpers, and relation classification in JS-friendly DTOs:

```js
const rawExact = await core.openExactStep(buffer, {
  fileName: "part.step",
});

const refA = {
  exactModelId: rawExact.exactModelId,
  exactShapeHandle: rawExact.exactGeometryBindings[0].exactShapeHandle,
  kind: "face",
  elementId: 1,
  transform: [
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
  ],
};
const refB = { ...refA, elementId: 2 };

const distance = await core.measureExactDistance(refA, refB);
const placement = await core.suggestExactDistancePlacement(refA, refB);
const relation = await core.classifyExactRelation(refA, refB);
```

The package-first exact measurement and helper SDK includes:

- `measureExactDistance(refA, refB)`, `measureExactAngle(refA, refB)`, and `measureExactThickness(refA, refB)`
- `suggestExactDistancePlacement(refA, refB)`, `suggestExactAnglePlacement(refA, refB)`, and `suggestExactThicknessPlacement(refA, refB)`
- `suggestExactRadiusPlacement(ref)` and `suggestExactDiameterPlacement(ref)`
- `classifyExactRelation(refA, refB)`
- `describeExactHole(ref)` and `describeExactChamfer(ref)`
- `suggestExactMidpointPlacement(refA, refB)`, `describeExactEqualDistance(refA, refB, refC, refD, options?)`, and `suggestExactSymmetryPlacement(refA, refB)`

Placement helpers return stable anchors plus a working-plane frame; relation classification returns `parallel`, `perpendicular`, `concentric`, `tangent`, or `none` together with any supporting geometry DTOs.

The shipped helper family stays intentionally narrow:

- `describeExactHole(ref)` only recognizes a supported cylindrical hole from a circular edge ref or cylindrical face ref.
- `describeExactChamfer(ref)` only recognizes a supported planar chamfer face ref.
- `suggestExactSymmetryPlacement(refA, refB)` is a midplane-style symmetry helper for supported parallel pairs.
- `suggestExactMidpointPlacement(...)` and `describeExactEqualDistance(...)` are package-first compositions over the shipped placement and pairwise measurement surface.

The shipped browser demo is a simplified integration sample. It keeps supported exact action routing, overlay rendering, and current-result session behavior in app code. For the current `v1.13` CAM sample, demo-owned workflow names (`clearance / step depth`, `center-to-center`, and `surface-to-center`) compose over shipped exact primitives; they are not new runtime or package API names. `occt-js` and `@tx-code/occt-core` do not publish candidate discovery, pinned sessions, or measurement-product UX as part of the package contract.

### Lower-level root Wasm reference

The root Wasm carrier still exposes the lower-level retained-model entrypoints directly:

```js
const exact = occt.OpenExactStepModel(buffer, {
  rootMode: "multiple-shapes",
});

const exactModelId = exact.exactModelId;
const exactShapeHandle = exact.exactGeometryBindings[0].exactShapeHandle;
const identity = [
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1,
];

const distance = occt.MeasureExactDistance(
  exactModelId, exactShapeHandle, "face", 1,
  exactShapeHandle, "face", 2,
  identity, identity,
);

const angle = occt.MeasureExactAngle(
  exactModelId, exactShapeHandle, "edge", 5,
  exactShapeHandle, "edge", 6,
  identity, identity,
);

const thickness = occt.MeasureExactThickness(
  exactModelId, exactShapeHandle, "face", 1,
  exactShapeHandle, "face", 2,
  identity, identity,
);

const placement = occt.SuggestExactDistancePlacement(
  exactModelId, exactShapeHandle, "face", 1,
  exactShapeHandle, "face", 2,
  identity, identity,
);

const relation = occt.ClassifyExactRelation(
  exactModelId, exactShapeHandle, "face", 1,
  exactShapeHandle, "face", 2,
  identity, identity,
);
```

The lower-level root surface also includes `SuggestExactAnglePlacement`, `SuggestExactThicknessPlacement`, `SuggestExactRadiusPlacement`, `SuggestExactDiameterPlacement`, `ClassifyExactRelation`, `DescribeExactHole`, and `DescribeExactChamfer`.

Richer feature discovery, overlay rendering, selection UX, label layout, and app-owned viewer policy remain downstream concerns. `occt-js` and `@tx-code/occt-core` stop at the exact Wasm/kernel and package-adapter boundary.

## Exact Lifecycle and Performance Workflow

Lifecycle ownership is explicit. The authoritative cleanup contract is:

- package-first: `openManagedExactModel(...)` / `openManagedExactStep(...)` + `dispose()` in `@tx-code/occt-core`
- lower-level root reference: `RetainExactModel(...)`, `ReleaseExactModel(...)`, and `GetExactModelDiagnostics()` in `@tx-code/occt-js`

Recommended package-first pattern:

```js
const managed = await core.openManagedExactModel(stepBytes, {
  fileName: "part.step",
});

try {
  const diagnostics = await core.getExactModelDiagnostics();
  console.log(diagnostics.liveExactModelCount);

  // run exact queries/placements/helpers against managed.exactModel refs
} finally {
  // explicit dispose is the authoritative cleanup path
  await managed.dispose();
}
```

`FinalizationRegistry` support in `@tx-code/occt-core` is best-effort only. It can reduce forgotten-handle leaks, but it is not a deterministic cleanup guarantee and must not replace explicit `dispose()` / `ReleaseExactModel(...)` usage.

For performance-sensitive downstream workflows:

- use repeatable perf checks with `npm run test:perf:exact`
- use long-session lifecycle/perf soak checks with `npm run test:soak:exact`

Both commands are explicit maintainer lanes and remain outside the unconditional root release gate.

## Prerequisites

| Tool | Version |
|------|---------|
| Emscripten SDK | 3.1.69 |
| CMake | 3.20+ |
| Git | (for OCCT submodule) |

## Setup

```bash
# Clone with submodule
git clone --recurse-submodules <repo-url>
cd occt-js

# If you already cloned the repo or are working in a fresh git worktree:
git submodule update --init --recursive occt

# Windows: install Emscripten into build/wasm/emsdk
tools/setup_emscripten_win.bat

# Linux/macOS: install Emscripten manually or use emsdk
```

For clean Windows worktrees, run `tools/setup_emscripten_win.bat` before `npm run build:wasm:win`.

## Build

```bash
# Windows
npm run build:wasm:win

# Linux/macOS: activate Emscripten first, then:
bash tools/build_wasm.sh
```

Output files are written to `dist/`:
- `occt-js.js` — packaged JavaScript loader
- `occt-js.wasm` — WebAssembly binary
- `occt-js.d.ts` — tracked TypeScript definitions published with the package

Root tests and downstream consumers require the generated `dist/occt-js.js` and `dist/occt-js.wasm` artifacts to exist. In a clean clone or worktree, build them before running `npm test`.
The tracked `dist/occt-js.d.ts` file is not regenerated by the Wasm build and should not be deleted. If it is missing, restore it with:

```bash
git restore --source=HEAD -- dist/occt-js.d.ts
```

The Windows build entrypoint fails early with a clear error if either prerequisite is missing:
- `occt/src/FoundationClasses/TKernel/Standard` from the `occt` git submodule
- `build/wasm/emsdk/emsdk_env.bat` from `tools/setup_emscripten_win.bat`

Windows build failures retain a log file at `build/wasm-build.log`. If a parallel build fails intermittently, retry with lower parallelism:

```bash
set BUILD_JOBS=1 && tools\build_wasm_win.bat Release
```

## Test

Run the fast preflight command when you want prerequisite and runtime-contract validation without relying on the full runtime suite:

```bash
npm run test:wasm:preflight
```

Run the full root verification gate after `npm run build:wasm:win` has produced `dist/occt-js.js` and `dist/occt-js.wasm`:

```bash
npm test
```

## Release Gate

Use `npm run test:release:root` as the authoritative root release verification command:

```bash
npm run test:release:root
```

This gate is runtime-first. It covers the Windows Wasm build, root governance contracts, `@tx-code/occt-core`, and the full root runtime suite.
That authoritative surface includes `test/exact_pairwise_measurement_contract.test.mjs`, `test/exact_placement_contract.test.mjs`, and `test/exact_relation_contract.test.mjs`, so the exact measurement SDK stays mandatory in root release verification.

When you intentionally want to audit the GSD planning corpus, run the separate planning audit:

```bash
npm run test:planning:audit
```

That `.planning/` audit validates milestone/archive consistency for maintainers, but it is separate from the authoritative root npm release gate.

When you intentionally want to audit the command/docs contract for secondary surfaces, run:

```bash
npm run test:secondary:contracts
```

That audit locks the conditional demo/Babylon/Tauri routing below, but it stays outside the root release gate.

When you intentionally want lifecycle/performance stress evidence on top of normal root verification, run:

```bash
npm run test:perf:exact
npm run test:soak:exact
```

These commands are explicit optional verification lanes for performance and long-session confidence. They do not replace `npm run test:release:root` and are not unconditional release blockers.

Demo, Babylon, and Tauri surfaces are conditional secondary-surface verification only. Run their follow-up checks when your release changes those paths:

| Touched paths | Follow-up commands | Scope |
|---------------|--------------------|-------|
| `demo/`, `demo/src/`, `demo/tests/` | `npm --prefix demo test` | demo node-style verification |
| `demo/`, `demo/src/`, `demo/tests/` | `npm --prefix demo run test:e2e` | browser smoke and measurement interaction verification for the current Project Home shell and demo loop |
| `demo/`, `demo/src/`, `demo/tests/` | `npm --prefix demo run build` | production demo build |
| `demo/src-tauri/` | `npm --prefix demo run tauri:build` | desktop-only follow-up verification |
| `packages/occt-babylon-loader/` | `npm --prefix packages/occt-babylon-loader test` | loader package verification |
| `packages/occt-babylon-viewer/` | `npm --prefix packages/occt-babylon-viewer test` | viewer package verification |
| `packages/occt-babylon-widgets/` | `npm --prefix packages/occt-babylon-widgets test` | widgets package verification |

## Repository Layout (2026-04-14)

This stays a single `occt-js` repository. The primary downstream contract is centered on:

- `@tx-code/occt-js`
  - Root Wasm carrier with the canonical packaged runtime artifacts.
- `@tx-code/occt-core` (`packages/occt-core`)
  - Engine-agnostic adapter with normalized CAD model output.
  - Unified import API for `step` / `iges` / `brep`.

Secondary repository-local layers remain available for demos and Babylon integrations:

- `@tx-code/occt-babylon-loader` (`packages/occt-babylon-loader`)
  - Babylon-facing model loader and scene builder.
- `@tx-code/occt-babylon-viewer` (`packages/occt-babylon-viewer`)
  - Scene-first Babylon viewer runtime helpers.
- `@tx-code/occt-babylon-widgets` (`packages/occt-babylon-widgets`)
  - Optional viewer widgets such as ViewCube.

## Babylon Packages

- `@tx-code/occt-babylon-loader`: OCCT model to Babylon nodes.
- `@tx-code/occt-babylon-viewer`: scene-first Babylon viewer runtime helpers.
- `@tx-code/occt-babylon-widgets`: optional viewer widgets such as ViewCube.

## API

```js
import OcctJS from "@tx-code/occt-js";
import occtWasmUrl from "@tx-code/occt-js/dist/occt-js.wasm?url";

const occt = await OcctJS({
    locateFile(filename) {
        return filename === "occt-js.wasm" ? occtWasmUrl : filename;
    }
});

const buffer = new Uint8Array(/* ... CAD file bytes ... */);
const result = occt.ReadFile("step", buffer, {
    rootMode: "one-shape",
    linearUnit: "millimeter",
    linearDeflectionType: "bounding_box_ratio",
    linearDeflection: 0.1,
    angularDeflection: 0.5,
    readNames: true,
    appearancePreset: "cad-solid",
    colorMode: "default",
    defaultColor: { r: 0.2, g: 0.4, b: 0.6 },
    defaultOpacity: 0.8
});

// Also available:
// occt.ReadIgesFile(buffer, options)
// occt.ReadBrepFile(buffer, options)

// result.success       — boolean
// result.sourceFormat  — "step"
// result.rootNodes     — tree of nodes with children, transforms, meshes[]
// result.geometries    — array of { positions, normals, indices, faces, edges, vertices, triangleToFaceMap }
// result.materials     — deduplicated color list
// result.stats         — { rootCount, nodeCount, partCount, triangleCount, ... }
```

## Runtime Geometry Transform Export

`occt-js` can also export transient runtime geometry bytes with a caller-supplied
4x4 affine matrix applied. This is intended for downstream runtime boundaries
that need STEP/BREP bytes in an already materialized placement while preserving
the original source CAD and placement metadata separately in application
authority.

```js
const sourceStep = new Uint8Array(/* source STEP bytes */);
const sourceToWorkpiece = [
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  25, -10, 5, 1,
];

const transformed = occt.TransformStepFile(sourceStep, sourceToWorkpiece, {
  linearUnit: "millimeter",
});

if (!transformed.success) {
  throw new Error(transformed.error);
}

// transformed.content is a Uint8Array containing transformed STEP bytes.
```

Contract rules:

- `TransformStepFile(...)` and `TransformBrepFile(...)` return exported CAD
  bytes that can be reopened by the corresponding `Read*File(...)` API.
- The transform matrix uses the same column-major layout as import and exact
  helper transforms, with translation in indices 12, 13, and 14.
- The generic `TransformFile(format, ...)` accepts `step`, `stp`, and `brep`;
  `stp` is normalized to STEP output.
- Invalid matrices, unsupported formats, import failures, and export failures
  return `{ success: false, error }`; callers should not substitute the source
  bytes for a failed non-identity transform.

`rootMode` behavior by format:

- `"one-shape"`: default. Multiple top-level XDE free shapes are exposed under one logical root node.
- `"multiple-shapes"`: preserves each top-level free shape as an independent root node.
- `BREP one-shape`: keeps the current single-root behavior.
- `BREP multiple-shapes`: if the imported file resolves to a compound/compsolid wrapper chain, `occt-js` unwraps single-child wrappers and exposes the first meaningful compound level as multiple root nodes.

## Orientation Analysis

`occt-js` also exposes a separate manufacturing-oriented orientation analysis API for single parts:

```js
const orientation = occt.AnalyzeOptimalOrientation("step", buffer, {
    mode: "manufacturing",
    linearUnit: "millimeter"
});

if (orientation.success) {
    // 4x4 transform that aligns the part into its suggested working pose.
    console.log(orientation.transform);

    // Local reference frame derived from the oriented bounding box.
    console.log(orientation.localFrame);

    // Analysis diagnostics.
    console.log(orientation.strategy);   // e.g. "planar-base-with-cylinder-support+projected-min-area-rect"
    console.log(orientation.stage1);     // base face / detected axis
    console.log(orientation.stage2);     // rotationAroundZDeg
    console.log(orientation.confidence); // 0..1 heuristic score
}
```

Supported formats for `AnalyzeOptimalOrientation(...)`:

- `step`
- `iges`
- `brep`

Current scope:

- single-part / `one-shape` analysis
- manufacturing-oriented heuristic based on exact B-Rep geometry
- optional `presetAxis` override to constrain stage 1 alignment

## License

This project links against OCCT which is licensed under the [GNU Lesser General Public License v2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html).
