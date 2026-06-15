---
name: Bug Report
about: Report a crash, rendering artifact, or incorrect behavior
title: ''
labels: bug
assignees: ''

---

## Description

A clear and concise description of the bug.

## Reproduction

Steps to reproduce:
1. Configure with `cmake --preset default`
2. Build with `cmake --build build/debug`
3. Run `./build/debug/Neurus.exe`
4. ...

## Expected Behavior

What you expected to happen.

## Actual Behavior

What actually happened. If this is a rendering issue, include screenshots if helpful.

## Environment

- **OS**: (e.g., Windows 11 23H2)
- **Vulkan SDK**: (e.g., 1.4.304.0)
- **GPU / Driver**: (e.g., NVIDIA RTX 4080, driver 565.90)
- **Build config**: Debug / Release
- **Qt version**: (e.g., 6.8.0)
- **CMake version**: (e.g., 3.29.0)

## Logs / Validation Layers

```
Paste any terminal output, Vulkan validation layer messages,
or debug assertion failures here.
```

## Checklist

- [ ] I can reproduce with a clean build (`cmake --build build/debug --clean-first`)
- [ ] This bug is reproducible on `main` at the latest commit

## Additional Context

Add any other context here (recent changes that may have introduced this,
frequency of occurrence, etc.).
