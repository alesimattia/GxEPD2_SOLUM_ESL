# Fonti esterne — board Waveshare, pratiche FCC, firmware OpenEPaperLink

Materiale raccolto fuori dai datasheet, con le deduzioni che ne seguono e le prove che
suggerisce. Le fonti primarie sono archiviate in questa cartella: quanto segue serve a sapere
cosa contengono e cosa se ne ricava.

Documenti correlati: [`122c/identificazione_pannello.md`](122c/identificazione_pannello.md),
[`../README.md`](../README.md), [`../README_122c.md`](../README_122c.md).

Datasheet Solomon Systech archiviati qui, e a cosa servono:

| File | Cosa è | Perchè sta qui |
|---|---|---|
| [`SSD1677_Rev1.0_2018-11_Solomon-Systech.pdf`](SSD1677_Rev1.0_2018-11_Solomon-Systech.pdf) | 960 source × 680 gate, BWR, command set SSD16xx | è il controller della 9.7" |
| [`SSD1683_Rev1.0_2021-01_Solomon-Systech.pdf`](SSD1683_Rev1.0_2021-01_Solomon-Systech.pdf) | 400 × 300, stessa famiglia | è l'unico della famiglia che documenti la **cascade** (§6.12), vedi §4 |
| [`SSD2677_Rev1.1_2023-08_Solomon-Systech.pdf`](SSD2677_Rev1.1_2023-08_Solomon-Systech.pdf) | 960 × 680, RAM a **2 bit/pixel**, command set in stile UC | è il chip a 4 colori della stessa taglia, vedi §5 |
| [`SSD2677_Rev1.0_2024-03_Solomon-Systech.pdf`](SSD2677_Rev1.0_2024-03_Solomon-Systech.pdf) | stesso chip, documento più esteso (35 pagine) | contiene la sezione cascade e il command set completo |

Non esiste una revisione pubblica dell'SSD1677 più recente della Rev 1.0: le copie reperibili su
cursedhardware e su e-paper-display.com sono lo stesso file byte per byte.


## 1. Board Waveshare E-Paper ESP32 Driver — wiki ufficiale

Fonte: <https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board>, testo estratto in
[`waveshare-E-Paper_ESP32_Driver_Board_wiki.txt`](waveshare-E-Paper_ESP32_Driver_Board_wiki.txt).

Il wiki conferma il pinout dell'interfaccia e-paper già ricavato dallo schematico — DIN=P14,
SCLK=P13, CS=P15, DC=P27, RST=P26, BUSY=P25 — e **non elenca alcun MISO**: riscontro indipendente
del fatto che il connettore interno non porta una linea di lettura.

**Lo switch n.1 non seleziona il "tipo" di pannello: sceglie la resistenza di sense del booster.**
La tabella del wiki è per posizione, non per diagonale:

| Posizione | Resistenza | Pannelli elencati |
|---|---|---|
| **A** | **3R** | 1.54", 1.54"(B), 2.13", 2.13"(B), 2.66", 2.66"(B), 2.9", 2.9"(B), 3.7", 4.2", 4.2"(B), **13.3"**, **13.3"(B)** |
| **B** | **0.47R** | 2.13"(D), 2.7", 2.9"(D), 4.01"(F), 4.2"(C), 5.65"(F), 5.83", 5.83"(B), 7.5", 7.5"(B) |

I pannelli SOLUM non sono in tabella. Il riferimento più vicino per geometria e silicio sono i
13.3" (SSD1677, 960 source), che stanno in **A**. Il wiki dice esplicitamente di provare l'altra
posizione se il display è anomalo o non si pilota: è un parametro hardware che agisce sul circuito
di boost, quindi va considerato prima di attribuire al driver un refresh debole, un ghosting o una
coda che non risponde.

Altro dal wiki:

- switch n.2 = alimentazione del modulo USB-UART; con lo switch su OFF **non si programma**;
- revisioni della board: **2022-07-28** seriale da CP2102 a **CH343**; **2024-12-30** micro-USB
  sostituito da **USB-C**, hardware per il resto compatibile. La board del progetto è la V3 CH343P
  con micro-USB, cioè fra le due revisioni;
- 4 MB flash, 520 KB SRAM, 29.46 × 48.25 mm, alimentazione 5 V con range 3.6–5.5 V;
- in confezione ci sono anche **adapter board e prolunga FFC**;
- il wiki **non** pubblica il pinout dell'header di espansione: la questione GPIO33 resta decisa
  solo dallo schematico ([`E-Paper_ESP32_Driver_Board_V3.pdf`](E-Paper_ESP32_Driver_Board_V3.pdf)).


## 2. Pratiche FCC del grantee SOLUM (2AFWN)

Elenco completo per le diagonali che interessano, da <https://fccid.io/2AFWN>:

| Modello | Data | Note |
|---|---|---|
| 2AFWN-EL097R2WRN | 2022-09-08 | 9.7", generazione precedente |
| **2AFWN-EL097F5CRC** | **2023-11-28** | 9.7" **Newton Core**, foto interne archiviate qui |
| 2AFWN-EL097F6W4A | 2024-02-29 | 9.7" Newton PRO, etichetta vetro `BWRY` |
| 2AFWN-EL097H2WRN | 2025-03-17 | 9.7", non ancora esaminata |
| 2AFWN-EL116HDBRD | 2023-09-07 | 11.6" |
| 2AFWN-EL116F5CRC | 2023-10-27 | 11.6" Core |
| 2AFWN-EL116F6W4A | 2024-06-21 | 11.6" PRO |
| 2AFWN-EL122R2WRN | 2022-09-08 | 12.2", generazione precedente |
| 2AFWN-EL122H6W4A | 2024-06-27 | 12.2" Newton PRO |
| 2AFWN-EL122H5CRC | 2024-08-29 | 12.2" Newton Core, foto ad alta risoluzione |

### Il codice modello SOLUM si decodifica

§3.6 "Label Marking" del [`Newton-PRO_Data-sheet_C-Lab_G_240320.pdf`](Newton-PRO_Data-sheet_C-Lab_G_240320.pdf)
pubblica la struttura del part number:

```
EL <taglia> <generazione> <colore scocca> <colore display> <tipo tag>
   097       F5            C               4                C
```

- **taglia**: `029` = 2.9", `097` = 9.7", `116` = 11.6", `122` = 12.2";
- **generazione**: `F5`, `F6`, `H3`, `H5`, `H6` — piattaforma della scheda tag;
- **colore scocca**: `W` = white, `B` = black (le unità del progetto portano `C`);
- **colore display**: **`R` = BWR**, **`4` = RED, YELLOW cioè BWRY**;
- **tipo tag**: `A` = Button + NFC + LED.

Applicato ai codici che compaiono in questa tabella:

| Unità | Codice | Campo colore |
|---|---|---|
| 9.7" del progetto | `EL097F5C`**`4`**`C` | `4`, linea PRO |
| 9.7" Core della pratica FCC | `EL097F5C`**`R`**`C` | `R`, BWR |
| 12.2" del progetto | `EL122H6W`**`4`**`A` | `4`, linea PRO |
| 12.2" Core della pratica FCC | `EL122H5C`**`R`**`C` | `R`, BWR |
| 11.6" Core | `EL116F5C`**`R`**`C` | `R`, BWR |

Il 9.7" del progetto e il 9.7" Core della pratica FCC sono l'unica coppia che differisce **in un
solo carattere** — stessa taglia, stessa generazione F5, stessa scocca, stesso tipo tag — e quel
carattere è il campo colore. Le due unità da 12.2" differiscono anche per generazione e scocca.

**Attenzione a quanto vale davvero.** Nel modello table del datasheet PRO *ogni* taglia, da 1.6" a
12.2", è elencata come `...W4A`: la cifra `4` è costante su tutta la linea, e la riga generale
`Display Colors: BWRY` porta la nota *"color options are not available for all sizes"*. La prova
che non è un dato per unità la danno le serigrafie sul vetro della stessa generazione F6:

| Unità | Codice | Serigrafia sul vetro |
|---|---|---|
| 9.7" PRO F6 | `EL097F6W4A` | `NEWTON PRO 9.7" **BWRY** Normal` |
| 11.6" PRO F6 | `EL116F6W4A` | `BWR normal` |
| 12.2" PRO F6 | `EL122H6W4A` | `Newton PRO 12.2" **BWR** normal` |

Stessa cifra `4`, film diversi. Quindi il campo colore **distingue la linea** — PRO nominalmente a
4 colori, Core `R` a 3 — e non il film montato sulla singola unità. Quello lo dice **solo la
serigrafia sul vetro**, che va letta sull'esemplare in mano, e in subordine la sonda.

### 9.7" Core — [`097c/fcc/fcc_2AFWN-EL097F5CRC_internal_photos.pdf`](097c/fcc/fcc_2AFWN-EL097F5CRC_internal_photos.pdf)

Foto a bassa risoluzione (1049 × 787). Immagini estratte:

- [`097c/fcc/097_f5crc_tag_board.jpg`](097c/fcc/097_f5crc_tag_board.jpg) — scheda tag serigrafata
  **`NEWTON_CORE 9.7_TAG_R01, 2023/08/25`**, **una sola** FFC 24 pin, una sola sezione boost;
- [`097c/fcc/097_f5crc_pannello_fronte.jpg`](097c/fcc/097_f5crc_pannello_fronte.jpg)
  — al centro del retro del vetro c'è un'etichetta bianca con **`YMS960672-097AAH-ES-W5`** e la data
  `20230902`. È un part number **di pannello** (960 × 672, 097) del fornitore del vetro, non un
  codice SOLUM; non ha riscontri pubblici. Vale la pena cercare la stessa etichetta sul pannello
  del progetto: identifica il vetro in modo più preciso del codice ESL;
- [`097c/fcc/097_f5crc_esploso.jpg`](097c/fcc/097_f5crc_esploso.jpg) — insieme, cornice e batteria.

La serigrafia dei colori sul vetro in questa pratica **non è leggibile**: la risoluzione è un
quarto di quella delle pratiche Core 12.2". Da qui non arriva nessuna risposta sul quarto colore.

### 12.2" Core

Le foto della pratica sono 4032 × 3024 salvate a strisce da 252 righe dentro il PDF. Ricomposte:

- [`122c/fcc/122_core_tag_board_due_FFC.jpg`](122c/fcc/122_core_tag_board_due_FFC.jpg) — la scheda
  `M3_NEWTON_CORE_12.2_TAG_H07 2024.02.22` intera;
Sulla stessa foto, i due connettori FFC hanno **larghezza e passo identici** (misurati sul profilo
di intensità: stessa larghezza al pixel, ~24 contatti ciascuno), quindi le due code montano lo
stesso connettore. Quello che invece **non** è simmetrico è l'elettronica attorno: vedi §4.


## 3. OpenEPaperLink — firmware dei tag nRF52811

Repo <https://github.com/OpenEPaperLink/Tag_FW_nRF52811>: è il firmware della **famiglia di tag dei
pannelli di questo progetto** (la scheda del 12.2" PRO monta un nRF52811). Sorgenti archiviati in
[`openepaperlink/nrf52811_tag_fw/`](openepaperlink/nrf52811_tag_fw/).

### 3.1 `dualssd.cpp` — due controller SSD su un solo chip select

Il secondo controller **non ha un CS proprio**: si indirizza **sommando 0x80 all'opcode**.

```
CONTROLLER_ONE 0x00 / CONTROLLER_TWO 0x80
0x11 → 0x91   0x44 → 0xC4   0x45 → 0xC5   0x4E → 0xCE   0x4F → 0xCF
0x24 → 0xA4   0x26 → 0xA6
```

Un solo CS, un solo BUSY, un solo RST, un solo bus. Il commento nel sorgente dice *"Those dual SSD
controller (SSD1683??) behave as 2 400px wide screens, that needs independent data transfers"*; il
pannello servito è il 5.85" 792 × 272, split sull'**asse X**, e ogni metà riceve mezza riga
(`buf + XRes/16`). I comandi comuni (`0x21` display update control, `0x3C` border, `0x20`
activation) vengono scritti una volta sola, senza offset.

**Conseguenza per il 12.2".** I due COF sono quasi certamente una coppia master/slave con questa
convenzione: il secondo controller non vuole un CS proprio e risponde agli opcode con il bit 7
alto. Attenzione però a cosa questo NON implica: lo slave non diventa pilotabile per il solo fatto
di essere indirizzato, perchè in cascade non ha nè oscillatore nè booster e dipende dai rail del
master. Serve prima il ponte fra le due code — vedi §4.

### 3.2 `HAL_Newton_M3.h` + `epd_spi.cpp` — sul cavo del pannello c'è più di quello che usiamo

Pin dell'interfaccia e-paper sul tag di fabbrica (numerazione nRF52811):
`RST 4, BS 2, CS 6, DC 5, BUSY 3, CLK 19, MOSI 20, HLT 23, VPP 24, POWER 7`.

| Segnale | Cosa è | Perchè conta |
|---|---|---|
| `EPD_POWER` | alimentazione del pannello commutata da GPIO | il pannello non è sempre alimentato |
| `EPD_BS` | **bus select**, tenuto **LOW** (commento nel codice: `// low works!`) | è la selezione 3 fili / 4 fili degli SSD16xx: se sulla coda cablata a mano BS è flottante, il controller può stare in 3 fili e ignorare tutto. Primo sospetto per la coda muta, e da verificare anche sulla coda che funziona |
| `EPD_VPP` | usato come **MISO** (`NRF_SPI0->PSELMISO = EPD_VPP`, e letto anche a bit-bang) | **una linea di lettura esiste sul cavo del pannello**: `0x2E` (User ID da OTP), `0x2F` (status), `0x1B` (temperatura) diventano raggiungibili se si individua quel contatto. È la via per chiudere il part number del controller |
| `EPD_HLT` | il commento lo dichiara **CS di una EEPROM esterna sul pannello** | sul pannello c'è una EEPROM sullo stesso bus: è la sede tipica di waveform/LUT e dati di pannello |

Il BUSY è gestito con due modalità, `EPD_BUSY_SSD` (attivo alto) e `EPD_BUSY_UC` (attivo basso),
coerente con il BUSY alto usato dai driver di questa libreria.

### 3.3 `tagtype_db.cpp` — i dati UICR di fabbrica dei tag

Il file contiene la tabella dei byte UICR di decine di tag reali, con la decodifica dei campi.
Righe rilevanti:

```
... 00 19 01 A0 02 C0 03 38 07 07 01 80 00 00 64 ...   9.7 SSD   (e "9.7 type 2", identica)
... 00 0A 01 80 02 C0 03 38 00 03 81 9D 00 00 4A ...   11.6" BWR
... 00 12 01 18 03 10 01 04 07 07 01 80 00 00 63 ...   5.85 BWR  (il dual-controller)
... 00 17 03 A8 00 A8 00 05 00 07 81 9D 00 00 66 ...   1.6 BWRY
```

Campi: offset `0x09` = tipo di controller (**0x19 = SSD 9.7**, 0x0A = SSD 4.2/11.6, 0x12/0x0F/0x15
= SSD varianti, 0x17 = BWRY), `0x0A` = terzo colore, `0x0B`-`0x0C` = Xres, `0x0D`-`0x0E` = Yres,
`0x12`-`0x13` = capability (pulsanti, LED, NFC), `0x16` = solumType.

Il byte del terzo colore vale **0x01 = BWR**, **0x02 = BWY** (riga "4.2 SSD Yellow"), **0x03 =
BWRY** (righe 1.6 / 2.4 / 3.0 BWRY). La riga della 9.7" dichiara **0x01**, cioè **BWR**, con
Xres 0x02A0 = 672, Yres 0x03C0 = 960, solumType 0x64 = `STYPE_SIZE_097`.

È l'evidenza più forte finora sulla questione del quarto colore, perchè è **dato di fabbrica
scritto nel tag**, non catalogo nè enum di terzi. Non chiude il caso dell'esemplare del progetto
(quelle righe sono tag M3/Core, il donor è un Pro F5), ma è concorde con l'enum OEPL e con il
crash del driver `unissd` sulla 9.7" a 4 colori.

Nel codice, `STYPE_SIZE_097` imposta `drawDirectionRight = true`, che scambia le risoluzioni
effettive: OEPL usa lo stesso landscape **960 × 672** del firmware di questo progetto, senza
mirror.

`dualssd` viene istanziato **solo** per 792 × 272: OEPL continua a non avere il 12.2", e
`oepl-definitions.h` scaricato di nuovo è identico alla copia già archiviata (nessuna
`STYPE_SIZE_122`, nessuna variante BWRY per la 9.7").

### 3.4 `unissd.cpp` — il ramo 9.7" upstream è cambiato

Il ramo `case 0x19` (9.7") è ora condiviso con `case 0x0A` (11.6") e scrive
`0x45 = 00 00 7F 02`, cioè finestra Y fino a **639** invece di 671, mentre il MUX resta
`0x01 = 9F 02` (671). Il resto coincide con la sequenza di init di fabbrica già documentata,
`0x21 = 08 00` compreso. La copia in
[`openepaperlink/oepl_display_driver_unissd.c`](openepaperlink/oepl_display_driver_unissd.c) viene
da un'altra base di codice (firmware "Universal", in C) e non ha questa discrepanza.


## 4. Cascade mode: perchè la seconda coda non può funzionare da sola

Fonte primaria: **datasheet SSD1683 §6.12**, archiviato in
[`SSD1683_Rev1.0_2021-01_Solomon-Systech.pdf`](SSD1683_Rev1.0_2021-01_Solomon-Systech.pdf).
Il testo, verbatim:

> The SSD1683 has a cascade mode that can cascade 2 chips [...] The pin M/S# is used to configure
> the chip. When M/S# is connected to VDDIO, the chip is configured as a master chip. When M/S# is
> connected to VSS, the chip is configured as a slave chip. [...] When the chip is configured as a
> slave chip, **its oscillator and booster & regulator circuit will be disabled. The oscillator
> clock and all booster voltages will come from the master chip.** Therefore, the corresponding
> pins including **CL, VDD, VGH, VGL, VSH1, VSH2, VSL, VGL and VCOM must be connected to the master
> chip.**

Il master va poi messo in cascade via software: comando **`0x21`, secondo parametro, bit B[4]
*ckouten*** — 0 chip singolo, 1 cascade, con la nota *"For cascade mode, connect CL pin between
Master sample with Slave sample"*.

### Le cinque evidenze, e cosa vale ciascuna

| # | Evidenza | Forza |
|---|---|---|
| 1 | Sul tag di fabbrica del 12.2" **un connettore FFC ha tutta l'elettronica analogica** (switcher, induttore, diodi, condensatori) e **l'altro è nudo**, con un fascio di piste che arriva dal primo. Verificato sulle foto PRO e su quelle Core a 4032 × 3024 | **diretta sul nostro pannello**: un chip con booster proprio avrebbe le sue passive lì accanto, e non ce ne sono |
| 2 | L'HAL del tag nRF52811 ha **un solo `EPD_CS`** per il pannello (`HAL_Newton_M3.h`) | diretta: due chip su un CS non sono separabili se non per opcode |
| 3 | `dualssd.cpp` di OEPL indirizza il secondo chip con **opcode\|0x80** e scrive `0x21` con **B = 0x10**, cioè il bit di cascade | il codice di fabbrica reimplementato, sulla stessa famiglia |
| 4 | GxEPD2 upstream fa **esattamente lo stesso** per il Good Display GDEY0579Z93 (5.79", 792×272, SSD1683, due chip): `src/gdey3c/GxEPD2_579c_GDEY0579Z93.cpp` | implementazione indipendente e funzionante dello stesso meccanismo |
| 5 | Il bring-up: con una coda si stampa 960 × 384 — è la metà che il firmware di produzione già disegna — e con l'altra, stesso cablaggio, non si stampa niente | comportamento previsto da master/slave, non da due chip pari |
| 6 | **Il conto dei gate.** L'SSD1677 ha 960 source e **680 gate**. Nativi: 9.7" = 672 gate, 11.6" = 640 (UICR `Xres 0x0280`), 12.2" = **768** | **derivazione, non osservazione**: il 12.2" sfora il limite di un chip solo, quindi due controller sono obbligati e lo split **deve** stare sull'asse gate — su quello source un chip solo basterebbe |
| 7 | **Il pin table dell'SSD1677 stesso**: `M/S#` esiste ed è dato *"reserved pin, should be connected to VDDIO"*; `CL` è **I/O**, *"should be left open in application"*, e compare nelle caratteristiche elettriche sia fra i VIH d'ingresso sia fra i VOH d'uscita | sono gli stessi due pin che l'SSD1683 §6.12 usa per la cascade: **l'hardware c'è nel nostro chip**, la Rev 1.0 lo classifica solo come riservato |

### Cosa NON è ancora dimostrato

- **Che il die del 12.2" sia esattamente un SSD1683.** Non può esserlo: SSD1683 è 400 × 300, qui
  servono 960 sorgenti per chip. È un fratello della stessa generazione.
- **Che la cascade valga anche nella direzione gate.** Il datasheet la descrive per estendere le
  sorgenti (2 × 400 = 800 su 300 gate, e l'SSD2677 arriva a 1920 × 680 con lo stesso meccanismo);
  il nostro pannello è diviso sulle gate (2 × 384 su 960 sorgenti). Il meccanismo elettrico — slave
  senza oscillatore nè booster, comandato per opcode — non dipende da come è diviso il pannello: è
  una proprietà dello strap `M/S#`. Ma la formulazione del datasheet parla di sorgenti, quindi la
  topologia gate resta un'estrapolazione.
- **Che la nostra 9.7" sia un SSD1677 Rev 1.0 puro.** Tre indizi dicono di no, e concordano: la
  Rev 1.0 definisce `0x21` con **un solo parametro**, mentre l'init di fabbrica della 9.7" in
  `unissd.cpp` scrive `0x21` con **due byte** (`0x08 0x00`), che è la forma SSD1683; un reference
  SSD1677 di terzi (<https://github.com/bigbag/papyrix-reader>, `docs/ssd1677-driver.md`) scrive
  anch'esso `0x21` a due byte, `0x40 0x00`, e commenta il secondo con **"single chip"** — cioè
  proprio il `ckouten` dell'SSD1683; e il pin `CL` è dichiarato bidirezionale pur essendo "da
  lasciare aperto". La nostra copia del datasheet è più vecchia del silicio che abbiamo in mano.
- **Dove corrano davvero i rail fra i due connettori.** Le foto mostrano il fascio ma non
  permettono di seguire pista per pista. Lo chiude un multimetro fra i due FFC a pannello staccato.

### Controesempio utile

Non tutti i pannelli a due controller si pilotano così: `src/gdem/GxEPD2_1085_GDEM1085T51.cpp` di
GxEPD2 (10.85", 1360 × 480) usa un **secondo chip select** (`_cs2`) e comandi di famiglia UC
(`0x91`/`0x92` partial in/out). Separate-CS e cascade sono due mondi, e la discriminante è la
famiglia del controller: SSD16xx cascade, UC due CS. Il 12.2" sta nel primo.

### Come si cabla, se la lettura è giusta

Non "un secondo CS su GPIO32", ma:

- **un solo** CS, SCK, MOSI, D/C, RES, BUSY, condivisi dai due chip;
- un **ponte passivo fra le due code** che porti CL, VDD, VGH, VGL, VSH1, VSH2, VSL, VCOM dal
  connettore master al connettore slave, cioè quello che fa il tag di fabbrica con quel fascio di
  piste;
- `M/S#` dello slave a VSS e quello del master a VDDIO — se il pin è portato sul FFC;
- `BS1` basso su entrambe (4 fili);
- nel firmware: `0x21` con B[4] = 1 sul master, e ogni comando indirizzato allo slave con
  l'opcode sommato a `0x80`; init, power on e refresh restano in broadcast.

## 5. SSD2677 — il fratello a 4 colori della stessa taglia

Scoperto per via indiretta: GxEPD2 1.6.6-1.6.9 ha aggiunto driver che lo nominano
(`epd4c/GxEPD2_1160c_GDEY116F51` 960 × 640 a 4 colori, `epd4c/GxEPD2_397c_GDEM0397F81` 800 × 480 a
4 colori, `epd/GxEPD2_576_GDEH0576T81` 920 × 680 B/N).

Caratteristiche che contano qui:

- **960 source × 680 gate**, gli stessi dell'SSD1677;
- RAM a **2 bit per pixel** — quattro colori nativi, licenza E Ink **Spectra 3100**;
- command set in stile **UC**, non SSD16xx: `0x00` PSR, `0x06` BTST, `0x50` CDI, `0x60` TCON,
  `0x61` TRES (source e gate a 16 bit), `0xE0` CCSET, `0xE3` PWS, `0x90`/`0x91`/`0x92` OTP;
- **cascade** fino a 1920 × 680, con `M/S#` e con il bit `CSEIN` di `0xE0`;
- preset di risoluzione di `0x00` PSR: `RES[1:0]` = **960 × 680 / 960 × 672 / 960 × 640 / 880 × 528**.

Quei preset sono esattamente le taglie SOLUM: **960 × 672 è la nostra 9.7"**, 960 × 640 è l'11.6".

Due cose ne seguono, e vanno tenute separate:

1. **Una nota storica.** La sequenza che il vecchio `README_122c.md` attribuiva a "UC8179"
   (`0x00`, `0x06`, `0x50`, `0x60`, `0x61 = 480 × 768`, `0xE0`) è in realtà una sequenza in stile
   SSD2677. Il testo è già stato sostituito con i comandi SSD16xx reali.
2. **Un discriminante mancato.** Sembrava di poter dire "se il pannello parla SSD16xx allora è
   BWR, perchè il 4 colori è SSD2677". **Non regge**: la Table 6-4 del datasheet SSD1677 mostra che
   anche lì le combinazioni delle due RAM sono quattro, con `LUT3` distinta e semplicemente
   *aliasata* su `LUT2` nella waveform a 3 colori. Vedi `../README.md` §0.10.

## 6. Pannelli commerciali affini — Good Display e Waveshare

I SOLUM non compaiono su quei cataloghi. Quello che ci si trova sono pannelli **affini**: stessa
taglia, stesso controller, stessa architettura a due code. Servono a confermare dall'esterno quello
che qui è ricostruito dai datasheet e dal firmware di fabbrica.

Un solo artefatto è archiviato nel repo, perchè è l'unico direttamente riapplicabile:
[`097c/gooddisplay_GDEM102Z91_arduino/`](097c/gooddisplay_GDEM102Z91_arduino/) — il demo Arduino di
un pannello **10.2" 960 × 640 BWR su SSD1677**, cioè un driver a due piani per il nostro stesso
controller, che gira sulla 9.7" cambiando MUX e finestra. Il resto del materiale raccolto (datasheet
GDEM102Z91 / GDEM102F91 / GDEM133T91, spec e schematico DESPI-C1248, schematico e datasheet
Waveshare 12.48") documenta pannelli che non sono i nostri e resta fuori dal repo, in
`A:\tmp\panelli_affini\`.

### 6.1 La regola colori/controller, su tutta la linea grande

| Modello | Risoluzione | Colori | Driver |
|---|---|---|---|
| GDEM102T91 | 960 × 640 | B/N | SSD1677 |
| GDEM102Z91 | 960 × 640 | **BWR** | **SSD1677** |
| GDEM102F91 | 960 × 640 | **BWRY** | **SSD2677** |
| GDEH116T91 | 960 × 640 | B/N | SSD1677 |
| GDEY116F91 | 960 × 640 | **BWRY** | **SSD2677** |
| GDEM133T91 | 960 × 680 | B/N | SSD1677 |

**Nessun pannello a 4 colori su SSD1677, in nessuna taglia**, a parità di risoluzione e connettore.
Il demo del 4 colori non usa affatto le due RAM: scrive **un solo stream `0x10` a 2 bit per pixel**
— `00` bianco (schermo pieno `0x55`), `01` giallo (`0xAA`), `10` rosso (`0xFF`), `11` nero (`0x00`)
— con un command set in stile UC (`0x00` PSR, `0x06` BTST, `0x30` PLL, `0x50` CDI, `0x61` TRES,
`0x04` power on, `0x07` deep sleep `0xA5`, `0x12` refresh). È lo stesso formato del path
`epdvarbwry` di OEPL per le SOLUM BWRY vere (§3).

Non chiude la questione del quarto colore sulla 9.7", perchè LUT3 esiste nel silicio e a decidere è
la waveform in OTP (§5 e la Table 6-4 nel README). Ma il quarto colore, ovunque lo si trovi, vive su
un formato che la 9.7" non parla.

### 6.2 Il tetto dei 680 gate, visto in commercio

**GDEM133T91** è 13.3", **960 × 680**, **un solo SSD1677 e una sola coda**, e il suo init programma
`0x01` MUX = **679** (`A7 02 00`) e cursore `0x4F` = 679. È il pannello a chip singolo più grande
esistente su questo silicio e cade esattamente sul limite dichiarato dal datasheet. Il conto che
obbliga il 12.2" a due controller con split sull'asse gate (§4) regge quindi anche dal lato
mercato: nessuno spinge un SSD1677 oltre 680 righe.

### 6.3 Come è cablato un pannello a due code, e cosa dice della cascade

Good Display **GDEY1248Z51** e Waveshare **12.48" Module (B)** sono lo stesso pannello: 1304 × 984,
due FPC da 30 pin, UC8179. Lo schematico del breakout ufficiale DESPI-C1248 e quello Waveshare
coincidono:

- **quattro controller con quattro chip select** — `CSB_M1`, `CSB_M2` sulla coda master, `CSB_S1`,
  `CSB_S2` sulla coda slave (aree 648×492, 656×492, 656×492, 648×492);
- **quattro BUSY**, **due DC**, **due RST**, **due BS** (`BS`, `BS_2`);
- **due sezioni di boost indipendenti**, con punti di misura separati `VGH_P1`/`VGL_P1` e
  `VGH_P2`/`VGL_P2` ("P1 MOS tube gate voltage", "P2 MOS tube gate voltage"), VCOM in comune;
- code **non intercambiabili**, serigrafate `WFT1248BZ23` (master) e `WFT1248BZ24` (slave):
  *"The master FPC and the slave FPC should not be reversed, otherwise the e-paper will not be
  refreshed."*

Il confronto chiude un dubbio e ne lascia aperto un altro. L'asimmetria master/slave delle due code
**non** è di per sè un'anomalia — ce l'ha anche un pannello di catalogo. Ma un pannello a controller
indipendenti la paga con **N chip select e N sezioni di boost**, mentre il tag di fabbrica della
12.2" ha un solo CS, una sola sezione analogica e il secondo connettore nudo alimentato da un fascio
di piste che arriva dal primo. Il conteggio dei pin esclude la topologia a controller indipendenti;
resta la cascade (§4).

Ne segue anche una nota sul `|0x80`: con un secondo chip select il broadcast non richiederebbe di
sommare `0x80` all'opcode, basterebbero due CS bassi insieme — è esattamente quello che fa
`GxEPD2_1248c` upstream con `_writeCommandAll()`. Il trucco degli opcode serve **perchè un secondo
CS non c'è**.

### 6.4 Circuito di boost, per un eventuale breakout

Dalla specifica DESPI-C1248 §4 "Problems of designing drive circuit", valida per qualunque e-paper
SPI: VGH tipico **+20 V**, VGL tipico **−20 V**; induttore **10 µH 1 A avvolto**; MOSFET
**Si1304BDL** o **Si1308EDL** (in alternativa AO3400); diodo Schottky equivalente a **MBR0530**;
socket FPC 0,5 mm con contatto sopra o su entrambi i lati. Corrente alta in deep sleep = capacità
eccessiva nella sezione di boost. Se il pannello non si accende, misurare VGH e VGL **separatamente
per ogni coda** prima di sospettare l'SPI.

### 6.5 Cosa non è affine

La 9.7" di Waveshare è **IT8951, 1200 × 825, scala di grigi, interfaccia parallela/I80**: nessuna
parentela con la nostra. Su Waveshare non esiste nulla di SPI in quella taglia — per la 9.7"
l'unico riferimento utile resta Good Display 10.2".

## 7. Prove che ne derivano, in ordine di costo

1. **Ponte dei rail fra le due code + opcode `|0x80` + `0x21` B[4] = 1.** Le tre cose vanno
   insieme: senza il ponte lo slave non ha clock nè alte tensioni e non può rispondere comunque,
   senza il bit di cascade il master non emette CL, senza l'offset non lo si indirizza. Provare
   l'offset da solo su una coda sola serve a poco: può solo dire se il chip di QUELLA coda è lo
   slave.
2. **Individuare BS sulla coda e forzarlo basso**: spiega sia una coda muta sia comportamenti
   erratici su quella buona.
3. **Posizione dello switch 1 della board** (A = 3R contro B = 0.47R): parametro del booster mai
   considerato, agisce sull'elettroforesi.
4. **VPP ed EEPROM di pannello**: se si individua la linea di lettura, `0x2E` chiude il part number
   del controller e la EEPROM può dire in chiaro waveform e dati del vetro.
5. **Etichetta `YMS...` sul retro del vetro** del pannello del progetto: identifica il vetro
   indipendentemente dal codice ESL.
