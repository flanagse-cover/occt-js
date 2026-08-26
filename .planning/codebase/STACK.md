# Technology Stack

**Analysis Date:** 2026-04-17

## Languages

**Primary:**
- C++17 - core CAD import, triangulation, exact-query, and Embind bridge code lives in `src/*.cpp` and `src/*.hpp`, with the build defined in `CMakeLists.txt`.
- JavaScript (ES modules) - the published JS surface, local packages, demo runtime, and Node-based tooling/tests live in `packages/*/src/*.js`, `demo/src/*.js`, `demo/src/*.jsx`, `tools/check_wasm_prereqs.mjs`, `test/*.mjs`, and `demo/tests/*.mjs`.

**Secondary:**
- Rust 2021 - the desktop shell lives in `demo/src-tauri/src/*.rs`, with dependencies pinned in `demo/src-tauri/Cargo.toml`.
- Windows batch - the Windows Wasm bootstrap/build flow lives in `tools/setup_emscripten_win.bat`, `tools/build_wasm_win.bat`, `tools/build_wasm_win_release.bat`, and `tools/build_wasm_win_dist.bat`.
- Bash - the Unix Wasm build flow lives in `tools/build_wasm.sh`.
- JSX - the browser/desktop UI layer is React-on-JSX in `demo/src/main.jsx`, `demo/src/App.jsx`, and `demo/src/components/*.jsx`.

## Runtime

**Environment:**
- Node.js - required by the root package scripts in `package.json`, the demo scripts in `demo/package.json`, the local package tests in `packages/*/package.json`, and the prerequisite checker in `tools/check_wasm_prereqs.mjs`.
- Node.js 22 - the only explicit Node version in repo config appears in `.github/workflows/deploy-demo.yml`.
- Browser runtime - the published Wasm carrier and the Vite demo run in the browser; production/demo loading logic is in `demo/src/hooks/useOcct.js`.
- Tauri desktop runtime - the desktop shell embeds the Vite build and loads bundled Wasm assets via `demo/src-tauri/tauri.conf.json` and `demo/src-tauri/src/lib.rs`.
- Emscripten 3.1.69 plus MinGW 7.1.0 64-bit - the Windows build toolchain is installed locally under `build/wasm/emsdk` by `tools/setup_emscripten_win.bat`.

**Package Manager:**
- npm - primary JS package manager for the root package and `demo/`, driven by `package.json` and `demo/package.json`.
- Cargo - package manager for the Tauri shell, driven by `demo/src-tauri/Cargo.toml`.
- Lockfile: `package-lock.json`, `demo/package-lock.json`, and `demo/src-tauri/Cargo.lock` are present.
- Secondary lockfile: `pnpm-lock.yaml` is present at the repo root, but `package.json` does not declare npm workspaces or pnpm-specific scripts.

## Frameworks

**Core:**
- OpenCASCADE Technology (OCCT) 8.0.1 - the CAD kernel is vendored as the `occt/` git submodule declared in `.gitmodules` and compiled through `CMakeLists.txt`.
- Emscripten Embind - the JS bridge is implemented in `src/js-interface.cpp`, with `--bind`, `-sMODULARIZE=1`, and `-sEXPORT_NAME='OcctJS'` configured in `CMakeLists.txt`.
- React 18.3.1 - the viewer app is implemented in `demo/src/main.jsx`, `demo/src/App.jsx`, and `demo/package.json`.
- Babylon.js 9.0.0 - rendering/runtime integration lives in `demo/src/lib/babylon-runtime.js`, `packages/occt-babylon-loader/src/*.js`, and `packages/occt-babylon-viewer/src/*.js`.
- Tauri 2 - desktop packaging and native integration live in `demo/src-tauri/Cargo.toml`, `demo/src-tauri/tauri.conf.json`, and `demo/package.json`.

**Testing:**
- Node built-in test runner - root and package tests run via `node --test` in `package.json` and `packages/occt-core/package.json`, `packages/occt-babylon-loader/package.json`, `packages/occt-babylon-viewer/package.json`, and `packages/occt-babylon-widgets/package.json`.
- Playwright 1.58.2 - browser E2E coverage is configured in `playwright.config.mjs` and exercised from `demo/tests/demo.spec.mjs`.

**Build/Dev:**
- CMake 3.20+ - the Wasm/native target definition starts at `CMakeLists.txt`.
- Vite 6.0.0 - the demo dev/build pipeline is configured in `demo/vite.config.js` and scripted in `demo/package.json`.
- Tailwind CSS 4.0.0 - styling is wired through `@tailwindcss/vite`, `tailwindcss`, and `demo/src/globals.css`.
- Tauri CLI 2.10.1 - desktop dev/build commands are exposed from `demo/package.json`.

## Key Dependencies

**Critical:**
- `@tx-code/occt-js` - the root npm package exports `dist/occt-js.js`, `dist/occt-js.wasm`, and `dist/occt-js.d.ts` from `package.json`; these are the canonical runtime artifacts referenced by `AGENTS.md`.
- `@tx-code/occt-core` - `packages/occt-core/package.json` exports `packages/occt-core/src/index.js`, which wraps the Wasm module with normalized import and exact-query APIs implemented in `packages/occt-core/src/occt-core.js`.
- `@tx-code/occt-babylon-loader` - `packages/occt-babylon-loader/package.json` exports `packages/occt-babylon-loader/src/index.js` and adapts OCCT model data into Babylon scene objects.
- `@tx-code/occt-babylon-viewer` - `packages/occt-babylon-viewer/package.json` exports `packages/occt-babylon-viewer/src/index.js` and provides the viewer runtime used by the demo.
- `@tx-code/occt-babylon-widgets` - `packages/occt-babylon-widgets/package.json` exports `packages/occt-babylon-widgets/src/index.js` for optional viewer widgets.
- `@babylonjs/core` and `@babylonjs/materials` - these power the 3D renderer in `demo/package.json`, `packages/occt-babylon-viewer/package.json`, and `demo/src/lib/babylon-runtime.js`.
- `@tauri-apps/api`, `@tauri-apps/plugin-dialog`, and `@tauri-apps/plugin-fs` - the desktop viewer uses these in `demo/src/hooks/useOcct.js` and `demo/src/lib/desktop-file.js`.

**Infrastructure:**
- `tauri`, `tauri-build`, `tauri-plugin-dialog`, `tauri-plugin-fs`, and `tauri-plugin-log` - native shell/build integration is defined in `demo/src-tauri/Cargo.toml` and `demo/src-tauri/src/lib.rs`.
- `@vitejs/plugin-react` - React/Vite integration is configured in `demo/vite.config.js`.
- `@tailwindcss/vite`, `tailwindcss`, `class-variance-authority`, `clsx`, and `tailwind-merge` - styling and component class composition live in `demo/package.json` and `demo/src/components/ui/button.jsx`.
- `zustand` - demo viewer state is stored in `demo/src/store/viewerStore.js`.
- `@playwright/test` - browser E2E automation is defined in `playwright.config.mjs`.
- Local file dependencies - the demo consumes `packages/occt-core`, `packages/occt-babylon-loader`, `packages/occt-babylon-viewer`, and `packages/occt-babylon-widgets` via `file:` references in `demo/package.json` instead of a workspace manifest.

## Configuration

**Environment:**
- No `.env` files were detected at the repo root or under `demo/`.
- No required runtime secrets or API-key variables are referenced in `package.json`, `demo/package.json`, `demo/src`, `packages/*/src`, `tools`, or `.github/workflows`.
- `import.meta.env.DEV` in `demo/src/hooks/useOcct.js` is the only repo-local environment switch used to choose local `dist/` assets during development and CDN assets for production web loads.
- Tauri runtime configuration is defined in `demo/src-tauri/tauri.conf.json`, desktop permission scope is defined in `demo/src-tauri/capabilities/default.json`, and Windows chrome overrides live in `demo/src-tauri/tauri.windows.conf.json`.

**Build:**
- Root Wasm build config lives in `CMakeLists.txt`.
- Windows build orchestration lives in `tools/setup_emscripten_win.bat`, `tools/build_wasm_win.bat`, `tools/build_wasm_win_release.bat`, and `tools/build_wasm_win_dist.bat`.
- Unix build orchestration lives in `tools/build_wasm.sh`.
- Build prerequisite enforcement lives in `tools/check_wasm_prereqs.mjs`; it checks for `occt/`, `build/wasm/emsdk/emsdk_env.bat`, and `dist/occt-js.d.ts`.
- Demo build config lives in `demo/vite.config.js`.
- Desktop build config lives in `demo/src-tauri/build.rs` and `demo/src-tauri/tauri.conf.json`.
- Demo deployment config lives in `.github/workflows/deploy-demo.yml`.

## Platform Requirements

**Development:**
- Windows Wasm development requires the OCCT submodule in `occt/`, the local Emscripten SDK at `build/wasm/emsdk`, Node/npm, and the tracked type definition file `dist/occt-js.d.ts`, as enforced by `tools/check_wasm_prereqs.mjs`.
- Unix-like Wasm development requires an activated Emscripten toolchain exposing `emcmake` and `emmake`, plus Node/npm, as assumed by `tools/build_wasm.sh`.
- Desktop work requires Rust 1.77.2 or newer-compatible toolchain support, per `demo/src-tauri/Cargo.toml`.
- Browser/demo development runs from `demo/` with Vite on port `5173`, as configured in `demo/vite.config.js`.

**Production:**
- The primary production output is the npm-distributed Wasm carrier exported by `package.json` from `dist/occt-js.js`, `dist/occt-js.wasm`, and `dist/occt-js.d.ts`.
- The browser demo is built to `demo/dist` and deployed to GitHub Pages by `.github/workflows/deploy-demo.yml`.
- The desktop shell bundles `../../dist/occt-js.js` and `../../dist/occt-js.wasm` into the Tauri app resource directory via `demo/src-tauri/tauri.conf.json`.

---

*Stack analysis: 2026-04-17*
