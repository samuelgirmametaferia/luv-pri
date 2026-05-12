#!/bin/bash
gdb -ex "run" -ex "bt" -ex "quit" --args bin/luvc tests/gm.lv
