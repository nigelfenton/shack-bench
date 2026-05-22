#pragma once

// Compile-time identification of this TCI Monitor binary. Populated by CMake
// from the git working tree at configure time, plus the compiler's __DATE__
// and __TIME__ at the moment BuildInfo.cpp itself was compiled.
//
// "git" values are captured the last time `configure.bat` (or `cmake -B
// build`) was re-run — if you only rebuild without reconfiguring, the hash
// may be stale. The compile date catches that on the rebuild side.

namespace TciMon {

extern const char* const kBuildGitHash;     // short SHA, or "unknown"
extern const char* const kBuildGitDate;     // ISO 8601 commit date, or "unknown"
extern const char* const kBuildGitBranch;   // branch name, or "unknown"
extern const char* const kBuildGitDirty;    // "yes" if working tree had local mods at configure
extern const char* const kBuildDate;        // __DATE__ " " __TIME__
extern const char* const kBuildHostOs;      // "Windows" / "macOS" / "Linux" / "unknown"

} // namespace TciMon
