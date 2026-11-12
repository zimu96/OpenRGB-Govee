# Protocollo BLE Govee H613A (note di reverse engineering)

- Dispositivo: nome BLE `ihoment_H613A_XXXX`, firmware 1.07.06, hardware 1.00.01. Solo Bluetooth LE.
- Servizio GATT proprietario: `00010203-0405-0607-0809-0a0b0c0d1910`
  - `...2b11`: scrittura (solo write-without-response; con BlueZ/D-Bus `WriteValue` con opzione `type=command`)
  - `...2b10`: notifiche (la striscia non conferma i comandi di colore)
- Pacchetti da 20 byte: byte 0 = `0x33` (comando) o `0xAA` (query/keepalive), ultimo byte = XOR dei primi 19.
  - Accensione/spegnimento: `33 01 01` / `33 01 00`
  - Colore: `33 05 02 R G B`
  - Keepalive: `AA 01 00 ... AB`
- **Keepalive obbligatorio**: la striscia chiude il collegamento dopo circa 11 secondi senza traffico. Un pacchetto `AA 01` ogni 2 secondi mantiene il collegamento stabile (provato oltre 60 s).
- Un solo client BLE alla volta: l'app del telefono e il plugin non possono essere collegati insieme.
- Con BlueZ: segnare il dispositivo come `Trusted` evita che BlueZ lo dimentichi, cosi i collegamenti successivi sono diretti (circa 3 s da zero).
- Riferimento per il formato dei pacchetti: progetto di reverse engineering dell'H6159 (stesso formato 0x33 + XOR).
- Non indagati: luminosita hardware (`33 04 xx`), modalita musica.

## Note aggiunte (da verificare sulla H613A)
Dai progetti pubblici per strisce della stessa famiglia (H6127/H6139/H6159), stesso formato `0x33` + XOR:
- Luminosita hardware: `33 04 LL` (LL = 0..255).
- Scene native: `33 05 04 ID` con ID = 00 sunrise, 01 sunset, 04 movie, 05 dating, 07 romantic, 08 blinking, 09 candlelight, 0f snowflake.
- Colore per segmento (15 segmenti): `33 05 0b R G B MASCHERA_BASSA MASCHERA_ALTA` (bit 0..7 = segmenti 1..8, bit 8..14 = segmenti 9..15). Strisce piu recenti usano la famiglia `33 05 15 ...`.
