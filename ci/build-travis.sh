#!/bin/bash

qt_dir=${1}
src_dir=${2}

set -e
OS=`uname`

mkdir build
pushd build

cmake \
    -DACTIVE_NETWORK=rai_test_network \
    -DRAIBLOCKS_TEST=ON \
    -DRAIBLOCKS_GUI=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT=/usr/local \
    -DQt5_DIR=${qt_dir} \
    ..


if [[ "$OS" == 'Linux' ]]; then
    make -j2
else
    sudo make -j2
fi

# Exclude flaky or stalling tests.
#./core_test --gtest_filter="-gap_cache.gap_bootstrap:bulk_pull.get_next_on_open:system.system_genesis"
${src_dir}/ci/test.sh || /bin/true

popd
