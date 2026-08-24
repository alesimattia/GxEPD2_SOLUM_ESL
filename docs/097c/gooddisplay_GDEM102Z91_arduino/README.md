# Demo Arduino Good Display GDEM102Z91 — driver SSD1677 BWR di riferimento

Origine: <https://www.good-display.com/product/487.html>, risorsa "Arduino Sample Code"
(`A32-GDEM102Z91.rar`, 2023-07-24). Qui stanno i cinque sorgenti del driver; il file
`Ap_29demo.h` dell'archivio originale è escluso: sono 787 KB di bitmap di esempio, senza valore
per noi.

## Perchè questo materiale sta nel repo

Il pannello **non è** il nostro: GDEM102Z91 è un 10.2" **960 × 640** con pitch 0,2245 mm, mentre la
9.7" SOLUM è **960 × 672** con pitch 0,210 mm. Non è un rimarchio.

Quello che è direttamente riapplicabile è il **codice**: stesso controller **SSD1677**, stessa
architettura a **due piani di RAM** (`0x24` bianco/nero, `0x26` accent), stesso FPC a 24 pin. Gira
sulla 9.7" cambiando tre valori:

| Registro | GDEM102Z91 | SOLUM 9.7" |
|---|---|---|
| `0x01` MUX | `7F 02 00` (639) | `9F 02 00` (671) |
| `0x45` finestra Y | fino a `0x27F` = 639 | fino a `0x29F` = 671 |
| `0x4F` cursore Y | `7F 02` | `9F 02` |

Attenzione a `0x11`: qui vale `0x01`, nell'init di fabbrica SOLUM vale `0x02`. Cambia il verso di
scan, e con esso il senso di `0x45` e `0x4F` — vanno cambiati insieme, non uno alla volta.

## Cosa conferma

- **`0x0C` soft start `AE C7 C3 C0 80`**, ultimo byte compreso. GxEPD2 `GDEH116T91` usa `0x40`; il
  valore dell'init di fabbrica SOLUM è quello che usa anche Good Display su un pannello BWR.
- **`0x3C = 0x01`** commentato nel sorgente **"LUT1, for white"**: conferma indipendente della
  numerazione delle LUT della Table 6-4 del datasheet SSD1677, su cui è costruita la sonda
  `examples/097c/panel_diagnostic`.
- Il piano accent si scrive **invertito** (`EPD_W21_WriteDATA(~datasRW[i])`), come fa il driver
  della libreria.
- Nessuna LUT caricata via `0x32`: la waveform è in OTP.

Licenza: sorgenti pubblicati da Dalian Good Display come materiale di supporto ai propri pannelli,
archiviati qui a scopo di riferimento tecnico.
