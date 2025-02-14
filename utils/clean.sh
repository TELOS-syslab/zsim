#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

if [ -z "$ZSIMPATH" ]; then
    ZSIMPATH='.'
fi

NUMCPUS=$(grep -c ^processor /proc/cpuinfo)

if [ "$1" = "all" ]
then
	echo "Cleaning all ..."
	#export LIBCONFIGPATH
	#cd $LIBCONFIGPATH
	#make clean-libtool
	#cd -
	cd $ZSIMPATH
	scons -c
	rm -rf $ZSIMPATH/build/ $ZSIMPATH/bin/
	cd -
else
	echo "Cleaning Zsim ..."	
	cd $ZSIMPATH
	scons -c
	rm -rf $ZSIMPATH/build/ $ZSIMPATH/bin/
	cd -
fi

