# GxEPD2_SOLUM_097c_960x672 — Driver custom

Driver header-only per pannello e-paper **SOLUM ESL 9.7"** (672w × 960h px
native portrait → `setRotation(0)` → 960w × 672h px landscape,
convenzione `NwxMh` = N px larghezza × M px altezza;
4 colori nativi: bianco/nero/rosso/giallo, controller SSD1677) su
**ESP32**. Estende [GxEPD2](https://github.com/ZinggJM/GxEPD2) di
Jean-Marc Zingg, fornendo:

- **API `showImage()` unificata** come unico entry-point one-shot di
  stampa immagine, con hibernate automatico opzionale. Due overload:
  descrittore generico (output dello script Python) e bitmap raw 1bpp
  B/N (formato [image2cpp](https://javl.github.io/image2cpp/));
- **3 API siblings uniformi** `writeImageBlack` / `writeImageRed` /
  `writeImageYellow` per scrittura single-channel, usate nel flusso
  paged con yellow iniettato "out-of-band" (vedi [sezione dedicata](#3-perchè-il-yellow-è-out-of-band-nel-flusso-paged));
- **sistema di descrittori universali** (`GxEPDImage::Descriptor`) che
  porta con sè formato e dimensioni dell'immagine (BW / BWR / BWRY);
- **API per il 4° colore** (giallo) sul comando `0x28` del controller
  SSD1677. Che `0x28` sia davvero il piano giallo di questo pannello resta un
  assunto **non confermato**: vedi
  [§0.10](#010-il-4-colore-non-appare-questione-aperta).

Per un esempio d'uso completo (sketch, moduli Weather/Calendar, flussi di
boot, OTA) vedi il progetto che la consuma:
[ePaper-weather-dashboard](https://github.com/alesimattia/ePaper-weather-dashboard).

---

## Indice

- [Installazione](#installazione)
- [0. Il pannello SOLUM 9.7"](#0-il-pannello-solum-97)
- [Origine](#origine)
- [1. `GxEPDImage::showImage()` — unico entry-point pubblico](#1-gxepdimageshowimage--unico-entry-point-pubblico)
- [2. Tre API siblings single-channel uniformi](#2-tre-api-siblings-single-channel-uniformi)
- [3. Perchè il yellow è "out-of-band" nel flusso paged](#3-perchè-il-yellow-è-out-of-band-nel-flusso-paged)
- [4. Sistema di descrittori universali (`namespace GxEPDImage`)](#4-sistema-di-descrittori-universali-namespace-gxepdimage)
- [5. Ottimizzazioni rispetto al driver stock](#5-ottimizzazioni-rispetto-al-driver-stock)
- [6. API completa](#6-api-completa)
- [Licenza](#licenza)

---

## Installazione

Libreria Arduino a sè stante che **dipende da GxEPD2**: questo repo non la
contiene, la estende con il driver di un pannello che upstream non supporta.

1. installa **GxEPD2** (>= 1.6.9) e **Adafruit GFX Library** dal Library
   Manager dell'IDE;
2. installa questa libreria: *Sketch → #include libreria → Aggiungi libreria
   .ZIP*, oppure clonando il repo in `Documents/Arduino/libraries/`;
3. nello sketch:

```cpp
#include <GxEPD2_3C.h>
#include <GxEPD2_SOLUM_097c_960x672.h>

// Globale, non locale a setup(): selectSPI() conserva il puntatore a hspi
SPIClass hspi(HSPI);

// CS, DC, RST, BUSY come cablati sulla Waveshare E-Paper ESP32 Driver Board
GxEPD2_3C<GxEPD2_SOLUM_097c_960x672, GxEPD2_SOLUM_097c_960x672::HEIGHT / 8>
    display(GxEPD2_SOLUM_097c_960x672(15, 27, 26, 25));

void setup()
{
  hspi.begin(13, 12, 14, 15);                                  // SCK, MISO, MOSI, SS
  display.epd2.selectSPI(hspi, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 2, false);
  display.setRotation(0);                                      // 960w × 672h landscape
  display.setFullWindow();
}
```

Requisiti: target **ESP32** — il driver usa `SPIClass::writeBytes` e ha i
`delay(1)` di yield WDT per ESP8266 rimossi dai hot path — e alimentazione
3,3 V su VCC **e su tutte le data line** del pannello, che non sono
5V-tolerant. Adafruit_GFX serve solo se `ENABLE_GxEPD2_GFX` è attivo.

Se preferisci non installarla e tenerla dentro il progetto che la usa (come
submodule), l'include diventa relativo al path del submodule:
`#include "GxEPD2_SOLUM_ESL/src/GxEPD2_SOLUM_097c_960x672.h"`.

---

## 0. Il pannello SOLUM 9.7"

Caratteristiche del pannello ricavate dai cataloghi SOLUM in [`docs/`](docs/)
(`Newton-Pro_Specifications.pdf`, `Newton-Core_Specifications.pdf`), dallo
schematico `E-Paper_ESP32_Driver_Board_V3.pdf` della board Waveshare e dalle
verifiche su hardware. Spiegano il perchè di diverse scelte del driver, quindi
conviene leggerle prima del resto.

### 0.1 Identificazione: quale 9.7" è

Il 9.7" SOLUM esiste in tre varianti, tutte 672 × 960 px a 121 dpi ma con film
e involucro diversi:

| Linea | Codice modello | Colori | Modulo ESL | Segni distintivi |
|---|---|---|---|---|
| Newton Pro gen. F5 | `EL097F5C4C/WWW` (cornice grigia), `EL097F5W4C/WWW` (bianca), `EL097F5B4C/WWW` (nera) | BWRY | 168,05 × 224,07 × 14,81 mm | nessun grado IP, **nessun pulsante**, LED 7 colori |
| Newton Pro gen. F6 | `EL097F6W4A/WWW` (bianca), `EL097F6B4A/WWW` (nera); cornice dichiarata anche grigia o personalizzata | BWRY | 170,2 × 223,6 × 14,85 mm | **IP68**, **1 pulsante**, LED 7 colori |
| Newton Core | modello TBD nel catalogo | **BWR** (3 colori); BW / BWR / BWRY nella variante "Core 4COLOR" | 167,65 × 220,04 × 13,80 mm | cornice solo bianca o nera, nessun grado IP, nessun pulsante |

Decodifica del codice: `EL` + `097` (pollici) + `F5`/`F6` (generazione) +
`C`/`W`/`B` (colore cornice) + `4C`/`4A` (revisione).

Il donor di questo progetto è un **Pro gen. F5**: cornice grigia, nessun
pulsante e nessun grado IP, cioè i due tratti che il F6 ha e il F5 no. La cornice
da sola non basta a distinguerli, perchè il catalogo la dichiara grigia su
entrambi, ma esclude il Core, offerto solo in bianco o nero. Nemmeno il LED a 7
colori discrimina: c'è su tutti e due i Pro.

Il codice modello **non garantisce il film a 4 colori**, perchè il set colori è
un'opzione di build dello stesso modulo: la pagina Newton Core 4COLOR 9.7"
dichiara `BW / BWR / BWRY` e la tabella di famiglia del Newton Pro (formati
1.6"–11.6") dichiara `BW / BWR / BWY`. Lo stesso 672 × 960 viene quindi prodotto
con due, uno o nessun colore d'accento, e solo una prova sul pannello dice quale
si ha in mano: vedi [§0.10](#010-il-4-colore-non-appare-questione-aperta).

### 0.2 Geometria e densità

| Grandezza | Valore |
|---|---|
| Risoluzione | 672 × 960 px (come da catalogo, orientamento portrait del modulo) |
| Area attiva | 141,12 × 201,6 mm |
| Pixel pitch | **0,210 mm esatti su entrambi gli assi** → pixel quadrati |
| Densità | 120,95 dpi (il catalogo arrotonda a 121 dpi) |
| Diagonale attiva | 246,08 mm = 9,69" → il "9.7" è la diagonale attiva, non il modulo |
| Aspect ratio | esattamente 7:10 |
| Conversioni | 4,7619 px/mm; 1 pt tipografico = 1,68 px (corpo 10 pt ≈ 17 px, 12 pt ≈ 20 px) |
| Curiosità utile | l'area attiva è quasi esattamente una pagina A5 (141 × 202 mm contro 148 × 210 mm) |

I pixel perfettamente quadrati sono il motivo per cui non serve nessuna
correzione di aspect ratio nè in `epd_image_converter.pyw` nè in `showImage`.

### 0.3 Source, gate e MUX

Il controller indirizza il pannello come **960 source × 672 gate**: nel driver
`WIDTH = 960` è l'asse X della RAM (comando `0x44`) e `HEIGHT = 672` l'asse Y
(comando `0x45`). Il modulo è montato in portrait — per questo il catalogo
riporta "672 × 960" — e con `setRotation(0)` il layer GFX lavora in landscape
960w × 672h.

**Il MUX era ereditato dall'upstream.** Fino alla correzione,
`_InitDisplay()` programmava il MUX (`0x01`) con `0xA7 0x02 0x00` = `0x02A7` =
679, cioè **680 gate lines**: il valore del pannello GDEM133Z91
(`HEIGHT = 680`), preso tale e quale. Questo pannello ne ha 672, e infatti
`_setPartialRamArea` imposta la finestra Y su `0..671`: il controller scandiva
8 gate lines inesistenti a ogni refresh — nessun artefatto visibile (sotto non
c'è film), ma ~1,2% del tempo di refresh, circa 260 ms sui 22 s.

Ora il MUX è `0x9F 0x02 0x00` = 671 → **672 gate lines**. L'immagine attesa è
identica (la finestra RAM era già `0..671`), il refresh dovrebbe accorciarsi di
quei ~260 ms; ridurre le gate line scandite accorcia anche il tempo di frame
della waveform, quindi lo scarto di impulso è dell'ordine dell'1% — sotto la
soglia percepibile, ma **la verifica su hardware non è ancora stata fatta**.
`full_refresh_time` resta a 22.000 ms: è il timeout di attesa BUSY, tenerlo
largo è corretto. Insieme al valore è stato corretto anche il commento
`// Set MUX as 527`, sbagliato e anch'esso ereditato dall'upstream.

### 0.4 Temperatura: range strettissimo

Range di esercizio dichiarato **0 ~ 40 °C** (32 ~ 104 °F). Non è un dettaglio
formale: la waveform è compensata in temperatura, `_InitDisplay()` seleziona il
**sensore interno** al controller (`0x18 = 0x80`) e il byte `0xF7` del comando
`0x22` include il passo "load temperature" a ogni refresh. Fuori range il
pannello non è specificato: sotto 0 °C sono attesi ghosting e refresh
incompleti. La temperatura di stoccaggio non è dichiarata in nessuno dei due
cataloghi.

L'FPC porta anche le linee **TSCL / TSDA**, quindi esiste la via hardware per un
sensore di temperatura I²C esterno (`0x18 = 0x48` su SSD1677) se servisse
compensare fuori dal range del sensore interno. Oggi non usata.

### 0.5 Vita attesa

Nell'ESL originale il pannello era alimentato da un pacco **CR2450 × 5** con
dichiarazione "up to 10 years @ **1 update/day**" (il Newton Core 9.7" dichiara
2 update/day). Il punto di progetto del prodotto è quindi nell'ordine di
**~3.650 refresh in tutta la vita**. È una dichiarazione sul budget energetico
delle batterie, non una specifica di endurance del film, ma l'intento di
progetto è chiaro: un pannello pensato per aggiornarsi una o due volte al
giorno. Da tenere presente prima di costruirci sopra qualcosa che faccia molti
refresh al giorno.

### 0.6 Interfaccia elettrica (dallo schematico Waveshare V3)

Connettore FPC 24 pin `J10`, pin ricavati dal netlist dello schematico:

| Pin | Segnale | Pin | Segnale |
|---|---|---|---|
| 4 | HLT_CTL | 12 | SDI (MOSI) |
| 5 | GDR | 13 | VDDIO |
| 6 | RESE | 14 | VCI |
| 7 | VGL | 15 | VDD |
| 8 | BS | 16 | **VPP** |
| 9 | RST | 17 | VSH |
| 10 | D/C | 18 | PREVGH |
| 11 | CS | 1–3, 19–24 | PREVGL, VCOM, VGH, TSCL, TSDA, GND / NC |

Tre conseguenze pratiche:

- il rail del pannello è **EPD_3.3V**, non i 5 V di ingresso della board: nè
  VCC nè le data line sono 5V-tolerant;
- i rail analogici VGH / VGL / VSH / PREVGH / PREVGL / VCOM sono generati dal
  charge pump **a bordo della board** (GDR / RESE più i condensatori da
  1 µF/50 V sui pin 21/23/24), non dal pannello: se un refresh esce sbiadito
  conviene guardare lì prima che nel codice;
- il pin 16 è **VPP**, la tensione di programmazione dell'OTP: il percorso per
  *scrivere* l'OTP è fisicamente presente sul connettore. Un motivo in più per
  limitarsi alla lettura (`0x2D`, Read Register for Display Option) se un giorno
  si volesse indagare quali waveform il pannello dichiara.

### 0.7 SPI e volumi di transfer

Lo sketch configura il bus con `SPISettings(10000000, MSBFIRST, SPI_MODE0)` su
HSPI. A 10 MHz il **limite fisico è 0,8 µs/byte** (8 bit / 10 MHz): tutte le
stime di tempo SPI in questo documento derivano da qui, non da misure a
oscilloscopio.

| Voce | Byte | Bulk (0,8 µs/B) | Per-byte upstream (~1,5 µs/B) |
|---|---|---|---|
| 1 piano full-screen (960 × 672 / 8) | 80.640 | ~65 ms | ~121 ms |
| 3 piani, `writeScreenBuffer()` | 241.920 | ~194 ms | ~363 ms |
| 1 page, 1 piano (960 × 84 / 8) | 10.080 | ~8 ms | ~15 ms |
| loop paged completo B+R (8 page × 2 piani) | 161.280 | ~129 ms | ~242 ms |
| refresh BWRY steady-state (paged B+R + piano giallo + cleanup `0x26`) | 322.560 | ~258 ms | ~484 ms |

Il template è istanziato come `GxEPD2_3C<Layout::Panel, Panel::HEIGHT / 8>` →
page height 84 righe, **8 page** per refresh, ~20 KB di RAM per i due piani
(contro i ~157 KB dei due piani full-screen non paginati: l'ESP32-WROOM-32
della board è senza PSRAM).

### 0.8 Tempi di refresh

| Costante | Valore | Nota |
|---|---|---|
| `full_refresh_time` | 22.000 ms | refresh elettroforetico completo |
| `partial_refresh_time` | 22.000 ms | identico: non esiste fast partial update |
| `power_on_time` / `power_off_time` | 100 / 250 ms | |
| `busy_timeout` | 25.000.000 µs (25 s) | passato a `GxEPD2_EPD`, 3 s di margine sul refresh |
| reset duration | 2 ms | `display.init(115200, true, 2, false)` nello sketch |

`hasFastPartialUpdate = false` con `partial_refresh_time == full_refresh_time`
non è una lacuna di questo driver: **nessuno** dei 36 pannelli a 3 e 4 colori
presenti in `epd3c/`, `epd4c/`, `gdem3c/`, `gdey3c/` di GxEPD2 ha il fast
partial. La ragione è strutturale: il partial veloce di SSD1677 è
**differenziale** e usa la RAM `0x26` come buffer "previous", mentre qui `0x26`
è il piano rosso — la RAM che servirebbe per il differenziale è occupata dal
colore. Il fratello monocromatico con lo stesso controller
([`GxEPD2_1330_GDEM133T91`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdem/GxEPD2_1330_GDEM133T91.h),
960 × 680) fa 4.500 ms full e **600 ms** partial proprio perchè quella RAM è
libera.

Corollario: il tempo sta nella waveform, non nei dati. **Non inviare i piani
accent non accorcia il refresh**, risparmia solo i ~130 ms di SPI della tabella
in §0.7 — che il dirty-tracking di §5 già incassa.

### 0.9 Cosa i cataloghi SOLUM non dicono

`Newton-Pro_Specifications.pdf` e `Newton-Core_Specifications.pdf` sono
cataloghi di prodotto ESL, non datasheet di pannello: non contengono tempo di
refresh, waveform o LUT, VCOM, timing SPI, controller, pinout FPC nè tensioni di
driving. Ricerche esplicite su `refresh`, `update time`, `grayscale`, `VCOM`,
`mux`, `storage temp`, `cycle` non danno alcun risultato. Tutto il lato
elettrico e di timing viene quindi dal datasheet SSD1677 di Solomon Systech più
le verifiche su hardware.

E il datasheet SSD1677 va preso con le pinze proprio sui piani colore: **il
codice `0x28` non è documentato come piano accent**, risulta come VCOM Sense,
comando senza parametri. Il datasheet Solomon non è comunque autorevole su questo
silicio, che può essere una variante custom con OTP proprio: non dice quante
DISPLAY Mode l'OTP contenga nè come sarebbe strutturata una LUT a 4 colori. Vedi
[§0.10](#010-il-4-colore-non-appare-questione-aperta).

### 0.10 Il 4° colore non appare: questione aperta

Sul pannello non compare nessun pixel giallo, nè dai descrittori BWRY di
`showImage` nè da `writeImageYellow` chiamata a mano. Non compaiono nemmeno
artefatti: il resto del frame è corretto, bianco nero e rosso inclusi.

Che `0x28` sia il piano giallo è un **assunto**, non un fatto verificato:

- nel datasheet pubblico SSD1677 `0x28` è VCOM Sense, un comando senza
  parametri: i byte che seguono con D/C alto vengono scartati;
- **nessun driver di GxEPD2 1.6.9 usa `0x28` come piano immagine**; gli unici
  match del codice in `src/` sono dati di bitmap.

L'ipotesi alternativa più solida è che il 4° colore stia nella **combinazione a
2 bit dei due piani già esistenti**. Quello che la catena produce oggi, dato che
`drawPixel` di `GxEPD2_3C` scrive bit=1 = "non questo colore" e il driver manda
`0x24` senza invert e `0x26` con invert:

| colore | `0x24` | `0x26` |
|---|---|---|
| bianco | 1 | 0 |
| nero | 0 | 0 |
| rosso | 1 | 1 |
| — | 0 | 1 |

La quarta combinazione **non viene mai generata**. Su silicio BWR il rosso vince
a prescindere dal bit BW, quindi lì sarebbe rosso; su una variante a 4 colori è
invece il code point naturale del giallo.

Il controller non è interrogabile: sul FPC a 24 pin non esiste una linea SDO, il
pin 12 è SDI e basta (vedi
[§0.6](#06-interfaccia-elettrica-dallo-schematico-waveshare-v3)), quindi `0x27`
read RAM, `0x2E` User ID read e `0x2F` status sono inutilizzabili. L'unica
diagnostica possibile è scrivere un pattern e guardare il pannello: lo fa
[`examples/color_test`](examples/color_test/color_test.ino), che vive in questa
libreria perchè serve a costruire il driver, non il progetto consumer. Stampa in
un solo refresh le quattro combinazioni della tabella su bande orizzontali
numerate, più una fascia di controllo con `0x28 = 0xFF`, poi ripete il refresh in
DISPLAY Mode 2 e chiude sul seriale con una scheda di osservazione che mappa ogni
esito visibile sulla conseguenza per il driver.

Finchè la questione è aperta, quello che questo README dice sul piano `0x28`
descrive **come il driver è scritto**, non un comportamento osservato.

---

## Origine

Il driver è **header-only** (`inline` nell'`.h`, nessuna `.cpp`) e nasce
come fork del driver upstream
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)
(pannello Good Display 13.3" 3-colori). Eredita da `GxEPD2_EPD` (classe
base della libreria) e implementa la sequenza di comandi specifica del
pannello SOLUM 9.7" su controller SSD1677:

- soft-reset (`0x12`)
- soft-start (`0x0C`)
- MUX per 672 gate lines (`0x01 = 0x9F 0x02 0x00`) — corretto rispetto alle
  680 dell'upstream, vedi [§0.3](#03-source-gate-e-mux)
- bordo bianco (`0x3C = 0x01`)
- sensore di temperatura interno (`0x18 = 0x80`)
- entry-mode x/y increase (`0x11 = 0x03`)
- finestra RAM full-window (`0x44` / `0x45` / `0x4E` / `0x4F`)
- full-window refresh (`0x22 = 0xF7` + `0x20`)
- power-off (`0x22 = 0xC3`)

Rispetto ai driver stock di GxEPD2 per pannelli simili, questa versione
introduce:

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
`GxEPD2_3C`, dopo `fillScreen()` e prima di `nextPage()`. Supporta tutti
e 3 i formati del descrittore (BW / BWR / BWRY) — gestisce internamente
il yellow out-of-band per il caso BWRY.

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
GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, 960, 672));
```

Il chiamante è responsabile di: aprire il loop paged e chiamare
`hibernate()` se vuole spegnere il pannello. Il reset di
`preserveYellow(false)` per il caso BWRY è **gestito automaticamente**
dentro `refresh()` del driver al termine del loop paged.

**Idempotency canale yellow.** Per i descrittori BWRY, `showImage`
scrive il canale 0x28 al MASSIMO una volta per loop paged. La guardia è
`showImagePageHint() == 0`, cioè la prima iterazione; le 2..8 saltano la
chiamata `writeImageYellow`. Una scrittura full-screen del piano giallo sono
80.640 byte ≈ 65 ms a 10 MHz (vedi [§0.7](#07-spi-e-volumi-di-transfer)):
le 7 riscritture evitate valgono **~450 ms per refresh**.

Un pre-write out-of-band del chiamante su 0x28 **non** sopprime la scrittura
di `showImage`: `writeImageYellow` imposta la propria finestra RAM e tocca solo
quella, quindi un overlay giallo dello sketch e il giallo dell'immagine
convivono se le aree sono disgiunte. Per far vincere solo il giallo custom si
passa a `showImage` un descrittore BWR.

**Multi-call per page.** `showImage` può essere chiamata 0, 1 o N volte
all'interno di una stessa iterazione del paged loop senza problemi: il
page-tracking interno avanza il counter solo quando il template chiude
la page (`writeImage(black, color)` chiamato da `nextPage()`), non a
ogni chiamata `showImage`. Utile per **compositing multi-immagine**:
sovrapporre più descrittori con offset `(x, y)` diversi, ognuno
disegnato correttamente nella sola porzione che intersecta la page
corrente. Vedi §5 "Loop pixel di `showImage` row-skip" per i dettagli
del meccanismo.

### Casi d'uso e firme

| # | Caso d'uso | Firma array (in `.h` incluso) | Firma chiamata |
|---|---|---|---|
| 1 | Bitmap **B/N raw** (image2cpp) | `const unsigned char img_xxx[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BW_IMAGE(img_xxx, w, h));` |
| 2 | **BWR** da `epd_image_converter.pyw` | `const GxEPDImage::Descriptor img_xxx_desc;` *(auto-generato)* | `GxEPDImage::showImage(display, img_xxx_desc);` |
| 3 | **BWRY** da `epd_image_converter.pyw` | `const GxEPDImage::Descriptor img_xxx_desc;` *(auto-generato)* | `GxEPDImage::showImage(display, img_xxx_desc);` |
| 4 | **BWR raw inline** (2 piani separati) | `const unsigned char img_b[], img_r[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BWR_IMAGE(img_b, img_r, w, h));` |
| 5 | **BWRY raw inline** (3 piani separati) | `const unsigned char img_b[], img_r[], img_y[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BWRY_IMAGE(img_b, img_r, img_y, w, h));` |
| 6 | **BWRY con yellow pre-iniettato** (compositing yellow custom) | `const unsigned char img_b[], img_r[], img_y[] PROGMEM = { … };` | `display.epd2.writeImageYellow(custom_y, x, y, w, h, pgm);` + `display.epd2.preserveYellow(true);` + `GxEPDImage::showImage(display, GXEPD_BWR_IMAGE(img_b, img_r, w, h));` |
| 7 | **Single-channel diretto** (no GFX) | `const unsigned char img_b[], img_r[], img_y[] PROGMEM = { … };` | `display.epd2.writeImageBlack(img_b, x, y, w, h, true);` + `…Red(…)` + `…Yellow(…)` + `display.epd2.refresh(false);` |

Casi **1–6** vanno chiamati dentro un loop `firstPage()` / `nextPage()`
con `fillScreen()` prima, e seguiti da `display.hibernate()` se si vuole
spegnere il pannello. Il reset del flag `preserveYellow(false)` per il
caso BWRY è gestito automaticamente dentro `refresh()` del driver, non
serve farlo a mano.

- Per i casi **3** e **5**, `showImage` scrive il canale 0x28 una sola volta
  per refresh grazie all'idempotency-check sul page-hint (le iterazioni 2..8
  del paged loop saltano la riscrittura).
- Nel caso **6** il descrittore è BWR, quindi `data2` è nullo e `showImage`
  lascia intatto il giallo custom. Con un descrittore BWRY scriverebbe anche
  il proprio `data2`: le due scritture convivono se le aree sono disgiunte,
  si sovrappongono altrimenti.

Il caso **7** bypassa il template GFX e si chiama standalone (incluso
`refresh()` esplicito).

## 2. Tre API siblings single-channel uniformi

I 3 canali del controller SSD1677 sono esposti con shape identica per
scritture single-channel (no refresh):

```cpp
void writeImageBlack (const uint8_t* bitmap, int16_t x, int16_t y,
                      int16_t w, int16_t h, bool pgm = true);  // cmd 0x24
void writeImageRed   (const uint8_t* bitmap, int16_t x, int16_t y,
                      int16_t w, int16_t h, bool pgm = true);  // cmd 0x26
void writeImageYellow(const uint8_t* bitmap, int16_t x, int16_t y,
                      int16_t w, int16_t h, bool pgm = true);  // cmd 0x28
```

Convenzione bitmap input: bit=1 dove il pixel **non** appartiene a quel
canale (formato compatibile con lo script Python e image2cpp). Per gli
accent (red/yellow) il driver applica `~data` prima del transfer SPI
per allinearsi alla polarity nativa SSD1677 (bit=1 in RAM = colorante
acceso).

## 3. Perchè il yellow è "out-of-band" nel flusso paged

Il driver custom origina da `GxEPD2_1330c_GDEM133Z91`, un driver del
ramo **`gdem3c`** di GxEPD2, pensato per pannelli a **3 colori**
(bianco/nero/+1 accent). Tutta l'organizzazione del rendering di GxEPD2
ruota attorno al template `GxEPD2_3C<Driver, page_height>`, che fa da
intermediario tra il layer GFX (Adafruit_GFX) e il driver. Questo
template ha un'architettura **hard-coded su 2 canali**:

- mantiene un buffer GFX paged in RAM con **due piani** (black + accent)
- nel loop `firstPage()` / `nextPage()` invoca **una sola hook** sul
  driver: `writeImage(black, color, ...)` in modalità full-window
  ([GxEPD2_3C.h:368](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/GxEPD2_3C.h#L368)); la variante
  `writeImagePart(black, color, ...)` è usata solo con `setPartialWindow`
  ([GxEPD2_3C.h:273](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/GxEPD2_3C.h#L273))
- non ha nè campi nè API per un terzo canale

Quando abbiamo confermato che il pannello SOLUM supporta nativamente un
quarto colore (giallo via comando `0x28`), l'opzione "pulita" sarebbe
stata scrivere un template `GxEPD2_4C` custom — fork con buffer paged a
3 piani e nuova hook `writeImagePart(black, red, yellow, ...)`. Sarebbe
stato un refactor invasivo della libreria upstream, fuori scope.

La soluzione pragmatica adottata è il pattern **"yellow out-of-band"**:

```
┌────────────────────────────────────────────────────────────────┐
│  writeImageYellow(buffer_giallo, ...)    ← scrive 0x28 PRIMA   │
│  preserveYellow(true)                    ← protegge 0x28       │
│                                                                │
│  display.firstPage();                                          │
│  do {                                                          │
│    fillScreen(WHITE);                                          │
│    drawBitmap(...);    // GFX nel buffer paged 2-canali (B+R)  │
│    drawText(...);      // GFX                                  │
│    ...                                                         │
│  } while (display.nextPage());                                 │
│  ↓                                                             │
│  ogni nextPage() chiama writeImage(black, color, ...)          │
│  che internamente NON tocca 0x28 finchè _preserve_yellow=true  │
│                                                                │
│  preserveYellow(false)                   ← ripristina cleanup  │
└────────────────────────────────────────────────────────────────┘
```

Il giallo viene **iniettato manualmente** prima del loop paged e
**protetto** dal cleanup automatico tramite il flag `_preserve_yellow`.
Il flag viene **resettato automaticamente** dentro `refresh()` del
driver, chiamato dal template alla fine del loop paged: il chiamante
non deve preoccuparsene.

Tutta questa complessità (writeImageYellow + preserveYellow + decodifica
bitmap pixel-per-pixel + auto-reset) è incapsulata nella free function
template `GxEPDImage::showImage(display, desc)` (vedi §1): il chiamante
deve solo aprire il loop paged.

**Idempotency.** `showImage` è idempotente sul canale 0x28: scrive
`writeImageYellow` al massimo una volta per loop paged grazie al check
`showImagePageHint() == 0`. Le iterazioni 2..8 di `nextPage()` trovano il
page-hint avanzato e saltano la riscrittura: ~450 ms risparmiati per refresh
(7 × 80.640 byte a 0,8 µs/byte, vedi [§0.7](#07-spi-e-volumi-di-transfer)).
Un yellow custom scritto dal chiamante prima di `firstPage()` sopravvive
comunque, perchè `writeImageYellow` scrive solo dentro la finestra RAM che
imposta (vedi caso 6 della tabella in §1).

I 3 siblings `writeImageBlack` / `writeImageRed` / `writeImageYellow`
sono **simmetrici a livello di API**, ma in pratica B+R sono scritti
dal template e Y è scritto manualmente — l'asimmetria viene dal
template GxEPD2_3C, non dal driver.

### Pitfall: `drawPixel(x, y, GxEPD_YELLOW)` non scrive sul piano giallo

Conseguenza diretta dell'architettura 2-canali del template upstream: nel
sorgente di `GxEPD2_3C.h` la funzione `drawPixel` ha questa condizione:

```cpp
else if ((color == GxEPD_RED) || (color == GxEPD_YELLOW))
  _color_buffer[i] = (_color_buffer[i] & ...);   // scrive nel piano red (0x26)
```

`GxEPD_YELLOW` viene **trattato come `GxEPD_RED`** — la libreria non
distingue. Il pixel "giallo" finisce sul piano rosso del controller
(`0x26`) e MAI sul piano giallo (`0x28`). Questo è invisibile su pannelli
3-colori (B+W+R), mentre su un pannello a 4 colori si tradurrebbe in un
pixel rosso al posto del giallo atteso.

Per scrivere il piano `0x28` ci sono due strade, entrambe già
incapsulate nel driver custom — con l'avvertenza di
[§0.10](#010-il-4-colore-non-appare-questione-aperta), che sul pannello
quelle scritture non producono giallo:

1. **`GxEPDImage::showImage(display, descriptor)`** con descrittore
   `FORMAT_BWRY_1BPP` — il giallo viene iniettato direttamente sul
   controller via `writeImageYellow()` e protetto durante il loop paged.
   Strada preferita per immagini pre-encoded.
2. **`writeImageYellow()` + `preserveYellow(true)`** chiamati a mano
   prima di `firstPage()`, per compositing custom (es. la barra
   gialla temp-range del banner Weather, vedi
   [`Weather.h` di ePaper-weather-dashboard](https://github.com/alesimattia/ePaper-weather-dashboard/blob/main/Weather.h)). Il driver auto-resetta il flag dentro
   `refresh()` al termine del loop paged.

## 4. Sistema di descrittori universali (`namespace GxEPDImage`)

```cpp
namespace GxEPDImage {
  enum Format : uint8_t {
    FORMAT_BW_1BPP   = 0,   // 1 buffer 1bpp (compat image2cpp)
    FORMAT_BWR_1BPP  = 1,   // buffer separati black + red
    FORMAT_BWRY_1BPP = 2,   // buffer separati black + red + yellow
  };

  struct Descriptor {
    Format format;
    uint16_t width, height;
    const uint8_t *data0, *data1, *data2;
  };
}
```

Il descrittore porta con sè formato e dimensioni. Per costruire
descrittori inline lo header espone tre macro di comodo:

```cpp
GXEPD_BW_IMAGE(ptr, w, h)
GXEPD_BWR_IMAGE(black, red, w, h)
GXEPD_BWRY_IMAGE(black, red, yellow, w, h)
```

Lo script Python `epd_image_converter.pyw` genera automaticamente una
variabile `img_<nome>_desc` ad ogni conversione, pronta per essere
passata a `showImage()`.

## 5. Ottimizzazioni rispetto al driver stock

### Confronto driver custom vs. base GDEM133Z91 e patterns moderni di GxEPD2

La tabella sotto mette in evidenza ogni divergenza dal driver upstream
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)
(che è il driver SSD1677 più vicino al pannello SOLUM) e dai patterns
adottati negli altri driver moderni della libreria.

| Aspetto | GDEM133Z91 base | Driver custom SOLUM | Stato |
|---|---|---|---|
| Init RAM 3 piani (B/R/Y) | solo B+R (0x24, 0x26) | B+R+Y (0x24, 0x26, 0x28) | ≈ effetto di 0x28 da confermare, vedi [§0.10](#010-il-4-colore-non-appare-questione-aperta) |
| `delay(1)` ESP8266 WDT in hot path | presente in `_writeImage`/`_writeImagePart` | rimosso (target ESP32, task WDT 5 s) | ✓ migliore |
| Delay dopo SWRESET | 10 ms (sotto-stimato) | 200 ms (datasheet SSD1677 ~100-300 ms) | ✓ migliore |
| Entry-mode `0x11` | inviato a ogni `_setPartialRamArea` | inviato 1 volta in `_InitDisplay` | ✓ migliore |
| Cleanup accent dirty-tracking | n/a (cleanup sempre o mai) | flag `_color_dirty` / `_yellow_dirty` + `_cleanAccentIfDirty` | ✓ ottimizzato |
| Polarity cleanup accent | n/a | `0x00` esplicito (= accent spento, polarity nativa) | ✓ corretto vs. bug latente `0xFF` |
| `hibernate()` guard idempotente | scrive 0x10 sempre, anche su re-entry | early return se già `_hibernating` | ✓ migliore |
| `_init_display_done` reset on hibernate | sì | sì | = parità |
| 4° canale (yellow `0x28`) | non gestito | API single-channel + `preserveYellow` | ≈ API presente, sul pannello non produce giallo, vedi [§0.10](#010-il-4-colore-non-appare-questione-aperta) |
| Cleanup accent in `writeImage(bitmap, ...)` BW | non fa | sì (entrambi 0x26 e 0x28 dirty-checked) | ✓ migliore |
| Cleanup accent in `writeImagePart(bitmap, ...)` BW | non fa | sì (allineato a `writeImage` per simmetria) | ✓ migliore |
| Reset `_preserve_yellow` post-refresh | n/a | centralizzato in `_Update_Full()` | ✓ corretto |
| Reset `_preserve_yellow` in `hibernate()` | n/a | sì (simmetria con refresh) | ✓ corretto |
| Loop pixel di `showImage` | itera tutti i pixel × tutte le 8 page (drawPixel early-return) | row-skip rotation-aware via page-hint counter (1/8 delle iterazioni) | ✓ ottimizzato (~170 ms/refresh) |
| Transfer SPI verso il controller | per-byte `_pSPIx->transfer(uint8_t)` (~1.5 μs/byte) | row-buffered via `_pSPIx->writeBytes(buf, n)` (FIFO 64-byte ESP32, ~0.8 μs/byte = limite del clock a 10 MHz) | ✓ ottimizzato (~225 ms/refresh) |
| MUX gate lines (`0x01`) | 680, corretto per GDEM133Z91 (`HEIGHT = 680`) | **672** (`0x9F 0x02 0x00`), quante il pannello SOLUM ne ha davvero | ✓ corretto, ~260 ms/refresh — da verificare su hardware, vedi [§0.3](#03-source-gate-e-mux) |

### Dettaglio delle ottimizzazioni

- **`_setPartialRamArea()`** non riscrive più l'entry-mode ad ogni draw
  (spostato una tantum in `_InitDisplay`).
- **Dirty-flag** `_color_dirty` / `_yellow_dirty`: il cleanup di 0x26 e
  0x28 in `writeImage(bw)` viene saltato se i flag sono zero, evitando
  ~130 ms di SPI per draw quando si concatenano frame B/N (2 piani ×
  80.640 byte, vedi [§0.7](#07-spi-e-volumi-di-transfer)).
- **Helper `_cleanAccentIfDirty(cmd, flag)`** centralizza la pulizia
  scrivendo `0x00` (polarity nativa SSD1677 = accent spento), corretto
  rispetto al precedente `0xFF` che scriveva "accent ON ovunque" — bug
  latente mascherato da hibernate+SWRESET ad ogni wake.
- **`hibernate()`** protetto contro chiamate multiple (early return se
  `_hibernating == true`); resetta i dirty-flag e `_preserve_yellow`
  perchè il SWRESET successivo riporta la RAM controller a stato noto.
- **`writeImagePart(bitmap, ...)` BW** allineato a `writeImage(bitmap, ...)`:
  pulisce gli accent dirty prima di scrivere il piano `0x24`, evitando
  che red/yellow residui di un draw colorato precedente trasparissero
  sotto la zona BW.
- **Reset `_preserve_yellow`** centralizzato in `_Update_Full()` (chiamato
  da entrambi gli overload di `refresh()`) invece di duplicarlo. Auto-reset
  garantito al termine di ogni ciclo di rendering.
- **`delay(1)` rimossi** da `_writeImage` / `_writeImagePart` (servivano
  come yield WDT per ESP8266; ESP32 ha task WDT 5 s, ampio margine). In un
  refresh paged full-window `_writeImage` è chiamato 2 volte per page (piani
  `0x24` e `0x26`) × 8 page: **~16 ms** risparmiati, un tick FreeRTOS per
  chiamata, più i tick delle eventuali `_writeScreenBuffer`.
- **`GxEPDImage::showImage` row-skip**: il loop pixel skippa a priori le
  righe sorgente fuori dalla page corrente del template `GxEPD2_3C`,
  riducendo le iterazioni di un fattore ~8: 7 passate da ~24 ms evitate su
  8, **~170 ms** risparmiati per refresh full-screen. Il template upstream tiene `_current_page`
  privato senza getter pubblico, quindi il driver custom mantiene un
  counter `_show_image_page_hint` parallelo: reset a 0 dentro
  `setPaged()` (override del hook `virtual` di `GxEPD2_EPD` chiamato da
  `firstPage()` del template), avanzato dentro `writeImage(black, color, ...)`
  (invocato dal template ESATTAMENTE una volta per page in `nextPage()`),
  reset difensivo dentro `_Update_Full()`. Bonus: `showImage` può essere
  chiamata 0, 1 o N volte all'interno della stessa page senza
  desincronizzare il counter (il counter avanza solo a chiusura page,
  non per ogni chiamata `showImage`). Include anche hoist del row offset
  fuori dal loop interno e fallback al loop completo per rotation 1/3
  (90°, dove la skip-by-row non si applica perchè le righe sorgente
  mappano sull'asse x dell'output).
- **Bulk SPI transfer**: i tre hot path SPI (`_writeImage`,
  `_writeImagePart`, `_writeScreenBuffer`) usano `_pSPIx->writeBytes(buf, n)`
  invece di `_transfer(uint8_t)` per-byte. Il base class `GxEPD2_EPD`
  espone `_pSPIx` come `protected`, quindi il subclass può chiamarlo
  direttamente. Il loop interno popola un buffer di stack
  (120 byte/riga per le immagini, 256 byte di chunk per la scrittura
  costante) e lo scarica con `writeBytes`, che usa la FIFO 64-byte
  dell'ESP32 e arriva al limite del clock SPI
  (~0.8 μs/byte a 10 MHz, contro ~1.5 μs/byte della transfer per-byte upstream
  — `_writeData(buf, n)` di `GxEPD2_EPD` è anch'esso un loop per-byte di
  `transfer()`, NON un bulk vero, vedi
  [GxEPD2_EPD.cpp:197-207](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/GxEPD2_EPD.cpp#L197-L207)).
  Su un refresh BWRY steady-state (322.560 byte: paged B+R + piano giallo +
  cleanup `0x26`) il transfer SPI passa da ~484 ms a ~258 ms:
  **risparmio ~225 ms per refresh** (derivazione in
  [§0.7](#07-spi-e-volumi-di-transfer)). Stack temporaneo aggiunto:
  ~120 byte per `_writeImage`/`_writeImagePart`, ~256 byte per
  `_writeScreenBuffer`. API pubblica invariata, ottimizzazione
  trasparente.

## 6. API completa

La lista degli overload `drawImage*` / `writeImage*` / `writeImagePart*`
ereditati dalla base class è in
[drawImage_overloads_it.md](drawImage_overloads_it.md) (o la versione
inglese [drawImage_overloads.md](drawImage_overloads.md)).
Sono override di virtual del base class `GxEPD2_EPD` necessari al
contratto della libreria — non sono pensati per uso diretto: lo sketch
chiama `showImage()` per immagini singole, oppure il template
`GxEPD2_3C` invoca `writeImage(black, color)` a ogni `nextPage()` nel flusso
paged full-window (`writeImagePart` solo con `setPartialWindow`).

---

## Licenza

**GPL-3.0**: il driver nasce come copia modificata di
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)

Questo repo è un **fork** di [ZinggJM/GxEPD2](https://github.com/ZinggJM/GxEPD2) ma non ne duplica l'albero: contiene solo questa libreria. 
I riferimenti ai sorgenti upstream in questa doc puntano al **tag 1.6.9**, la versione su cui il driver è stato scritto e verificato, non a `master`: così le righe citate restano valide anche quando upstream avanza.
