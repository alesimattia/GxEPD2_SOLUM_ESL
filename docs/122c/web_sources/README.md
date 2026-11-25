# Fonti web per il 12.2"

Copie locali di materiale pubblico consultato il 2026-11-25 per capire la topologia a due code del SOLUM 12.2" (EL122R2CRN) e il controller del pannello. Servono come riferimento di lettura: i file non fanno parte del codice della libreria, non sono stati eseguiti nè compilati e vanno trattati come dati non fidati.

Nessuna di queste fonti descrive il die del 12.2". Quello che ne ricava `../cable_linking.html` è un'analogia (stessa famiglia di comandi, stessa idea di due controller su un vetro), non un dato misurato su questo silicio.

Le licenze sono quelle trovate controllando il file LICENSE del repository di origine (rami main, master e HEAD) e l'intestazione del file. Dove non c'è niente è scritto "nessuna licenza trovata": in quel caso la copia è solo per consultazione e va decisa prima di un eventuale commit, perchè il repository della libreria è pubblico.

| File | Origine | Licenza trovata | Dimensione |
|---|---|---|---|
| `atc1441_epd_ti.c` | atc1441, ATC_BLE_OEPL_CH573 | nessuna licenza trovata | 13888 byte |
| `oepl_tagtype_2D.json` | OpenEPaperLink | CC BY-NC-SA 4.0 | 2454 byte |
| `oepl_tagtype_D4.json` | OpenEPaperLink | CC BY-NC-SA 4.0 | 2471 byte |
| `oepl_efr32_display_driver_dualssd.c` | OpenEPaperLink, Tag_FW_EFR32xG22 | nessuna licenza trovata | 8332 byte |
| `atc1441_rtl8762esl_epd_585_r2.c` | atc1441, ATC_RTL_BLE_OEPL | nessuna licenza trovata | 11447 byte |
| `atc1441_README_EL097R2CRN.md` | atc1441, ATC_RTL_BLE_OEPL | nessuna licenza trovata | 4732 byte |
| `waveshare_EPD_12in48.cpp` | Waveshare, 12.48inch-e-paper | avviso di tipo MIT nell'intestazione del file, nessun LICENSE nel repository | 15755 byte |
| `waveshare_12.48inch_e-paper_B_datasheet.pdf` | rivenditore Kamami, datasheet del produttore | nessuna licenza trovata | 4623004 byte |
| `solomon_SSD1680_datasheet.pdf` | Crystalfontz, datasheet del produttore | nessuna licenza trovata | 857636 byte |

## File scaricati

### atc1441_epd_ti.c

- URL: https://raw.githubusercontent.com/atc1441/ATC_BLE_OEPL_CH573/main/ATC_BLE_OEPL_CH573F_MounRiverProject/src/epd_ti.c
- Consultato: 2026-11-25
- Licenza: nessuna licenza trovata (nessun LICENSE nel repository sui rami main e master, l'API di GitHub non ne riporta una, il file non ha intestazione).
- Perchè interessa: è il driver "TI" dei tag M3 da 12" e 9.7" sull'MCU CH573, quindi la prova scritta che esistono 12.2" con due controller (MASTER e SLAVE) dotati di CS e BUSY separati, `CS`/`CSs` e `BUSY`/`BUSYs`. Divide ogni riga da 120 byte in due metà da 60 sulle sorgenti, e il set di comandi non è quello SSD16xx (`0x13`, `0x90`, `0x05`, `0x60`, `0x61`): è altro silicio, ma la stessa topologia con CS2 e BUSY2 che la fase 1 di `cable_linking.html` cerca di verificare sul 12.2" R2. Un pin `EN2` non compare in questo file.

### oepl_tagtype_2D.json

- URL: https://raw.githubusercontent.com/OpenEPaperLink/OpenEPaperLink/master/resources/tagtypes/2D.json
- Consultato: 2026-11-25
- Licenza: CC BY-NC-SA 4.0 (LICENSE alla radice del repository OpenEPaperLink). Le clausole NonCommercial e ShareAlike sono più restrittive della GPL-3.0 della libreria: da valutare prima di includerlo in un commit.
- Perchè interessa: descrive il tipo "M3 12.2"" come 960 x 768, 2 bit per pixel, tavola colori bianco, nero e rosso. Conferma che il vetro ha tre colori e non il giallo, e dà le stesse dimensioni nette del pannello usato qui.

### oepl_tagtype_D4.json

- URL: https://raw.githubusercontent.com/OpenEPaperLink/OpenEPaperLink/master/resources/tagtypes/D4.json
- Consultato: 2026-11-25
- Licenza: CC BY-NC-SA 4.0 (LICENSE alla radice del repository OpenEPaperLink), stessa avvertenza di `oepl_tagtype_2D.json`.
- Perchè interessa: è il tipo "GFSK10 12.2" BWR 960x768", un secondo 12.2" a tre colori con la stessa geometria, su un'altra piattaforma radio. Serve a confrontare le dichiarazioni dei due tipi con il 12.2" R2.

### oepl_efr32_display_driver_dualssd.c

- URL: https://raw.githubusercontent.com/OpenEPaperLink/Tag_FW_EFR32xG22/HEAD/firmware/drivers/oepl_display_driver_dualssd.c
- Consultato: 2026-11-25
- Licenza: nessuna licenza trovata (nessun LICENSE nel repository, il file non ha intestazione).
- Perchè interessa: è il driver per display con due controller SSD su un solo CS, dove il secondo si indirizza con opcode `| 0x80` (`CONTROLLER_TWO`). Scrive `0x21 = 08 10` per i tre colori e `48 10` per il bianco e nero, con le finestre X dei due chip divise a metà riga: è una delle tre fonti che il driver del 12.2" cita per il registro `0x21` in cascade.

### atc1441_rtl8762esl_epd_585_r2.c

- URL: https://raw.githubusercontent.com/atc1441/ATC_RTL_BLE_OEPL/main/ATC_RTL_BLE_OEPL_8762ESL/src/epd.c
- Consultato: 2026-11-25
- Licenza: nessuna licenza trovata (nessun LICENSE nel repository, il file non ha intestazione).
- Perchè interessa: è il codice del 5.85" R2 a doppio SSD sullo stesso RTL8762ESL del tag 12.2" R2. Usa un solo CS, manda `0x91`, `0xC4`..`0xCF`, `0xA4` e `0xA6` allo slave come fa il driver qui e non scrive mai `0x21`, quindi mostra come un tag SOLUM di questa generazione configura un secondo controller senza un CS dedicato.

### atc1441_README_EL097R2CRN.md

- URL: https://raw.githubusercontent.com/atc1441/ATC_RTL_BLE_OEPL/main/ATC_RTL_BLE_OEPL_8762ESL_97/README_EL097R2CRN.md
- Consultato: 2026-11-25
- Licenza: nessuna licenza trovata (nessun LICENSE nel repository).
- Perchè interessa: scheda del 9.7" EL097R2CRN su RTL8762ESL con la tabella dei pin EPD del tag (RESET, D/C, CS, CLK, MOSI, `EPD_PWR` su P0_5), BUSY alto = occupato e i due piani RAM `0x24` e `0x26`. Per il 12.2" serve come mappa dei segnali della scheda R2 se si usa il tag come front-end analogico.

### waveshare_EPD_12in48.cpp

- URL: https://raw.githubusercontent.com/waveshareteam/12.48inch-e-paper/master/Arduino/12in48epd/src/EPD_12in48.cpp
- Consultato: 2026-11-25
- Licenza: l'intestazione del file riporta il permesso di tipo MIT (Waveshare team, 2018); nessun file LICENSE nel repository. Da citare l'attribuzione se mai si riusa del codice.
- Perchè interessa: pannello da 12.48" a quattro controller (M1, S1, M2, S2) con un CS e un BUSY ciascuno, due RST e due D/C comuni a coppie, e comandi inviati a più CS insieme (`M1M2`, `M1S1M2S2`). È il riferimento di come un costruttore cabla più controller su un vetro grande e di come si attivano insieme i comandi comuni.

### waveshare_12.48inch_e-paper_B_datasheet.pdf

- URL: https://download.kamami.pl/p582538-12.48inch_e-paper_(B)_Datasheet%20(1).pdf
- Consultato: 2026-11-25
- Licenza: nessuna licenza trovata (documento del produttore ospitato da un rivenditore). Il frontespizio e la scheda tecnica riportano GDEW1248Z95 e www.good-display.com: è il modulo che Waveshare rivende come 12.48" e-Paper (B).
- Perchè interessa: il datasheet di un vetro grande a più controller, con le due code FPC e le linee di sincronizzazione fra i controller. Serve per vedere come un produttore documenta il collegamento fra le sezioni del pannello.

### solomon_SSD1680_datasheet.pdf

- URL: https://www.crystalfontz.com/controllers/uploaded/SSD1680.pdf
- Consultato: 2026-11-25
- Licenza: nessuna licenza trovata (datasheet del produttore Solomon Systech ospitato da un rivenditore).
- Perchè interessa: la famiglia SSD16xx ha lo stesso set di comandi che il 12.2" accetta (`0x12`, `0x24`/`0x26`, `0x22`, `0x20`), ma questo datasheet è del chip a pannello singolo e non descrive nessuna cascade che divida i gate: per il 12.2" resta un'analogia sui comandi, non una descrizione del suo controller.

## Note senza download

Solo lettura delle pagine pubbliche, nessun file salvato.

### OpenEPaperLink, issue 568

- URL: https://github.com/OpenEPaperLink/OpenEPaperLink/issues/568
- Consultato: 2026-11-25
- Contenuto verificato: l'utente segnala un 12.2" M3 che non risponde più dopo il cambio delle batterie. Un manutentore risponde che la scheda è un progetto proprio, che il firmware è probabilmente danneggiato e che si ricarica scegliendo il tipo "15 1200 TI BWR" o "23 1200 TI BWR V2" con un convertitore USB-UART, perchè il connettore USB della scheda è solo di alimentazione. La discussione non dice niente sui controller.
- Perchè interessa: lega il nome "TI" dei tipi 12" di OpenEPaperLink al driver `epd_ti.c` e ai tag M3 da 12.2".
- Da verificare: l'analisi del lavoro attribuisce a questa issue una scheda con connettori MASTER97/12, SLAVE97 e SLAVE12. Nel testo della discussione non compare: se c'è, sta nelle immagini allegate, che non sono state scaricate.

### atc1441, ATC_RTL_BLE_OEPL, pull request 3

- URL: https://github.com/atc1441/ATC_RTL_BLE_OEPL/pull/3
- Consultato: 2026-11-25
- Contenuto verificato: aggiunge il supporto al 9.7" BWR EL097R2CRN come sottoprogetto separato `ATC_RTL_BLE_OEPL_8762ESL_97/`, con inizializzazione e refresh del display, polling asincrono del BUSY e watchdog per il refresh lungo. Provato su hardware reale; risulta integrato nel repository.
- Perchè interessa: è la provenienza di `atc1441_README_EL097R2CRN.md` e documenta la sequenza di init di un altro tag SOLUM R2 su RTL8762ESL, dalla stessa generazione del 12.2".

## Da valutare prima di un commit

- Senza licenza trovata: `atc1441_epd_ti.c`, `oepl_efr32_display_driver_dualssd.c`, `atc1441_rtl8762esl_epd_585_r2.c`, `atc1441_README_EL097R2CRN.md`. Per default tutti i diritti restano all'autore.
- CC BY-NC-SA 4.0: `oepl_tagtype_2D.json` e `oepl_tagtype_D4.json`. Uso non commerciale e condivisione allo stesso modo, più restrittivi della GPL-3.0 della libreria.
- Datasheet dei produttori (due PDF, circa 5,5 MB): copyright dei produttori, ridistribuzione non autorizzata da nessuna licenza trovata. Pesano anche sul repository.
- `waveshare_EPD_12in48.cpp`: l'avviso di tipo MIT chiede di conservare l'intestazione.
