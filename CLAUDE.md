# Shack Bench — notes for Claude

The repo folder is still named `TciMonitor`, but the product, CMake project and executable are `ShackBench` (GitHub: nigelfenton/shack-bench).

## Git workflow

- **Docs-only changes** (`README.md`, `docs/`, this file) are pushed straight to `main`. The "changes must be made through a pull request" ruleset bypass warning is expected for these — no need to flag it.
- **Everything else** (`src/`, `tests/`, `CMakeLists.txt`, `.github/`, `installer/`, `resources/`) goes via a branch and a pull request.
