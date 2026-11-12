#!/bin/sh
# Builds Hoverstrike.v32 with the Vircon32 development tools (compile, assemble,
# png2vircon, wav2vircon and packrom must be on your PATH) and Python 3 with NumPy and Pillow.
set -e
cd "$(dirname "$0")"
python3 tools/make_assets.py assets
mkdir -p obj bin
compile src/main.c -o obj/main.asm
assemble obj/main.asm -o obj/main.vbin
png2vircon assets/vector_lines.png -o obj/vector_lines.vtex
for s in laser tank_boom missile hit crash rack_clear hum blip; do
    wav2vircon assets/$s.wav -o obj/$s.vsnd
done
packrom RomDefinition.xml -o bin/Hoverstrike.v32
echo "Built bin/Hoverstrike.v32"
