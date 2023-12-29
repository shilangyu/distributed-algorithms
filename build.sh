#!/bin/bash

set -e

MAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}"

if [[ "$MAKE_BUILD_TYPE" != "Debug" && "$MAKE_BUILD_TYPE" != "Release" ]]; then
	echo "BUILD_TYPE can be either Debug or Release"
	exit 1
fi

# Change the current working directory to the location of the present file
cd "$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

rm -rf target
mkdir target
cd target
cmake -DCMAKE_BUILD_TYPE=$MAKE_BUILD_TYPE ..
cmake --build .
mv src/da_proc .
