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
//   showImage() è un template unico e non ha rami condizionali per driver,
//   quindi chi aggiunge un driver deve implementare questi due metodi, e sono
//   i soli obbligatori:
//
//     void    setPaged() override      reset del page-hint. Il virtual della
//                                      base viene chiamato da firstPage().
//     int16_t showImagePageHint()      page corrente dedotta dal contatore.
//                                      Il template GxEPD2_3C tiene
//                                      _current_page privato senza getter,
//                                      quindi il driver mantiene un contatore
//                                      parallelo, che avanza a ogni page
//                                      ricevuta da nextPage() e si azzera a
//                                      ogni inizio di loop e a ogni refresh.
//
//   Sul display basta l'API di GxEPD2_3C: pageHeight(), getRotation(),
//   height(), fillRect() e drawInvertedBitmap().
//
//   Il pinout invece non passa da qui: è la struct uniforme di
//   GxEPD2_SOLUM_Pins.h, che ogni driver accetta nel costruttore.
//
// TERZO PIANO: API OPZIONALE DEL DRIVER, NON PARTE DEL CONTRATTO
//   showImage() compone i due piani che il template GxEPD2_3C sa gestire,
//   black e red, e non tocca un eventuale terzo piano. Un driver il cui
//   pannello ne abbia davvero uno può esporre le primitive per scriverlo
//   (writeImageYellow, preserveYellow, isYellowPreserved), e il chiamante le
//   usa out-of-band: il piano si scrive PRIMA di firstPage() e si protegge
//   per la durata del loop paged. Sono API del singolo driver, quindi un
//   driver a due piani non le dichiara affatto e non ha nessun no-op da
//   scrivere.
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
    // Tre buffer black + red + yellow. showImage() rende data0 e data1: il
    // terzo piano resta a disposizione del chiamante, che lo scrive con le
    // primitive del driver se il pannello montato ne ha uno.
    FORMAT_BWRY_1BPP  = 2,
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
   * Come showImage() tratta i pixel bianchi dell'immagine. Il template
   * GxEPD2_3C riparte da un buffer bianco a ogni pagina e manda al controller
   * i due piani interi, quindi un bianco non dipinto non lascia traccia del
   * frame precedente: dipingerlo serve solo a coprire ciò che la stessa pagina
   * ha già disegnato sotto l'immagine.
   */
  enum class Paint : uint8_t
  {
    Opaque,   // dipinge anche il bianco, sopra qualunque cosa la pagina contenga
    InkOnly   // dipinge solo nero e rosso, per un'immagine su fondo bianco
  };

  /**
   * Unico entry-point pubblico per stampare un'immagine sui pannelli SOLUM
   * di questa libreria. Va chiamata DENTRO un loop paged
   * firstPage()/nextPage() del template GxEPD2_3C.
   *
   * È un wrapper di GxEPD2_3C::drawInvertedBitmap(), che disegna i bit a 0 di
   * un piano, cioè la convenzione bit=1=NOT color delle bitmap prodotte da
   * epd_image_converter.pyw e da image2cpp invertito: il piano rosso si
   * disegna per primo e il nero sopra, così dove entrambi sono accesi vince il
   * nero. Le righe sopra la pagina corrente si saltano a priori con il
   * page-hint del driver, il resto lo taglia drawPixel(). Con Paint::Opaque un
   * fillRect() bianco sulle stesse righe precede i due piani e costa un
   * drawPixel per pixel; con Paint::InkOnly si paga solo l'inchiostro.
   *
   * Un eventuale terzo piano del descrittore (data2) non viene toccato: è il
   * chiamante a scriverlo prima di firstPage() con le primitive del driver,
   * sui pannelli che ne hanno uno. Vedi la nota in cima al file.
   *
   * Esempio one-shot full-screen:
   *
   *   display.firstPage();
   *   do {
   *     GxEPDImage::showImage(display, *desc_ptr, 0, 0, GxEPDImage::Paint::InkOnly);
   *   } while (display.nextPage());
   *   display.hibernate();
   *
   * Per immagini raw image2cpp B/N usare la macro GXEPD_BW_IMAGE inline:
   *
   *   GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, w, h));
   *
   * @tparam DisplayT  template instance di GxEPD2_3C<Driver, page_height>
   * @param display    riferimento al display (drawInvertedBitmap, fillRect, epd2)
   * @param d          descrittore dell'immagine (BW / BWR / BWRY)
   * @param x, y       offset dell'angolo top-left (default 0,0)
   * @param paint      Opaque (default) o InkOnly
   */
  template<typename DisplayT>
  inline void showImage(DisplayT& display, const Descriptor& d,
                        int16_t x = 0, int16_t y = 0, Paint paint = Paint::Opaque)
  {
    const int16_t  w      = static_cast<int16_t>(d.width);
    const int16_t  h      = static_cast<int16_t>(d.height);
    const uint16_t stride = static_cast<uint16_t>((w + 7) / 8);

    // Righe sorgente da saltare prima del disegno. La page k del template copre
    // le righe fisiche da _pw_y + k * pageH in giù, con _pw_y = 0 a schermo
    // pieno; _pw_y è privato e il driver lo vede solo dopo la prima page, ma
    // non è mai negativo. Quindi si salta un solo lato, quello che nessuna
    // finestra può anticipare, e il resto lo taglia drawPixel():
    //   rotation 0: le righe sopra la page, cioè prima di pageY;
    //   rotation 2: drawPixel() ribalta y su HEIGHT, quindi le righe logiche
    //               dopo HEIGHT - pageY;
    //   rotation 1 e 3: le righe sorgente cadono sull'asse x, nessun salto.
    const int16_t pageH = static_cast<int16_t>(display.pageHeight());
    const int16_t pageY = static_cast<int16_t>(display.epd2.showImagePageHint()) * pageH;
    const uint8_t rot   = display.getRotation();

    int16_t pyStart = 0;
    int16_t pyEnd   = h;
    if (rot == 0)
      pyStart = static_cast<int16_t>(max(static_cast<int16_t>(0), static_cast<int16_t>(pageY - y)));
    else if (rot == 2)
      pyEnd = static_cast<int16_t>(min(h, static_cast<int16_t>(display.height() - pageY - y)));
    if (pyEnd <= pyStart) return;

    const int16_t  top     = static_cast<int16_t>(y + pyStart);
    const int16_t  rows    = static_cast<int16_t>(pyEnd - pyStart);
    const uint32_t skipped = static_cast<uint32_t>(pyStart) * stride;

    if (paint == Paint::Opaque)
      display.fillRect(x, top, w, rows, GxEPD_WHITE);
    if (d.data1)
      display.drawInvertedBitmap(x, top, d.data1 + skipped, w, rows, GxEPD_RED);
    display.drawInvertedBitmap(x, top, d.data0 + skipped, w, rows, GxEPD_BLACK);
    // Nessun avanzamento del page-hint qui: lo fa il driver quando
    // GxEPD2_3C::nextPage() gli consegna la page. showImage può essere
    // chiamata 0, 1 o N volte nella stessa page senza desincronizzare il counter.
  }
} // namespace GxEPDImage

// Macro di comodo per descrittori inline (utile per i frame array nello sketch).
#define GXEPD_BW_IMAGE(ptr, w, h)          { GxEPDImage::FORMAT_BW_1BPP,   (w), (h), (const uint8_t*)(ptr), nullptr, nullptr }
#define GXEPD_BWR_IMAGE(pb, pr, w, h)      { GxEPDImage::FORMAT_BWR_1BPP,  (w), (h), (const uint8_t*)(pb), (const uint8_t*)(pr), nullptr }
#define GXEPD_BWRY_IMAGE(pb, pr, py, w, h) { GxEPDImage::FORMAT_BWRY_1BPP, (w), (h), (const uint8_t*)(pb), (const uint8_t*)(pr), (const uint8_t*)(py) }

#endif
