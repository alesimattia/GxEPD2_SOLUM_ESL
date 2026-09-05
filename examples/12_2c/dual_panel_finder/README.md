# dual_panel_finder — cosa misura, e quanto costa

Sonda di bring-up del pannello **SOLUM 12.2"** (`EL122H6W4A`, 960×768, due
controller SSD16xx da 960×384). Parla a SPI diretta su una coda alla volta e non
usa nè il driver della libreria nè GxEPD2: serve a stabilire cosa il pannello sa
fare, prima e indipendentemente dal codice che lo pilota.

Su questo pannello **un refresh costa ~19 s**, quindi il numero di refresh è la
misura del costo del test. La regola con cui è organizzato è che ogni refresh
deve rispondere a una domanda che nient'altro può rispondere.

## I refresh, e la domanda a cui ognuno risponde

| Sonda | Refresh | Domanda |
|---|---|---|
| candidate di init | 3 | quale init fa rispondere il pannello |
| sweep MUX | 3 | il refresh scala con le gate line? |
| polarità del piano BW | 2 | `0x24` bit=1 è bianco o nero, e `0x21` inverte? |
| frame a bande | 1 | che colore rende ogni coppia di bit |
| partial d'area | 5 | la finestra RAM confina il refresh? |
| differenziale approfondita | 3 | il controller confronta i piani? quanto costa il carico? |
| partial con LUT via `0x32` | 2 | una waveform breve dall'MCU viene applicata? |
| quarto colore (livelli) | 2 | il film separa VSH1 da VSH2? |
| banchi per temperatura | 4 | l'OTP ha più di un set di waveform? |
| RAM ping-pong | 1 | l'interruttore del differenziale funziona? |
| deep sleep | 3 | dorme, ignora i comandi, si risveglia e stampa? |
| fase driver | 1 | il driver della libreria dipinge quello che le sonde hanno trovato? |
| **totale** | **30** | ~11 minuti con le pause |

Col **secondo FFC cablato** se ne aggiungono due — il frame dello slave a
`opcode|0x80` e il secondo della fase driver — e il totale va a **32**.

Se il partial risultasse esistere le passate condizionali scattano, il test sale
a **35 refresh** (~13 minuti, 37 col secondo FFC), e quei cinque in più servono
a caratterizzarlo.

## Perchè non ci sono più refresh di questi

Tre criteri, applicati a tutte le sonde. Nessuno rimuove un'informazione: quello
che si taglia è deducibile da un'altra misura, oppure non poteva dire niente.

1. **Niente baseline dove il refresh la stabilisce da sè.** Una passata che porta
   la RAM a bianco più le proprie fasce, se dipinge fissa lei il fondo; se non
   dipinge il vetro resta quello di prima, ed è esattamente l'esito da leggere.
   Le sonde con LUT, dei banchi e del ping-pong non hanno una baseline.

2. **Niente passate strutturalmente mute.** La matrice LUT × Display Mode ha
   quattro celle ma due non possono dipingere: in Mode 1 la fascia nera cade su
   LUT0, che nella LUT del GDEH116T91 è a zero, e in Mode 2 cade su LUT2, che
   nella LUT riassegnata alla Table 6-4 è a zero. Si provano le due diagonali, e
   quale delle due dipinge dice anche come il silicio legge le due RAM.

3. **Passate condizionali dove l'esito è deducibile.** Si saltano da sè,
   stampando il perchè — e tutte insieme si riattivano dalla voce **esaustivo**
   del menu:

   | Passata | Salta se | Perchè non serve |
   |---|---|---|
   | `0xCF` e `0xC7` | `0x99` è breve | cercano il guadagno di non ricaricare la LUT, che ha per tetto il costo del carico — e `0x99` lo misura in un secondo |
   | bypass di `0x26` via `0x21` | differenza zero = massima | se coincidono, che il contenuto dei piani non entri nel conto è già dimostrato |
   | passate 4 e 5 dell'area | la passata 1 non guadagna | catena di partial e scaling con l'altezza hanno senso solo se c'è un guadagno da far scalare |

Le **pause di osservazione** (`OBSERVE_MS`, 10 s) non sono tagliabili: servono a
leggere il vetro prima che la sonda successiva lo sovrascriva.

## Cosa misura senza spendere un refresh

La tabella sopra elenca le sonde a video. Il resto il test lo misura senza
accendere un pixel, ed è la parte che vale sulla coda che **non** stampa:

| Misura | Comando | Cosa dice |
|---|---|---|
| livello del BUSY a riposo | — | riletto contro i due pull interni: solo così si distingue una linea pilotata da una flottante, che a pin nudo danno lo stesso livello |
| HV Ready Detection | `0x14` = `0x77` | cool down 10 ms × 8, 7 cicli, quindi **massimo 560 ms**. Il datasheet dà la detection per conclusa quando HV è pronta, quindi la **durata** del BUSY è la risposta: molto sotto il massimo = alte tensioni presenti, al massimo = mai arrivate |
| VCI Detection | `0x15` | al livello POR; qui il datasheet non promette una conclusione anticipata, e conta solo che il BUSY reagisca |
| registri in lettura | `0x2F` / `0x2E` / `0x1B` | `0x2F` ha un POR noto (`0x01`) e fa da prova di validità del percorso; i suoi bit 5 e 4 sono i flag di HV Ready e VCI, cioè l'esito esplicito invece che dedotto dai tempi. `0x2E` è lo User ID da OTP |
| pattern hardware | `0x46` / `0x47` | prova di vita che non dipende dal push SPI dei 46 KB per piano |
| power on e power off | `0x22` = `0xC0` / `0xC3` | tarano `power_on_time` e `power_off_time` del driver |
| bit 7 dell'opcode | 5 comandi | confronto fra opcode nudo e `\|0x80` sulla reazione del BUSY: dice se **questo** controller esegue gli opcode offset |
| clock SPI | — | metà banda a 4 MHz e metà a 20 MHz nello stesso frame |

Sul connettore della board la linea dati in uscita non esiste, quindi le tre
letture di registro diranno sempre "nessuna linea dati": è una proprietà del
**connettore**, non della coda.

## Le dieci leve del partial

Su un SSD16xx le cose che possono accorciare un refresh sono numerabili. Sono
queste, e il test le tocca tutte:

| # | Leva | Comando | Dove viene provata |
|---|---|---|---|
| 1 | banco Mode 2 dell'OTP | `0x22` bit 3 | sonda d'area (`0xFC`) e differenziale approfondita |
| 2 | confronto fra i due piani | — | differenziale approfondita, differenza zero contro massima |
| 3 | ricarica di LUT e temperatura | `0x22` bit 5 e 4 | differenziale approfondita: `0xCF`, `0xC7` e `0x99` |
| 4 | opzioni contenuto RAM | `0x21` | differenziale approfondita, rosso bypassato |
| 5 | RAM ping-pong | `0x37` F[6] | sonda del ping-pong, alla cieca |
| 6 | finestra RAM | `0x44` / `0x45` | sonda d'area, con la fascia di trappola |
| 7 | border waveform | `0x3C` | sonda d'area (`0x01` e `0x80`) e sonda con LUT (`0xC0`) |
| 8 | waveform scritta dall'MCU | `0x32` | sonda del partial con LUT custom, due varianti |
| 9 | banchi di waveform in OTP per range di temperatura | `0x18` / `0x1A` | sonda dei banchi, quattro passate |
| 10 | gate line scandite | `0x01` MUX | sweep del MUX, tre valori |

L'ordine in cui il test le esegue non è casuale: prima si esaurisce quello che
l'OTP offre da sè, poi si chiede all'OTP se ha altri banchi, e solo alla fine si
prova a scrivere una waveform che il produttore non ha qualificato su questo
film.

**Se tutte e dieci danno la durata del refresh pieno**, il partial su questo
pannello non esiste, e `hasFastPartialUpdate = false` nel driver è un fatto e non
una scelta prudenziale. È il solo modo di dirlo con certezza, perchè le prime
sette misurano tutte **la stessa waveform**: ogni `0x22` col bit 4 attivo la
ricarica dall'OTP, e il sensore interno a temperatura ambiente ne seleziona
sempre lo stesso banco.

**Se una è più corta**, l'ordine di preferenza per il driver è: la **9** per
prima, perchè è una waveform di fabbrica tarata su questo film e si ottiene con
due registri prima del refresh; poi la **10**, che è geometria e non tocca la
waveform; poi la **8**, che funziona ma usa una waveform che il produttore non ha
qualificato qui.

Resta **fuori portata una sola leva, e per scelta**: alzare le tensioni di
sorgente con `0x04` accorcerebbe la migrazione del pigmento, ma è l'unica che può
danneggiare il film in modo permanente, e senza la waveform del produttore non
c'è modo di sapere quanto margine ci sia.

## Sequenza di una esecuzione

**Fase probe** — BUSY a riposo e prova dei pull → sweep delle candidate di init
(elettrico) → sonda del bit 7 dell'opcode (elettrica) → sweep del MUX (3 refresh
cronometrati, nessuna pausa) → costo del push ai tre clock → un frame di
identificazione per candidata viva (una pausa ciascuno) → polarità del piano BW
(2 frame, 2 pause) → frame delle 4 bande con i box a finestra parziale (pausa) →
partial d'area → differenziale approfondita → partial con LUT custom → quarto
colore → sonda del secondo controller, solo col secondo FFC cablato → banchi per
temperatura → RAM ping-pong → deep sleep → riepilogo della fase.

**Fase driver** — init del driver al suo clock di default → un frame con fondo
bianco, 5 tile e barra accent → `hibernate()`. Ripetuta per i due modelli di
indirizzamento se il secondo FFC è cablato.

**In coda** — la scheda di osservazione, comune alle due fasi.

Le pause sono i punti in cui il test si ferma perchè il refresh successivo
cancella il precedente, e il test non può guardare il pannello al posto tuo.

## Il menu al boot: una sola compilazione

Non c'è più niente da cambiare nel sorgente. Al reset lo sketch stampa un menu e
aspetta **dieci secondi**; senza risposta parte col profilo salvato in NVS, quindi
un run non presidiato si comporta come prima e il log resta catturabile su file.
INVIO a vuoto vale ovunque «tieni quello che c'è» e risponde subito, così
confermare un profilo già buono non costa un timeout per domanda.

| Voce | Cosa decide |
|---|---|
| **sonde** | quali eseguire, una per una. È la voce che taglia i tentativi: si ripete una singola sonda senza rifare gli undici minuti |
| **coda** | quale coda è infilata nel connettore. Solo l'etichetta del report: il firmware non ha modo di accorgersene da sè |
| **secondo connettore FFC** | abilita testimone sull'altro BUSY, sonda \|0x80 e la seconda passata driver in `ADDRESSING_CASCADE`. **Non è autorilevabile**: BUSY sta su GPIO35 e i GPIO 34..39 non hanno pull interni, quindi la prova "pilotato o flottante" lì non funziona |
| **esaustivo** | esegue anche le passate condizionali: da 30 a 35 refresh, 32-37 col secondo FFC cablato |
| **polarità del piano BW** | di norma si lascia *da misurare*: **la sonda la determina e la applica dentro lo stesso run**, poi la salva. Si forza a mano solo per rifare una misura con l'altra convenzione |
| **pause** | 0 (non presidiato), 10, 30 o 60 s. Ogni pausa riparte comunque premendo INVIO |
| **parametri di misura** | i numeri che le sonde spazzano: le quattro temperature forzate, i tre valori di MUX, i tre clock SPI, il timeout di un refresh e la finestra per il multimetro |

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

I **parametri di misura** chiudono l'ultima ricompilazione. Il caso che pesa è la
temperatura: il §6.9 dà l'OTP per capace di **34 banchi** e sulla banda ne stanno
quattro per run, quindi coprirli tutti è questione di riavvii e non di riflash.
Restano fissi per scelta le sequenze `0x22`, le tabelle LUT, i valori di border
`0x3C` e i parametri di deep sleep: non sono parametri di una misura, sono
l'esperimento, e ogni sonda prova già le combinazioni che hanno senso.

Le scelte si salvano in NVS a ogni modifica, quindi dopo lo swap fisico della coda
e il riavvio non si ridigita niente — polarità compresa.

## Il log su file

L'ESP32 non può scrivere sul disco del PC: il file lo produce lo script di
cattura, che apre la porta, inoltra la tastiera verso la board e scrive tutto su
un `.txt` mentre il run gira.

```
python3 ../../tools/registra_run.py /dev/cu.usbmodemXXXX -o .
```

Senza argomenti elenca le porte disponibili. Non ha dipendenze: sola libreria
standard, la porta va in raw a 115200 con `stty`.

## Come si legge l'esito

Ogni schermata che va guardata porta un **numero**, in un riquadro in alto a
destra dell'area appena ridipinta e in testa a tutte le righe di log che la
riguardano, fra parentesi quadre; i blocchi in cui il test chiede di guardare il
vetro stanno fra una barra di `v` e una di `^`. Fanno eccezione le passate che il
riquadro falserebbe — differenza zero e differenza massima della differenziale —
e la sonda dello slave, dove la RAM è scritta con gli opcode offset e un riquadro
scritto con opcode nudi finirebbe sul master: lì il numero resta nel solo log.

Alla fine il test stampa una **scheda di osservazione**: l'indice delle schermate
prodotte, le righe da compilare guardando il vetro e, per ogni esito, la
conseguenza operativa sul driver. Due cose vanno lette prima delle altre:

- **polarità del piano BW**, perchè tutto ciò che nomina un colore dipende da
  lei. Non è deducibile dal datasheet — la Rev 1.0 definisce `0x21` con un solo
  parametro mentre l'init di fabbrica gliene scrive due — e non è misurabile
  elettricamente, perchè il read-back non esiste su queste code e nessuna
  grandezza leggibile dal firmware dipende dal contenuto dei piani. La sonda la
  riduce a due domande binarie con il riferimento nello stesso frame, e l'esito
  va messo in `BW_POLARITY`: da quel momento ogni sonda scrive il byte giusto;
- **le dieci leve del partial**, la sezione qui sopra: è il consuntivo che dice
  quando `hasFastPartialUpdate = false` è un fatto misurato invece che prudenza.

Gli output di due run precedenti, una coda per file, sono in
[`dual_panel_finder_coda_lunga.txt`](dual_panel_finder_coda_lunga.txt) — la coda
che risponde — e [`dual_panel_finder_coda_corta.txt`](dual_panel_finder_coda_corta.txt).
Sono registrazioni storiche: precedono la numerazione delle schermate, quindi il
loro formato non corrisponde più a quello che il test stampa oggi.

## Nota sul pannello

Il quarto colore su questo esemplare è **una questione ancora aperta**: il codice
modello `EL122H6W4A` ha campo colore `4`, cioè BWRY nominale, mentre il vetro
porta serigrafato `Newton PRO 12.2" BWR normal`. Il frame a bande da solo non
scioglie la contraddizione, perchè mostra cosa rende ogni coppia di bit sotto la
waveform dell'OTP e non cosa il film sa fare: la Table 6-4 aliasa LUT3 su LUT2,
quindi un film a quattro pigmenti darebbe lo stesso esito di uno a tre. Chi
risponde è il probe dei livelli di sorgente, che pilota LUT2 a VSH1 e LUT3 a
VSH2 — tensioni dell'OTP, perchè quelle stanno ai byte 105..109 della LUT e
`0x32` scrive solo 0..104.

Il 9.7", che la stessa domanda l'ha chiusa con la misura, è documentato in
[`../../../README.md`](../../../README.md#010-il-4-colore-non-esiste-questione-chiusa) §0.10.
