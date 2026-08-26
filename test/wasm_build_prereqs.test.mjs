import test from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import {
  assertOcctSubmodulePresent,
  assertTypeDefinitionsPresent,
  assertWindowsEmsdkPresent,
  createMissingOcctSubmoduleError,
  createMissingTypeDefinitionsError,
  createMissingWindowsEmsdkError
} from "../tools/check_wasm_prereqs.mjs";

test("createMissingOcctSubmoduleError explains how to initialize the OCCT submodule", () => {
  const error = createMissingOcctSubmoduleError("E:/repo/occt/src/FoundationClasses/TKernel/Standard");
  assert.match(error.message, /occt\/src\/FoundationClasses\/TKernel\/Standard/);
  assert.match(error.message, /git submodule update --init --recursive occt/);
});

test("createMissingWindowsEmsdkError explains how to install the local toolchain", () => {
  const error = createMissingWindowsEmsdkError("E:/repo/build/wasm/emsdk/emsdk_env.bat");
  assert.match(error.message, /build\/wasm\/emsdk\/emsdk_env\.bat/);
  assert.match(error.message, /tools\\setup_emscripten_win\.bat/);
});

test("createMissingTypeDefinitionsError explains how to restore tracked type definitions", () => {
  const error = createMissingTypeDefinitionsError("E:/repo/dist/occt-js.d.ts");
  assert.match(error.message, /dist\/occt-js\.d\.ts/);
  assert.match(error.message, /git restore --source=HEAD -- dist\/occt-js\.d\.ts/);
});

test("assertOcctSubmodulePresent fails when occt sources are missing", () => {
  const repoRoot = mkdtempSync(path.join(tmpdir(), "occt-js-prereqs-"));
  assert.throws(
    () => assertOcctSubmodulePresent(repoRoot),
    /git submodule update --init --recursive occt/
  );
});

test("assertWindowsEmsdkPresent fails when the local emsdk is missing", () => {
  const repoRoot = mkdtempSync(path.join(tmpdir(), "occt-js-prereqs-"));
  mkdirSync(path.join(repoRoot, "occt", "src", "FoundationClasses", "TKernel", "Standard"), { recursive: true });
  assert.throws(
    () => assertWindowsEmsdkPresent(repoRoot),
    /tools\\setup_emscripten_win\.bat/
  );
});

test("assertTypeDefinitionsPresent fails when tracked type definitions are missing", () => {
  const repoRoot = mkdtempSync(path.join(tmpdir(), "occt-js-prereqs-"));
  assert.throws(
    () => assertTypeDefinitionsPresent(repoRoot),
    /git restore --source=HEAD -- dist\/occt-js\.d\.ts/
  );
});

test("assertTypeDefinitionsPresent does not require built dist runtime artifacts", () => {
  const repoRoot = mkdtempSync(path.join(tmpdir(), "occt-js-prereqs-"));
  const typesPath = path.join(repoRoot, "dist", "occt-js.d.ts");

  mkdirSync(path.dirname(typesPath), { recursive: true });
  writeFileSync(typesPath, "export {};\n");

  assert.doesNotThrow(() => assertTypeDefinitionsPresent(repoRoot));
});
