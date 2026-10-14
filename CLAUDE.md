# Shack Bench — notes for Claude

The repo folder is still named `TciMonitor`, but the product, CMake project and executable are `ShackBench` (GitHub: nigelfenton/shack-bench).

## Git workflow

- **Docs-only changes** (`README.md`, `docs/`, this file) are pushed straight to `main`. The "changes must be made through a pull request" ruleset bypass warning is expected for these — no need to flag it.
- **Everything else** (`src/`, `tests/`, `CMakeLists.txt`, `.github/`, `installer/`, `resources/`) goes via a branch and a pull request.

## CI

- `release.yml` is the only workflow and runs **only on `v*` tags or manual dispatch** — PRs get no checks. To test a branch: `gh workflow run release.yml --ref <branch>` (the release job is skipped off-tag, so nothing publishes).
- Stacked PRs: retarget the upper PR to `main` *before* merging the lower one with `--delete-branch`, or GitHub closes it.

## Local build quirk

- Norton on this machine quarantines freshly built `tx_gate_test.exe` (it opens a loopback socket) → later links fail `LNK1104`. Exclude `build-rel\`; CI is the source of truth for that test.
