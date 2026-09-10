# dual_panel_finder — cosa misura, e quanto costa

Suite di reverse engineering del **SOLUM 12.2"** (`EL122H6W4A`), 768 × 960 px nativi pilotati
landscape come **960 w × 768 h**, due controller SSD16xx. Il suo prodotto è un **log** che, insieme
alle schermate viste sul vetro, basta a correggere `src/GxEPD2_SOLUM_122c_960x768.h` perchè piloti
il pannello intero.

Stato del bring-up: con la **coda lunga** il driver stampa correttamente su **metà pannello**, e la
metà dipinta è quella adiacente alla propria COF. Con la **coda corta**, infilata nello stesso
connettore da 24 pin senza adattatore, non stampa niente.

## La geometria, dalla fonte del produttore

Datasheet SOLUM Newton PRO §3.1, letto su tre taglie:

| Taglia | Risoluzione | Asse gate | Code |
|---|---|---|---|
| 9.7" | `672 x 960 px (121dpi) / 141.1 x 201.6 mm` | 672 | una |
| 11.6" | `640 x 960 px (100dpi) / 163.0 x 244.5 mm` | 640 | una |
| **12.2"** | `768 x 960 px (102dpi) / 190.1 x 237.6 mm` | **768** | **due** |

Il pitch è 0,2475 mm su tutte e tre, e **solo il 12.2" sfora i 680 gate dell'SSD1677 — ed è
l'unico con due COF**. La correlazione è completa e viene dai numeri del produttore.

Le foto FCC (`docs/122c/fcc/122_pannello_retro.jpg`) chiudono la disposizione: il vetro misura
~190 × 237 mm col metro accanto, e le due code escono dai **due bordi lunghi opposti alla stessa
altezza**, ognuna con il **proprio COF**. Quindi non c'è un COF passivo alimentato dall'altro: sono
due driver IC. Il bordo di bonding è quello da 237,6 mm, cioè l'asse da 960 px = le 960 **source**;
l'asse perpendicolare, 768 px, si divide in **384 gate per controller**. E i due COF si guardano da
bordi opposti, quindi il secondo è ruotato di 180°: la specchiatura è un effetto atteso, non
un'ipotesi.

## La cascade è esclusa, e non da un'ipotesi

SSD1683 §6.12, verbatim:

> *"The SSD1683 has a cascade mode that can cascade 2 chips to achieve the display resolution up to
> **800 (sources) x 300 (gates)**. […] When the chip is configured as a slave chip, its oscillator
> and booster & regulator circuit will be disabled."*

Il chip singolo è 400 × 300: la cascade **raddoppia l'asse source e lascia i gate dove sono**. Due
argomenti indipendenti convergono:

1. **aritmetica** — una coppia in cascade di chip di questa famiglia darebbe 1920 source × **680
   gate**, e 680 < 768: non copre l'asse gate del 12.2" per nessun cablaggio;
2. **osservazione** — con la coda lunga sola il pannello dipinge **960 px di larghezza piena**,
   mentre in cascade il master coprirebbe metà delle sorgenti, cioè 480 px.

La topologia è quindi **due controller indipendenti, ciascuno col proprio oscillatore**, che
condividono i **rail di pilotaggio** — e il datasheet SSD1677 li dà per alimentabili dall'esterno
(Features p.5, Table 5-4: *"VGH, VGL, VSH1, VSH2, VSL can be connected to external power supply"*).
È la spiegazione del secondo connettore nudo sulla scheda del tag, e fa una **previsione che
discrimina**:

| | slave in cascade | chip indipendente a rail condivisi |
|---|---|---|
| oscillatore | disabilitato | **proprio, vivo** |
| BUSY su SWRESET / `0x47` / `0x34` | **fermo su tutto** | **risponde** |
| `0x14` HV Ready | fermo | parte e **va al massimo**: l'HV non arriva mai |
| refresh | non parte | parte e non dipinge |

È esattamente quello che misura la sonda `4`.

## Tre limiti di registro, e la domanda sul pannello intero

Un controller solo **non** può coprire 768 righe, e lo dicono tre limiti indipendenti:

- **§8.4**, `0x45`: *"00h <= YSA[9:0], YEA[9:0] <= **2A7h**"* — la finestra RAM in Y si ferma alla
  riga **679**;
- **§8.1**, `0x01`: *"Multiplex ratio (MUX ratio) from 300 MUX to **680** MUX"*;
- **p.5 e Table 5-5**: RAM `960x680 bit` per piano, uscite `G[679:0]`.

Il massimo che un SSD1677 vero potrebbe dipingere è **680 righe su 768**. Restano due strade per i
960 × 768 da una coda sola, ed entrambe sono economiche: la sonda `b` (`opcode|0x80`, zero refresh
se il BUSY non si muove) prova che il pannello ponticelli internamente, e la sonda `0` chiede al
silicio se è davvero una Rev 1.0, spazzando MUX e finestra **fuori specifica**.

## E se il die non fosse un SSD1677 Rev 1.0?

Due indizi dicono che sia più recente: l'init di fabbrica SOLUM scrive `0x21` con **due** byte, e
la Rev 1.0 ne dichiara **uno solo**. Le differenze fra le due revisioni sono altrettanti segnali di
identità, e ognuna costa zero o un refresh:

| Punto | SSD1677 Rev 1.0 | SSD1683 | Sonda |
|---|---|---|---|
| `0x21` | un solo parametro | due: **B[4] ckouten**, cascade selection | `8` manda `{08,00}` e vede se il chip lo accetta; primo byte, secondo byte e posizione si spazzano dalla voce `p` 13 |
| `0x10` deep sleep | `00` e `11` soltanto | `00`, **`01` Mode 1**, `11` Mode 2 | `u`, gratis |
| `0x01` B[0] TB | **Reserved** | **"scan from G299 to G0"**: reverse scan gate in hardware | `j` |
| `0x0C` soft start | 5 byte, due livelli | 4 byte, forza per fase | `1`, col livello dalla voce `p` 10 |

La riga che conta è la terza: se il die si comporta da 1683, `0x01` con B[0] = 1 **ribalta la banda
in hardware**, e tutto il problema della specchiatura — il pezzo più fragile del driver — sparisce.
Un refresh.

## Le sonde, e la riga di driver che ognuna decide

Nessuna sonda sta fuori dalla selezione di default: **`t` esegue la suite intera**. Il numero fra
parentesi è il **minimo**, cioè quello che la sonda spende con l'esaustivo spento; l'esaustivo, che
è acceso di default, rimette in gioco le passate condizionali.

### Elettriche, zero refresh

| Tasto | Sonda | Cosa decide nel driver |
|---|---|---|
| `1` | candidate di init, sei varianti | `_InitDisplay`, e il pattern hardware `0x46`/`0x47` per `_writeScreenBuffer` |
| `2` | bit 7 dell'opcode | se `opcode\|0x80` indirizza qualcosa |
| `3` | costo del push ai tre clock | il clock di `selectSPI` |
| `4` | **impronta della coda, e confronto** | il modello di indirizzamento, con la tabella sopra |
| `5` | **SPI a 3 fili: BS1 flottante** | niente nel driver: il **cablaggio** |
| `6` | **reset a ritentativi** | `_resetDual` e l'attesa dopo lo SWRESET |

### Il pannello intero e la specchiatura

| Tasto | Sonda | Refresh | Cosa decide |
|---|---|---|---|
| `b` | secondo controller a `opcode\|0x80` | 1 | `ADDRESSING_CASCADE` |
| `0` | **identità del die: MUX fuori specifica** | 2 | `PART_HEIGHT`, la famiglia del controller |
| `j` | **TB = 1, reverse scan gate** | 1 | `_reverseBits`: cancellabile o no |
| `e` | **specchiatura via entry mode `0x11`** | 1 | `ScreenPart::_setPartialRamArea`, `setSlaveMirror` |

### A frame

| Tasto | Sonda | Refresh | Cosa decide |
|---|---|---|---|
| `7` | frame di identificazione | 3 | quale init fa rispondere il pannello, e il verso |
| `8` | polarità del piano BW e `0x21` | 2 | `_InitDisplay 0x21`, la polarità |
| `9` | 4 bande dei piani più i box | 1 | il quarto colore, la finestra parziale in X |
| `a` | partial d'area | 3 (+4) | `hasFastPartialUpdate`, `partial_refresh_time` |
| `d` | differenziale | 1 (+4) | se il controller confronta i due piani |
| `l` | **LUT via `0x32` più le tensioni** | 3 (+1) | `setPartialLut`, `_Init_Part 0x04` |
| `y` | quarto colore, livelli di sorgente | 2 | se `writeImageYellow` va implementata |
| `q` | **bitmask degli stadi di `0x22`** | 2 (+2) | `_Update_Full`, la sequenza più corta che dipinge |
| `m` | sweep del MUX | 0 | quali valori il registro accetta |
| `g` | banchi di waveform per temperatura | 4 | `busy_timeout`, `full_refresh_time` |
| `o` | RAM ping-pong `0x37` in Mode 2 | 1 | `0x37` F[6] |
| `u` | deep sleep | 3 | `hibernate`, e la famiglia del controller |
| `r` | fase driver | 1 | il driver contro il silicio |

Totale: **30 refresh minimi** (~9 minuti di solo pannello), **41 in esaustivo** (~13 minuti), più
il tempo che ci si mette a guardare ogni schermata.

Le tre sonde ridimensionate rispetto alla versione precedente non sono state indebolite: il MUX
cronometrato (3 → 0), il partial d'area (5-7 → 2) e il differenziale (3-6 → 1) rimisuravano ciò che
il **9.7", stesso silicio**, ha già chiuso — la durata non scala col MUX (24010/24031/24033 ms a
671/335/167), la finestra confina l'area ma non accorcia la durata (168, 48 e 24 righe danno tutte
24,65 s), e il controller non confronta i piani (`0xFC`/`0xFF`/`0xCF`/`0xC7` tutti a 24,6-24,8 s con
2 ms di scarto). Le passate restano, in esaustivo.

Il **ping-pong** invece è stato *corretto*, non ridotto: `0x37` F[6] è il ping-pong della RAM, e il
datasheet aggiunge *"RAM ping-pong function is **not support for Display Mode 1**"*. La versione
precedente scriveva il primo byte a `0xFF` — è riservato e va a zero — e lasciava `F[3:0]` a zero,
cioè WS[35:32] in Mode 1: chiedeva al silicio una combinazione dichiarata non supportata, ed è per
quello che il README la chiamava un tentativo alla cieca.

## L'ordine della diagnosi

È la sequenza in cui le ipotesi si escludono, dalla più economica alla più costosa. **Le prime due
riguardano il pannello intero e vengono prima di tutto**, perchè un esito positivo rende
irrilevante la coda muta.

1. **`b` con la coda lunga sola** — gli opcode `|0x80` accendono la seconda banda? Zero refresh se
   il BUSY non si muove. Un esito negativo qui è **atteso**, perchè `|0x80` è il meccanismo della
   cascade e la cascade è esclusa: conferma il quadro invece di lasciare la domanda aperta.
2. **`0` e `j`** — che die è, e TB = 1 ribalta la banda? Tre refresh, e decidono il pezzo più
   fragile del driver.
3. **`4` con la lunga**, poi `c` per dichiarare la coda e **`4` con la corta** — cascade o rail
   condivisi, secondo la tabella sopra.
4. **`5`** sulla corta — se in 3 fili risponde, è `BS1` flottante e il rimedio è un pull-down.
5. **`6`** sulla corta — esclude che sia una questione di tempi di reset.
6. **multimetro a pannello staccato** — continuità di `M/S#` verso VDDIO o VSS su ciascuna coda (il
   pin table lo dà *"reserved pin, should be connected to VDDIO"*), `BS1` verso massa, e **dove
   finiscono VGH/VGL/VSH1/VSH2/VSL**: se corrono dal primo FFC al secondo, il quadro dei rail
   condivisi è confermato senza scrivere una riga di codice.
7. Solo se 1-6 non concludono, le due prove hardware più sotto.

## Il gate: l'operatore fa cambiare schermata

Da una schermata alla successiva si passa **solo premendo un tasto**. Un frame costa ~19 s e quello
successivo lo cancella, quindi una schermata che avanza da sè è una misura buttata. Il gate non
spegne il pannello — la differenziale usa di proposito sequenze senza power down, e uno spegnimento
fra le passate cambierebbe la misura — e non tocca SPI, così vale anche dentro la sonda del deep
sleep, dove il controller è sordo per costruzione. Nella fase driver il gate arriva dallo stesso
punto, via `setScreenAdvanceHook()`.

Dove l'esito decide una riga del driver il gate fa una **domanda**, e la risposta vale come
avanzamento: le voci si numerano da 1, e lo zero o un INVIO a vuoto valgono "non l'ho guardata" e
non annotano niente.

## Il menu: una compilazione per sessione

`loop()` legge **un carattere**: lanciare una sonda costa un tasto. I sottomenu e le domande del
gate leggono invece una **riga**, chiusa da INVIO, perchè scelgono fra più di dieci voci.

| Tasto | Cosa fa |
|---|---|
| `t` | esegue le sonde selezionate, con il preventivo dei refresh |
| `s` | riepilogo: registro delle misure, indice delle schermate, confronto fra le code, scheda per il driver |
| `k` | caratteristiche note: ridichiara quello che una sessione precedente ha già determinato |
| `n` | quali sonde eseguire |
| `p` | parametri di misura: temperature, i quattro MUX, gate per controller, clock, timeout, finestra del multimetro, e i parametri di registro (voci 10..15: soft start, entry mode, TB, `0x21` con la sua posizione, reset, border sotto LUT) |
| `c` | quale coda è infilata: è anche la colonna in cui la sonda `4` scrive |
| `f` | secondo connettore FFC cablato |
| `x` | esaustivo: le passate condizionali |
| `w` | polarità del piano BW |
| `v` | righe di dettaglio, spente di default |
| `i` | re-init con una candidata scelta fra le sei |
| `z` | configurazione ai valori di fabbrica |
| `h` | ristampa il menu |

**Niente NVS**: un example non lascia stato nella flash della board. La configurazione vive quanto
la sessione, e la voce `k` è il modo di ridichiarare quello che si sa già.

Il clamp del MUX arriva al **massimo del registro, 1024**, e non ai 680 del chip: la sonda `0` deve
poter chiedere 768 linee, cioè uscire di proposito dalla specifica, perchè è così che si scopre se
il die è davvero una Rev 1.0. Non si tocca nessuna tensione facendolo, e chiedere più linee di
quante ne esistano è un overflow del contatore di scansione — il 9.7" ha già girato a MUX 167 e
335, cioè *sotto* il pavimento dichiarato, senza danni.

Le **tensioni** hanno invece un tetto vero, ed è il POR: `VSH1 = 0x41` (15 V) è il massimo
consentito, e `waveformGuard()` lo impone a runtime prima di ogni carico di waveform. Alzare VSH1
sopra i 15 V è la sola leva che può danneggiare il film in modo permanente; **riportare** una
tensione al POR non è invece uscire dai default del chip.

## I file

| File | Contenuto |
|---|---|
| `dual_panel_finder.ino` | tabella delle sonde, sottomenu, runner, scheda per il driver, menu |
| `Config.h` | configurazione della sessione, limiti del silicio col riferimento al datasheet, input seriale, `waveformGuard()` |
| `Report.h` | frame numerati, gate, registro delle misure, log a tag |
| `Graphics.h` | font 5×7, riquadro del numero, composizione delle righe. Non tocca SPI |
| `Controller.h` | bus a 4 e 3 fili, init e candidate, refresh, registri, LUT. Non conosce le sonde |
| `ProbesElettriche.h` | le sonde che non spendono un refresh |
| `ProbesFrame.h` | le sonde che dipingono un frame e lo fanno guardare |
| `ProbesWaveform.h` | waveform, LUT, temperatura, sonno, secondo controller |
| `ProbesDriver.h` | la fase che misura il driver invece del silicio |

Il confine fra `Graphics.h` e `Controller.h` non è di comodo: comporre un pattern **fuori** dalla
regione cronometrata è ciò che tiene il tempo di calcolo dell'MCU fuori dalle durate misurate.

## Come si legge il log

Una riga per fatto, tag in colonna fissa, e fra parentesi quadre il numero del **riquadro in alto a
destra** dell'area appena ridipinta:

```
== 9  4 bande dei piani piu' i box (1 refresh) ==
[12] MEAS  4 BANDE E BOX                 22=F7   19012 ms
[12] guarda: che colore rende ogni banda? e i tre box neri della banda 1 sono allineati?
[12]   1) come la banda dell'accent, nessun colore nuovo
[12]   2) un colore distinto dalle altre tre
[12] SEEN  1
[12] VERDICT tre colori: le primitive del terzo piano restano senza corpo
DRV  full_refresh_time      19012 ms      <- sonda 9 [12]
```

- `MEAS` una misura, con il parametro di `0x22` quando la passata è un refresh;
- `guarda:` cosa cercare sul vetro, e le voci della domanda quando c'è;
- `SEEN` la risposta dell'operatore;
- `VERDICT` una conclusione che una misura decide da sè;
- `DRV` una riga da cambiare nel driver, con la sonda e la schermata da cui viene.

Il numero **sta sul frame**, non sulla sonda: una sonda con quattro passate produce quattro numeri.
Le passate che scrivono una cifra *dentro* l'immagine — le fasce — prenotano il numero prima di
dipingere, così la cifra sulla fascia e il riquadro in alto a destra portano **lo stesso** numero.
Prima del primo frame il prefisso non si stampa: un `[0]` indicherebbe una schermata che non
esiste.

Il tasto `v` accende le righe di dettaglio (`.`), spento di default: su un refresh da 19 s otto
righe che dicono "sta ancora andando" servono quando una misura non torna, non mentre torna. Le
motivazioni non stanno sul seriale ma nei commenti dei file e qui.

## Le due prove che aspettano hardware in più

### A. Le due code insieme, due chip select

**È la prova che il datasheet sostiene**, non il ponte in cascade. Serve un secondo breakout FFC.

Condivisi: **SCK 13**, **MOSI 14**, **DC 27**, **RST 26**, 3V3, GND. Propri della seconda coda:

- **CS_S su GPIO32** — *non* GPIO33, che su questa board non è portato fuori: le net dello
  schematico saltano dal 32 al 34, e un `digitalWrite` sul 33 non arriva da nessuna parte;
- **BUSY_S su GPIO35**, che è input-only: va bene per un BUSY e non per un CS.

Da evitare 0/2/12, che sono pin di strapping.

In firmware: voce `f` del menu a "cablato", e la candidata di init `per-CS` (tasto `i`, voce 6), che
inizializza **ciascun** chip sul proprio chip select invece di mandare tutto in broadcast. Criterio
di riuscita: si accende la banda dell'altro controller.

Avvertenza: `GPIO35` non ha pull interni pilotabili, quindi un BUSY flottante si legge come
"occupato" e manda ogni attesa in timeout. La sonda `4` lo distingue guardando il livello a riposo
con e senza pull — ma quella prova vale sul BUSY della coda sotto test, che sta su GPIO25. Per
averla anche sul secondo, portalo su 4, 21 o 22.

### B. Ponte dei rail di pilotaggio

Serve se il secondo chip **risponde ai comandi ma non dipinge**, che è la firma del quadro a rail
condivisi. Da ponticellare fra i due FFC i cinque rail che il datasheet sancisce come alimentabili
dall'esterno: **VGH, VGL, VSH1, VSH2, VSL**.

**`CL` non serve.** Ogni chip ha il proprio oscillatore, e il datasheet lo dà *"should be left open
in application"*: serviva solo nell'ipotesi cascade, che è esclusa. `VCOM` è un pin a condensatore
per chip, quindi generato localmente: se sia condiviso lo dice il multimetro, non il datasheet.

**Prima di saldare**, a pannello staccato: continuità di `M/S#` verso VDDIO o VSS su ciascuna coda,
`BS1` verso massa, e dove finiscono i cinque rail. Se sulla corta `M/S#` va a VSS è lo slave di una
coppia in cascade, e allora è il pannello a non essere quello che il conteggio dei gate dice: vale
la pena rileggere tutto da capo prima di saldare qualsiasi cosa.

## Il log su file

```
python3 ../../tools/registra_run.py /dev/cu.usbmodemXXXX -o .
```

Solo libreria standard, seriale raw a 115200 via `stty`. Il monitor deve poter **inviare**: il gate
si chiude con un tasto qualsiasi, e le domande leggono una riga chiusa da INVIO.

## Nota sul pannello: il quarto colore

Il codice modello `EL122H6W4A` ha campo colore **4**, e §3.6 del datasheet SOLUM lo decodifica
*"4 = RED, YELLOW (BWRY)"*. Ma la riga *Display Colors* di §3.1 porta la nota del produttore
stesso — **"color options are not available for all sizes"** — quindi il campo è una designazione
di **linea**, non del film montato. Il vetro dell'unità porta serigrafato `Newton PRO 12.2" BWR
normal`: è quello il dato per esemplare.

Il frame a bande (sonda `9`) non può chiudere la questione da solo, perchè la Table 6-4 aliasa LUT3
su LUT2 e le due combinazioni con l'accent acceso rendono lo stesso colore per costruzione. Chi
risponde è la sonda `y`, che pilota LUT2 a VSH1 e LUT3 a VSH2 con una waveform propria: è così che
un film BWRY separa rosso e giallo, per soglia di tensione. Sul 9.7" quella stessa sonda ha chiuso
la questione in senso negativo — VSH1 nero, VSH2 rosso, nessun giallo — e il driver di quel
pannello non dichiara affatto le primitive del terzo piano.
