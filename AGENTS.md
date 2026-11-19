# NOAT Project Instructions

## Build environment
- GitHub Actions is the official build environment.
- Do not run Android builds on the developer's phone.
- Do not install Android SDK, NDK, CMake, or Android Studio locally.
- Do not run Gradle compilation or native compilation locally.
- Use GitHub Actions to validate all build changes.

## Project preservation
- Preserve the existing application ID and namespace.
- Preserve minSdk 31 and targetSdk 36 unless explicitly requested.
- Preserve existing application behavior and resources.
- Preserve all configured native ABIs.
- Keep native code compatible with C++17.
- Do not replace existing application source code unnecessarily.

## Git workflow
- Work on feature branches.
- Never force-push.
- Never commit secrets, signing keys, tokens, or credentials.
- Inspect diffs and run git diff --check before committing.
- Use pull requests targeting main.
- Do not claim a build succeeded until GitHub Actions confirms it.
