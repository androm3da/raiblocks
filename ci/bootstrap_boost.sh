#!/usr/bin/env bash


BOOST_BASENAME=boost_1_66_0
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}
BOOST_URL=http://sourceforge.net/projects/boost/files/boost/1.66.0/${BOOST_BASENAME}.tar.gz/download

set -o nounset
set -o errexit

wget -O ${BOOST_BASENAME}.tar.gz ${BOOST_URL}
tar xf ${BOOST_BASENAME}.tar.gz
cd ${BOOST_BASENAME}
rm -f project-config.jam
./bootstrap.sh
./b2 --with-atomic --with-chrono --with-filesystem --with-log \
     --with-program_options --with-regex --with-system --with-thread \
     --no-samples --no-tests link=static threading=multi \
     --prefix=${BOOST_ROOT} install

rm -rf ${BOOST_BASENAME}
rm -f ${BOOST_BASENAME}.tar.gz
cd ..


mkdir -p ${HOME}/boost
touch ${HOME}/boost/.built
