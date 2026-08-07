#!/usr/bin/env bash
#
#   The Collapse of The Wave Function - build, check, and optionally start the thing.
#
#   Copyright (C) 2026 doctorspider42. GPLv3-or-later, same as the rest of Source/.
#
#   The whole litany in one command:
#
#       ./build.sh                 configure if needed, build, run the offline checks
#       ./build.sh --run           ... and start the standalone when they pass
#       ./build.sh --release       with link-time optimisation, for a binary to hand out
#       ./build.sh --shot          also render the panel to docs/panel.png
#       ./build.sh --sweep         also print the level curve of each state
#       ./build.sh --cab           also print the cabinet's magnitude response
#       ./build.sh --clean --run   from scratch
#
#   LTO is off unless asked for: it costs minutes on every link and buys a few per cent of
#   CPU in the finished binary, so it belongs on the build you ship and nowhere else.

set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

build_type=Release
build_dir="$here/build"
lto=OFF
install_vst3=ON
run_it=false
take_shot=false
run_sweep=false
run_cab=false
run_checks=true
do_clean=false

usage()
{
    cat <<'EOF'
The Collapse of The Wave Function - build, check, and optionally start the thing.

    ./build.sh                 configure if needed, build, run the offline checks
    ./build.sh --run           start the standalone once everything has passed
    ./build.sh --release       build with LTO (slow links, faster binary)
    ./build.sh --shot          render the panel to docs/panel.png
    ./build.sh --sweep         print the level curve of each state (for Shapers.h)
    ./build.sh --cab           print the cabinet's magnitude response
    ./build.sh --clean --run   from scratch

Options:
  --run          start the standalone once everything has built and passed
  --release      build with LTO
  --debug        build unoptimised, into build-debug/
  --shot         render the panel to docs/panel.png
  --sweep        run the makeup-gain sweep
  --cab          run the cabinet response measurement
  --no-check     skip the offline DSP checks
  --no-install   do not copy the VST3 into ~/.vst3
  --clean        delete the build directory first
  -h, --help     this
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --run)        run_it=true ;;
        --release)    lto=ON ;;
        --debug)      build_type=Debug; build_dir="$here/build-debug" ;;
        --shot)       take_shot=true ;;
        --sweep)      run_sweep=true ;;
        --cab)        run_cab=true ;;
        --no-check)   run_checks=false ;;
        --no-install) install_vst3=OFF ;;
        --clean)      do_clean=true ;;
        -h|--help)    usage; exit 0 ;;
        *)            echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

if [[ "$do_clean" == true ]]; then
    step "removing $build_dir"
    rm -rf "$build_dir"
fi

if [[ ! -f "$here/libs/JUCE/CMakeLists.txt" ]]; then
    step "fetching JUCE"
    git -C "$here" submodule update --init --recursive
fi

# ---------------------------------------------------------------------------
# Configure only when something it depends on has actually changed. A no-op reconfigure
# costs about as long as an incremental build does.
# ---------------------------------------------------------------------------
cached()
{
    [[ -f "$build_dir/CMakeCache.txt" ]] \
        && grep -qx "$1" "$build_dir/CMakeCache.txt"
}

# Only stamped into release builds. The build ID is a compile definition on the target
# JUCE itself is compiled into, so changing it rebuilds the whole framework - a fine price
# for a binary you are about to hand to someone, a ridiculous one on every commit.
build_id=""

if [[ "$lto" == ON ]]; then
    build_id="$(git -C "$here" rev-parse --short=7 HEAD 2>/dev/null || echo local)"
fi

if ! cached "CMAKE_BUILD_TYPE:STRING=$build_type" \
   || ! cached "COLLAPSE_LTO:BOOL=$lto" \
   || ! cached "COLLAPSE_BUILD_TOOLS:BOOL=ON" \
   || ! cached "COLLAPSE_BUILD_ID:STRING=$build_id" \
   || ! cached "COLLAPSE_COPY_AFTER_BUILD:BOOL=$install_vst3"; then

    step "configuring ($build_type, LTO $lto)"

    generator=()
    if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
        generator=(-G Ninja)
    fi

    cmake -B "$build_dir" -S "$here" "${generator[@]}" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCOLLAPSE_BUILD_TOOLS=ON \
        -DCOLLAPSE_LTO="$lto" \
        -DCOLLAPSE_COPY_AFTER_BUILD="$install_vst3" \
        -DCOLLAPSE_BUILD_ID="$build_id"
fi

step "building"
started=$SECONDS
cmake --build "$build_dir" --config "$build_type" --parallel
printf '\nbuilt in %d s\n' "$((SECONDS - started))"

artefacts="$build_dir/Collapse_artefacts/$build_type"
devtool="$build_dir/CollapseDevTool_artefacts/$build_type/CollapseDevTool"
standalone="$artefacts/Standalone/The Collapse of The Wave Function"

if [[ "$run_checks" == true ]]; then
    step "offline checks"

    if ! "$devtool" check; then
        echo
        echo "checks failed - nothing was started. Pass --no-check to build and run anyway." >&2
        exit 1
    fi
fi

if [[ "$run_sweep" == true ]]; then
    step "level sweep"
    "$devtool" sweep
fi

if [[ "$run_cab" == true ]]; then
    step "cabinet response"
    "$devtool" cab
fi

if [[ "$take_shot" == true ]]; then
    step "rendering the panel"
    mkdir -p "$here/docs"
    ( cd "$here/docs" && "$devtool" shot panel.png )
fi

step "built"

if [[ "$install_vst3" == ON ]]; then
    echo "  VST3 installed into ~/.vst3"
fi

echo "  standalone: $standalone"
echo "  devtool:    $devtool"

if [[ "$run_it" == true ]]; then
    if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
        echo
        echo "no display - the standalone has a window and will not open here." >&2
        exit 1
    fi

    step "starting the standalone"
    exec "$standalone"
fi
