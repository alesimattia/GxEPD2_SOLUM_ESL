# API of the `GxEPD2_SOLUM_097c_960x672` driver

Complete reference for the public methods of the custom driver
[GxEPD2_SOLUM_097c_960x672.h](src/GxEPD2_SOLUM_097c_960x672.h) for the
SOLUM 9.7" panel (672w × 960h native portrait, SSD1677 controller, 3 colors: white, black and red).

## Custom API (application level)

### `GxEPDImage::showImage()` — single entry point

Free function template in the `GxEPDImage` namespace (lives in
[`src/GxEPDImage.h`](src/GxEPDImage.h), not as a class method). It is
**the only public function to display an image** on the SOLUM panels,
reusable from any sketch:

```cpp
template<typename DisplayT>
void GxEPDImage::showImage(DisplayT& display,
                           const GxEPDImage::Descriptor& d,
                           int16_t x = 0, int16_t y = 0,
                           GxEPDImage::Paint paint = GxEPDImage::Paint::Opaque);
```

Must be called **inside** a `firstPage()`/`nextPage()` loop of the
`GxEPD2_3C` template, after `fillScreen()` and before `nextPage()`.
Supports all 3 descriptor formats (BW / BWR / BWRY) and renders the black
and red planes: `data2` of a three-plane descriptor is not touched.

It is a wrapper of `GxEPD2_3C::drawInvertedBitmap()`: the red plane is drawn
first and the black one on top, so where both are set black wins. Source rows
above the current page are skipped using the driver's page-hint.
`Paint::Opaque` (default) first adds a white `fillRect()` on the same rows;
`Paint::InkOnly` paints only black and red ink. Either way the previous frame
leaves no ghosting: `GxEPD2_3C` clears its buffer to white on every page and
sends both planes in full, so an unpainted white leaves no trace, and `Opaque`
only covers what the same page already drew under the image. The display needs
`pageHeight()`, `getRotation()`, `height()`, `fillRect()` and
`drawInvertedBitmap()`; the driver must provide `setPaged()` and
`showImagePageHint()`.

**Caller responsibilities:**
- Open the paged loop (`firstPage()` + `do { ... } while (nextPage())`)
- Call `display.hibernate()` to power off the panel

Minimal one-shot full-screen example:

```cpp
display.firstPage();
do {
  display.fillScreen(GxEPD_WHITE);
  GxEPDImage::showImage(display, *desc_ptr);
} while (display.nextPage());
display.hibernate();
```

For raw image2cpp B/W bitmaps use the inline macro:

```cpp
GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, 960, 672));
```

### `writeImageBlack` / `writeImageRed` and the partial opt-in API: single-channel siblings

These are not in the driver: they live in the `GxEPD2_SOLUM_097c_PartialProbe`
subclass of
[`examples/097c/panel_diagnostic/PartialProbeDriver.h`](examples/097c/panel_diagnostic/PartialProbeDriver.h),
with the signatures below. Whoever wants them builds the display with that class
instead of the driver, since `GxEPD2_3C` is instantiated with the subclass type:

```cpp
GxEPD2_3C<GxEPD2_SOLUM_097c_PartialProbe, GxEPD2_SOLUM_097c_PartialProbe::HEIGHT / 8>
    display(GxEPD2_SOLUM_097c_PartialProbe(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }));
```

The firmware does not call them: it only uses the differential refresh inside the
paged loop. The open points of these APIs, and of two driver sequences still to
measure on the glass, are in `ISSUES.md` of the parent repo
(ePaper-weather-dashboard), which includes this library as a submodule.

`writeImageBlack` and `writeImageRed` write a single plane of the controller,
**without refresh**, for manual compositing:

```cpp
void writeImageBlack(const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x24
void writeImageRed  (const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x26
```

Input bitmap convention: `bit=1` where the pixel does **NOT** belong to
that channel (compatible format with Python script and image2cpp). The
subclass applies `~data` before SPI transfer for the accent, to match the
native SSD1677 polarity (`bit=1` in RAM = colorant ON).

Neither calls `refresh()`: the caller is responsible for that.

The partial itself (640 ms, black and white only) has its own entry points. Under
its LUT `0x26` is the previous frame in black and white polarity, not the accent,
so a frame updated in partial carries no red:

```cpp
void writeScreenBufferPrevious(uint8_t value = 0xFF);
void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y,
                          int16_t w, int16_t h,
                          bool invert = false, bool mirror_y = false, bool pgm = false);
void refreshPartial();
void refreshPartial(int16_t x, int16_t y, int16_t w, int16_t h);  // coordinates are declarative
void drawImagePartial(const uint8_t* bitmap, int16_t x, int16_t y,
                      int16_t w, int16_t h, bool invert = false, bool pgm = true);
void drawImagePartialPart(const uint8_t* bitmap,
                          int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                          bool invert = false, bool pgm = true);
void setPartialLut(const uint8_t* lut);  // 110 bytes, waveform setting layout
```

`x` and `w` of `drawImagePartial*()` must be multiples of 8. The recipe to draw
text and shapes through a `GFXcanvas1` and `drawImagePartial(..., invert = true,
pgm = false)` is in [README §3.1](README.md#31-il-partial-in-bianco-e-nero-640-ms-e-come-ci-si-scrive-dentro).

### `hibernateDeep()`

```cpp
void hibernateDeep();
```

Like `hibernate()`, but it forces deep sleep even when `hibernate()` avoids it to
keep the differential reference. It drops the reference and loses the controller
RAMs, so the next frame is a full refresh. It is for callers who know the panel
will stay idle for long, and like `hibernate()` it needs RST wired.

### BUSY faults: `busyFault()` and `lastUpdateMs()`

```cpp
GxEPD2_SOLUM_BusyFault busyFault() const;   // None, TooShort, Timeout
uint32_t lastUpdateMs() const;
```

Every BUSY wait of the driver checks its outcome. `Timeout` means BUSY is still
high when the wait expires: `BUSY_TIMEOUT_US` (120 s) for refreshes,
`SHORT_BUSY_TIMEOUT_US` (5 s) for short waits (power on and off, SWRESET,
hardware pattern fills). `TooShort` means a refresh ended before the minimum
plausible duration, `MIN_PLAUSIBLE_FULL_REFRESH_MS` (5000 ms) or
`MIN_PLAUSIBLE_PARTIAL_REFRESH_MS` (300 ms), so the controller did not run the
waveform.

A fault raises a latch that skips the following waits and the `0x20` command, and
`refresh(false)` then validates neither the shown frame nor the differential
reference. The latch is cleared in `setPaged()` and at the start of the public
APIs that write and refresh outside the paged loop (`clearScreen()`,
`drawImage*()`, `drawNative()`, `refresh(x, y, w, h)`). `lastUpdateMs()` is the
duration of the last refresh in milliseconds, from the `0x20` command to BUSY
low, and 0 if it did not start. The driver never writes to `Serial`.

---

## API inherited from `GxEPD2_EPD` (virtual override)

Kept for compatibility with the `GxEPD2_3C` template and the upstream
library contract. **Not intended for direct use from the sketch**: the
sketch uses `showImage()` or lets the template invoke these methods
internally during the paged flow.

### Bitmap (write without refresh; coordinates `x` and `w` must be multiples of 8)

```cpp
void writeImage(const uint8_t bitmap[],
                int16_t x, int16_t y, int16_t w, int16_t h,
                bool invert = false, bool mirror_y = false, bool pgm = false);

void writeImage(const uint8_t* black, const uint8_t* color,
                int16_t x, int16_t y, int16_t w, int16_t h,
                bool invert = false, bool mirror_y = false, bool pgm = false);
```

### Bitmap part (window of a larger bitmap)

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

`writeImage(black, color, ...)` is the **HOT PATH** invoked 8 times
per refresh by the `GxEPD2_3C` template during `nextPage()` (8 = number
of pages, given by the `GxEPD2_3C<Driver, HEIGHT/8>` template).
`writeImagePart(black, color, ...)` is the area write that
`GxEPD2_3C::displayWindow()` calls, once.

### Native (raw controller sprite)

```cpp
void writeNative(const uint8_t* data1, const uint8_t* data2,
                 int16_t x, int16_t y, int16_t w, int16_t h,
                 bool invert = false, bool mirror_y = false, bool pgm = false);
```

### Draw (write + full-window refresh)

Same signatures as `writeImage*`/`writeImagePart*`/`writeNative` but
with final refresh:

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

`refresh(bool)` is where a frame of the paged loop becomes a partial, a full
refresh or nothing. By default it performs a full-window refresh (~24 s), which
is what the template needs for **colour** frames: a black and white partial there
would drop the red. With the differential refresh enabled
(`setDifferentialRefresh(true)`), an armed frame whose red plane is unchanged
fires the black and white partial (~0.64 s) instead; with
`setSkipUnchanged(true)`, a frame identical to the one on the glass launches
nothing and `frameSkipped()` returns true. `refresh(x, y, w, h)` ignores its
coordinates and always performs a full-window refresh outside the paged loop.
The partial's opt-in API (`drawImagePartial()` and siblings) is in the
`GxEPD2_SOLUM_097c_PartialProbe` subclass, not in the driver.

`hibernate()` powers off the analog and puts the controller in deep sleep (only
with RST wired, `rst >= 0`) or, when the differential refresh holds a valid
reference, in Sleep with the RAMs kept. Call it to reduce power consumption
between distant refreshes; `hibernateDeep()` is the variant that always takes
deep sleep.

### Buffer init

```cpp
void clearScreen(uint8_t value = 0xFF);                                     // all white
void clearScreen(uint8_t black_value, uint8_t color_value);                 // BWR
void writeScreenBuffer(uint8_t value = 0xFF);
void writeScreenBuffer(uint8_t black_value, uint8_t color_value);
```

`writeScreenBuffer` writes directly to the 2 RAM planes of the controller with
the indicated values (native SSD1677 polarity: `0x00` = accent OFF, `0xFF` =
accent ON everywhere). With `color_value != 0x00` the accent plane is left dirty,
so a following single-channel draw cleans it instead of painting everything red.

---

## Descriptor system + macros

```cpp
namespace GxEPDImage {
  enum Format : uint8_t {
    FORMAT_BW_1BPP   = 0,   // 1 buffer 1bpp (image2cpp compat)
    FORMAT_BWR_1BPP  = 1,   // 2 separate buffers: black + red
    FORMAT_BWRY_1BPP = 2,   // 3 separate buffers: black + red + yellow
  };

  struct Descriptor {
    Format format;
    uint16_t width;
    uint16_t height;
    const uint8_t* data0;
    const uint8_t* data1;  // null for BW
    const uint8_t* data2;  // null for BW/BWR
  };
}
```

Macros to instantiate inline descriptors (useful in firmware `frame
array` or inside `showImage(GXEPD_BWRY_IMAGE(...))`):

| Macro | Expansion |
|---|---|
| `GXEPD_BW_IMAGE(ptr, w, h)` | B/W single-buffer descriptor |
| `GXEPD_BWR_IMAGE(pb, pr, w, h)` | 3-color descriptor (black + red) |
| `GXEPD_BWRY_IMAGE(pb, pr, py, w, h)` | 3-plane descriptor (black + red + yellow) |

For a 3-plane descriptor `showImage()` renders `data0` and `data1` only: no
driver in this library writes `data2`, which is left to a caller with a
four-color panel and its own primitive to drive it.

The `epd_image_converter.pyw` script automatically generates a variable
`img_<name>_desc` of type `GxEPDImage::Descriptor` at each conversion,
ready to be passed to `showImage()`.

---

## Relevant change history

- **`showImage(Descriptor&)` + `showImage(uint8_t*, w, h)` (class methods)
  + `drawImage(const Descriptor&)` + `drawImageBWRY()` + private helpers
  `_drawDescriptor` / `_drawBWRY`** → all removed and replaced by the
  free function template `GxEPDImage::showImage<DisplayT>(display, desc)`
  living in the `GxEPDImage` namespace. Single public entry point for
  displaying an image, independent of the silicon: it only talks to the public
  API of the driver and of the `GxEPD2_3C` template.
- **`drawImageRGB565` / `rgb565ToQuadColor` / `FORMAT_RGB565` /
  `GXEPD_RGB565_IMAGE`** → removed along with RGB565 support.
