# API del driver `GxEPD2_SOLUM_097c_960x672`

Riferimento completo dei metodi pubblici del driver custom
[GxEPD2_SOLUM_097c_960x672.h](src/GxEPD2_SOLUM_097c_960x672.h) per il
pannello SOLUM 9.7" (672w × 960h native portrait, controller SSD1677, 3 colori: bianco, nero e rosso).

## API custom (livello applicativo)

### `GxEPDImage::showImage()` — entry-point unico

Free function template nel namespace `GxEPDImage` (vive in
[`src/GxEPDImage.h`](src/GxEPDImage.h), non come metodo classe). È
**l'unica funzione pubblica per stampare un'immagine** sui pannelli SOLUM,
riusabile da qualsiasi sketch:

```cpp
template<typename DisplayT>
void GxEPDImage::showImage(DisplayT& display,
                           const GxEPDImage::Descriptor& d,
                           int16_t x = 0, int16_t y = 0,
                           GxEPDImage::Paint paint = GxEPDImage::Paint::Opaque);
```

Va chiamata **dentro** un loop `firstPage()`/`nextPage()` del template
`GxEPD2_3C`, dopo `fillScreen()` e prima di `nextPage()`. Supporta tutti
e 3 i formati (BW / BWR / BWRY) e rende i piani black e red: `data2` di un
descrittore a tre piani non viene toccato.

È un wrapper di `GxEPD2_3C::drawInvertedBitmap()`: il piano rosso si disegna per
primo e il nero sopra, così dove entrambi sono accesi vince il nero. Le righe
sopra la pagina corrente si saltano con il page-hint del driver.
`Paint::Opaque` (default) fa precedere ai due piani un `fillRect()` bianco sulle
stesse righe, `Paint::InkOnly` dipinge solo l'inchiostro nero e rosso. In
nessuno dei due casi resta ghosting del frame precedente: `GxEPD2_3C` sbianca il
buffer a ogni pagina e manda al controller i due piani interi, quindi un bianco
non dipinto non lascia traccia, e `Opaque` serve solo a coprire ciò che la
stessa pagina ha già disegnato sotto l'immagine. Al display servono
`pageHeight()`, `getRotation()`, `height()`, `fillRect()` e
`drawInvertedBitmap()`; al driver `setPaged()` e `showImagePageHint()`.

**Responsabilità del chiamante:**
- Aprire il loop paged (`firstPage()` + `do { ... } while (nextPage())`)
- Chiamare `display.hibernate()` se vuole spegnere il pannello

Esempio one-shot full-screen:

```cpp
display.firstPage();
do {
  display.fillScreen(GxEPD_WHITE);
  GxEPDImage::showImage(display, *desc_ptr);
} while (display.nextPage());
display.hibernate();
```

Per bitmap raw image2cpp B/N usare la macro inline:

```cpp
GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, 960, 672));
```

### `writeImageBlack` / `writeImageRed` e le API opt-in del partial: siblings single-channel

Non stanno nel driver: sono nella sottoclasse `GxEPD2_SOLUM_097c_PartialProbe` di
[`examples/097c/panel_diagnostic/PartialProbeDriver.h`](examples/097c/panel_diagnostic/PartialProbeDriver.h),
con le firme qui sotto. Chi le vuole usare costruisce il display con quella
classe al posto del driver, perchè `GxEPD2_3C` va istanziato con il tipo della
sottoclasse:

```cpp
GxEPD2_3C<GxEPD2_SOLUM_097c_PartialProbe, GxEPD2_SOLUM_097c_PartialProbe::HEIGHT / 8>
    display(GxEPD2_SOLUM_097c_PartialProbe(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }));
```

Il firmware non le chiama e usa solo il refresh differenziale dentro il loop
paged. I punti aperti di queste API, e di due sequenze del driver da misurare sul
vetro, sono in `ISSUES.md` del repo padre (ePaper-weather-dashboard), che include
questa libreria come submodule.

`writeImageBlack` e `writeImageRed` scrivono un singolo piano del controller,
**senza refresh**, per il compositing manuale:

```cpp
void writeImageBlack(const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x24
void writeImageRed  (const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x26
```

Convenzione bitmap input: `bit=1` dove il pixel **NON** appartiene a quel
canale (formato compatibile con script Python e image2cpp). La sottoclasse
applica `~data` prima del transfer SPI per l'accent, per allinearsi alla polarity
nativa SSD1677 (`bit=1` in RAM = colorante ON).

Nessuna delle due chiama `refresh()`: il chiamante è responsabile di farlo.

Il partial vero e proprio (640 ms, solo bianco e nero) ha le sue porte. Sotto la
sua LUT `0x26` non è l'accent ma il frame precedente in polarità B/N, quindi un
frame aggiornato in partial è senza rosso:

```cpp
void writeScreenBufferPrevious(uint8_t value = 0xFF);
void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y,
                          int16_t w, int16_t h,
                          bool invert = false, bool mirror_y = false, bool pgm = false);
void refreshPartial();
void refreshPartial(int16_t x, int16_t y, int16_t w, int16_t h);  // coordinate dichiarative
void drawImagePartial(const uint8_t* bitmap, int16_t x, int16_t y,
                      int16_t w, int16_t h, bool invert = false, bool pgm = true);
void drawImagePartialPart(const uint8_t* bitmap,
                          int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                          bool invert = false, bool pgm = true);
void setPartialLut(const uint8_t* lut);  // 110 byte, layout del waveform setting
```

`x` e `w` di `drawImagePartial*()` vanno multipli di 8. La ricetta per scrivere
testo e forme con una `GFXcanvas1` e `drawImagePartial(..., invert = true,
pgm = false)` è nel [README §3.1](README.md#31-il-partial-in-bianco-e-nero-640-ms-e-come-ci-si-scrive-dentro).

### `hibernateDeep()`

```cpp
void hibernateDeep();
```

Come `hibernate()`, ma forza il deep sleep anche quando `hibernate()` lo evita per
tenere il riferimento del differenziale. Butta il riferimento e perde le RAM del
controller, quindi il frame dopo è un refresh pieno. Serve a chi sa che il
pannello resterà fermo a lungo, e come `hibernate()` il deep sleep c'è solo con
RST cablato.

### Guasti del BUSY: `busyFault()` e `lastUpdateMs()`

```cpp
GxEPD2_SOLUM_BusyFault busyFault() const;   // None, TooShort, Timeout
uint32_t lastUpdateMs() const;
```

Ogni attesa del BUSY del driver ne controlla l'esito. `Timeout` vuol dire BUSY
ancora alto allo scadere dell'attesa: `BUSY_TIMEOUT_US` (120 s) per i refresh,
`SHORT_BUSY_TIMEOUT_US` (5 s) per le attese brevi (power on e off, SWRESET,
pattern hardware). `TooShort` vuol dire che un refresh è finito prima della
durata minima plausibile, `MIN_PLAUSIBLE_FULL_REFRESH_MS` (5000 ms) o
`MIN_PLAUSIBLE_PARTIAL_REFRESH_MS` (300 ms), quindi il controller non ha
eseguito la waveform.

Un guasto alza un latch che salta le attese seguenti e il comando `0x20`, e
`refresh(false)` non convalida nè il frame mostrato nè il riferimento del
differenziale. Il latch si azzera in `setPaged()` e all'inizio delle API
pubbliche che scrivono e rinfrescano fuori dal loop paged (`clearScreen()`,
`drawImage*()`, `drawNative()`, `refresh(x, y, w, h)`). `lastUpdateMs()` è la
durata in ms dell'ultimo refresh, dall'invio di `0x20` al BUSY basso, e 0 se non
è partito. Il driver non scrive mai su `Serial`.

---

## API ereditate da `GxEPD2_EPD` (override virtual)

Mantenute per compatibilità con il template `GxEPD2_3C` e il contratto
della libreria upstream. **Non sono pensate per uso diretto dallo
sketch**: lo sketch usa `showImage()` o lascia che il template invochi
internamente questi metodi durante il flusso paged.

### Bitmap (write senza refresh; coordinate `x` e `w` devono essere multipli di 8)

```cpp
void writeImage(const uint8_t bitmap[],
                int16_t x, int16_t y, int16_t w, int16_t h,
                bool invert = false, bool mirror_y = false, bool pgm = false);

void writeImage(const uint8_t* black, const uint8_t* color,
                int16_t x, int16_t y, int16_t w, int16_t h,
                bool invert = false, bool mirror_y = false, bool pgm = false);
```

### Bitmap part (window di una bitmap più grande)

```cpp
void writeImagePart(const uint8_t bitmap[],
                    int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert = false, bool mirror_y = false, bool pgm = false);

void writeImagePart(const uint8_t* black, const uint8_t* color,
                    int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert = false, bool mirror_y = false, bool pgm = false);
```

`writeImage(black, color, ...)` è il **HOT PATH** invocato 8 volte
per refresh dal template `GxEPD2_3C` durante `nextPage()` (8 = numero
di pagine, dato dal template `GxEPD2_3C<Driver, HEIGHT/8>`).
`writeImagePart(black, color, ...)` è la scrittura d'area che chiama
`GxEPD2_3C::displayWindow()`, una volta.

### Native (sprite raw del controller)

```cpp
void writeNative(const uint8_t* data1, const uint8_t* data2,
                 int16_t x, int16_t y, int16_t w, int16_t h,
                 bool invert = false, bool mirror_y = false, bool pgm = false);
```

### Draw (write + refresh full window)

Stesse signature dei `writeImage*`/`writeImagePart*`/`writeNative` ma
con refresh finale:

```cpp
void drawImage(const uint8_t bitmap[], ...);
void drawImage(const uint8_t* black, const uint8_t* color, ...);
void drawImagePart(const uint8_t bitmap[], ...);
void drawImagePart(const uint8_t* black, const uint8_t* color, ...);
void drawNative(const uint8_t* data1, const uint8_t* data2, ...);
```

### Refresh / power management

```cpp
void refresh(bool partial_update_mode = false);
void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
void powerOff();
void hibernate();
```

`refresh(bool)` è il punto in cui un frame del loop paged diventa partial,
refresh pieno o niente. Di default esegue un full-window refresh (~24 s), ed è
ciò che serve al template con un frame **a colori**: un partial in bianco e nero
gli farebbe perdere il rosso. Con il refresh differenziale attivo
(`setDifferentialRefresh(true)`) un frame armato, il cui piano rosso è invariato,
lancia invece il partial in bianco e nero (~0,64 s); con `setSkipUnchanged(true)`
un frame identico a quello sul vetro non lancia niente e `frameSkipped()` vale
`true`. `refresh(x, y, w, h)` ignora le coordinate e fa sempre un full-window
refresh fuori dal loop paged. Le API opt-in del partial (`drawImagePartial()` e
sorelle) stanno nella sottoclasse `GxEPD2_SOLUM_097c_PartialProbe`, non nel
driver.

`hibernate()` spegne l'analogico e mette il controller in deep sleep (solo con
RST cablato, `rst >= 0`) oppure, quando il refresh differenziale tiene un
riferimento valido, in Sleep con le RAM conservate. Va invocato per ridurre i
consumi tra refresh distanti; `hibernateDeep()` è la variante che prende sempre
il deep sleep.

### Buffer init

```cpp
void clearScreen(uint8_t value = 0xFF);                                     // tutto bianco
void clearScreen(uint8_t black_value, uint8_t color_value);                 // BWR
void writeScreenBuffer(uint8_t value = 0xFF);
void writeScreenBuffer(uint8_t black_value, uint8_t color_value);
```

`writeScreenBuffer` scrive direttamente sui 2 piani RAM del controller con i
valori indicati (polarity nativa SSD1677: `0x00` = accent spento, `0xFF` =
accent ON ovunque). Con `color_value != 0x00` il piano accent resta sporco,
così un disegno a canale singolo successivo lo ripulisce invece di dipingere
tutto di rosso.

---

## Sistema descrittore + macro

```cpp
namespace GxEPDImage {
  enum Format : uint8_t {
    FORMAT_BW_1BPP   = 0,   // 1 buffer 1bpp (compat image2cpp)
    FORMAT_BWR_1BPP  = 1,   // 2 buffer separati: black + red
    FORMAT_BWRY_1BPP = 2,   // 3 buffer separati: black + red + yellow
  };

  struct Descriptor {
    Format format;
    uint16_t width;
    uint16_t height;
    const uint8_t* data0;
    const uint8_t* data1;  // null per BW
    const uint8_t* data2;  // null per BW/BWR
  };
}
```

Macro per istanziare descrittori inline (utili nei `frame array` del
firmware o dentro `showImage(GXEPD_BWRY_IMAGE(...))`):

| Macro | Espansione |
|---|---|
| `GXEPD_BW_IMAGE(ptr, w, h)` | descrittore B/N a 1 buffer |
| `GXEPD_BWR_IMAGE(pb, pr, w, h)` | descrittore 3-colori (black + red) |
| `GXEPD_BWRY_IMAGE(pb, pr, py, w, h)` | descrittore a 3 piani (black + red + yellow) |

Di un descrittore a 3 piani `showImage()` rende `data0` e `data1`: nessun driver
della libreria scrive `data2`, che resta a disposizione di un chiamante con un
pannello a quattro colori e una primitiva propria per pilotarlo.

Lo script `epd_image_converter.pyw` genera automaticamente una variabile
`img_<nome>_desc` di tipo `GxEPDImage::Descriptor` ad ogni conversione,
pronta per essere passata a `showImage()`.

---

## Storia dei cambiamenti rilevanti

- **`showImage(Descriptor&)` + `showImage(uint8_t*, w, h)` (metodi classe)
  + `drawImage(const Descriptor&)` + `drawImageBWRY()` + helper privati
  `_drawDescriptor` / `_drawBWRY`** → tutti rimossi e sostituiti dalla
  free function template `GxEPDImage::showImage<DisplayT>(display, desc)`
  che vive nel namespace `GxEPDImage`. Unico entry-point pubblico per
  stampare un'immagine, indipendente dal silicio: parla solo con l'API pubblica
  del driver e con quella del template `GxEPD2_3C`.
- **`drawImageRGB565` / `rgb565ToQuadColor` / `FORMAT_RGB565` /
  `GXEPD_RGB565_IMAGE`** → rimossi insieme al supporto RGB565.
