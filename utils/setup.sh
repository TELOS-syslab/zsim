#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

apt-get install -y build-essential g++-12 gdb scons automake autoconf m4 perl flex bison byacc time
apt-get install -y libconfig-dev libconfig++-dev libhdf5-dev libelf-dev libxerces-c-dev
#apt-get install -y libboost-all-dev libbz2-dev zlib1g-dev libc6-dev libicu-dev
#apt-get install -y libgoogle-glog-dev
apt-get install -y python3 python3-pip && pip3 install scons==4.0.0
ln -s `which python3` /usr/bin/python
ln -s /usr/include/asm-generic /usr/include/asm
git config --global --add safe.directory $ZSIMPATH

