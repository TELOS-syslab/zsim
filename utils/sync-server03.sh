#!/usr/bin/env bash
current_dir=$(dirname $0)
source $current_dir/env.sh

rsync -azP -e "ssh -p 2213" $ZSIMPATH/. zjq@inet.pkusys.org:~/chunk/git/git-own/zsim/ --include='**.gitignore' --exclude='/.git' --filter=':- .gitignore' --delete-after

rsync -azP -e "ssh -p 2213" $ZSIMPATH/src/ zjq@inet.pkusys.org:~/chunk/git/git-own/zsim/src/
rsync -azP -e "ssh -p 2213" $ZSIMPATH/tests/ zjq@inet.pkusys.org:~/chunk/git/git-own/zsim/tests/
rsync -azP -e "ssh -p 2213" $ZSIMPATH/utils/ zjq@inet.pkusys.org:~/chunk/git/git-own/zsim/utils/

rsync -azP -e "ssh -p 2213" $ZSIMPATH/output/results/ zjq@inet.pkusys.org:/mnt/sdb3/result/