// =============================================================================
// Sistema di descrittori immagine e showImage() condiviso dai driver SOLUM
// di questa libreria.
//
// Vive in un header a parte perchè il namespace GxEPDImage è unico per tutta
// la libreria: se ogni driver lo ridefinisse, includere due header driver
// nella stessa translation unit sarebbe un errore di compilazione.
//
// Il template showImage() è indipendente dal silicio: parla solo con l'API
// pubblica del driver e con quella del template GxEPD2_3C che lo avvolge.
//
// API RICHIESTA A UN DRIVER DI QUESTA LIBRERIA
//   Chi aggiunge un driver deve implementarla tutta, anche quando una parte
//   non ha senso sul suo pannello: showImage() è un template unico e non ha
//   rami condizionali per driver. Sono cinque metodi pubblici:
//
//     void    setPaged() override      reset del page-hint. Il virtual della
//                                      base viene chiamato da firstPage().
//     int16_t showImagePageHint()      page corrente dedotta dal contatore.
//                                      Il template GxEPD2_3C tiene
//                                      _current_page privato senza getter,
//                                      quindi il driver mantiene un contatore
//                                      parallelo: azzerato in setPaged() e in
//                                      _Update_Full(), avanzato in
//                                      writeImage(black, color, ...).
//     void    writeImageYellow(...)    terzo piano. No-op su un pannello che
//                                      non ce l'ha.
//     void    preserveYellow(bool)     protezione del terzo piano durante il
//                                      loop paged. No-op come sopra.
//     bool    isYellowPreserved()      stato del flag. true costante come sopra.
//
//   Un driver a due piani dichiara le tre primitive del giallo come no-op: il
//   ramo FORMAT_BWRY_1BPP non scatta mai su di lui, perchè è guardato dal
//   formato del descrittore, non dal tipo del driver.
//
//   Il pinout invece non passa da qui: è la struct uniforme di
//   GxEPD2_SOLUM_Pins.h, che ogni driver accetta nel costruttore.
// =============================================================================

#ifndef _GxEPDImage_H_
#define _GxEPDImage_H_

#include <Arduino.h>
#include <GxEPD2_EPD.h>

// ===========================================================================
// Sistema descrittore immagine universale.
// Permette di passare a showImage() un puntatore opaco che descrive formato,
// dimensioni e canali dell'immagine. Compatibile con:
//   - bitmap 1bpp B/N generate da image2cpp (const uint8_t* raw)
//   - bitmap 3-colori (black + red) generate da epd_image_converter.pyw
//   - bitmap 4-colori (black + red + yellow) con canale giallo nativo
// ===========================================================================
namespace GxEPDImage
{
  enum Format : uint8_t
  {
    FORMAT_BW_1BPP    = 0,  // 1 bpp singolo buffer (compat image2cpp)
    FORMAT_BWR_1BPP   = 1,  // due buffer separati black + red
    FORMAT_BWRY_1BPP  = 2,  // tre buffer black + red + yellow
  };

  /**
   * Descrittore universale di immagine. I puntatori data1/data2 sono
   * opzionali a seconda del formato.
   */
  struct Descriptor
  {
    Format format;
    uint16_t width;
    uint16_t height;
    const uint8_t* data0;
    const uint8_t* data1;
    const uint8_t* data2;
  };

  /**
   * Unico entry-point pubblico per stampare un'immagine sul pannello SOLUM
   * 4-colori. Va chiamata DENTRO un loop paged firstPage()/nextPage() del
   * template GxEPD2_3C, dopo fillScreen() e prima di nextPage().
   *
   * Responsabilità del chiamante (vedi esempio sotto):
   *   1. Aprire il loop paged (firstPage + do { ... } while (nextPage()))
   *   2. Chiamare display.hibernate() se vuole spegnere il pannello
   *
   * Il reset di preserveYellow avviene automaticamente dentro refresh()
   * al termine del loop paged: il chiamante NON deve farlo a mano.
   *
   * Strategia (specifica del display SOLUM, non riusabile su altri):
   *   - canale yellow (0x28): scritto direttamente sul controller la prima
   *     volta che la funzione gira, e protetto durante il loop paged via
   *     preserveYellow(true). Il template GxEPD2_3C upstream vede solo 2
   *     canali (black + red) e non può pilotare il giallo: questa è
   *     l'unica via per pilotare il 4o colore del pannello.
   *   - canali black + red: decodificati pixel-per-pixel con drawPixel,
   *     perchè la convenzione bit=1=NOT color delle bitmap (output dello
   *     script python e di image2cpp invertito) è opposta a quella di
   *     Adafruit_GFX::drawBitmap (bit=1=IS color), e BWR richiede
   *     compositing di 2 piani che drawBitmap non fa nativamente.
   *
   * Idempotency canale yellow: il yellow viene scritto al MASSIMO una
   * volta per loop paged. La guardia è il page-hint a 0, cioè la prima
   * iterazione: le iterazioni 2..8 di nextPage() saltano writeImageYellow,
   * 7 riscritture da 80.640 byte evitate, ~450 ms a refresh BWRY con SPI a
   * 10 MHz. Un pre-write out-of-band del chiamante su 0x28 non sopprime
   * questa scrittura: writeImageYellow tocca solo la propria finestra RAM,
   * quindi aree disgiunte convivono.
   *
   * Costo loop drawPixel: ~24 ms su ESP32 a 240 MHz, irrilevante rispetto
   * ai ~22 s di refresh elettroforetico.
   *
   * Esempio one-shot full-screen:
   *
   *   display.firstPage();
   *   do {
   *     display.fillScreen(GxEPD_WHITE);
   *     GxEPDImage::showImage(display, *desc_ptr);
   *   } while (display.nextPage());
   *   display.hibernate();
   *
   * Per immagini raw image2cpp B/N usare la macro GXEPD_BW_IMAGE inline:
   *
   *   GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, w, h));
   *
   * @tparam DisplayT  template instance di GxEPD2_3C<Driver, page_height>
   * @param display    riferimento al display (per drawPixel + display.epd2)
   * @param d          descrittore dell'immagine (BW / BWR / BWRY)
   * @param x, y       offset dell'angolo top-left (default 0,0)
   */
  template<typename DisplayT>
  inline void showImage(DisplayT& display, const Descriptor& d,
                        int16_t x = 0, int16_t y = 0)
  {
    const int16_t  w      = static_cast<int16_t>(d.width);
    const int16_t  h      = static_cast<int16_t>(d.height);
    const uint16_t stride = static_cast<uint16_t>((w + 7) / 8);

    /** Il piano yellow va scritto una volta per loop paged e il page-hint a 0
     *  identifica la prima iterazione. Guardare isYellowPreserved() era un
     *  bug: quel flag lo alza anche un pre-write del chiamante su un'altra
     *  area di 0x28 (la barra temp-range dello sketch), e il giallo
     *  dell'immagine non veniva mai inviato. */
    if (d.format == FORMAT_BWRY_1BPP && d.data2
        && display.epd2.showImagePageHint() == 0)
    {
      display.epd2.writeImageYellow(d.data2, x, y, w, h, true);
      display.epd2.preserveYellow(true);
    }

    // Bounds della page corrente nel frame OUTPUT. Per rotation 0 e 2 (nessuno
    // scambio assi) le righe sorgente con y output fuori dalla page possono
    // essere skippate prima del loop -> 1/8 delle iterazioni complessive.
    // Per rotation 1 e 3 (90 deg) le righe sorgente mappano sull'asse x
    // dell'output, quindi la skip-by-row non si applica: fallback al loop
    // completo (parità funzionale con la versione originale).
    const int16_t pageH    = static_cast<int16_t>(display.pageHeight());
    const int16_t pageY    = static_cast<int16_t>(display.epd2.showImagePageHint()) * pageH;
    const uint8_t rot      = display.getRotation();
    const bool    can_skip = (rot == 0 || rot == 2);

    const int16_t pyStart = can_skip
        ? static_cast<int16_t>(max(static_cast<int16_t>(0), static_cast<int16_t>(pageY - y)))
        : static_cast<int16_t>(0);
    const int16_t pyEnd = can_skip
        ? static_cast<int16_t>(min(h, static_cast<int16_t>(pageY + pageH - y)))
        : h;

    for (int16_t py = pyStart; py < pyEnd; ++py)
    {
      // Hoist del row offset fuori dal loop interno: py * stride viene
      // calcolato 1 volta per riga invece che per ogni pixel.
      const uint32_t rowOffset = static_cast<uint32_t>(py) * stride;
      const uint8_t* row0 = d.data0 + rowOffset;
      const uint8_t* row1 = d.data1 ? (d.data1 + rowOffset) : nullptr;

      for (int16_t px = 0; px < w; ++px)
      {
        const uint16_t byteIdx = static_cast<uint16_t>(px >> 3);
        const uint8_t  bitMask = 0x80 >> (px & 7);

        uint16_t color = GxEPD_WHITE;
        if (!(pgm_read_byte(row0 + byteIdx) & bitMask))
          color = GxEPD_BLACK;
        else if (row1 && !(pgm_read_byte(row1 + byteIdx) & bitMask))
          color = GxEPD_RED;

        display.drawPixel(x + px, y + py, color);
      }
    }
    // Nessun avanzamento del page-hint qui: è fatto dentro
    // writeImage(black, color, ...) quando GxEPD2_3C::nextPage() chiude la
    // page sul controller. showImage può essere chiamata 0, 1 o N volte
    // nella stessa page senza desincronizzare il counter.
  }
} // namespace GxEPDImage

// Macro di comodo per descrittori inline (utile per i frame array nello sketch).
#define GXEPD_BW_IMAGE(ptr, w, h)          { GxEPDImage::FORMAT_BW_1BPP,   (w), (h), (const uint8_t*)(ptr), nullptr, nullptr }
#define GXEPD_BWR_IMAGE(pb, pr, w, h)      { GxEPDImage::FORMAT_BWR_1BPP,  (w), (h), (const uint8_t*)(pb), (const uint8_t*)(pr), nullptr }
#define GXEPD_BWRY_IMAGE(pb, pr, py, w, h) { GxEPDImage::FORMAT_BWRY_1BPP, (w), (h), (const uint8_t*)(pb), (const uint8_t*)(pr), (const uint8_t*)(py) }

#endif
