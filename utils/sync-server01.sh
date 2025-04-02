#!/usr/bin/env bash
current_dir=$(dirname $0)
source $current_dir/env.sh

rsync -azP -e "ssh -p 2211" $ZSIMPATH/output/ zjq@inet.pkusys.org:/chunk/git/git-own/zsim/output/collect/
