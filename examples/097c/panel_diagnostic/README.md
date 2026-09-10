# panel_diagnostic — la suite che corregge il driver

Sonda di bring-up del pannello **SOLUM ESL 9.7"** (672 × 960 nativi, pilotato landscape come
960 × 672, controller SSD1677). Parla al controller **a SPI diretta** e non usa GxEPD2: il driver
custom [`../../../src/GxEPD2_SOLUM_097c_960x672.h`](../../../src/GxEPD2_SOLUM_097c_960x672.h) è
l'oggetto della misura, non lo strumento.

Il prodotto di questa suite è un **log** che, insieme alle schermate viste sul vetro, basta a
correggere quel driver. Due domande sono aperte:

1. **Da quale driver di GxEPD2 il driver custom dovrebbe derivare.** Oggi viene da
   `GDEM133Z91`, e la scelta non è mai stata verificata sul vetro.
2. **Se il suo partial refresh funziona.** Le sequenze ci sono, ma nessuno le ha misurate
   attraverso una catena completa.

Il driver deriva da `GxEPD2_EPD` e può fare **override** dei suoi virtual (`refresh`, `powerOff`,
`hibernate`, `writeImage*`, `setPaged`) oltre a ridefinire i propri metodi interni
(`_InitDisplay`, `_Update_Full`, `_Init_Part`, `_Update_Part`, `_PowerOn`, `_PowerOff`,
`_cleanColorIfPrevious`). Il blocco `DRV` del riepilogo dice, voce per voce, quale metodo
rivedere e con quale valore.

## Una sola compilazione

Nessun `#define` cambia il comportamento e non esistono varianti dello sketch: gli unici
`#ifndef` sono le include guard. Tutto ciò che in un ciclo di reverse engineering verrebbe voglia
di ritoccare e riflashare è un parametro a runtime, scelto dal menu e persistito in NVS:

| Tasto | Cosa cambia |
|---|---|
| `c` | temperature forzate, valori di MUX, clock SPI, timeout di un refresh |
| `e` | la **sequenza di init** editabile: reset, SWRESET, soft start, MUX, entry mode, `0x21`, `0x22`, power off |
| `l` | la **waveform** editabile: sorgente, VSH1/VSH2/VSL/VGH/VCOM, frame di reset e drive, cicli, frame rate, border |
| `x` | esaustivo: accende le passate condizionali dentro ogni sonda |
| `v` | dettagli sul seriale (byte, microsecondi, salita del BUSY) |
| `r` | riporta il profilo ai valori di fabbrica |

`cfg.seq` e `cfg.lut` sono due blob in NVS con un byte di versione: un tentativo riuscito
sopravvive al reboot ed è ripetibile alla lettera. La sonda `m` li esegue su una passata
arbitraria, e il riepilogo li stampa accanto alle misure.

## Il formato del log

Una riga per fatto, tag in colonna fissa, e fra parentesi quadre il numero del riquadro in alto a
destra dell'**area appena ridipinta** — non sempre nello stesso posto, perchè le passate hanno
finestre diverse.

```
== 1  4 bande Mode 1 (1 refresh) ==
[12] MEAS  LE 4 BANDE MODE 1                22=F7   24015 ms
[12] guarda: 1 bianca, 2 nera, 3 e 4 rosse: la linea y=504 si vede? [o/n]
[12] SEEN  o
[12] VERDICT Mode 1 24015 ms, Mode 2 24015 ms: stesso banco
```

| Tag | Cosa è |
|---|---|
| `MEAS` | una misura: descrizione, parametro di `0x22`, durata del BUSY. La descrizione è **la stessa stringa scritta sul vetro** |
| `guarda:` | cosa cercare sul pannello, una riga sola |
| `SEEN` | il tasto che l'operatore ha premuto: `o`, `n`, una cifra. Finisce nel registro accanto alla misura, quindi il log si legge senza appunti esterni |
| `VERDICT` | una conclusione che una durata decide da sè |
| `DRV` | una voce per il driver, nel riepilogo |

Le motivazioni non stanno sul seriale: un run completo fa una quarantina di refresh, e un
paragrafo per sonda renderebbe illeggibile proprio la parte che serve. Stanno nei commenti dei
file e in questo README.

**Il log lo scrive il PC**, perchè l'ESP32 non può:

```
python3 ../../tools/registra_run.py /dev/cu.usbmodemXXXX -o .
```

Senza argomenti elenca le porte. Nessuna dipendenza oltre la libreria standard.

## Le sonde

Un refresh pieno costa **~24 s**: il numero fra parentesi è quanti ne fa la sonda nel caso non
esaustivo. Nessuna sonda avanza da sola, e ognuna apre con reset più init e chiude col pannello
spento, quindi si lanciano in qualunque ordine.

### Silicio e colori

| Tasto | Sonda | Domanda |
|---|---|---|
| `b` | bus e controller (0) | quanto costa il bus a blocchi da 120, 256 e 4096 byte; quanto durano `0xC0`, `0xC3`, `0x83`, `0x03`, `0xB1`, `0x99`; se `0x14` HV ready e `0x15` VCI reagiscono; se `0x13`, che il datasheet non ha, alza il BUSY; se i registri in lettura valgono qualcosa |
| `1` | 4 bande Mode 1 (1) | quali colori rendono le quattro combinazioni dei due piani. È anche il **riferimento di durata** di tutte le altre sonde |
| `2` | 4 bande Mode 2 (1) | stessa RAM, banco di waveform diverso: cambia qualcosa? |
| `3` | livelli di sorgente (2) | LUT2 a VSH1 contro LUT3 a VSH2: il film separa le due tensioni? È l'unica sonda autorizzata al code point VSH2 |

### La waveform dell'OTP

| Tasto | Sonda | Domanda |
|---|---|---|
| `4` | sequenze di `0x22` (3, con `x` 10) | il controller **confronta i due piani**? Una passata con i piani identici contro una con i piani opposti. Con `x` anche `FF`, `CF`, `C7`, `F4`, `0x21{40 00}`, `0x21{08 00}`, `0x37` ping-pong, `0x99` |
| `5` | finestra RAM e trappola (3, con `x` 5) | la finestra limita l'area ridipinta? Il discriminante è la **fascia di trappola** a y=176..215, scritta in RAM e mai compresa in nessuna finestra: se resta bianca le finestre valgono |
| `6` | banchi per temperatura (4, con `x` 5) | l'OTP ha più di un set di waveform? Con `x` anche il banco caldo alla GDEQ0426T82, `0x1A = 0x5A` più `0x22 = 0xD7` |
| `7` | MUX ridotto (3) | il refresh scala con le gate line scandite? |

### Le sequenze di partenza

| Tasto | Sonda | Domanda |
|---|---|---|
| `i` | sequenze di partenza (7, con `x` 8) | quale init rende meglio: sette varianti a confronto, più quella editabile |
| `w` | waveform piena del 370 (3) | una waveform B/N completa scritta dall'MCU gira su questo film, e a che prezzo per il rosso |

### Il partial

| Tasto | Sonda | Domanda |
|---|---|---|
| `8` | partial LUT senza `0x04` (2) | quale delle due diagonali LUT × Display Mode dipinge, con le tensioni dell'OTP |
| `p` | catena di partial (2, con `x` 3) | le sequenze del driver reggono una catena, e l'uscita non lascia trappole |
| `t` | taratura waveform (4+) | sweep di VSH1, struttura e frame, e il bianco: sette varianti per schermata su bande a contatto |
| `0` | deep sleep (4, con `x` 6) | quale parametro di `0x10` addormenta, se si torna, e **se la RAM sopravvive** |
| `m` | passata libera (1) | qualunque combinazione, senza riflashare |

`a` esegue tutto tranne `m`, poi stampa il riepilogo. `s` stampa il riepilogo quando serve.

## Perchè tutto è a SPI diretta

Il driver custom nasce da un driver upstream di GxEPD2 e nè quella scelta nè il suo partial sono
verificati. Se la sonda parlasse al pannello attraverso il driver, ogni misura erediterebbe le sue
assunzioni: un nero pallido non distinguerebbe "il film è così" da "il driver manda la sequenza
sbagliata". Qui si parla al controller, e del driver si riproducono le **sequenze**, comando per
comando.

[`Controller.h`](Controller.h) tiene la corrispondenza, una primitiva per metodo:

| Primitiva | Metodo del driver | Sequenza |
|---|---|---|
| `initPanel()` | `_InitDisplay()` | SWRESET, `0x0C`, `0x01`, `0x3C`, `0x18`, finestra |
| `writePlaneRect()` | `_writeImage()` | finestra, piano, righe da `w/8` byte |
| `partialInit()` | `_Init_Part()` | `0x3C = 0xC0`, `0x32` byte 0..104, `0x04` byte 106..108 |
| `partialPowerOn()` / `powerOffPanel()` | `_PowerOn()` / `_PowerOff()` | `0x22 = 0xC0` / `0xC3`, `0x83`, `0x03` |
| `partialUpdate()` | `_Update_Part()` | power on, finestra piena, `0x22 = 0xCC` |
| `drawPartial()` | `drawImagePartial()` | `0x24`, passata, ricopia in `0x26` |
| `fullRefresh()` | `_Update_Full()` | border di produzione, `0x22 = 0xF7` |
| `setWaveform()` | `setPartialLut()` | power off, poi `partialInit()` |
| `fillByPattern(0x46, 0xFF)` | `writeScreenBufferPrevious()` | pattern hardware su `0x26` |
| `fillByPattern(0x46, 0x00)` | `_cleanColorIfPrevious()` | pattern hardware su `0x26` |

L'unica cosa che arriva dalla libreria è la costante `GxEPD2_SOLUM_097c_lut_partial`: la taratura
deve confrontare esattamente i byte che andrebbero in produzione. Nessuna istanza del driver viene
creata.

## I nove driver SSD1677 di GxEPD2, e la scelta della base

In GxEPD2 1.6.9 i driver su questo controller sono nove, e differiscono in punti che contano:

| Driver | Colori | Init | Refresh | Partial |
|---|---|---|---|---|
| `gdem3c/GDEM133Z91` 960×680 | BWR | `0x0C[4]=80`, MUX 679 | `F7` | **no** |
| `epd3c/750c_Z90` 880×528 | BWR | `0x0C[4]=40`, `0xB1` in init | `C7` | **no** |
| `gdey3c/GDEY116Z91` 960×640 | BWR | solo SWRESET e `0x3C` | `F7` | **no** |
| `epd/1160_T91` 960×640 | B/N | `0x0C[4]=40`, `0xB1` | `F4` | `0x32` + `0xCC` |
| `gdem/GDEM133T91` 960×680 | B/N | come Z91, `delay(15)` | `F7` | `0xFC` |
| `gdem/GDEM102T91` 960×640 | B/N | `0x0C[4]=FF` | `F7` | `0xFC` |
| `gdem/GDEM0397T81` 800×480 | B/N | `0x18` prima di `0x0C` | `F7` / `1A`+`D7` | `0x21{00 00}` + `0xFC` |
| `gdeq/GDEQ0426T82` 800×480 | B/N | `0x18` prima di `0x0C`, SM=2 | `F7` / `1A`+`D7` | `0x21{00 00}` + `0xFC` |
| `epd/370_TC1` 280×480 | B/N | tensioni, VCOM, `0x37`, LUT via `0x32` | `CF` | `CF` |

Due fatti che orientano la scelta: **nessuno dei tre driver a tre colori ha il partial**, e i sei
monocromatici lo hanno tutti con la RAM `0x26` come frame precedente. È il motivo per cui il
partial del driver custom viene dal `1160_T91` e non dalla sua base. I due più recenti,
`GDEQ0426T82` e `GDEM0397T81`, sono il riferimento SSD1677 moderno della libreria: scrivono `0x21`
a due byte prima di ogni refresh e sanno chiedere il banco caldo con `0x1A`.

I criteri, in ordine: (1) init che rende nero pieno e rosso saturo; (2) power e sleep con BUSY
coerente; (3) architettura del partial trasferibile a un film BWR; (4) controllo della waveform
dall'MCU. Li chiudono le sonde `i`, `b`, `p`, `t`, `w`, `0`, e il blocco `DRV` stampa per ciascuno
la variante che ha misurato meglio.

**L'esito della campagna completa**: sei sequenze su sette non mostrano difetti di resa,
la custom e `GDEM133Z91` comprese, e a distinguere le altre sono convenzioni di coordinate, non la
resa. La base non va quindi cambiata, ma nemmeno si può dire che le altre siano peggio. L'unica con
un difetto vero è `GDEQ0426T82`, e il colpevole è il **bit SM** nel terzo byte di `0x01`, che porta
la scansione a pari-poi-dispari e fa uscire il pannello a righe alternate — upstream stesso lo
commenta `// SM (interlaced) ??`. Il secondo idioma bocciato è **`0x21 = {40 00}` prima del
refresh pieno**, che bypassa la RED RAM e **fa sparire l'accent**: quel comando funziona eccome,
forma a due byte compresa, ed è corretto sui monocromatici da cui viene ma distruttivo su un BWR.
`0x18` prima di `0x0C` e il banco caldo restano non discriminati.

**Una variante non viene provata di proposito**: l'init nuda di `GDEY116Z91`, che manda solo
SWRESET e `0x3C` lasciando tutto il front-end analogico al POR. Togliere il soft start è la sola
istruzione che può stressare il booster, e senza una waveform del produttore non c'è modo di
sapere quanto margine ci sia.

Oltre a GxEPD2, la sonda `i` riproduce due sequenze che non vengono da lì: l'**init di fabbrica
SOLUM** (`docs/openepaperlink/nrf52811_tag_fw/unissd.cpp`, ramo `0x19`), che è la più autorevole
per questo vetro, e il **demo Good Display GDEM102Z91**, il gemello commerciale 960 × 640 BWR sullo
stesso controller. Il firmware nRF scrive la finestra Y fino a 639 perchè il suo ramo è condiviso
con l'11.6": qui si usa 671, che è il conteggio vero delle gate di questo pannello.

## Le regole di sicurezza del film

Sono verificate a runtime da `waveformGuard()`, prima di ogni `0x32`, e non sono prudenza generica:

- **mai il code point `11`** nei byte VS, che porta una sorgente a VSH2, la tensione del pigmento
  rosso su questo film. L'unica eccezione è la sonda `3`, che esiste apposta per separare le due
  tensioni;
- **VSH1 non oltre `0x41` = 15 V**, il POR del controller e il punto a cui il pannello ha già
  girato: alzarlo accorcerebbe la migrazione del pigmento, ma è l'unica leva che può danneggiare
  il film in modo permanente;
- **VCOM non oltre `0x44` = −1,7 V**, il valore che Good Display accoppia a questa LUT
  sull'SSD1677;
- **VGH solo al POR**: pilota i transistor, non il pigmento;
- **mai `0x36`**, che scriverebbe l'OTP;
- MUX 300..680 e SPI ≤ 20 MHz, dal datasheet.

## Caratteristiche del vetro che vincolano le misure

Da [`../../../docs/`](../../../docs/):

| Fonte | Fatto | Conseguenza |
|---|---|---|
| Newton Core p. 22, UICR OEPL (terzo colore `0x01`), FCC `EL097R2CRN` | 9.7" Core = **BWR**, 672 × 960, 121 dpi | la guardia su VSH2 vale per ogni waveform |
| Newton PRO §Precautions | esercizio **0–40 °C**, Freezer −25..0, **ghosting dichiarato sotto 15 °C** | la sonda `6` legge i banchi con questa chiave; per una variante Freezer si imposta −20 dal menu |
| Good Display GDEM102Z91, gemello 960 × 640 BWR | full refresh **20 s a 25 °C**, esercizio 0–50 °C | i 24 s di questo pannello sono nella norma della famiglia: nessuna waveform veloce nascosta nell'OTP è attesa |
| Schematico board V3 | **nessun SDO** sul FPC 24 pin | `0x1B`, `0x2E`, `0x2F` restano solo come test di validità del percorso |
| Wiki Waveshare | lo switch 1 sceglie la resistenza di sense del **booster** (A = 3R, B = 0,47R) | se i colori escono deboli o pieni di ghosting, va provata l'altra posizione prima di dare la colpa alla waveform |
| SSD1677 vs SSD1683 | `0x10` ha un solo modo nella Rev 1.0; l'SSD1683 ne ha due, e il modo 1 **ritiene la RAM** | la sonda `0` prova `0x01` e `0x03` e misura la ritenzione |

## Cosa il log dice al driver

Il blocco `DRV` del riepilogo mappa ogni rilevazione sul metodo da rivedere:

| Voce | Metodo | Da quale sonda |
|---|---|---|
| soft start, ordine `0x18`/`0x0C`, MUX B, attesa dopo SWRESET | `_InitDisplay()` | `i`, `b` |
| `0x21` prima del refresh, oggi assente | `_Update_Full()`, `_Update_Part()` | `4` con `x` |
| `full_refresh_time`, `_busy_timeout` | costanti e costruttore | `1`, `6` |
| `power_on_time`, `power_off_time`, sequenza di off | `_PowerOn()`, `_PowerOff()` | `b` |
| LUT, `0x04`, border, `0x22 = 0xCC`, `partial_refresh_time` | `_Init_Part()`, `_Update_Part()` | `8`, `p`, `t` |
| allineamento di `0x26` e invariante di catena | `drawImagePartial()`, `writeImagePrevious()` | `p` |
| uscita dalla catena | `_cleanColorIfPrevious()`, `setPaged()` | `p` con `x` |
| refresh d'area: pieno o partial | override di `refresh(x, y, w, h)` | `5` |
| refresh pieno B/N veloce | un `_Init_Full()` alla 370 e un'API dedicata | `w` |
| banco caldo | `_Update_Full()` con `0x1A` | `6` con `x` |
| parametro di sonno, RAM al risveglio | `hibernate()`, `_InitDisplay()` | `0` |
| VGH e VCOM: scriverli o lasciarli all'OTP | `_Init_Part()` | `t` |
| `hasFastPartialUpdate`, DISPLAY Mode | costanti, `_Update_Part()` | `8`, `2`, `4` |

## Misure già acquisite

Dai run precedenti, come termine di paragone quando si rilegge un log nuovo:

| Grandezza | Valore |
|---|---|
| refresh pieno `0xF7` a temperatura ambiente | 24009-24019 ms su diciannove passate a MUX di default |
| lo stesso a 0 °C forzati | 59060 ms |
| lo stesso a 40 °C | 22962 ms |
| lo stesso a 70 °C, fuori range | 22959 ms: **non viene rifiutato** |
| partial con LUT via `0x32` e `0x22 = 0xCC` | 640 ms |
| modello della durata del partial | 20,0 ms × frame + 83 ms, **valido per le sequenze che non spengono**; quelle con i bit 1 e 0 aggiungono ~140 ms |
| sequenze di Mode 2 dell'OTP, `0xFF` e `0xFC` | 225 e 85 ms, e **non dipingono affatto** |
| MUX 680 / 672 / 336 | 24009 / 24007 / 24032 ms: **non scala** |
| finestra RAM | **non confina**: la fascia di trappola, mai inclusa in una finestra, è comparsa nera |
| pattern hardware `0x46` / `0x47` | 8 ms contro 73 del push SPI |
| bus a blocchi da 120 / 256 / 4096 byte | 0,902 / 0,897 / 0,895 us/byte, l'89% del limite a 10 MHz |
| risveglio dal deep sleep | 234 ms di reset più init |
| deep sleep `0x10 = 0x01` contro `0x03` | RAM **ritenuta** contro RAM **persa e indefinita** |
| waveform piena B/N del 370_TC1 | 979 ms, 25x, ma **non pilota in modo coerente**: resta parte della schermata precedente, righe verticali grigie, e la fascia rossa **sbiadisce** a ogni passata |
| power off `0xC3` / `0x83` / `0x03` | 221 / 139 / 139 ms |

## Le risposte del log che non vanno prese alla lettera

Il tasto che l'operatore preme finisce nel registro come `SEEN`, ma la domanda
che gli è stata posta non sempre distingue quello che serve. Nella campagna
completa **sette risposte su trentacinque schermate** vanno rilette guardando la
foto, e due stavano per far cambiare il driver nel verso sbagliato. Chi rilegge
un log senza le schermate deve saperlo.

| riga | domanda posta | `SEEN` | cosa mostra il vetro |
|---|---|---|---|
| `[5]` | «la trappola è rimasta BIANCA?» | `o` | è **nera**: risposta invertita, e la finestra NON confina |
| `[4]` | «è la frase dell'ultima passata?» | `o` | è quella della **prima**, `0X22=F7`: le due passate `0xFC` non hanno dipinto |
| `[3]` | «colore diverso? `o` = quarto colore» | `n` | le bande sono **nera e rossa**, cioè diverse: rispondeva alla seconda metà della domanda |
| `[2]` | «gli stessi colori di Mode 1?» | `o` | vero ma fuorviante: **la schermata non è mai comparsa** |
| `[9]` | «testo dritto?» | `n` | **due** artefatti previsti: specchiamento dall'entry mode `0x02` e negativo da `0x21` in BW inverse. Nessun difetto |
| `[13]` | «testo dritto?» | `n` | un artefatto previsto, il rosso assente per il bypass di `0x21`, **più** un difetto vero: il nero diventa grigio a righe per il bit SM |
| `[14]` | «testo dritto?» | `n` | capovolgimento previsto dall'entry mode `0x01`, **nessun difetto** |

La schermata `[9]` merita una nota, perchè il riquadro col numero è il
discriminante ed è facile non accorgersene. La sonda lo **pre-inverte**
esattamente quando la variante porta `0x21` in BW inverse (`ProbesUpstream.h:104`,
`Controller.h:544`), così resta leggibile. Sul vetro lo schermo è in prevalenza
**nero** col riquadro normale: l'inversione ha agito, ed è la conferma che la
sequenza di fabbrica è stata riprodotta fedelmente. Se `0x21` non avesse agito
si vedrebbe il contrario su entrambi i fronti, fondo bianco e riquadro in
negativo.

Le tre righe della sonda `i` hanno la stessa causa: il docstring dichiara che
specchiamento, negativo e capovolgimento **sono la conferma** che la sequenza è
stata riprodotta fedelmente, ma la domanda chiede «testo dritto?», che li boccia
tutti e tre.

## Quattro righe del log che non dicono quello che sembrano

- **`1485 ms per passata`** nella catena di partial **non è il costo di un
  partial**. `waitRefresh()` aspetta fino a 800 ms dopo ogni discesa del BUSY
  per riconoscere un refresh multifase, e quel tempo è escluso dai ms riportati
  ma non dall'orologio: 640 di pannello + 800 di finestra + ~45 di push = 1485.
  Una passata in catena su una fascia di 168 righe costa quindi **~680 ms**, cioè
  640 di pannello più i due push da 18, e a schermo pieno **~790**;
- **`DRV _Init_Part senza 0x04  640 ms`** è una durata valida ma un esito
  visivo parzialmente cancellato, e va letto insieme al vetro. Le due varianti
  della sonda `8` girano in sequenza, e la seconda, in Mode 1 con la LUT
  riassegnata, trova le righe della prima su `LUT1` e le riporta verso il
  bianco. La fascia in cima resta però **grigio chiaro con un fantasma**,
  quindi la prima variante **aveva annerito**: la LUT caricata via `0x32`
  pilota anche senza `0x04`, e il ruolo di quel comando è la **saturazione**,
  non l'accensione. Della qualità del nero senza `0x04` quella schermata non
  dice niente;
- **`DRV  hibernate  0x10 = 0x01` non è un consiglio.** È il **primo**
  parametro che ha fatto dormire il controller, perchè `sleepParamFound` viene
  scritto una volta sola al primo esito positivo e i parametri si provano
  nell'ordine `0x01`, `0x03`, `0x11`. Il verdetto che conta è un altro, e sta
  due righe più in su nel log: `0x01` **ritiene** la RAM, `0x03` la perde e la
  lascia indefinita. Il driver manda `0x03` di proposito, perchè il firmware
  rifà comunque un refresh pieno a ogni risveglio e quella è l'accoppiata che
  non può sbagliare;
- **la fase 2 della taratura confronta a tensioni diverse.** `loadVariant()`
  sostituisce la tensione vincente della fase 1 solo alle varianti composte,
  quindi la banda 1 gira alla tensione del driver e le bande 2..7 a quella
  vincente. La **fase 3 è invece pulita**, tutte e sette a 15 V.

Altri due limiti di disegno da conoscere prima di concludere qualcosa:

- **`_cleanColorIfPrevious()` non viene esercitato nel caso negativo** se
  `esaustivo` è spento: la passata di trappola `[7b]` è condizionale, e senza di
  lei manca anche la riga `DRV` corrispondente;
- **l'allineamento di `0x26` che apre la catena non copre il riquadro del
  numero** dipinto dal refresh di riferimento. Su quei pixel le due RAM
  coincidono, quindi cadono su `LUT3` e non vengono mai pilotati: l'angolo in
  alto a destra della fascia resta illeggibile per tutta la catena, e non è
  ghosting.

## Cosa la suite non esercita, di proposito

- il **motore di dithering** (`0x25`): scrive lui stesso nella RAM B/N a partire dal cursore,
  quindi un tentativo alla cieca sporcherebbe le bande;
- il canale **`0x28`**: già misurato, alza il BUSY per ~10 s e non dipinge, quindi è VCOM Sense
  come dice il datasheet e non un terzo piano;
- l'init nuda di `GDEY116Z91`, per la ragione detta sopra;
- la **scrittura dell'OTP** (`0x36`): irreversibile.
