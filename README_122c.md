# GxEPD2_SOLUM_122c_960x768 — driver custom

> **Stato: una banda validata, le due bande insieme no.** Il command set del
> driver è **SSD16xx** e lo split è per righe, 960×384 per controller — entrambi
> misurati e già nel codice. Con una sola coda cablata si stampa correttamente
> una banda 960×384, ed è la metà che il firmware di produzione già disegna.
>
> **Quello che resta non verificato è il modello a due chip select** su cui è
> costruito il dispatch di questo file. Le evidenze raccolte dicono che i due
> controller sono con ogni probabilità una coppia in **cascade**: un solo CS, lo
> slave indirizzato sommando `0x80` all'opcode, e soprattutto lo slave **senza
> oscillatore nè booster**, che riceve clock e tensioni dal master. Se
> `examples/12_2c/dual_panel_finder` lo conferma, di questo driver cambiano il
> dispatch e il modo di parlare al secondo controller. Fino ad allora il codice
> resta com'è. Evidenze in
> [docs/fonti_esterne.md §4](docs/fonti_esterne.md) e
> [docs/122c/identificazione_pannello.md](docs/122c/identificazione_pannello.md),
> stato punto per punto in
> [§5](#5-punti-todoverify-da-validare-al-bring-up).

Il codice modello si legge come `EL <taglia> <generazione> <colore scocca>
<colore display> <tipo tag>`, quindi `EL122H6W4A` = 12.2", generazione H6,
scocca bianca, campo colore `4`, tag con pulsante. Il `4` **non** va letto come
"questa unità è BWRY": è costante su tutta la linea PRO, e la serigrafia sul
vetro della stessa unità dice `Newton PRO 12.2" BWR normal`. Il colore lo decide
il vetro; dettaglio in [docs/fonti_esterne.md §2](docs/fonti_esterne.md).

Driver header-only per pannello e-paper **SOLUM 12.2"** (linee Newton PRO e
Newton Core / M3, stesso pannello)
(960×768 px, 3 colori nativi: bianco/nero/rosso, **2 controller SSD16xx**
da 960×384 ciascuno) su **ESP32**. Estende
[GxEPD2](https://github.com/ZinggJM/GxEPD2) di Jean-Marc Zingg, fornendo:

- **API `showImage()` unificata** come unico entry-point one-shot di
  stampa immagine. Due overload: descrittore generico (output dello
  script Python) e bitmap raw 1bpp B/N (formato
  [image2cpp](https://javl.github.io/image2cpp/));
- **2 API siblings uniformi** `writeImageBlack` / `writeImageRed` per
  scrittura single-channel. Le primitive del terzo piano
  (`writeImageYellow`, `preserveYellow`, `isYellowPreserved`) sono dichiarate
  ma ancora senza corpo, in attesa che il bring-up dica se il quarto colore su
  questo pannello esista: vedi [§6](#6-convivenza-con-il-driver-solum-97);
- **sistema di descrittori universali** (`GxEPDImage::Descriptor`) condiviso
  con il driver 9.7", di cui qui sono utili i formati BW e BWR;
- **dispatch dual-controller master/slave** trasparente al chiamante: le due
  metà del pannello sono bande da 960×384 sull'asse corto e il codice le
  spartisce per righe, traslando lo slave di `y - M.HEIGHT` — vedi
  [§3](#3-architettura-dual-controller-masterslave).

Il driver 9.7" della stessa libreria è documentato in [README.md](README.md).
Per il contesto applicativo (sketch principale, moduli Weather/Calendar,
flussi di boot, OTA) vedi il progetto che la consuma:
[ePaper-weather-dashboard](https://github.com/alesimattia/ePaper-weather-dashboard).

---

## Installazione

Vale quella della libreria, in [README.md](README.md#installazione): questo
driver è un secondo header dello stesso pacchetto. Nello sketch:

```cpp
#define SOLUM_PANEL_122C
#include <GxEPD2_3C.h>
#include <GxEPD2_SOLUM.h>

// Pinout: cs, dc, rst, busy, cs2, busy2, sck, miso, mosi
GxEPD2_3C<GxEPD2_SOLUM_DRIVER_CLASS, SOLUM_MAX_HEIGHT(GxEPD2_SOLUM_DRIVER_CLASS)>
    display(GxEPD2_SOLUM_DRIVER_CLASS(
        GxEPD2_SOLUM_Pins{ 15, 27, 26, 25, 33, 35, 13, 12, 14 }));
```

Sketch di esempio nella libreria:

- `examples/12_2c/dual_panel_finder` — **unico example del 12.2", in due fasi
  separabili**. La *fase probe* è una sonda a SPI diretta su una coda alla
  volta, che non usa nè questo driver nè GxEPD2: confronta tre candidate di
  init, misura le gate line reali col righello numerato, stabilisce il verso
  della banda, stampa le quattro combinazioni dei piani e i colori pieni, e
  senza guardare un pixel misura se le alte tensioni salgono (HV Ready
  Detection 0x14) tentando anche i registri in lettura. Verifica inoltre
  l'addressing a finestra parziale con x diverso da zero, cronometra power on e
  power off, prova entrambi i parametri di deep sleep e ripete il pattern a
  20 MHz per validare il clock che il driver usa per default.
  Due sonde chiudono le domande che il resto non poteva porre, e lo fanno
  caricando una waveform via `0x32`, cioè scritta dall'MCU invece che presa
  dall'OTP: il **partial** con `0x22 = 0xCC` / `0xC4`, che hanno il bit 4
  *load LUT* spento e quindi non sovrascrivono la LUT custom — le passate
  `0xFC` e `0xF4` della sonda d'area ce l'hanno attivo, quindi misurano sempre
  la waveform dell'OTP e non possono dire niente su una breve; e il **quarto
  colore per livello di sorgente**, che pilota LUT2 a VSH1 e LUT3 a VSH2 per
  distinguere un film a tre pigmenti da una waveform che ne pilota solo tre —
  distinzione che il frame a bande non fa, perchè la Table 6-4 aliasa LUT3 su
  LUT2. Nessuna delle due tocca le tensioni: stanno ai byte 105..109 della LUT,
  e `0x32` scrive solo 0..104.
  La *fase driver* esercita invece **questo** driver
  attraverso l'ombrello di selezione: `clearScreen()` sui tre colori pieni,
  dispatch a due controller e un frame di tile a cavallo della giunzione fra le
  bande. Il vecchio `color_cycle` è stato assorbito qui.

---

## Indice

- [Installazione](#installazione)
- [Origine](#origine)
- [1. `GxEPDImage::showImage()` — unico entry-point pubblico](#1-gxepdimageshowimage--unico-entry-point-pubblico)
- [2. Due API siblings single-channel uniformi](#2-due-api-siblings-single-channel-uniformi)
- [3. Architettura dual-controller master/slave](#3-architettura-dual-controller-masterslave)
- [4. Sistema di descrittori universali (`namespace GxEPDImage`)](#4-sistema-di-descrittori-universali-namespace-gxepdimage)
- [5. Punti `TODO[VERIFY]` da validare al bring-up](#5-punti-todoverify-da-validare-al-bring-up)
- [6. Convivenza con il driver SOLUM 9.7"](#6-convivenza-con-il-driver-solum-97)
- [7. API completa](#7-api-completa)
- [8. Cablaggio hardware (Waveshare board / ESP32 Dev Board generica)](#8-cablaggio-hardware-waveshare-board--esp32-dev-board-generica)

---

## Origine

Il driver è **header-only** (`inline` nell'`.h`, nessuna `.cpp`) e nasce
come **strategia ibrida**: scheletro strutturale dual-controller dal
driver upstream
[`GxEPD2_1248c`](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd3c/GxEPD2_1248c.cpp)
(pannello Good Display GDEY1248Z51 12.48" 3-colori, controller **UC8179**
master/slave), con porting integrale delle custom features dal driver
SOLUM 9.7" del progetto
[`GxEPD2_SOLUM_097c_960x672`](README.md)
(`namespace GxEPDImage`, bulk-SPI, `_cleanAccentIfDirty`, page-hint,
`setPaged()` override).

### Perchè 1248c e non 1330c

Il driver SOLUM 9.7" del progetto era stato fork dal
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)
(SSD1677, single-controller). Per il 12.2" la scelta del driver di
partenza è stata riconsiderata: il pannello presenta **2 cavi FFC**, cioè
un'architettura **dual-controller master/slave**, che il 1330c non gestisce
nativamente e il 1248c sì.

| Criterio | 1330c (base 9.7") | **1248c (base 12.2")** |
|---|---|---|
| Cavi FFC supportati nativamente | 1 | **2** (master + slave) |
| Costruttore | `(cs, dc, rst, busy)` | **`(cs_m, cs_s, dc, rst, busy_m, busy_s)`** |
| Pattern split-buffer | assente | **implementato** (`_writeCommandAll`, `_writeCommandMaster`, `_waitWhileAnyBusy`, `ScreenPart`) |
| Controller IC | SSD1677 | UC8179 |

La scelta è stata giusta per lo **scheletro** e sbagliata per il **command
set**: il pannello è a controller SSD16xx, quindi il 1248c resta il modello
della struttura a `ScreenPart` mentre le sequenze di init, power e refresh
vanno prese dal 9.7". Il perchè in
[docs/122c/identificazione_pannello.md §4](docs/122c/identificazione_pannello.md#4-controller-perchè-è-ssd16xx-e-non-uc8179):
l'SSD1677 ha 960 source × 680 gate, e con 768 gate da coprire in due chip
nessuna spartizione sta dentro i 800×600 dell'UC8179.

### Sequenza comandi: SSD16xx, presa dal 9.7"

Del 1248c è rimasta solo la **struttura**. Tutti i comandi UC8179 sono stati
sostituiti con quelli SSD16xx del driver 9.7" della libreria, che gira sullo
stesso silicio:

> Nota storica, per chi ritrovasse la vecchia sequenza in un diff: quello che
> questo documento chiamava "sequenza UC8179" (`0x00` PSR, `0x06` BTST, `0x50`
> CDI, `0x60` TCON, `0x61` TRES `= 480 × 768`, `0xE0`) è in realtà una sequenza
> in stile **SSD2677**, il chip Solomon a 4 colori da 960 × 680 con command set
> UC — archiviato in `docs/`. Non è il nostro: il pannello risponde a SSD16xx.

| Cosa | Comando |
|---|---|
| soft reset | `0x12` + 200 ms |
| soft start | `0x0C = AE C7 C3 C0 80` |
| driver output control (MUX) | `0x01`, derivato da `PART_HEIGHT`: oggi 383 → 384 gate line |
| border waveform | `0x3C = 0x01` |
| sensore di temperatura interno | `0x18 = 0x80` |
| entry mode | `0x11 = 0x03` |
| finestra RAM e cursore | `0x44` / `0x45` / `0x4E` / `0x4F`, in pixel |
| piani immagine | `0x24` B/N, `0x26` accent |
| power on / power off | `0x22 = 0xC0` / `0xC3` + `0x20` |
| refresh | `0x22 = 0xF7` + `0x20` |
| deep sleep | `0x10 = 0x03` |

L'init non fa power on: su SSD16xx lo fa la display update sequence.

Rispetto ai driver stock di GxEPD2 per pannelli simili, questa versione
introduce le ottimizzazioni descritte nelle sezioni successive.

> Nota sulla scelta della base, con quello che si sa oggi: per un pannello a due
> controller **SSD16xx in cascade** il modello upstream più vicino non è il
> 1248c (UC8179, quattro CS separati) ma
> [`GxEPD2_579c_GDEY0579Z93`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey3c/GxEPD2_579c_GDEY0579Z93.cpp)
> — Good Display 5.79" 792×272, due controller SSD1683 su **un solo CS**, con lo
> slave indirizzato da `opcode | 0x80` e il mirror di una metà fatto con l'entry
> mode. Se il bring-up conferma la cascade è da lì che va ripreso il dispatch.

---

## 1. `GxEPDImage::showImage()` — unico entry-point pubblico

Free function template nel namespace `GxEPDImage` (vive nel driver `.h`):

```cpp
template<typename DisplayT>
void GxEPDImage::showImage(DisplayT& display,
                           const GxEPDImage::Descriptor& d,
                           int16_t x = 0, int16_t y = 0);
```

È **l'unica funzione pubblica per stampare un'immagine** sul pannello.
Va chiamata **dentro** un loop `firstPage()`/`nextPage()` del template
`GxEPD2_3C`, dopo `fillScreen()` e prima di `nextPage()`. Supporta i 2
formati del descrittore disponibili (BW / BWR — niente BWRY).

Pattern minimale one-shot:

```cpp
display.firstPage();
do {
  display.fillScreen(GxEPD_WHITE);
  GxEPDImage::showImage(display, *my_desc_ptr);
} while (display.nextPage());
display.hibernate();
```

Per immagini raw image2cpp B/N basta wrappare con la macro `GXEPD_BW_IMAGE`:

```cpp
GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, 960, 768));
```

Il chiamante è responsabile di: aprire il loop paged e chiamare
`hibernate()` se vuole spegnere il pannello.

**Multi-call per page.** `showImage` può essere chiamata 0, 1 o N volte
all'interno di una stessa iterazione del paged loop senza problemi: il
page-tracking interno avanza il counter solo quando il template chiude
la page (`writeImage(black, color)` chiamato da `nextPage()`), non a
ogni chiamata `showImage`. Utile per **compositing multi-immagine**:
sovrapporre più descrittori con offset `(x, y)` diversi, ognuno
disegnato correttamente nella sola porzione che intersecta la page
corrente. Vedi §3 "Loop pixel di `showImage` row-skip" per i dettagli
del meccanismo (identico al 9.7").

### Casi d'uso e firme

| # | Caso d'uso | Firma array (in `.h` incluso) | Firma chiamata |
|---|---|---|---|
| 1 | Bitmap **B/N raw** (image2cpp) | `const unsigned char img_xxx[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BW_IMAGE(img_xxx, w, h));` |
| 2 | **BWR** da `epd_image_converter.pyw` | `const GxEPDImage::Descriptor img_xxx_desc;` *(auto-generato)* | `GxEPDImage::showImage(display, img_xxx_desc);` |
| 3 | **BWR raw inline** (2 piani separati) | `const unsigned char img_b[], img_r[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BWR_IMAGE(img_b, img_r, w, h));` |
| 4 | **Single-channel diretto** (no GFX) | `const unsigned char img_b[], img_r[] PROGMEM = { … };` | `display.epd2.writeImageBlack(img_b, x, y, w, h, true);` + `…Red(…)` + `display.epd2.refresh(false);` |

Casi **1–3** vanno chiamati dentro un loop `firstPage()` / `nextPage()`
con `fillScreen()` prima, e seguiti da `display.hibernate()` se si vuole
spegnere il pannello.

Il caso **4** bypassa il template GFX e si chiama standalone (incluso
`refresh()` esplicito).

---

## 2. Due API siblings single-channel uniformi

I 2 piani RAM del controller SSD16xx sono esposti con shape identica per
scritture single-channel (no refresh):

```cpp
void writeImageBlack(const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x24
void writeImageRed  (const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x26
```

Convenzione bitmap input: bit=1 dove il pixel **non** appartiene a quel
canale (formato compatibile con lo script Python e image2cpp). Per
l'accent rosso il driver applica `~data` prima del transfer SPI per
allinearsi alla polarity nativa SSD16xx (bit=1 in RAM = accent acceso).

Differenza rispetto al 9.7": qui le primitive del terzo piano esistono, ma
**senza corpo**. Non le chiede il contratto della libreria, che è di due soli
metodi (`setPaged()` e `showImagePageHint()`) e in cui `showImage()` compone
black e red senza toccare un eventuale terzo piano: sono dichiarate perchè su
questo pannello il quarto colore è **una questione ancora aperta**. Il codice
modello `EL122H6W4A` ha campo colore `4`, cioè BWRY nominale, mentre il vetro
porta serigrafato `Newton PRO 12.2" BWR normal`, e il bring-up è fermo alla
seconda coda muta: non c'è modo di misurarlo. Quando lo si saprà, o prendono un
corpo vero o vanno rimosse — il 9.7", che la stessa domanda l'ha chiusa con la
misura, non le dichiara affatto. Vedi §6.

---

## 3. Architettura dual-controller master/slave

A differenza del 9.7" (single-controller SSD1677), il 12.2" è pilotato
da **2 controller SSD16xx** in configurazione master/slave, ciascuno
collegato a uno dei 2 cavi FFC del pannello e responsabile di **960×384**.

### ScreenPart inner class

Il driver introduce una inner class `ScreenPart` (pattern preso dal
1248c, dove sono 4 ScreenPart M1/S1/M2/S2; qui semplificato a 2 → M, S).
Ogni `ScreenPart` gestisce le scritture verso un singolo controller:

- proprio CS, DC condiviso col master (i 2 controller condividono SCK,
  MOSI, MISO, DC, RST a livello hardware);
- `WIDTH` e `HEIGHT` riferiti alla **metà** del pannello che gestisce;
- flag `_mirror_x` / `_mirror_y`, impostabili dallo sketch con
  `setMasterMirror()` / `setSlaveMirror()`, per ribaltare la banda che scandisce
  dal proprio bordo verso il centro nel verso opposto all'altra. Il
  ribaltamento è nel data path (ordine di righe, byte e bit), non nei registri:
  è una scelta di implementazione, non un obbligo — il contatore di indirizzo
  saprebbe decrementare da sè con `0x11`, ed è così che GxEPD2 tratta le due
  metà del GDEY0579Z93.

### Split del frame buffer

Lo split cade sull'**asse corto** del pannello: ogni controller pilota una
**banda 960×384**. Misurato al bring-up stampando un rettangolo 960×384 con
l'ESP32 su una sola coda FFC.

```
                       WIDTH = 960
   ┌───────────────────────────────────────────────────┐
   │   MASTER (cs_m)   960 × 384   righe 0..383        │  FFC #1
   ├───────────────────────────────────────────────────┤  HEIGHT = 768
   │   SLAVE  (cs_s)   960 × 384   righe 384..767      │  FFC #2
   └───────────────────────────────────────────────────┘
```

I 960 px sono l'asse **source** (le due code si attaccano al centro dei bordi
lunghi, che sono i bordi dei source), i 768 px sono l'asse **gate**, spartito
384 + 384. Quale delle due bande stia su quale coda è ancora da annotare, e
una delle due va probabilmente ribaltata perchè le bande risultino contigue e
con lo stesso verso.

### Dispatch outer-class

L'outer class `GxEPD2_SOLUM_122c_960x768` espone un'API GxEPD2 standard
(`writeImage`, `writeImagePart`, ecc.) e internamente fa dispatch verso
le 2 ScreenPart:

```cpp
M.writeImagePart(command, bitmap, ..., x, y, ...);
S.writeImagePart(command, bitmap, ..., x, y - M.HEIGHT, ...);
```

Le coordinate `y` per lo slave sono traslate di `-M.HEIGHT` per
riallinearle al sistema di coordinate locale dello slave (righe 0..383 della
sua banda). Il clipping lo fa la `ScreenPart`, quindi la `y` negativa che il
master riceve per le righe basse, e viceversa, non produce scritture.

Per i comandi globali (init, power, refresh) il driver fornisce 2
helper `_writeCommandAll(uint8_t)` / `_writeDataAll(uint8_t)` che
abbassano simultaneamente CS_M e CS_S, in modo che entrambi i controller
ricevano lo stesso comando in parallelo. Il busy wait usa il pattern
`_waitWhileAnyBusy` che attende OR-degli-AND-negati: usciamo solo
quando NESSUNO dei due controller è busy.

> ⚠️ È qui che il modello potrebbe rivelarsi sbagliato. Se i due controller sono
> una coppia in cascade stanno su **un solo** chip select e si distinguono per
> il bit 7 dell'opcode: `_writeCommandAll` con due CS non avrebbe più senso, e
> `_cs_s` / `_busy_s` sparirebbero. Lo decide
> `examples/12_2c/dual_panel_finder`.

### Modalità single-CS per bring-up

Per il bring-up iniziale è disponibile una variante di costruttore
`(cs, dc, rst, busy)` che cabla solo il master (slave passato a `-1`).
In questa modalità la `ScreenPart S` ritorna `isActive() == false` e
tutte le scritture verso lo slave vengono saltate. Permette di
validare il primo controller in isolamento prima di cablare il secondo.

---

## 4. Sistema di descrittori universali (`namespace GxEPDImage`)

Il namespace è unico per la libreria e vive in
[`src/GxEPDImage.h`](src/GxEPDImage.h), incluso da tutti i driver:

```cpp
namespace GxEPDImage {
  enum Format : uint8_t {
    FORMAT_BW_1BPP   = 0,   // 1 buffer 1bpp (compat image2cpp)
    FORMAT_BWR_1BPP  = 1,   // buffer separati black + red
    FORMAT_BWRY_1BPP = 2,   // 3 buffer: showImage rende i primi due
  };

  struct Descriptor {
    Format format;
    uint16_t width, height;
    const uint8_t *data0, *data1, *data2;  // data2 ignorato su questo driver
  };
}
```

Il descrittore porta con sè formato e dimensioni. Per costruire
descrittori inline lo header espone le macro di comodo:

```cpp
GXEPD_BW_IMAGE(ptr, w, h)
GXEPD_BWR_IMAGE(black, red, w, h)
```

`GXEPD_BWRY_IMAGE` esiste, ma `showImage()` rende i soli `data0` e `data1`: il
terzo piano di un descrittore non viene scritto da nessuna parte, su nessun
driver. Per pilotarlo servirebbe una scrittura out-of-band del chiamante, prima
di `firstPage()`, con una primitiva del driver che abbia davvero un corpo.

Lo script Python `epd_image_converter.pyw` genera automaticamente una
variabile `img_<nome>_desc` ad ogni conversione, pronta per essere
passata a `showImage()`. Per il 12.2" lo script va invocato con il
flag BWR (no canale giallo).

---

## 5. Punti `TODO[VERIFY]` da validare al bring-up

I datasheet di prodotto sono materiale marketing e **non dichiarano** nè il
controller IC, nè come i 2 FFC si dividano i pixel, nè la sequenza di init.
Quello che si sa lo si sa dalle etichette di fabbrica, dalle foto del teardown
di certificazione e dalle misure di bring-up: tutto raccolto in
[docs/122c/identificazione_pannello.md](docs/122c/identificazione_pannello.md).

Stato punto per punto, con il valore che il codice usa oggi:

| Punto | Nel codice oggi | Stato | Cosa fare |
|---|---|---|---|
| Controller IC | sequenza SSD16xx presa dal 9.7" | **chiuso** | — |
| Split master/slave | righe 0..383 / 384..767, `y - M.HEIGHT` | **chiuso** | — |
| Geometria per controller | `0x01` MUX da `PART_HEIGHT` = 384, finestre `0x44`/`0x45` in pixel | **chiuso** | — |
| Come si indirizza il secondo controller | due CS, `_writeCommandAll` li abbassa insieme | **aperto, ed è il punto critico** | Le evidenze puntano alla cascade: un solo CS e `opcode \| 0x80`, più `0x21` B[4] = 1 sul master. Lo prova `dual_panel_finder`; se conferma, dispatch da rifare sul modello `GxEPD2_579c_GDEY0579Z93` |
| Perchè la seconda coda non risponde | n/a | **aperto, spiegazione probabile: è lo slave** | In cascade lo slave non ha oscillatore nè booster: servono CL e i rail portati dal connettore master, vedi [docs/fonti_esterne.md §4](docs/fonti_esterne.md) |
| Perchè i controller sono due, e divisi per righe | `PART_HEIGHT` = 384 | **chiuso, per conto** | L'SSD1677 ha 960 source e **680 gate**. Nativi: 9.7" = 672 gate, 11.6" = 640, 12.2" = **768**. Il 12.2" sfora il limite di un chip solo, quindi due controller sono obbligati e lo split **deve** stare sull'asse gate: su quello source un chip solo basterebbe. Non è più un'osservazione, è una derivazione |
| Quale coda è quale banda | M = prima metà | aperto | Annotare al bring-up del secondo controller, eventualmente swap M↔S via cablaggio |
| Ribaltamento della banda | data path (righe, byte, bit) con `setSlaveMirror(true, true)` di default | aperto quale combinazione serva | Provare le quattro combinazioni dallo sketch. Alternativa da valutare: entry mode `0x11` con decremento, come fa GxEPD2 sul GDEY0579Z93 |
| Refresh time | `full_refresh_time = 25000` ms | da misurare | Allineare al tempo reale misurato sulla banda che stampa |
| Clock SPI | default 20 MHz nel driver | da verificare | Su alcuni pannelli SSD1677 il controller non tollera clock alti (il 9.7" della libreria gira a 10 MHz). Se comparissero errori intermittenti, abbassare il clock è la prima prova |
| LUT | OTP del controller | da verificare | Se ghosting, LUT esplicite via `0x32` |

---

## 6. Convivenza con il driver SOLUM 9.7"

Il namespace `GxEPDImage` non è duplicato: sta in
[`src/GxEPDImage.h`](src/GxEPDImage.h) e lo includono tutti i driver della
libreria. Includere due header driver nella stessa translation unit non
ridefinisce niente, e il template `showImage()` è uno solo per entrambi i
pannelli.

Il contratto che quel namespace chiede sono **due soli metodi**, `setPaged()` e
`showImagePageHint()`: `showImage()` compone i piani black e red e non tocca un
eventuale terzo piano, quindi nessun driver deve dichiarare primitive che non
gli servono, e nessuno ha no-op imposti dal contratto. Il namespace dichiara
anche `FORMAT_BWRY_1BPP`, di cui `showImage()` rende i primi due piani: un
descrittore a tre piani è quindi stampabile su entrambi i driver, con il terzo
ignorato, e lo stesso firmware compila contro i due senza rami condizionali.

La scelta del pannello si fa a monte, con il define di selezione che
[`src/GxEPD2_SOLUM.h`](src/GxEPD2_SOLUM.h) traduce in include e nome della
classe — vedi [Selezione del driver](README.md#selezione-del-driver). Anche
l'arità del costruttore è uniforme: entrambi i driver accettano la struct
`GxEPD2_SOLUM_Pins`, e questo legge `cs2` / `busy2` per il secondo controller
e `sck` / `miso` / `mosi` per il bus, campi che sul 9.7" restano a -1. Uno
sketch che cambia pannello non riscrive nè l'include nè la riga di costruzione
del display.

**Bus SPI**: come il 9.7", questo driver passa da `_pSPIx` / `_spi_settings`
della base `GxEPD2_EPD`, ScreenPart comprese. Il default impostato dai
costruttori è l'oggetto `SPI` globale a 20 MHz; un `selectSPI(hspi, ...)`
chiamato prima di `init()` lo sostituisce.

---

## 7. API completa

La lista degli overload `drawImage*` / `writeImage*` / `writeImagePart*`
ereditati dalla base class è documentata in
[drawImage_overloads_it.md](drawImage_overloads_it.md)
(o la versione inglese
[drawImage_overloads.md](drawImage_overloads.md))
— le firme sono identiche per i 2 driver, cambia solo l'implementazione
sottostante (single-controller nel 9.7", dual-controller qui).

Sono override di virtual del base class `GxEPD2_EPD` necessari al
contratto della libreria — non sono pensati per uso diretto: lo sketch
chiama `showImage()` per immagini singole, oppure il template
`GxEPD2_3C` invoca `writeImagePart(black, color)` durante il flusso
paged.

Differenze API rispetto al 9.7":

| API | 9.7" | 12.2" |
|---|---|---|
| `clearScreen(value)` | ✓ | ✓ |
| `clearScreen(black, color)` | ✓ | ✓ |
| `clearScreen(black, color, yellow)` | **assente** | **assente** |
| `writeScreenBuffer(value)` | ✓ | ✓ |
| `writeScreenBuffer(black, color)` | ✓ | ✓ |
| `writeScreenBuffer(black, color, yellow)` | **assente** | **assente** |
| `writeImageBlack` | ✓ (cmd 0x24) | ✓ (cmd 0x24) |
| `writeImageRed` | ✓ (cmd 0x26) | ✓ (cmd 0x26) |
| `writeImageYellow` | **assente** (0x28 è VCOM Sense, misurato) | dichiarata, corpo vuoto |
| `preserveYellow` / `isYellowPreserved` | **assenti** | dichiarate, corpo vuoto |
| `setPaged()` override | ✓ | ✓ |
| `showImagePageHint()` getter | ✓ | ✓ |
| Bulk-SPI `writeBytes` | ✓ | ✓ |
| Dual-controller dispatch | n/a | ✓ (M + S) |
| Costruttore single-CS bring-up | n/a | ✓ |

---

## 8. Cablaggio hardware (Waveshare board / ESP32 Dev Board generica)

> Questa sezione è la versione Markdown della pagina HTML interattiva
> [connessioni.html](docs/122c/connessioni.html). I diagrammi sono file SVG
> separati ([cablaggio_waveshare.svg](docs/122c/cablaggio_waveshare.svg) e
> [cablaggio_devboard.svg](docs/122c/cablaggio_devboard.svg)) per garantire il
> rendering corretto sia in VS Code Markdown preview sia su GitHub. La
> pagina HTML standalone offre stile più ricco (dark theme, code
> highlighting, anchor links) ed è consigliata per la consultazione
> offline; il contenuto tecnico è lo stesso di questa sezione.

### Concetti chiave

Il pannello ha **2 cavi FFC da 21 pin**: ognuno porta a uno dei due
controller IC (master / slave) integrati nel pannello stesso. Per
pilotarlo da un solo ESP32 servono 9 pin GPIO totali:

- 🟧 **5 condivisi** (`SCK`, `MOSI`, `DC`, `RST`, più `VCC` e `GND`):
  un solo pin GPIO ESP32, che si dirama in parallelo verso entrambi i FFC;
- 🟦 **2 dedicati al master** (`CS_M`, `BUSY_M`): solo verso FFC #1;
- 🟪 **2 dedicati allo slave** (`CS_S`, `BUSY_S`): solo verso FFC #2.

`MISO` non è obbligatorio per il driver ma alcuni layout lo richiedono
per leggere temperatura / OTP del controller.

### Setup A — Waveshare E-Paper ESP32 Driver Board

Riferimento:
[prodotto Waveshare](https://www.waveshare.com/e-paper-esp32-driver-board.htm) ·
[wiki](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board).
La board ha **un solo connettore FFC interno** con pinout SPI
hardware-cablata. Il FFC interno pilota il **master**; per lo
**slave** servono 2 GPIO liberi presi dai pin header laterali e un
secondo connettore FFC esterno cablato a mano.

![Schema cablaggio Waveshare E-Paper ESP32 Driver Board](docs/122c/cablaggio_waveshare.svg)

| Segnale | GPIO ESP32 | Sorgente | Destinazione | Tipo |
|---|---|---|---|---|
| SCK | 13 | FFC interno + jumper | FFC #1 + FFC #2 | 🟧 condiviso |
| MOSI (DIN) | 14 | FFC interno + jumper | FFC #1 + FFC #2 | 🟧 condiviso |
| DC | 27 | FFC interno + jumper | FFC #1 + FFC #2 | 🟧 condiviso |
| RST | 26 | FFC interno + jumper | FFC #1 + FFC #2 | 🟧 condiviso |
| 3V3 / GND | — | FFC interno + jumper | FFC #1 + FFC #2 | 🟧 condiviso |
| **CS_M** | 15 | FFC interno della board | FFC #1 | 🟦 master |
| **BUSY_M** | 25 | FFC interno della board | FFC #1 | 🟦 master |
| **CS_S** | ~~33~~ **32** | Pin header espansione | FFC #2 esterno | 🟪 slave |
| **BUSY_S** | 35 | Pin header espansione | FFC #2 esterno | 🟪 slave |

> ⚠️ **GPIO33 non è portato fuori su questa board.** Nelle net dello schematico
> compaiono 32, 34 e 35 su pin consecutivi dell'header e il 33 non c'è: il
> candidato per `CS_S` è **GPIO32**, output-capable, sull'header e non usato da
> nient'altro. Gli snippet qui sotto riportano ancora 33 perchè è il valore che
> sta nel firmware: se la coda lunga non risponde, questo è il primo posto da
> guardare, prima del pannello.
>
> ⚠️ E prima ancora: se i due controller sono una coppia in **cascade**, un
> secondo chip select non serve affatto — serve invece un ponte passivo che
> porti CL e i rail dal connettore master a quello slave. Vedi
> [docs/fonti_esterne.md §4](docs/fonti_esterne.md). I due pin della cascade
> stanno nel pin table dell'SSD1677 stesso, solo dichiarati riservati: `M/S#`
> *"reserved pin, should be connected to VDDIO"* e `CL` **I/O**, *"left open in
> application"* ma presente sia fra i VIH d'ingresso sia fra i VOH d'uscita.

Costruttore corrispondente:

```cpp
#define SOLUM_PANEL_122C
#include <GxEPD2_3C.h>
#include <GxEPD2_SOLUM.h>

GxEPD2_3C<GxEPD2_SOLUM_DRIVER_CLASS,
          SOLUM_MAX_HEIGHT(GxEPD2_SOLUM_DRIVER_CLASS)>
    display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{
        /*cs   */ 15,    // FFC interno della board
        /*dc   */ 27,
        /*rst  */ 26,
        /*busy */ 25,
        /*cs2  */ 33,    // FFC esterno cablato a mano
        /*busy2*/ 35,
        /*sck  */ 13,
        /*miso */ 12,    // -1 se non si legge dal controller
        /*mosi */ 14 }));
```

> ⚠️ **Caveat board Waveshare.** Il connettore FFC interno è 24-pin
> standard Waveshare; i FFC del Solum 12.2" sono 21-pin. Verifica con
> multimetro la corrispondenza pin-a-pin prima di alimentare —
> tipicamente serve un adattatore 24→21 pin tra la board e il FFC del
> Solum.

### Setup B — ESP32 Dev Board generica + 2 FFC breakout 0.5 mm

Per chi parte da una **ESP32 Dev Board nuda** (es. ESP32-WROOM-32 DevKitC,
NodeMCU-32S, ecc.) e **2 FFC breakout 0.5 mm a 21 pin** (reperibili su
AliExpress / Amazon, ~5 € la coppia). Più flessibile della Waveshare board:
pinout SPI scegliibile, FFC simmetrici (no adattatore), tutti i 9 GPIO
accessibili sui pin header DIP.

![Schema cablaggio ESP32 Dev Board generica con 2 FFC breakout 0.5 mm](docs/122c/cablaggio_devboard.svg)

| Segnale | GPIO ESP32 | Cablaggio | Tipo |
|---|---|---|---|
| SCK | 18 (VSPI default) | jumper a entrambi i breakout | 🟧 condiviso |
| MOSI (DIN) | 23 | jumper a entrambi i breakout | 🟧 condiviso |
| MISO | 19 (opzionale) | jumper a entrambi i breakout | 🟧 condiviso |
| DC | 27 | jumper a entrambi i breakout | 🟧 condiviso |
| RST | 26 | jumper a entrambi i breakout | 🟧 condiviso |
| 3V3 / GND | — | rail breadboard a entrambi i breakout | 🟧 condiviso |
| **CS_M** | 15 | jumper al breakout #1 | 🟦 master |
| **BUSY_M** | 25 | jumper al breakout #1 | 🟦 master |
| **CS_S** | 33 | jumper al breakout #2 | 🟪 slave |
| **BUSY_S** | 35 | jumper al breakout #2 | 🟪 slave |

Costruttore corrispondente (SPI default VSPI):

```cpp
// I tre campi del bus restano a -1: "quelli di default della board".
GxEPD2_3C<GxEPD2_SOLUM_DRIVER_CLASS,
          SOLUM_MAX_HEIGHT(GxEPD2_SOLUM_DRIVER_CLASS)>
    display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{
        /*cs  */ 15, /*dc   */ 27, /*rst  */ 26, /*busy */ 25,
        /*cs2 */ 33, /*busy2*/ 35 }));
```

> ✅ **Setup consigliato per il bring-up.** Tutti i segnali sono fisicamente
> accessibili con sonda logica / oscilloscopio sui pin della breadboard,
> e si possono swappare velocemente master ↔ slave scollegando 2 jumper
> invece di rifare il PCB.

### Strategia di bring-up step-by-step

1. **Step 1 — una sola coda cablata: fatto.** Con il costruttore single-CS
   ```cpp
   // cs2 e busy2 a -1: le scritture verso la metà slave vengono saltate.
   display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }));
   ```
   una banda **960×384** si stampa correttamente: il controller risponde,
   BUSY viene rilasciato, la geometria per controller è quella. Da qui vengono
   le conclusioni su famiglia del controller e split.
2. **Step 2 — seconda coda: non stampa nulla.** Lo stesso cablaggio spostato
   sull'altro FFC non produce alcun aggiornamento. Da escludere in quest'ordine:
   - **ordine dei pin ribaltato** sulla seconda coda: le due code escono da
     bordi opposti, se sono la stessa parte la seconda è ruotata di 180° e il
     pin *n* cade sul pin *N+1-n*. Verifica di continuità sui GND prima di
     alimentare;
   - **rail di boost non portati** sul secondo attacco: logica presente,
     elettroforesi impossibile, quindi nessun pixel si muove;
   - **BUSY o RST fuori posizione**: il driver resta appeso in attesa e non
     arriva alcun comando.

   **Spiegazione che le comprende tutte, ed è la più probabile**: quella coda è
   lo **slave di una coppia in cascade**, e da sola non può funzionare per
   costruzione — niente oscillatore, niente booster, clock e tensioni dal
   master. Lo dicono il datasheet SSD1683 §6.12 e il tag di fabbrica, che ha
   l'elettronica analogica attorno a un solo connettore e l'altro nudo.

   Dettaglio delle ipotesi e come distinguerle in
   [docs/122c/identificazione_pannello.md §5-§6](docs/122c/identificazione_pannello.md#6-perchè-la-seconda-coda-resta-muta)
   e [docs/fonti_esterne.md §4](docs/fonti_esterne.md).

### Approvigionamento breakout FFC: usare 22 / 24 pin con 1 pin libero

I breakout FFC 0.5 mm a **21 pin esatti** non sono uno standard di mercato:
in pratica si trovano solo a **20 pin** (standard "TFT panel") o a **22 / 24
pin**. La soluzione pratica è **usare un breakout da 22 o 24 pin lasciando
1 (o più) pin scoperto su un lato**.

> ⚠️ **Errore catastrofico da evitare.** Uno shift di 1 pin = pinout
> completamente sbagliata = potenziale corto su `VCC` / `GND` distruttivo
> per il controller del pannello. Il protocollo di verifica multimetro qui
> sotto è **obbligatorio** prima di alimentare.

#### Allineamento corretto

Il FFC a 21 pin si infila nel connettore da 22 (o 24) pin allineato a **un
solo lato**, lasciando 1 (o 3) pin liberi sul lato opposto:

```
Connettore 22 pin del breakout (vista dall'alto):
┌─────────────────────────────────────────┐
│ 1  2  3  4  5  6  7 ... 20 21 22        │
│ ▒  ▒  ▒  ▒  ▒  ▒  ▒      ▒  ▒  ░        │  ░ = pin lasciato libero
│ │  └──────── 21 pad del FFC ─────┘      │  ▒ = pad del FFC
│ pin 1 del FFC                pin 21     │
└─────────────────────────────────────────┘
              allinea a SINISTRA (pin 22 libero)


Connettore 24 pin del breakout (configurazione consigliata):
┌────────────────────────────────────────────────┐
│ 1  2  3  4  5  6  7 ... 20 21 22 23 24         │
│ ▒  ▒  ▒  ▒  ▒  ▒  ▒      ▒  ▒  ░  ░  ░         │
│ │  └──── 21 pad del FFC ─────┘                 │
│ pin 1 del FFC          pin 21                  │
└────────────────────────────────────────────────┘
              allinea a SINISTRA (pin 22-24 liberi)
```

**Quale lato scegliere?** Tipicamente il pin 1 del FFC è marcato con un
triangolino bianco/blu sul ribbon. Allineamento standard: il pin 1 del FFC
va sul pin 1 del connettore. Quindi **lascia liberi i pin in eccesso
sull'estremità opposta**. Se la marcatura non è visibile, il pin 1 del FFC
è quasi sempre `VCC` o `GND` — si capisce con multimetro.

#### Procedura di verifica multimetro (obbligatoria)

1. **Inserisci il FFC nel breakout** allineato come deciso (es. tutto a
   sinistra, sui pin 1..21).
2. **Multimetro in continuità (beep test):** misura tra il pin 1 del
   breakout (uscita DIP 2.54 mm) e ciascun pad esposto del FFC dal lato
   opposto. Quello che dà beep è la giunzione. Verifica che corrisponda
   davvero al pin 1 del FFC come previsto dal datasheet del pannello.
3. **Test alimentazione (resistenza):** identifica `VCC` e `GND` del FFC
   (di solito i pin estremi o adiacenti). Misura con multimetro in
   resistenza tra questi 2:
   - se dà ~0 Ω → **CORTO**, il FFC è inserito al contrario o shifted, NON ALIMENTARE;
   - se dà alcune centinaia di kΩ o ∞ → OK, prosegui.
4. Solo se il test passa, applica i 3.3V con un alimentatore current-limited
   (es. ~100 mA) per i primi secondi e verifica che la corrente assorbita
   sia ragionevole (decine di mA a riposo).

#### Tabella alternative

| Soluzione | Pro | Contro |
|---|---|---|
| **Breakout 22 pin, 1 pin libero** | Esatto count + 1, allineamento più sicuro (solo 1 pin in eccesso) | Meno comuni dei 24-pin |
| **Breakout 24 pin, 3 pin liberi** ⭐ | Standard di mercato, facile da trovare (è lo stesso connettore Waveshare) | 3 pin liberi richiedono attenzione extra all'allineamento |
| **Breakout "universale" 30 pin** | Copre anche pannelli più larghi futuri | Spazio sprecato in breadboard |
| **Breakout 20 pin + taglio FFC** | Economico | **Distruttivo**, si perde 1 segnale, sconsigliato senza datasheet che confermi che il pin tagliato è NC |
| **PCB custom 21 pin (Hirose FH12-21S-0.5SH)** | Soluzione definitiva, dimensione esatta | 1-2 settimane consegna, richiede KiCad/EasyEDA |

⭐ **Soluzione consigliata**: breakout 24 pin. Sono lo stesso passo del
connettore FFC interno della Waveshare board (24 pin 0.5 mm), facilissimi da
reperire su AliExpress / Amazon a ~3 € l'uno. Cerca *"24 pin 0.5 mm FPC
connector breakout DIP"*. Per il setup B servono **2 unità**.

#### Verifica preliminare obbligatoria

Prima di acquistare, misura con un calibro la **larghezza del FFC** del
Solum 12.2":

- 21 pin a **0.5 mm pitch** → larghezza ~11.5 mm (= 21 × 0.5 + margine);
- 21 pin a **1.0 mm pitch** → larghezza ~21 mm.

Se misuri ~21 mm il pitch è 1.0 mm e servono breakout 1.0 mm (raro su
pannelli moderni ma capita su ESL vecchi).

### Caveat hardware riassunti

| Categoria | Vincolo |
|---|---|
| **Tensione** | Pannello NON 5V tolerant. VCC + tutte le data line a 3.3V. |
| **Lunghezza jumper** | < 10 cm consigliato. > 15 cm → ringing visibile a 20 MHz. Se i byte SPI sono corrotti, abbassa il clock SPI. |
| **Alimentazione** | Pacco da 6× CR2450 da datasheet → assorbimento non banale. Se la 3V3 cala sotto 3.0V durante refresh, alimenta il pannello da fonte esterna 3.3V. |
| **GPIO 35 input-only** | OK per BUSY_S, NON usabile per CS_S. Per CS_S serve un GPIO output-capable: sulla board Waveshare **32** (consigliato), in alternativa 4, 21, 22. Il **33 non è portato fuori** su quella board. |
| **Strapping pins** | GPIO 0, 2, 5, 12, 15 sono strapping. GPIO 15 (CS_M) è OK perchè parte HIGH. Evita di mettere il pannello su GPIO 0, 2, 12. |
