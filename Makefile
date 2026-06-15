# ---------------------------------------------------------------------------
# Neurus — Convenience Build Wrapper
#
# This Makefile provides quick-build shortcuts. All real logic is in CMake.
# Requires: cmake >= 3.27, Visual Studio 2022 with C++20 toolchain
# ---------------------------------------------------------------------------

.PHONY: configure build test clean nobuild release help

# --- Debug (default) ---

configure:
	cmake --preset default

build:
	cmake --build build/debug --config Debug

test:
	cd build/debug && ctest -C Debug --output-on-failure

clean:
	cmake --build build/debug --target clean

# --- Release ---

release:
	cmake --preset release && cmake --build build/release --config Release

# --- Visual Studio 2022 (outside source tree) ---

nobuild:
	cmake --preset vs2022
	@echo ""
	@echo "  Visual Studio solution generated at ../Neurus_VS2022/Neurus.sln"
	@echo "  Open it in VS 2022 and press F5 to build and run."
	@echo ""

# --- Help ---

help:
	@echo "Neurus Build System"
	@echo "==================="
	@echo ""
	@echo "  make configure   — Configure Debug build (VS 2022)"
	@echo "  make build       — Build Debug"
	@echo "  make test        — Run tests (Debug)"
	@echo "  make clean       — Clean Debug build"
	@echo "  make release     — Configure + Build Release"
	@echo "  make nobuild     — Generate VS 2022 solution at ../Neurus_VS2022"
	@echo ""
