import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export function resolveRepoRoot(defaultPath = fileURLToPath(new URL("../", import.meta.url))) {
  return defaultPath;
}

export function resolveOcctSourceMarker(repoRoot = resolveRepoRoot()) {
  return path.join(repoRoot, "occt", "src", "FoundationClasses", "TKernel", "Standard");
}

export function resolveWindowsEmsdkMarker(repoRoot = resolveRepoRoot()) {
  return path.join(repoRoot, "build", "wasm", "emsdk", "emsdk_env.bat");
}

export function resolveTypeDefinitionsMarker(repoRoot = resolveRepoRoot()) {
  return path.join(repoRoot, "dist", "occt-js.d.ts");
}

export function createMissingOcctSubmoduleError(markerPath) {
  return new Error(
    `Missing ${markerPath}. Initialize the OCCT sources first with "git submodule update --init --recursive occt".`
  );
}

export function createMissingWindowsEmsdkError(markerPath) {
  return new Error(
    `Missing ${markerPath}. Install the local Emscripten toolchain first with "tools\\setup_emscripten_win.bat".`
  );
}

export function createMissingTypeDefinitionsError(markerPath) {
  return new Error(
    `Missing ${markerPath}. Restore the tracked type definitions with "git restore --source=HEAD -- dist/occt-js.d.ts".`
  );
}

export function assertOcctSubmodulePresent(repoRoot = resolveRepoRoot()) {
  const markerPath = resolveOcctSourceMarker(repoRoot);
  if (!existsSync(markerPath)) {
    throw createMissingOcctSubmoduleError(markerPath);
  }
}

export function assertWindowsEmsdkPresent(repoRoot = resolveRepoRoot()) {
  const markerPath = resolveWindowsEmsdkMarker(repoRoot);
  if (!existsSync(markerPath)) {
    throw createMissingWindowsEmsdkError(markerPath);
  }
}

export function assertTypeDefinitionsPresent(repoRoot = resolveRepoRoot()) {
  const markerPath = resolveTypeDefinitionsMarker(repoRoot);
  if (!existsSync(markerPath)) {
    throw createMissingTypeDefinitionsError(markerPath);
  }
}

function main() {
  const modes = new Set(process.argv.slice(2));
  const repoRoot = resolveRepoRoot();

  assertOcctSubmodulePresent(repoRoot);

  if (modes.has("windows")) {
    assertWindowsEmsdkPresent(repoRoot);
  }

  if (modes.has("dist-types")) {
    assertTypeDefinitionsPresent(repoRoot);
  }
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try {
    main();
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
}
