# OpenRGB-Govee

Plugin per OpenRGB che controlla via **Bluetooth LE** le strisce LED Govee **H613A** e le mostra come un normale dispositivo OpenRGB.

> **Cosa e' stato provato:** tutto su una striscia H613A. Scene native e segmenti su altri modelli non sono verificati.

## Funzioni
- Dispositivo "Govee H613A" con 15 modi: **Direct**, **Static**, **Breathing**, **Flashing**, **Strobe**, **Heartbeat**, **Spectrum Cycle**, **Color Fade** (2 colori), **Random**, **Candle**, **Fire**, **Police**, **Aurora**, **Lightning**, **Off**.
- **Luminosita** e **velocita** regolabili dalla scheda del dispositivo, come per gli altri dispositivi OpenRGB (gli effetti sono generati dal plugin, quindi funzionano uguali su ogni striscia).
- Di default **resta collegato** (cambi di colore immediati) e si ricollega da solo se la connessione cade. Opzione per collegarsi solo quando serve e scollegarsi dopo alcuni secondi senza cambi (durante gli effetti resta collegato).
- Parla direttamente con BlueZ (D-Bus), senza Qt Bluetooth; segna la striscia come "fidata" per BlueZ cosi i collegamenti successivi sono diretti (circa 3 s da zero).
- Compatibile con qualunque plugin di OpenRGB che agisce sui dispositivi (dissolvenze di luminosita, profili, sincronizzazioni).
- Scheda "Govee": stato, indirizzo Bluetooth, elenco delle strisce trovate, ordine dei canali colore, pulsanti di prova.

## Novita' 1.1.0
- **Scene native** della striscia (Sunrise, Sunset, Movie, Dating, Romantic, Blinking, Candlelight, Snowflake): modi "Scena: ...", inviati come singolo pacchetto e ripetuti per sicurezza.
- **Direct a segmenti** (sperimentale, 15 segmenti): si attiva dalla scheda Govee e riavviando OpenRGB; pulsante di prova.
- Risposta piu' pronta e affidabile: colore ripetuto all'uscita da una scena, pausa dopo l'accensione, un solo OpenRGB alla volta comanda la striscia (blocco di istanza).

## Requisiti
Linux con BlueZ, Qt6 (con Qt6 DBus) e OpenRGB con plugin API v5.

## Compilare
    cmake -S . -B build && cmake --build build -j$(nproc)
    ./installa.sh

## Note
- La striscia accetta **un solo collegamento alla volta**: chiudi l'app Govee sul telefono.
- Protocollo: servizio `00010203-0405-0607-0809-0a0b0c0d1910`, caratteristica di scrittura `...2b11` (solo scrittura senza risposta); pacchetti da 20 byte, `0x33` iniziale e checksum XOR. Colore: `33 05 02 R G B`; accensione/spegnimento: `33 01 01/00`.
- La striscia chiude il collegamento dopo ~11 s di silenzio: il plugin manda un pacchetto "tienimi sveglio" (`AA 01 ... AB`) ogni 2 s.
- Non incluso: modalita a tempo di musica.

Licenza GPL-2.0-or-later.
