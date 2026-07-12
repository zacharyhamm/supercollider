#!/bin/zsh

set -e

repo_dir="${0:A:h}"
build_dir="$repo_dir/build-make"
app="$build_dir/Install/SuperCollider/SuperCollider/SuperCollider.app/Contents/MacOS/SuperCollider"

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    print -u2 "CMake build directory not configured: $build_dir"
    print -u2 "Configure the project before launching it."
    exit 1
fi

# CMake's incremental build checks whether any source or generated input has
# changed, so this is a no-op when the installed app is already up to date.
cmake --build "$build_dir" --target install

if [[ ! -x "$app" ]]; then
    print -u2 "SuperCollider executable not found: $app"
    exit 1
fi

nohup "$app" >/tmp/supercollider-ide.log 2>&1 &
disown
