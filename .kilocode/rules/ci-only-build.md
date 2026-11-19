# CI-only Android build policy

This project is developed from a phone-first environment.

Mandatory rules:
1. Never compile Android code locally.
2. Never run ./gradlew assembleDebug, assembleRelease, build, test, or CMake builds locally.
3. Never install Android Studio, Android SDK, NDK, or large build toolchains locally.
4. Use GitHub Actions for all compilation and test execution.
5. Do not download large Android toolchains to the phone.
6. Inspect source changes statically and use git diff --check locally.
7. Preserve the existing project architecture unless a requested change requires otherwise.
8. Never put signing credentials or keystores in Git.
9. Report CI failures accurately. Do not claim success without a successful workflow.
