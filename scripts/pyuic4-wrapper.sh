#!/bin/sh

PYUIC4=$1
LD_LIBRARY_PATH=$2:$LD_LIBRARY_PATH
PYTHONPATH=$3:$PYTHONPATH
shift 3

export LD_LIBRARY_PATH PYTHONPATH

exec python $(dirname $0)/pyuic4-wrapper.py $@