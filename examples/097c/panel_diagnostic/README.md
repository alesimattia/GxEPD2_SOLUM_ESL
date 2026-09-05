# panel_diagnostic — cosa costa, e cosa abilita

Sonda di bring-up del pannello **SOLUM ESL 9.7"** (672×960 nativi, pilotato
landscape come 960×672, controller SSD1677). Parla a SPI diretta e non usa nè
GxEPD2 nè il driver della libreria: serve a stabilire cosa il pannello sa fare,
prima e indipendentemente dal codice che lo pilota.

Qui stanno il costo del test e i criteri con cui è organizzato, e in coda il
**lavoro futuro** che le sonde abilitano — voci da cancellare man mano che
vengono implementate. Cosa misura ogni sonda e come si legge l'esito lo dice il
commento in testa a [`panel_diagnostic.ino`](panel_diagnostic.ino) e la scheda di
osservazione che il test stampa alla fine.

## Il costo del test, e perchè ogni refresh c'è

Un refresh su questo pannello costa **24,8 s**, quindi il numero di refresh è la
misura del costo: **27 nel caso tipico, ~13 minuti** con le pause. La regola è che
ogni refresh deve rispondere a una domanda che nient'altro può rispondere, e da
qui tre criteri applicati a tutte le sonde.

1. **Niente baseline dove il refresh la stabilisce da sè.** Una passata che porta
   la RAM a bianco più le proprie fasce, se dipinge fissa lei il fondo; se non
   dipinge il vetro resta quello di prima, ed è esattamente l'esito da leggere.
   Le sonde con LUT e dei banchi non hanno baseline.

2. **Niente passate strutturalmente mute.** La matrice LUT × Display Mode ha
   quattro celle ma due non possono dipingere: in Mode 1 la fascia nera cade su
   LUT0, a zero nella LUT del GDEH116T91, e in Mode 2 cade su LUT2, a zero nella
   LUT riassegnata alla Table 6-4. Si provano le due diagonali.

3. **Passate condizionali dove l'esito è deducibile**, ed è la parte che pesa di
   più:

   | Passata | Salta se | Perchè non serve |
   |---|---|---|
   | `0xCF`, `0xC7`, `0xFF` | `0x99` è breve | dipingono senza ricaricare la LUT, quindi al massimo risparmiano quello che il carico costa — e `0x99` lo misura in un secondo, per questo viene eseguito **prima** |
   | passate 3, 3b e 4 della differenziale | differenza zero = massima | rifanno lo stesso confronto con `0x26` bypassata, col `0x21` di fabbrica e con l'init di fabbrica intera: cinque refresh che hanno senso solo se il confronto base ha lasciato una domanda aperta. Se zero e massima coincidono il controller non guarda il contenuto dei piani, e nessun registro può fargli guardare quello che non guarda |
   | passate 4 e 5 dell'area | la passata 1 non guadagna | catena di partial e scaling con l'altezza hanno senso solo se c'è un guadagno da far scalare |

   Se il partial risultasse esistere le condizioni scattano tutte, il test sale a
   **37 refresh**, e sono refresh ben spesi. Si disattivano in blocco dalla voce
   **esaustivo** del menu al boot: allora nessuna passata viene saltata, nemmeno
   quella che secondo le misure precedenti non potrebbe dire niente — è la
   modalità da usare quando si sta facendo reverse engineering del silicio e non
   si vuole che il test decida al posto proprio, perchè una passata che
   "dovrebbe" confermare il già noto è dove una sorpresa si nasconderebbe.

Le **pause di osservazione** (10 s) non si tagliano: servono a leggere il vetro
prima che la sonda successiva lo sovrascriva.

## Il menu al boot, e il log su file

Non c'è niente da cambiare nel sorgente: al reset lo sketch stampa un menu e
aspetta **dieci secondi**, poi parte col profilo salvato in NVS. Un run non
presidiato si comporta quindi come prima, e il log resta catturabile su file.
INVIO a vuoto vale ovunque «tieni quello che c'è» e risponde subito, così
confermare un profilo già buono non costa un timeout per domanda.

| Voce | Cosa decide |
|---|---|
| **sonde** | quali eseguire, una per una. Su un test da 27-37 refresh è la voce che conta: si ripete una singola sonda senza rifare venti minuti |
| **esaustivo** | esegue anche le passate condizionali: da 27 a 37 refresh |
| **pause** | 0 (non presidiato), 10, 30 o 60 s. Ogni pausa riparte comunque premendo INVIO, quindi una lettura lenta non costa più un run intero |
| **parametri di misura** | i numeri che le sonde spazzano: le quattro temperature forzate, i tre valori di MUX, il clock SPI, il timeout di un refresh e la finestra per il multimetro |

Il **timeout di un refresh** vale per tutte le passate tranne quelle che ne
chiedono uno deliberatamente diverso: la sonda dei banchi per temperatura ha un
pavimento di **120 s**, perchè verso il freddo la waveform si allunga e coi 40 s
del default la passata fredda scadeva proprio dove il banco è diverso dagli
altri. Alzare il timeout dal menu alza anche quello della sonda; abbassarlo non
lo scende sotto il pavimento. Se una passata scade lo stesso, il refresh non
viene interrotto: si attende la discesa del BUSY e si stampa la durata vera, che
è il dato interessante.

Delle quattro temperature l'ultima è la **passata di controllo**, e va tenuta
fuori dal range di esercizio dichiarato — **0 °C ~ 40 °C**, concorde in tre
fonti di `docs/`. Lì il datasheet promette che nessun TR corrisponde e che
«display will not be updated», quindi un BUSY brevissimo è un rifiuto e non una
waveform veloce: è il controllo che tiene onesta la lettura delle altre tre. Il
marcatore `FUORI RANGE` sul vetro segue il valore, non lo slot, quindi se quel
punto viene portato dentro 0..40 la sonda lo dice invece di dare per rifiutato
un refresh che rifiutato non è.

I **parametri di misura** sono la voce che chiude l'ultima ricompilazione. Il caso
che pesa è la temperatura: il §6.9 dà l'OTP per capace di **34 banchi** e sul
vetro ne stanno quattro per run, quindi coprirli tutti è questione di riavvii —
prima erano nove riflash, ognuno con anche le frasi delle fasce da riscrivere a
mano, perchè il valore di `0x1A` era cablato nelle stringhe. Ora le frasi si
compongono dal valore scelto, quindi sul vetro c'è sempre il numero davvero
programmato.

Restano fissi nel sorgente, e per scelta: le sequenze `0x22`, le tabelle LUT, i
valori di border `0x3C` e i due parametri di deep sleep `0x10`. Non sono
parametri di una misura, **sono l'esperimento** — ogni sonda prova già le
combinazioni che hanno senso, e le celle che mancano sono mute per costruzione.

Il file di log lo scrive il PC, perchè l'ESP32 non può: lo script di cattura apre
la porta, inoltra la tastiera verso la board e registra tutto mentre il run gira.

```
python3 ../../tools/registra_run.py /dev/cu.usbmodemXXXX -o .
```

Senza argomenti elenca le porte disponibili. Nessuna dipendenza: sola libreria
standard.

## Le dieci leve del partial

Il refresh pieno costa 24,8 s, e su un SSD1677 le cose che possono accorciarlo
sono numerabili. Sono queste, e il test le tocca tutte:

| # | Leva | Comando | Dove viene provata |
|---|---|---|---|
| 1 | banco Mode 2 dell'OTP | `0x22` bit 3 | sonda differenziale, e la approfondita per gli interruttori |
| 2 | confronto fra i due piani | — | differenziale approfondita, differenza zero contro massima |
| 3 | RAM ping-pong | `0x37` F[6] | differenziale approfondita, tentativo alla cieca |
| 4 | opzioni contenuto RAM | `0x21` | differenziale approfondita: rosso bypassato e init di fabbrica |
| 5 | LUT e temperatura non ricaricate | `0x22` bit 5 e 4 | differenziale approfondita, `0xCF` e `0xC7` |
| 6 | finestra RAM | `0x44` / `0x45` | sonda d'area, con la fascia di trappola |
| 7 | border waveform | `0x3C` | sonda d'area (`0x01` contro `0x80`) e sonda con LUT (`0xC0`) |
| 8 | waveform scritta dall'MCU | `0x32` | sonda del partial con LUT custom, le due diagonali |
| 9 | banchi di waveform in OTP per range di temperatura | `0x18` / `0x1A` | sonda dei banchi, quattro passate |
| 10 | gate line scandite | `0x01` MUX | sonda del MUX, tre valori |

Le ultime tre righe sono quelle che i run più vecchi non avevano: **le prime sei
misuravano tutte la stessa waveform**, perchè ogni sequenza di `0x22` col bit 4
attivo la ricarica dall'OTP, e il sensore interno a temperatura ambiente ne
seleziona sempre lo stesso banco. Da qui l'ordine di esecuzione: prima si
esaurisce quello che l'OTP offre da sè, poi si chiede all'OTP se ha altri banchi,
e solo alla fine si prova a scriverne una.

**Fuori portata, e per scelta**: alzare le tensioni di sorgente con `0x04`
accorcerebbe la migrazione del pigmento, ma è la sola leva che può danneggiare il
film in modo permanente, e senza la waveform del produttore non c'è modo di
sapere quanto margine ci sia. Il probe del quarto colore tocca `0x32` e non
`0x04` per la stessa ragione.

## Cosa il test non esercita, di proposito

- il **motore di dithering** (`0x25`): scrive lui stesso nella BW RAM a partire
  dal cursore, quindi un tentativo alla cieca rischia di sporcare le bande e
  invalidare la misura principale. Se servono i livelli intermedi va fatto in una
  sonda separata.
- il canale **`0x28`**: un run precedente ha risposto — alla scrittura alza il
  BUSY per ~10 s e non dipinge niente, quindi è VCOM Sense come dice il datasheet
  e non un terzo piano immagine.

Le finestre RAM della misura dei colori variano solo lungo Y a larghezza piena,
cioè lo stesso indirizzamento che il firmware usa a ogni page. Le finestre
ristrette anche lungo X compaiono in due soli posti, e in entrambi sono l'oggetto
della misura: il riquadro della sonda d'area e le fasce delle sonde con LUT.

## Cosa stampa sul seriale

Oltre all'esito dei colori e alla scheda finale, il test riporta:

| Blocco | Cosa contiene |
|---|---|
| ambiente di misura | chip, revisione, core, clock CPU e APB, clock SPI **effettivo** (il divisore intero dell'APB, non quello richiesto), heap libero e blocco massimo |
| init spacchettata | reset hardware, **durata reale del BUSY dopo lo SWRESET** (il driver aspetta 200 ms fissi, qui si misura), blocco di configurazione `0x0C`..`0x11`, finestra RAM piena |
| benchmark del bus | blocchi da 1, 8, 32, 120, 256, 1024 e 4096 byte, per separare l'overhead fisso per transazione dal costo per byte. I due che contano per il driver sono **120 byte**, la riga di `_writeImage`, e **256**, il chunk di `_writeScreenBuffer` |
| transazioni a singolo byte | la via di `writeCommand` / `writeData`, misurata ripetendo `0x7F` NOP |
| ogni fascia scritta | byte, microsecondi di finestra e di dati, us/byte, MB/s e quale percorso ha usato — blocchi da 256 per le fasce uniformi, righe da 120 per quelle con la frase |
| pattern hardware `0x46` / `0x47` | latenza di salita del BUSY, durata, e il guadagno sul push SPI che sostituiscono (80 640 byte per piano) |
| registri in lettura | `0x2F` status, `0x2E` User ID da OTP, `0x1B` temperatura. Sul connettore della Waveshare V3 **non tornano valori** ed è atteso: il FPC a 24 pin non porta fuori la linea dati. Il test lo dichiara invece di stampare rumore |
| refresh | per ogni passata: latenza di salita del BUSY, durata, e rilevamento di più fasi (il BUSY che si rialza entro 800 ms) |
| differenziale | durata delle due passate `0xFC`, confronto col refresh pieno e stima del ciclo B/N che ne risulterebbe |
| partial d'area | per ogni finestra i byte spinti, i microsecondi di push e i millisecondi di refresh, più il confronto fra finestre di altezza diversa, che dice se la durata scala con le gate line |
| deep sleep | quale parametro di `0x10` addormenta davvero il controller, se da addormentato ignora un refresh intero, costo del risveglio |
| riepilogo | totali del bus, attese BUSY, heap consumato, e la stima del ciclo di aggiornamento completo |

Il limite sulla lettura dei registri è del **connettore della board**, non del
pannello: il tag di fabbrica usa un pin del cavo come MISO, quindi la linea di
lettura esiste sul lato pannello e su una coda cablata a mano si può ricavare.
Con quella, `0x2E` identificherebbe il modulo senza ambiguità.

## Dove sta il resto

Il commento in testa allo sketch tiene solo i fatti che servono leggendo il
codice. Le fonti per esteso — tabella comandi dell'SSD1677 con le citazioni del
datasheet, pratiche FCC, firmware OpenEPaperLink, cataloghi Good Display, byte
UICR di fabbrica, part number del vetro ed EEPROM sul cavo — stanno in
[`../../../docs/fonti_esterne.md`](../../../docs/fonti_esterne.md) e nelle
memorie del progetto consumer.

Vale la pena ricordare le **due vie di identificazione che non passano da questo
sketch**, perchè costano un minuto: la serigrafia sul vetro, che sulle foto FCC
dichiara i colori taglia per taglia e unità per unità, e l'etichetta bianca sul
retro del vetro, che sull'esemplare Core porta il part number del **pannello**
(`YMS960672-097AAH-ES-W5`) e identifica il vetro meglio del codice ESL stampato
sul guscio.

# Lavoro futuro, condizionato all'esito delle sonde

Miglioramenti che questa sonda abilita, ma che **non vanno aperti finchè la
misura non li giustifica**. Le entry si cancellano dopo essere state
implementate.

## Partial refresh nei driver, se la sonda lo conferma

**Condizionata all'esito** della sonda «partial con LUT caricata via `0x32`»,
presente in entrambi gli example: [`panel_diagnostic.ino`](panel_diagnostic.ino)
per il 9.7" e [`../../12_2c/dual_panel_finder`](../../12_2c/dual_panel_finder)
per il 12.2". Va aperta solo se almeno una delle quattro varianti chiude il BUSY
sotto i 3 s **e** lascia sul vetro la fascia con la propria cifra.

Le due sonde condividono le stesse tre LUT e la stessa lettura dell'esito, ma
non lo stesso esito atteso: sono due film e due OTP diversi, quindi vanno
misurati separatamente e il driver di uno non eredita la conclusione dell'altro.

Le sonde provano **tre** strade, e l'ordine di preferenza per il driver non è
quello in cui sono eseguite:

1. **banchi di waveform per temperatura** (`0x18` / `0x1A`) — la migliore se
   funziona: la waveform è di fabbrica, tarata su questo film, e si ottiene con
   due registri prima del refresh. Il §6.9 del datasheet dà l'OTP per capace di
   34 set, uno per range di temperatura, e finora ne è stato esercitato uno solo;
2. **MUX ridotto** (`0x01`) — geometria, non tocca la waveform. Dà un partial
   per bande che partono dalla prima gate line;
3. **LUT scritta via `0x32`** — funziona ma usa una waveform che il produttore
   non ha qualificato su questo pannello, quindi è l'ultima scelta.

Se passa la 1, nel driver bastano `setForcedTemperature()` prima del refresh e il
ripristino del sensore interno dopo. Se passa la 2, si scrive `0x01` con il MUX
della fascia e si rimette a 671. Se passa solo la 3, serve il lavoro completo, in
[`../../../src/GxEPD2_SOLUM_097c_960x672.h`](../../../src/GxEPD2_SOLUM_097c_960x672.h)
e sul modello di `GxEPD2/src/epd/GxEPD2_1160_T91.cpp`:

- `hasFastPartialUpdate` a `true` e `partial_refresh_time` alla durata misurata;
- `_Init_Part()` che manda `0x3C = 0xC0` e i 105 byte di LUT via `0x32`, con la
  LUT della variante che ha vinto;
- `_Update_Part()` con la sequenza `0x22` della stessa variante, precedute da
  `_PowerOn()`;
- `writeImageAgain()` che scrive su `0x26` il frame precedente in polarità BW,
  al posto dell'accent;
- `refresh(x, y, w, h)` che imposta la finestra e chiama `_Update_Part()` invece
  di `_Update_Full()`;
- un contatore di partial consecutivi che forzi `_Update_Full()` ogni N, se la
  sonda ha mostrato ghosting cumulativo sulla stessa area.

**Prezzo, già accettato**: in Mode 2 la RAM `0x26` fa da frame precedente,
quindi un frame aggiornato in partial è in bianco e nero. Accent e partial non
convivono nello stesso frame, e la scelta è per frame e non per pixel — il
firmware dovrà decidere quali riquadri valga la pena aggiornare senza rosso.

Il refresh **d'area** invece esiste già ed è misurato (la finestra confina la
zona ridipinta in Y e in X), ma da solo non serve: la durata non dipende
dall'altezza della finestra, quindi restringerla fa guadagnare solo sul push
SPI, che è lo 0,6% del ciclo.

## Taglio a due piani della catena cinema sul 12.2"

Riguarda il **12.2"** e la sonda
[`../../12_2c/dual_panel_finder`](../../12_2c/dual_panel_finder), non questo
example; sta qui perchè le due voci condividono la stessa dipendenza dall'esito
delle sonde.

Oggi `Layout_122c.h` del firmware chiede tre piani al server
(`CINEMA_PLANES = 3`) perchè su quel pannello il quarto colore è ancora da
determinare: il codice modello `EL122H6W4A` ha campo colore `4` contro un vetro
che dice `BWR normal`, e il bring-up è fermo alla seconda coda muta.

Chi risponde è il **probe del quarto colore per livello di sorgente** in
`dual_panel_finder`: se le due metà della banda escono dello stesso colore, il
film ha tre pigmenti e il vetro dice il vero. Allora basta portare
`CINEMA_PLANES` a 2 e `colors=bwr` nella query di `CINEMA_URL`: il resto della
catena — fetch, allocazione, descrittore — deriva già dalla costante. Si
liberano 26130 byte di RAM/PSRAM e un terzo del tempo di fetch. Nello stesso
passaggio le tre primitive del terzo piano escono dal driver 12.2", come sono
già uscite da quello del 9.7".

Nello stesso passaggio va rivisto il descrittore del fallback in
`wallpaper/img_apple_bwry.h` del firmware, che oggi tiene il ramo BWRY proprio
per il 12.2".
