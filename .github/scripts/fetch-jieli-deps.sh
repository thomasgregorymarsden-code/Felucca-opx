#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CI helper: fetch the JieLi Linux toolchain and the AC79 SDK files the build needs, unless the
# cache already restored them. Both come from servers in China that often drop connections from
# GitHub's runners, so each download is retried with a growing pause.
set -e
SDK="$HOME/fw-AC79_AIoT_SDK"
SDK_TAG=AC79NN_SDK_V1.2.1_2023-12-13

retry() {   # retry N CMD...: run CMD up to N times
    n=$1; shift; i=1
    while :; do
        "$@" && return 0
        [ "$i" -ge "$n" ] && { echo "failed after $n attempts: $*"; return 1; }
        echo "attempt $i failed; retrying in $((i * 20)) s"
        sleep $((i * 20)); i=$((i + 1))
    done
}

fetch_sdk() {
    rm -rf /tmp/sdk
    git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=60 clone --depth 1 --branch "$SDK_TAG" \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git /tmp/sdk
}

if [ -x "$HOME/.jieli/toolchain/pi32v2/bin/clang" ]; then
    echo "toolchain: from cache"
else
    rm -rf "$HOME/.jieli"
    retry 6 tools/get_toolchain.sh
fi

if [ -f "$SDK/cpu/wl82/tools/uboot.boot" ]; then
    echo "SDK: from cache"
else
    retry 6 fetch_sdk
    mkdir -p "$SDK/cpu/wl82"
    mv /tmp/sdk/cpu/wl82/tools "$SDK/cpu/wl82/"   # the only SDK files the package uses
    rm -rf /tmp/sdk
fi

for f in uboot.boot cfg_tool.bin cfg/eq_cfg_hw.bin; do
    test -f "$SDK/cpu/wl82/tools/$f" || { echo "SDK file missing: $f"; exit 1; }
done
echo "JieLi toolchain and SDK files ready"
