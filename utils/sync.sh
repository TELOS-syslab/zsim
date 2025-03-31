#!/usr/bin/env bash
current_dir=$(dirname $0)
source $current_dir/env.sh

rsync -vhra $ZSIMPATH/. chunk@chunk-legion:/home/chunk/workspace/git/git-own/zsim/ --include='**.gitignore' --exclude='/.git' --filter=':- .gitignore' --delete-after
