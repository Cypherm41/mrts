#!/bin/bash
# ==============================================
#  Medieval Kingdoms - Build Script (Linux)
#  Works on: Arch, Artix, EndeavourOS, Manjaro
# ==============================================

set -e

echo "=============================================="
echo "  Medieval Kingdoms - Linux Build"
echo "=============================================="
echo ""

# --- Check for gcc ---
if ! command -v gcc &> /dev/null; then
    echo "ERROR: gcc not found."
    echo "Install with:  sudo pacman -S gcc"
    exit 1
fi
echo "[OK] gcc found: $(gcc --version | head -n1)"

# --- Check for raylib ---
RAYLIB_OK=0
if pkg-config --exists raylib 2>/dev/null; then
    RAYLIB_OK=1
    echo "[OK] raylib found via pkg-config ($(pkg-config --modversion raylib))"
elif [ -f /usr/include/raylib.h ] || [ -f /usr/local/include/raylib.h ]; then
    RAYLIB_OK=1
    echo "[OK] raylib headers found"
else
    echo ""
    echo "ERROR: raylib not found."
    echo ""
    echo "Install it with ONE of these:"
    echo "  sudo pacman -S raylib          (if available in your repos)"
    echo ""
    echo "If raylib is NOT in your repos, build it from source:"
    echo "  sudo pacman -S git cmake base-devel mesa"
    echo "  git clone --depth 1 https://github.com/raysan5/raylib.git"
    echo "  cd raylib/src && make PLATFORM=PLATFORM_DESKTOP"
    echo "  sudo make install"
    echo ""
    exit 1
fi

echo ""
echo "Compiling..."

# --- Determine link flags ---
if pkg-config --exists raylib 2>/dev/null; then
    CFLAGS="$(pkg-config --cflags raylib)"
    LIBS="$(pkg-config --libs raylib)"
else
    CFLAGS=""
    LIBS="-lraylib -lGL -lm -lpthread -ldl -lrt -lX11"
fi

# --- Compile ---
gcc -std=c11 -O2 -Wall \
    $CFLAGS \
    -o medieval_rts \
    main.c \
    $LIBS -lm

if [ $? -eq 0 ]; then
    echo ""
    echo "=============================================="
    echo "  BUILD SUCCESSFUL"
    echo "=============================================="
    echo ""
    echo "Run the game with:"
    echo "    ./medieval_rts"
    echo ""
    echo "Or use:  ./run_game.sh"
    echo ""
else
    echo ""
    echo "BUILD FAILED - see errors above."
    exit 1
fi
