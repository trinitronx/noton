#!/bin/bash

# format code
clang-format -i noton.c

# remove old
rm ./noton

# debug(slow)
cc -std=c89 -DDEBUG -Wall -Wno-unknown-pragmas -Wpedantic -Wshadow -Wextra -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion -Wvla -g -Og -fsanitize=address -fsanitize=undefined noton.c -L/usr/local/lib -lSDL2 -lportmidi -o noton

# build(fast)
# cc noton.c -std=c89 -Os -DNDEBUG -g0 -s -Wall -Wno-unknown-pragmas -L/usr/local/lib -lSDL2 -lportmidi -o noton

# Size
echo "Size: $(du -sk ./noton)"

if [ -d "$HOME/bin" ] && [ -e ./noton ]
then
	cp ./noton $HOME/bin
    echo "Installed: $HOME/bin" 
fi

# run
./noton
