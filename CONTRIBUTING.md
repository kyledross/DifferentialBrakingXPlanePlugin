# Contributing
## Development environment

This is a C++20 CMake project. Configure it with CMake 3.16 or later and a
C++20 compiler. The X-Plane SDK headers required to compile the plugin are
vendored in `SDK/`.

Run a local Release build and test suite with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the portable Linux build and tests with:

```bash
./docker-build.sh
```

The Docker script creates ignored `docker-build/` and `docker-output/`
directories. `docker-output/` contains the files used by the GitHub release
workflow.

## Project conventions

- Keep the plugin compatible with the C++20 standard configured in
  `CMakeLists.txt`.
- Use the vendored X-Plane SDK headers rather than machine-specific SDK paths.
- Keep tests self-contained and runnable with CTest.
- Do not add generated build products, local X-Plane installations, IDE state,
  or Docker output to version control.

## Release workflow

Pushing a tag matching `v*` runs the Linux build and test workflow, packages
the tested output, and creates a draft GitHub release. Review the generated
release notes and publish the draft manually.
