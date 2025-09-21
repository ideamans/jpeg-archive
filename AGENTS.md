# Repository Guidelines

## Project Structure & Module Organization
Top-level CLI utilities (`jpeg-recompress`, `jpeg-compare`, `jpeg-hash`, and the `jpeg-archive` helper script) live at the repo root. Core C modules reside in `src/` (utilities, editing, hashing, smallfry) alongside the bundled `src/iqa` perceptual quality library. Tests live in `test/`, combining C harnesses (`test/test.c`, `test/libjpegarchive.c`) and shell runners (`test.sh`, `memory_leak_test.sh`) that populate `test-output/`.

## Build, Test, and Development Commands
Run `make build` once to fetch mozjpeg v4.1.5 into `deps/built/mozjpeg`. `make all` builds the binaries and `libjpegarchive.a`, while `make install PREFIX=/path` stages them locally. `make test` compiles the harnesses and executes `test/test.sh`; use `make clean` before release builds to clear artifacts.

## Coding Style & Naming Conventions
All C sources compile with `-std=c99 -Wall -O3 -fPIC`; match that toolchain and keep warnings clean. Follow the existing 4-space indentation with braces on the same line (`void func(void) {`). Prefer lowerCamelCase for functions, snake_case for locals, and ALL_CAPS for macros, and keep CLI flags hyphen-case. Use concise block comments only when intent is non-obvious, and reuse helpers in `util.c` when emitting errors.

## Testing Guidelines
`make test` builds the binaries, runs unit exercises, and post-processes sample JPEGs; it downloads `test/test-files.zip` on demand, so ensure network access or pre-populate the folder. For focused work on the IQA library, run `cd src/iqa && RELEASE=1 make && make test`. Add deterministic fixtures to `test/test-files/`, keep artifacts small, and run `test/memory_leak_test.sh` or the platform tools from `CI.md` when tracking leaks.

## Commit & Pull Request Guidelines
Commit summaries are terse and action-oriented; follow `area: change` or short imperative phrases (Japanese or English) under ~60 characters, with additional detail in the body if needed. Reference issue IDs in the body (`Refs #123`) and call out new runtime or build requirements. Pull requests should link the motivating issue, list the tests you ran (`make test`, `src/iqa make test`, etc.), and highlight any metrics reviewers should verify.

## Dependency Notes & Configuration
mozjpeg is pinned to v4.1.5; install `cmake` and `nasm` (e.g., `brew install cmake nasm` or `apt install cmake nasm`) before `make build`. GCC or Clang both work; set `CC=clang` when you want analyzer diagnostics. Regenerate `deps/` via `make build` rather than committing artifacts, and document toolchain tweaks in `CI.md`.
