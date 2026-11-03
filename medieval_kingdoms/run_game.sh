#!/bin/bash
# ==============================================
#  Medieval Kingdoms - Launch Script (Linux)
# ==============================================

cd "$(dirname "$0")"

if [ ! -f ./medieval_rts ]; then
    echo "Game not built yet. Run:  ./build_linux.sh"
    exit 1
fi

echo "Launching Medieval Kingdoms..."
./medieval_rts
