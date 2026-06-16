[![Actions Status](https://github.com/filipdutescu/modern-cpp-template/workflows/MacOS/badge.svg)](https://github.com/filipdutescu/modern-cpp-template/actions)
[![Actions Status](https://github.com/filipdutescu/modern-cpp-template/workflows/Windows/badge.svg)](https://github.com/filipdutescu/modern-cpp-template/actions)
[![Actions Status](https://github.com/filipdutescu/modern-cpp-template/workflows/Ubuntu/badge.svg)](https://github.com/filipdutescu/modern-cpp-template/actions)
[![codecov](https://codecov.io/gh/filipdutescu/modern-cpp-template/branch/master/graph/badge.svg)](https://codecov.io/gh/filipdutescu/modern-cpp-template)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/filipdutescu/modern-cpp-template)](https://github.com/filipdutescu/modern-cpp-template/releases)

# Modern C++ Template

A compact starting point for modern CMake-based C++ projects.

## Features

* Modern **CMake** project layout with install/export support
* **CPM.cmake v0.42.0** as the only dependency mechanism
* **Catch2 v3.11.0** as the only unit test framework
* Automatic C++ standard selection: requires **at least C++20**, and upgrades to **C++23** or **C++26** when the active compiler supports it
* Optional **Clang-Tidy** and **Cppcheck** integration
* Optional **Doxygen** documentation target
* Optional **code coverage**, **LTO**, **ASAN**, and **ccache**
* GitHub Actions workflows for **Ubuntu**, **macOS**, **Windows**, and tagged releases
* A permissive [Unlicense](https://unlicense.org/) license

## Requirements

To use the template as shipped, you need:

* **CMake 3.25+**
* A C++ compiler with **at least C++20** support
* Network access during configure when CPM needs to download dependencies

At configure time, the template inspects `CMAKE_CXX_COMPILE_FEATURES` and selects:

* `C++26` when `cxx_std_26` is available
* otherwise `C++23` when `cxx_std_23` is available
* otherwise `C++20`

If the compiler cannot support C++20, configuration fails with a clear error.

## Getting Started

Create your project from this template, then rename the project-specific placeholders:

1. Change the project name in [CMakeLists.txt](CMakeLists.txt) from `Project` to your real project name.
2. Create `include/<your-project>/` and move or rename the sample headers accordingly.
3. Update [cmake/SourcesAndHeaders.cmake](cmake/SourcesAndHeaders.cmake) with your real source, header, and test files.
4. Rename [cmake/ProjectConfig.cmake.in](cmake/ProjectConfig.cmake.in) so the file name starts with your exact project name, for example `cmake/MyNewProjectConfig.cmake.in`.
5. Update any remaining `Project_...` option names in workflows, scripts, and docs once the project name changes.

The template defaults to a library build. If you want to build an executable, add a `src/main.cpp` and enable `Project_BUILD_EXECUTABLE=ON`.

## Building

```bash
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX=/absolute/path/to/install \
  -DCPM_SOURCE_CACHE=$HOME/.cache/CPM

cmake --build build
cmake --build build --target install
```

`CPM_SOURCE_CACHE` is optional, but strongly recommended for faster repeated builds and CI runs.

If you only want to build the library without tests:

```bash
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX=/absolute/path/to/install \
  -DCPM_SOURCE_CACHE=$HOME/.cache/CPM \
  -DProject_ENABLE_UNIT_TESTING=OFF
```

## Testing

Unit testing is enabled by default and uses **Catch2 v3** through CPM. No preinstalled Catch2 package is required.

```bash
cmake -S . -B build -DCPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --build build
ctest --test-dir build -C Debug -VV
```

If you prefer to skip tests entirely:

```bash
cmake -S . -B build -DProject_ENABLE_UNIT_TESTING=OFF
```

## Documentation

Enable Doxygen and build the `doxygen-docs` target:

```bash
cmake -S . -B build \
  -DProject_ENABLE_DOXYGEN=ON \
  -DProject_ENABLE_UNIT_TESTING=OFF

cmake --build build --target doxygen-docs
```

This generates documentation under `docs/`.

## Common Options

The main project options live in [cmake/StandardSettings.cmake](cmake/StandardSettings.cmake).

Useful examples:

* `Project_BUILD_EXECUTABLE=ON`
* `Project_BUILD_HEADERS_ONLY=ON`
* `Project_ENABLE_UNIT_TESTING=OFF`
* `Project_ENABLE_CLANG_TIDY=ON`
* `Project_ENABLE_CPPCHECK=ON`
* `Project_ENABLE_CODE_COVERAGE=ON`
* `Project_ENABLE_DOXYGEN=ON`
* `Project_ENABLE_LTO=ON`
* `Project_ENABLE_ASAN=ON`

## Make Targets

The included [Makefile](Makefile) provides shortcuts for local development:

* `make test`
* `make coverage`
* `make docs`
* `make install`
* `make format`

These targets use a local `.cpm-cache/` directory automatically.

## Continuous Integration

GitHub Actions workflows are included for:

* Ubuntu build, test, coverage, and install
* macOS build, test, and install
* Windows build, test, and install
* Tagged release archives

All workflows cache `CPM_SOURCE_CACHE` so Catch2 and future CPM-managed dependencies are reused across runs.

To skip CI on a particular commit, include either `[skip ci]` or `[ci skip]` in the commit message.

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for contribution and pull request guidelines.

## Versioning

This project follows [SemVer](https://semver.org/).

## Authors

* **Filip-Ioan Dutescu** - [@filipdutescu](https://github.com/filipdutescu)

## License

This template is licensed under the [Unlicense](https://unlicense.org/). See [LICENSE](LICENSE) for details.
