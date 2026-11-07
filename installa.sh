#!/usr/bin/env bash
# Installa il plugin Govee in OpenRGB (poi riavvia OpenRGB).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/.config/OpenRGB/plugins"
mkdir -p "$DEST"
cp "$DIR/libGoveePlugin.so" "$DEST/libGoveePlugin.so.new"
mv "$DEST/libGoveePlugin.so.new" "$DEST/libGoveePlugin.so"
echo "Installato in $DEST. Riavvia OpenRGB."
