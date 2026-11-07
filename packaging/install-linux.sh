#!/usr/bin/env bash
#----------------------------------------------------------------------------#
#  Installatore del plugin Govee per OpenRGB (Linux)                         #
#  Uso: bash install.sh                                                      #
#  Copia libGoveePlugin.so in ~/.config/OpenRGB/plugins/                     #
#----------------------------------------------------------------------------#
set -e

PLUGIN_DIR="$HOME/.config/OpenRGB/plugins"
SO="libGoveePlugin.so"

if [ ! -f "$SO" ]; then
    echo "ERRORE: $SO non trovato accanto a questo script." >&2
    echo "Esegui questo script dalla cartella dove hai estratto il file." >&2
    exit 1
fi

mkdir -p "$PLUGIN_DIR"

# Backup dell'eventuale versione precedente
if [ -f "$PLUGIN_DIR/$SO" ]; then
    BAK="$PLUGIN_DIR/$SO.bak-$(date +%Y%m%d-%H%M%S)"
    cp "$PLUGIN_DIR/$SO" "$BAK"
    echo "Backup della versione precedente salvato in:"
    echo "  $BAK"
    # rimuovo il file prima di copiare: sovrascriverlo mentre OpenRGB e' aperto puo' farlo andare in crash
    rm -f "$PLUGIN_DIR/$SO"
fi

cp "$SO" "$PLUGIN_DIR/$SO"

echo
echo "Installato: $PLUGIN_DIR/$SO"
echo
echo "Prossimi passi:"
echo "  1. Chiudi del tutto OpenRGB (anche dall'area di notifica) e riavvialo: openrgb --startminimized"
echo "  2. Impostazioni -> Plugin -> spunta 'Attivato' su Govee"
echo "  3. Chiudi l'app Govee sul telefono (la striscia accetta un solo collegamento)"
echo "  4. Il dispositivo 'Govee H613A' compare nell'elenco; la scheda 'Govee' mostra lo stato"
