#!/usr/bin/env bash

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export NUMA_NODE=1
export ZSIMPATH=$script_dir/..
export PINPATH=$ZSIMPATH/lib/pin-2.14
export DRAMSIMPATH=$ZSIMPATH/lib/dramsim2
export NVMAINPATH=$ZSIMPATH/lib/nvmain
export LIBCONFIGPATH=$ZSIMPATH/lib/libconfig
export BOOSTPATH=
export HDF5PATH=
export GLIBCPATH=

export LD_LIBRARY_PATH=$PINPATH/intel64/lib:$PINPATH/intel64/runtime:$PINPATH/intel64/lib:$PINPATH/intel64/lib-ext:$BOOSTPATH/lib:$HDF5PATH/lib:$LIBCONFIGPATH/lib:/usr/lib/x86_64-linux-gnu:/usr/local/lib:/usr/lib
export LIBRARY_PATH=$BOOSTPATH/lib:$HDF5PATH/lib:$LIBCONFIGPATH/lib
export INCLUDE=$BOOSTPATH/include:$HDF5PATH/include:$LIBCONFIGPATH/include
export CPLUS_INCLUDE_PATH=$BOOSTPATH/include:$HDF5PATH/include:$LIBCONFIGPATH/include

nrun() {
    numactl --cpunodebind=$NUMA_NODE --membind=$NUMA_NODE "$@"
}

mytime () {
     /usr/bin/time -f "real %es user %Us sys %Ss CPU %P\n(%Xtext+%Ddata %Mmax)k\t%Iinputs+%Ooutputs\n(%Fmajor+%Rminor)pagefaults\t%Wswaps" "$@"
}
