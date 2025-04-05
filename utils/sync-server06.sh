#!/usr/bin/env bash
current_dir=$(dirname $0)
source $current_dir/env.sh

# rsync -azP -e "ssh -p 2213" $ZSIMPATH/. yxr2@archlab:~/chunk/git/git-own/zsim/ --include='**.gitignore' --exclude='/.git' --filter=':- .gitignore' --delete-after

rsync -azP -e "ssh -p 1242" $ZSIMPATH/src/ yxr2@archlab:~/chunk/git/git-own/zsim/src/
rsync -azP -e "ssh -p 1242" $ZSIMPATH/tests/ yxr2@archlab:~/chunk/git/git-own/zsim/tests/
rsync -azP -e "ssh -p 1242" $ZSIMPATH/utils/ yxr2@archlab:~/chunk/git/git-own/zsim/utils/
