#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
YOUNGSERIF_FONT_SIZES=(12 14 16 18)
DMSANS_FONT_SIZES=(12 14 16 18)

for size in ${YOUNGSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="youngserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/YoungSerif/YoungSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

for size in ${DMSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="dmsans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/DMSans/DMSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(8 10 12 21 32)
UI_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="steinem_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Steinem/Steinem-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"

    python fontconvert.py $font_name $size $font_path --mono > $output_path
    echo "Generated $output_path"
  done
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
