#!/bin/bash

# Homebrew pkg-config Pfad für macOS setzen
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:${PKG_CONFIG_PATH}"

# Source files
SOURCE_FILES=(
   rdl.c rdl_compile.c rdl_eval.c  rdl_natives.c
   parser.c lexer.c )

# Header paths
INCLUDE="-I./include/"

# Output paths
OUT_PATH="./build/rdl"

# Compiler
CC="gcc"

# Build functions

build_native() {
    $CC rdlTest.c ${SOURCE_FILES[@]} $INCLUDE -g -o "$OUT_PATH" 

    if [ $? -eq 0 ]; then
        echo "✅: $OUT_PATH"
    else
        echo "❌ FEHLER"
        exit 1
    fi
}

build_tags() {
   /opt/homebrew/bin/ctags -R .
}

build_tags
build_native
if [ $? -eq 0 ]; then
   echo "DONE"
fi

