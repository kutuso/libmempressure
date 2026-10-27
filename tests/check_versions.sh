#!/bin/sh
set -eu
root="$1"
cmake_v=$(sed -n 's/^  VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -1)
header_v=$(sed -n 's/^#define MP_VERSION "\(.*\)"$/\1/p' "$root/include/mempressure.h")
pyproject_v=$(sed -n 's/^version = "\(.*\)"$/\1/p' "$root/pyproject.toml")
spec_v=$(sed -n 's/^Version: *\(.*\)$/\1/p' "$root/packaging/rpm/libmempressure.spec")
deb_v=$(sed -n '1s/^libmempressure (\([^)-]*\).*$/\1/p' "$root/debian/changelog")
status=0
for pair in "header:$header_v" "pyproject:$pyproject_v" "rpm spec:$spec_v" "debian changelog:$deb_v"; do
    name=${pair%%:*}
    value=${pair#*:}
    if [ "$value" != "$cmake_v" ]; then
        echo "version mismatch: $name is '$value', CMakeLists.txt is '$cmake_v'" >&2
        status=1
    fi
done
exit $status
