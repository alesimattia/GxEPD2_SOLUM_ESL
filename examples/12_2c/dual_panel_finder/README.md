# dual_panel_finder — cosa misura, e quanto costa

Suite di reverse engineering del **SOLUM 12.2"** (`EL122R2CRN`, lo stesso vetro delle linee
`EL122H6W4A` e Core), 768 × 960 px nativi pilotati landscape come **960 w × 768 h**, due controller
che parlano il set di comandi SSD16xx. Il suo prodotto è un **log** che, insieme alle schermate viste
sul vetro, basta a correggere `src/GxEPD2_SOLUM_122c_960x768.h` perchè piloti il pannello intero.

Stato del bring-up: con la **coda lunga** il driver stampa correttamente su **metà pannello**, e la
metà dipinta è quella adiacente alla propria COF, cioè le **righe 384..767** del driver (lo scambio
delle bande sta già nel driver). Con la **coda corta**, infilata nello stesso connettore da 24 pin
senza adattatore, non stampa niente: il BUSY segue il pull e nessun comando ottiene risposta.

Il pinout dei due connettori è quello della Waveshare ESP32 Driver Board V3: J1 con **CS 15** e
**BUSY 25**; il secondo connettore, sulla breakout della coda corta, con **CS 32** e **BUSY 22**
(pull-down interno, quindi un BUSY scollegato si legge "libero"). **BS1 è fisso a GND** tramite R11
da 0 ohm, non flottante.

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

## La topologia del secondo controller è aperta

Il controller del 12.2" è **sconosciuto**. Del die è misurato solo: set di comandi SSD16xx (`0x12`,
`0x24`/`0x26`, `0x22 = F7`, `0x20`), `0x11 = 03` necessario, TB funzionante, `0x46`/`0x47` inefficaci,
secondo byte di `0x21` accettato con effetto ignoto. SSD1677 e SSD1683 sono **analogie**: tutto ciò
che segue su cascade, `M/S#`, `CL`, rail e strap lo è, e nessuna misura dimostra che il die sia uno
di quei chip.

Che cosa esclude la coda corta muta in J1? Poco. Con BS1 basso (fisso a GND via R11 sulla board) un
controller **indipendente con la piedinatura standard** avrebbe risposto come la lunga, e non l'ha
fatto. **La cascade non è esclusa**: sincronizza due chip (clock `CL` e rail comuni), mentre come
gate e sorgenti si dividano fra loro lo decide il cablaggio del vetro, e due chip in cascade che
scandiscono ciascuno la propria metà sono un pannello dual-scan. Restano tre ipotesi:

| Osservazione sul secondo connettore | slave in cascade | piedinatura diversa o enable mancante | chip danneggiato |
|---|---|---|---|
| BUSY su SWRESET / `0x47` / `0x34` | fermo su tutto, senza il clock del master | fermo finchè il pad giusto non è abilitato | fermo |
| strap debole sul pad giusto (VDDIO o VSS) | indifferente | **il BUSY sale dopo SWRESET** | indifferente |
| ponte di `CL` e rail, sonda `b` | **la banda slave si dipinge** | indifferente | indifferente |

Sono previsioni per analogia, non misure. Le separano la fase 0 di `docs/122c/cable_linking.html`
(continuità e prova a diodo a pannello staccato) e il tasto `t`, che porta le sonde sul secondo
connettore in modalità **sola logica**: `SWRESET`, `0x34` e i pattern `0x47`/`0x46`, mai un `0x22`
con bit analogici seguito da `0x20`, perchè in quella fase i regolatori dello slave non hanno
condensatori.

## Tre limiti di registro, e la domanda sul pannello intero

**Se** il die fosse un SSD1677 Rev 1.0, un controller solo non potrebbe coprire 768 righe, e lo
direbbero tre limiti indipendenti del suo datasheet (un'analogia, il die è sconosciuto):

- **§8.4**, `0x45`: *"00h <= YSA[9:0], YEA[9:0] <= **2A7h**"* — la finestra RAM in Y si ferma alla
  riga **679**;
- **§8.1**, `0x01`: *"Multiplex ratio (MUX ratio) from 300 MUX to **680** MUX"*;
- **p.5 e Table 5-5**: RAM `960x680 bit` per piano, uscite `G[679:0]`.

Il massimo che un SSD1677 vero potrebbe dipingere è **680 righe su 768**. Restano due strade per i
960 × 768 da una coda sola: la sonda `b` (`opcode|0x80`, a ponte cascade fatto) prova se il secondo
chip si indirizza dallo stesso connettore, e la sonda `0` chiede al silicio se è davvero una Rev 1.0,
spazzando MUX e finestra **fuori specifica**.

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

Nessuna sonda sta fuori dalla selezione di default: **`T` esegue la suite intera** e **`t` la
procedura guidata** per fasi di cablaggio (vedi "L'ordine della diagnosi"). Il numero fra
parentesi è il **minimo**, cioè quello che la sonda spende con l'esaustivo spento; l'esaustivo, che
è acceso di default, rimette in gioco le passate condizionali.

### Elettriche, zero refresh

| Tasto | Sonda | Cosa decide nel driver |
|---|---|---|
| `1` | candidate di init, sei varianti | `_InitDisplay`, e il pattern hardware `0x46`/`0x47` per `_writeScreenBuffer` |
| `2` | bit 7 dell'opcode | se `opcode\|0x80` indirizza qualcosa |
| `3` | costo del push ai tre clock | il clock di `selectSPI` |
| `4` | **impronta della coda, e confronto** | il modello di indirizzamento, con la tabella sopra; BUSY letto con i due pull |
| `5` | **SPI a 3 fili, inconcludente con BS1 a GND** | niente nel driver: sulla breakout, dove BS1 è un pad libero, dice se serve un pull-down |
| `6` | **reset a ritentativi** | `reset_duration` di `init()`, cioè l'impulso di `_reset()` della base, e l'attesa dopo lo SWRESET; un BUSY fisso alto non conta come risposta |

Le sonde elettriche sono **puntabili sul secondo connettore** (tasto `B`, o la fase 2 di `t`): le
primitive di bus leggono una coppia (CS, BUSY) bersaglio, J1 (15/25) oppure il secondo connettore
(32/22), quindi `1`, `2`, `4`, `5` e `6` misurano anche la coda corta con la lunga ferma. Quando il
bersaglio è il secondo connettore con il cablaggio a sola logica, i comandi analogici (power on,
`0x14`, `0x15`, il refresh) sono bloccati. Un BUSY che non sale è un esito a sè (**mai salito**, non
una durata di 0 ms) e un BUSY che non scende è **ancora alto**: nessuno dei due si legge come
risposta del controller.

### Il pannello intero e la specchiatura

| Tasto | Sonda | Refresh | Cosa decide |
|---|---|---|---|
| `b` | secondo controller a `opcode\|0x80`, a ponte cascade fatto (richiede il secondo FFC) | 1 | `ADDRESSING_CASCADE`, `SECOND_CONTROLLER_CASCADE` |
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
| `r` | fase driver | 1 | il driver contro il silicio: con un controller solo compaiono i tile bassi e la metà inferiore di quello sulla giunzione |

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

È la sequenza in cui le ipotesi si separano, dalla più economica alla più costosa, e il tasto **`t`**
la percorre per fasi: chiede in che stato è il cablaggio ed esegue solo le sonde utili, nell'ordine
delle fasi di `docs/122c/cable_linking.html`. La coda lunga sta sempre in J1. Ogni frame passa dal
gate e chiede il proprio esito: quale banda ha dipinto, e come sono specchiati i tile dell'angolo
marcato (dritto = la tacca bianca in alto e a destra, 8 righe nere sopra e 16 sotto).

| Stato del cablaggio | Sonde | Criterio |
|---|---|---|
| 1. sola coda lunga in J1 | `4`, `1`, `2`, `6` su J1, poi la fase driver `r` (1 refresh) | la banda **bassa** esce dritta; i tile ad angolo marcato dicono la specchiatura del master |
| 2. coda corta sulla breakout, sola logica | `4`, `1`, `6` sul secondo connettore, BUSY con i due pull, poi la domanda sullo strap debole | il BUSY **sale dopo SWRESET** sul CS dello slave |
| 3. ponte cascade fatto | `b` (1 refresh), poi la fase driver in `ADDRESSING_CASCADE` | compaiono le **due** bande |
| 4. ponte a due CS fatto | la fase driver in `ADDRESSING_DUAL_CS` | compaiono le **due** bande |

Nella fase 2 la domanda guidata chiede se hai collegato debolmente (4,7-10 kohm, mai a filo) un pad
candidato strap o enable a VDDIO o a VSS, un pad e un livello alla volta, e ripete il criterio a ogni
cambio finchè il BUSY risponde o rinunci; un esito nullo non conclude e non esclude la cascade. Il
pad 19, VPP nella piedinatura standard, non si collega mai.

La procedura **chiude con la scheda `DRV`**, che nomina il valore di `SECOND_CONTROLLER_CASCADE` in
`Layout_122c.h` (`true` se le due bande compaiono in cascade, `false` con due CS) e le chiamate
`setMasterMirror()` e `setSlaveMirror()` ricavate dai tile. Il valore di `SECOND_CONTROLLER_CASCADE`
e le specchiature sono caratteristiche con un esito proprio, salvate in NVS come le altre.

Fuori dalla procedura restano nel menu le sonde che non dipendono dal cablaggio: `0` e `j`
(identità del die, TB = 1), le sonde a frame, il partial, le LUT, la temperatura, il ping-pong, lo
sleep. Prima di ogni ricablaggio la fase 0 di `cable_linking.html` (ispezione, prova a diodo e
continuità a board spenta, con la massa presa dall'header) resta la misura che non costa rischio,
e dice dove finiscono `M/S#`, `CL` e i rail.

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
| `t` | **procedura guidata**: chiede lo stato del cablaggio ed esegue solo le sonde utili, fino alla scheda `DRV` |
| `T` | esegue le sonde selezionate (la suite intera di default), con il preventivo dei refresh |
| `s` | riepilogo: registro delle misure, indice delle schermate, confronto fra le code, scheda per il driver |
| `k` | caratteristiche note: ridichiara quello che una sessione precedente ha già determinato |
| `n` | quali sonde eseguire |
| `p` | parametri di misura: temperature, i quattro MUX, gate per controller, clock, timeout, finestra del multimetro, e i parametri di registro (voci 10..15: soft start, entry mode, TB, `0x21` con la sua posizione, reset, border sotto LUT) |
| `c` | quale coda è infilata in J1: è anche la colonna in cui la sonda `4` scrive quando il bus punta J1 |
| `f` | secondo connettore FFC cablato |
| `B` | connettore su cui parla il bus: J1 (CS 15, BUSY 25) o il secondo (CS 32, BUSY 22) |
| `x` | esaustivo: le passate condizionali |
| `w` | polarità del piano BW |
| `v` | righe di dettaglio, spente di default |
| `i` | re-init con una candidata scelta fra le sei |
| `z` | configurazione ed esiti ai valori di fabbrica, e la NVS azzerata |
| `h` | ristampa il menu |

**NVS**: configurazione ed esiti delle caratteristiche stanno in NVS (`Preferences`, namespace
`dpf122`, un byte di versione in testa ai due blob: un profilo di un'altra versione si scarta) e si
salvano a ogni cambio. La configurazione comprende coda, secondo FFC, bersaglio del bus, cablaggio
dichiarato e tutti i parametri del menu `p`. Gli esiti rinviati dalla NVS restano marcati
**dichiarati** e non misurati, perchè non li ha prodotti questa esecuzione; vale ancora come una
misura ai fini dei prerequisiti. La voce `k` è il modo di ridichiarare a mano quello che si sa già,
e `z` azzera anche la NVS.

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
| `dual_panel_finder.ino` | tabella delle sonde, sottomenu, runner, procedura guidata, scheda per il driver, menu |
| `Config.h` | configurazione, persistenza in NVS, bersaglio del bus, limiti del silicio col riferimento al datasheet, input seriale, `waveformGuard()` |
| `Report.h` | frame numerati, gate, registro delle misure, esiti delle caratteristiche (anche in NVS), log a tag |
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

## Le prove che aspettano hardware in più

Il cablaggio della coda corta, i pad da misurare, i condensatori dei rail, l'ordine di messa in
servizio e la variante da scegliere stanno in `docs/122c/cable_linking.html`: qui restano i pin e
quello che la procedura guidata verifica.

Condivisi con J1: **SCK 13**, **MOSI 14**, **DC 27**, **RST 26**, GND. Propri del secondo
connettore:

- **CS su GPIO32**: la netlist dello schematico mette GPIO33 sull'header J3-8 (da confermare sulla
  serigrafia), ma il cablaggio del test usa il 32;
- **BUSY su GPIO22**, con il pull-down interno (`INPUT_PULLDOWN`): un BUSY scollegato o aperto si legge
  "libero" e non manda ogni attesa in timeout, e la prova "pilotato o flottante" funziona con i due
  pull. I GPIO 34..39 non hanno pull interni e non servirebbero a questo.

Da evitare 0/2/12, che sono pin di strapping (IO12 è anche il MISO fittizio, e non arriva a J1).

### A. Coda corta sulla breakout, sola logica

È la fase 2 di `t`. La candidata di init `per-CS` (tasto `i`, voce 6) esegue, con il bus su J1, un
secondo giro che seleziona il **solo** secondo connettore, manda il proprio `SWRESET` e misura la
salita del BUSY su GPIO22 entro 50 ms, con una riga di log. Criterio: **il BUSY sale dopo SWRESET sul
CS dello slave**. Se non sale, un BUSY fisso alto non è una risposta e un BUSY che non si muove con il
pull-down non è una prova di cascade.

### B. Ponti: cascade o due CS

Sono le fasi 3 e 4 di `t`, e vanno fatte solo dopo la fase 0 e, per i rail, dopo la fase a sola
logica. Nella variante **cascade** lo slave sta sul CS 15, si ponticellano `CL` e i rail, e la prova è
la sonda `b` seguita dalla fase driver in `ADDRESSING_CASCADE`; nella variante **due CS** il secondo
controller ha CS 32 e BUSY 22, la fase driver in `ADDRESSING_DUAL_CS` attiva `0x22` e `0x20` con i due
CS bassi insieme (le scariche di fine update di un chip agirebbero sui rail dell'altro) e i rail
comuni vanno ponticellati solo come `cable_linking.html` prescrive. L'esito è il valore di
`SECOND_CONTROLLER_CASCADE` che la scheda `DRV` nomina.

## Acquisire il log

L'ESP32 non può scrivere sul disco del PC: per avere il log su file si può usare
[`tools/registra_run.py`](../../../tools/registra_run.py), un monitor seriale che mostra l'output
della board, le inoltra i tasti con cui si risponde al gate e alle domande e registra tutto in un
file. Dalla cartella di questo esempio:

```
python3 ../../../tools/registra_run.py /dev/cu.usbmodemXXXX -o .
```

Lo script resta **solo macOS**: su Windows non serve, perchè da quel PC si compila soltanto e il
flash e l'acquisizione avvengono dal Mac o da un altro PC. Qualunque monitor si usi deve poter
**inviare**: il gate si chiude con un tasto qualsiasi, e le domande leggono una riga chiusa da INVIO,
che lo script inoltra come `\n`. Con la NVS un riavvio a metà cablaggio non perde configurazione ed
esiti, quindi si può riaprire la sessione e rilanciare `t`.

Il log finisce in `dual_panel_finder_<AAAAMMGG-hhmmss>.txt` nella cartella indicata da `-o`. Senza
la porta come argomento lo script elenca quelle disponibili, Ctrl-C chiude il file. Usa solo la
libreria standard ma gira su macOS, perchè si appoggia a `termios` e a `stty -f`.

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
