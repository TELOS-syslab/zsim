#!/bin/bash
current_dir=$(dirname $0)
source $current_dir/env.sh

NUMCPUS=$(grep -c ^processor /proc/cpuinfo)
buildtype=debug
if [ "$1" = "all" ]
then
	echo "Compiling all ..."
	
	#export LIBCONFIGPATH
	#cd $LIBCONFIGPATH
	#./configure --prefix=$LIBCONFIGPATH && make install
	#cd -

	# cd $ZSIMPATH/contrib/boost_1_59_0
	# ./bootstrap./sh
	# ./b2 --toolset=gcc-4.8 --clean-all -n
	# ./b2 --toolset=gcc-4.8 --buildtype=complete --cxxflags=" -g -std=c++0x -fabi-version=2 -D_GLIBCXX_USE_CXX11_ABI=0" -j $NUMCPUS
	# ./b2 --toolset=gcc-4.8 --buildtype=complete --prefix=$BOOST install
	# cd -

	cd $ZSIMPATH
	scons -j$NUMCPUS
	if [ -d $ZSIMPATH/bin ]
	then
		rm -rf $ZSIMPATH/bin
	fi
	cd -

	mkdir $ZSIMPATH/bin && cd $ZSIMPATH/bin
	ln -s ../build/opt/libzsim.so
	ln -s ../build/opt/zsim
	cd -
else
	echo "Compiling only ZSim ..."
	if [ "$buildtype" = "opt" ]
	then
		cd $ZSIMPATH
		scons --o -j$NUMCPUS
		if [ -d $ZSIMPATH/bin ]
		then
			rm -rf $ZSIMPATH/bin
		fi
		cd -

		mkdir $ZSIMPATH/bin && cd $ZSIMPATH/bin
		ln -s ../build/opt/libzsim.so
		ln -s ../build/opt/zsim
		cd -
	else 
		if [ "$buildtype" = "debug" ]
		then
			cd $ZSIMPATH
			scons --d -j$NUMCPUS
			if [ -d $ZSIMPATH/bin ]
			then
				rm -rf $ZSIMPATH/bin
			fi
			cd -
			
			mkdir $ZSIMPATH/bin && cd $ZSIMPATH/bin
			ln -s ../build/debug/libzsim.so
			ln -s ../build/debug/zsim
			cd -
		fi
	fi
	
	
fi
