#!/bin/sh

# git clone git://jausoft.com/srv/scm/cgit.git
# cd cgit/
# git checkout --track -b jau_config origin/jau_config
# git submodule init
# git submodule update

cp scripts/cgit.conf .
make NO_LUA=1

