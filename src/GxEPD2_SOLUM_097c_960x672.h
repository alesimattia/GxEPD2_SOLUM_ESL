// =============================================================================
// Driver custom per pannello e-paper SOLUM 9.7" 672w x 960h (native portrait) su ESP32.
// Convenzione: NwxMh = N px larghezza (X) x M px altezza (Y).
//
// Origine:
//   - Libreria base: GxEPD2 (https://github.com/ZinggJM/GxEPD2).
//   - Driver di partenza: GxEPD2_1330c_GDEM133Z91 (Good Display GDEM133Z91,
//     controller SSD1677). La logica di init, write RAM e refresh è ereditata
//     da lì e poi adattata al pannello SOLUM recuperato da ESL dismesse.
//
// Pannello pilotato (personalizzazione rispetto all'originale):
//   - Produttore: SOLUM (modulo ESL 9.7" riusato).
//   - Risoluzione: 672w x 960h native portrait (usato in landscape 960w x 672h dopo setRotation(0)).
//   - Colori: bianco / nero / rosso verificati sul pannello, pilotati dai
//     comandi 0x24 (BW plane) e 0x26 (accent) del controller SSD1677.
//   - Refresh: solo full refresh (~22 s), niente fast partial update.
//   - Controller: SSD1677, indirizzamento full-window a finestra parziale.
//   - Alimentazione: 3.3V su VCC e su tutte le data line (non 5V-tolerant).
//
// CANALE GIALLO (0x28): mai verificato sul pannello.
//   Da sapere quando si tocca: il datasheet SSD1677 Rev 1.0 (docs/) elenca due
//   soli piani immagine, 0x24 Write RAM (Black White) e 0x26 Write RAM (RED),
//   più il motore di dithering a 0x25, e alla posizione 0x28 mette "VCOM
//   Sense", un comando analogico che richiede CLKEN=1 e ANALOGEN=1, alza il
//   BUSY e non prende parametri. La parola "yellow" non compare nel datasheet.
//   Anche OpenEPaperLink (docs/openepaperlink/) cataloga il modulo come
//   SOLUM_M3_BWR_97. Dall'altra parte il datasheet SOLUM del donor dichiara
//   PIXEL COLORS = BWRY per la 9.7".
//   La contraddizione la risolve la misura, non la documentazione: la fa
//   examples/panel_diagnostic/panel_diagnostic.ino di questa libreria, che
//   esercita tutte le combinazioni dei due piani più il canale 0x28 e chiude
//   con la scheda di osservazione che mappa ogni esito sulla conseguenza per
//   questo file.
//
// Requisiti build:
//   - HW SPI (HSPI su ESP32 tramite la Waveshare E-Paper ESP32 Driver Board).
//   - Target ESP32 (Arduino core): i delay(1) di yield WDT ESP8266 sono
//     stati rimossi dai hot path; il firmware deve girare solo su ESP32.
//   - Adafruit_GFX opzionale: se ENABLE_GxEPD2_GFX=0 la libreria compila
//     senza le primitive grafiche (risparmio ~15 KB di flash).
//
// Aggiunte custom rispetto alla base GxEPD2:
//   - GxEPDImage::showImage(display, descriptor) come UNICO entry-point
//     pubblico per stampare un'immagine. Free function template (vive nel
//     namespace GxEPDImage del .h, non come metodo classe) che accetta
//     descrittori BW / BWR / BWRY e va chiamata dentro un loop paged
//     firstPage()/nextPage() del template GxEPD2_3C.
//   - 3 API siblings writeImageBlack / writeImageRed / writeImageYellow
//     per scrittura single-channel diretta sul controller (no GFX), usate
//     internamente da showImage e disponibili per compositing manuale.
//   - preserveYellow(bool): protezione del canale 0x28 durante il paged
//     loop (il template upstream vede solo 2 canali, il giallo va iniettato
//     manualmente prima del firstPage() e protetto fino al refresh finale).
//
// PITFALL — display.drawPixel(x, y, GxEPD_YELLOW):
//   Il template upstream GxEPD2_3C tratta GxEPD_YELLOW come se fosse
//   GxEPD_RED: in GxEPD2_3C.h il drawPixel ha la condizione
//     else if ((color == GxEPD_RED) || (color == GxEPD_YELLOW))
//       _color_buffer[i] = ... // scrive nel piano red
//   Quindi un pixel "giallo" disegnato via drawPixel finisce sul piano 0x26
//   (rosso) e MAI sul piano 0x28 (giallo). Per pilotare davvero il piano
//   yellow ci sono due vie:
//     1. Usare GxEPDImage::showImage(display, descriptor) con un descrittore
//        FORMAT_BWRY_1BPP — il giallo viene iniettato direttamente sul
//        controller via writeImageYellow() prima del loop paged e protetto.
//     2. Chiamare manualmente writeImageYellow() prima di firstPage() e poi
//        preserveYellow(true) per tenerlo durante il loop paged. Il driver
//        resetta automaticamente il flag dentro _Update_Full() al refresh.
//   Forkare GxEPD2_3C per gestire un terzo buffer yellow nativo richiederebbe
//   modifiche upstream non desiderate; le due strade sopra sono l'unica via
//   compatibile con la libreria stock.
//
// PAGE-TRACKING di showImage:
//   showImage skippa le righe sorgente che non intersecano la page corrente
//   del template GxEPD2_3C, riducendo il loop pixel a 1/8 delle iterazioni
//   complessive (7 passate da ~24 ms evitate su 8, ~170 ms risparmiati per
//   refresh full-screen). Per dedurre quale page è in corso (il template
//   tiene _current_page private senza getter pubblico) il driver mantiene un
//   counter _show_image_page_hint:
//     - reset a 0 dentro setPaged() (override del virtual base, chiamato da
//       GxEPD2_3C::firstPage() del template all'inizio di ogni loop paged)
//     - avanzato dentro writeImage(black, color, ...) (chiamato dal template
//       da nextPage() ESATTAMENTE una volta per page in full-window mode)
//     - reset difensivo dentro _Update_Full() al refresh finale
//
//   Conseguenza: showImage può essere chiamata 0, 1 o N volte all'interno
//   di una stessa page senza desincronizzare il counter — il counter avanza
//   solo quando il template chiude la page con writeImage(black, color).
//
//   LIMITAZIONE residua (irrilevante per il progetto): in modalità partial
//   window del template (setPartialWindow), nextPage() salta il writeImage()
//   per le pages che non intersecano la window — il counter si disallinea
//   per quelle iterazioni. Il progetto attuale usa solo setFullWindow,
//   quindi non incappa in questo edge case.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef _GxEPD2_SOLUM_097c_960x672_H_
#define _GxEPD2_SOLUM_097c_960x672_H_

#include <GxEPD2_EPD.h>

// Sistema di descrittori immagine e showImage(): condivisi con gli altri driver
// della libreria, vedi src/GxEPDImage.h.
#include "GxEPDImage.h"

class GxEPD2_SOLUM_097c_960x672 : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 960;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 672;
    static const GxEPD2::Panel panel = GxEPD2::GDEM133Z91;
    static const bool hasColor = true;
    static const bool hasPartialUpdate = true; // has partial window addressing, but uses full window refresh
    static const bool hasFastPartialUpdate = false;
    static const uint16_t power_on_time = 100; // ms, e.g. 82001us
    static const uint16_t power_off_time = 250; // ms, e.g. 222001us
    static const uint16_t full_refresh_time = 22000; // ms, e.g. 20476000us
    static const uint16_t partial_refresh_time = 22000; // ms, e.g. 20476000us
    // constructor
    GxEPD2_SOLUM_097c_960x672(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void clearScreen(uint8_t black_value, uint8_t color_value); // init controller memory and screen
    void clearScreen(uint8_t black_value, uint8_t color_value, uint8_t yellow_value); // init + yellow
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value); // init controller memory
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value, uint8_t yellow_value); // init + yellow
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false); // screen refresh from controller memory to full screen
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h); // screen refresh from controller memory, partial screen
    void powerOff(); // turns off generation of panel driving voltages, avoids screen fading over time
    void hibernate(); // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)

    // ------------------------------------------------------------------
    // API siblings per scrittura single-channel (senza refresh).
    //
    // Stesso shape per i 3 canali accent del controller SSD1677:
    //   writeImageBlack  -> cmd 0x24 (black/white plane, no invert)
    //   writeImageRed    -> cmd 0x26 (red accent, invert applicato)
    //   writeImageYellow -> cmd 0x28 (yellow accent, invert applicato)
    //
    // Convenzione bitmap input: bit=1 dove il pixel NON appartiene a quel
    // canale (stesso formato prodotto da epd_image_converter.pyw e
    // image2cpp). Il driver applica ~data prima del transfer per gli accent
    // per allinearsi alla polarity del controller (bit=1 nativo = accent ON).
    //
    // Usati in flusso paged (writeImageYellow prima di firstPage() +
    // preserveYellow(true) durante il loop) o per compositing manuale
    // multi-canale. NON chiamano refresh: è responsabilità del chiamante.
    // ------------------------------------------------------------------
    void writeImageBlack (const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);
    void writeImageRed   (const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);
    void writeImageYellow(const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);

    // Se preserve=true, writeImagePart NON azzera il canale 0x28 anche se
    // _yellow_dirty. Serve al flusso paged con yellow "out-of-band": il
    // template GxEPD2_3C scrive solo black+red via writeImagePart, perciò
    // il giallo va iniettato manualmente prima di firstPage() e protetto
    // durante il loop.
    //
    // Auto-reset: il flag torna automaticamente a false dentro refresh(),
    // chiamato dal template GxEPD2_3C al termine del loop paged. Il
    // chiamante NON deve fare preserveYellow(false) manualmente.
    //
    // Nota: red non ha un flag analogo perchè il template lo gestisce già.
    void preserveYellow(bool preserve) { _preserve_yellow = preserve; }

    // Getter del flag preserveYellow: dice se un yellow out-of-band è già
    // protetto per il loop paged corrente. L'idempotency di showImage sul
    // canale 0x28 usa invece showImagePageHint().
    bool isYellowPreserved() const { return _preserve_yellow; }

    // L'entry-point pubblico di stampa immagine è la free function template
    // GxEPDImage::showImage(display, desc) definita nel namespace sopra.
    // Va chiamata dentro un loop firstPage()/nextPage() del template GFX.

    // Hook virtual chiamato da GxEPD2_3C::firstPage() (vedi GxEPD2_3C.h:323)
    // all'inizio di ogni loop paged. Override del no-op base in GxEPD2_EPD.h:92.
    // Reset del page-hint per allinearlo a _current_page del template che
    // viene riportato a 0 in firstPage.
    void setPaged() override { _show_image_page_hint = 0; }

    // Getter del page-hint usato da GxEPDImage::showImage come surrogato di
    // _current_page del template GxEPD2_3C (privato, senza getter pubblico).
    // Permette a showImage di skippare a priori le righe sorgente fuori dalla
    // page corrente, riducendo il loop pixel a 1/8 delle iterazioni.
    int16_t showImagePageHint() const { return _show_image_page_hint; }
  private:
    // Pulizia selettiva di un canale accent: se il flag dirty è attivo
    // scrive 0x00 ovunque (polarity nativa SSD1677 = "accent spento") e
    // resetta il flag. Centralizza la semantica "clean accent" per evitare
    // il bug latente 0xFF (= accent ON ovunque) che esisteva in versioni
    // precedenti del driver.
    void _cleanAccentIfDirty(uint8_t command, bool& dirty_flag);

    void _writeScreenBuffer(uint8_t command, uint8_t value);

    /** Riempimento di un piano immagine tramite i comandi Auto Write RAM for
     *  Regular Pattern del SSD1677 (0x47 per 0x24, 0x46 per 0x26): il pattern
     *  lo genera il controller, quindi sul bus va un solo byte invece di
     *  80.640 e il piano si azzera in ~15 ms invece di ~65. È la stessa
     *  coppia di comandi che il firmware SOLUM di fabbrica usa in init.
     *  Ritorna false quando il piano o il valore non sono esprimibili come
     *  pattern, e il chiamante ripiega sul transfer SPI. */
    bool _fillPlaneByPattern(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();

    // Dirty flags: tracciano quando i canali RAM accent del controller
    // contengono dati non puliti dall'ultima writeScreenBuffer(). Permettono
    // di saltare il clean pre-draw quando non serve (tipico caso: catena di
    // immagini B/N consecutive). Risparmio per draw: ~15 ms su 0x26, che si
    // pulisce col pattern hardware, ~65 ms su 0x28, che passa dal bus
    // (80.640 byte a ~0.8us/byte).
    bool _color_dirty = false;   // 0x26 (red)
    bool _yellow_dirty = false;  // 0x28 (yellow)
    // Se true, writeImagePart salta il cleanup di 0x28 anche se dirty.
    // Permette al canale yellow scritto prima del loop paged di sopravvivere
    // fino al refresh finale (flusso rendering BWRY in paged mode).
    bool _preserve_yellow = false;

    // Counter usato da GxEPDImage::showImage per dedurre la page corrente del
    // template GxEPD2_3C, che mantiene _current_page private senza getter.
    // Avanzamento: dentro writeImage(black, color, ...), che il template chiama
    // ESATTAMENTE una volta per page in nextPage() (full-window mode).
    // Reset:
    //   - setPaged() (chiamato da firstPage() del template) → riallinea
    //   - _Update_Full() (al refresh finale) → simmetria difensiva
    int16_t _show_image_page_hint = 0;
};

// =============================================================================
// Implementazione inline dei metodi della classe.
//
// Scelta header-only: l'intero driver vive qui (no compilation unit .cpp).
// Tutti i metodi sono definiti `inline` per permettere l'inclusione da più
// TU senza violare la ODR; nel progetto attuale l'header viene incluso solo
// dal .ino, quindi è sempre una sola TU.
// =============================================================================

inline GxEPD2_SOLUM_097c_960x672::GxEPD2_SOLUM_097c_960x672(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_EPD(cs, dc, rst, busy, HIGH, 25000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t value)
{
  clearScreen(value, 0x00, 0x00);
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t black_value, uint8_t color_value)
{
  clearScreen(black_value, color_value, 0x00);
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t black_value, uint8_t color_value, uint8_t yellow_value)
{
  writeScreenBuffer(black_value, color_value, yellow_value);
  refresh(false);
}

inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t value)
{
  writeScreenBuffer(value, 0x00, 0x00);
}

inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t black_value, uint8_t color_value)
{
  writeScreenBuffer(black_value, color_value, 0x00);
}

// Init completo dei buffer del controller: B/N (0x24), rosso (0x26) e
// giallo (0x28). Tutti e tre i canali vengono inizializzati in modo che il
// prossimo draw parta da uno stato noto, senza residui da power-on.
inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t black_value, uint8_t color_value, uint8_t yellow_value)
{
  if (!_init_display_done) _InitDisplay();
  _writeScreenBuffer(0x24, black_value);   // set black/white
  _writeScreenBuffer(0x26, color_value);   // set red/white
  _writeScreenBuffer(0x28, yellow_value);  // set yellow/white
  _initial_write = false; // initial full screen buffer clean done
  // Dopo una pulizia completa dei buffer i canali sono "clean" per definizione.
  _color_dirty = false;
  _yellow_dirty = false;
}

// Riempie un piano a schermo pieno con un valore costante. Se il controller
// sa generare il pattern da sè la scrittura non passa dal bus; il transfer
// SPI resta come fallback per i casi che il generatore non copre.
inline void GxEPD2_SOLUM_097c_960x672::_writeScreenBuffer(uint8_t command, uint8_t value)
{
  if (_fillPlaneByPattern(command, value)) return;
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(command);
  _startTransfer();
  // Bulk SPI: invece di chiamare _transfer(value) 80640 volte (full-window
  // a WIDTH*HEIGHT/8 byte), pre-riempiamo un buffer di stack con il valore
  // costante e lo scarichiamo a chunk via writeBytes(). Saving ~56 ms per
  // piano (80.640 byte, da ~1.5us a ~0.8us/byte). Percorso raggiunto solo
  // quando il pattern hardware non copre il caso: piano 0x28, oppure un
  // valore che non sia 0x00 o 0xFF.
  // Buffer 256 byte: più grande della FIFO 64-byte ESP32 così la
  // primitiva interna gestisce più write concatenate senza overhead extra.
  uint8_t buf[256];
  memset(buf, value, sizeof(buf));
  uint32_t remaining = uint32_t(WIDTH) * uint32_t(HEIGHT) / 8;
  while (remaining > 0)
  {
    uint32_t chunk = remaining > sizeof(buf) ? (uint32_t)sizeof(buf) : remaining;
    _pSPIx->writeBytes(buf, chunk);
    remaining -= chunk;
  }
  _endTransfer();
}

inline bool GxEPD2_SOLUM_097c_960x672::_fillPlaneByPattern(uint8_t command, uint8_t value)
{
  // Solo i due piani immagine hanno un comando Auto Write Pattern dedicato.
  uint8_t pattern_command;
  if (command == 0x24) pattern_command = 0x47;
  else if (command == 0x26) pattern_command = 0x46;
  else return false;
  // Il generatore emette un livello per step, quindi sa esprimere soltanto i
  // due valori a bit uniformi: qualunque altro deve passare dal bus.
  if (value != 0x00 && value != 0xFF) return false;
  /** Finestra piena impostata comunque, per lasciare area e cursore nello
   *  stesso stato in cui li lascia il percorso SPI. */
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  /** A[7] = valore del primo step, A[6:4] = 111 -> step height 680,
   *  A[2:0] = 111 -> step width 960: un unico step copre tutta la RAM nativa.
   *  Il pattern ignora la finestra di 0x44/0x45 e riempie tutti i 960x680;
   *  le 8 gate line oltre la 672 non vengono mai scandite (MUX 671). */
  _writeCommand(pattern_command);
  _writeData(value ? 0xF7 : 0x77);
  // Il controller alza BUSY per tutta la generazione del pattern.
  _waitWhileBusy("_fillPlaneByPattern", 50);
  return true;
}

// Scrive una bitmap B/W sul canale nero (0x24) lasciando gli accent puliti.
// Al primo write _writeImage richiama writeScreenBuffer() che azzera già
// tutto; nei draw successivi puliamo 0x26 / 0x28 SOLO se i rispettivi flag
// dirty sono attivi (risparmio SPI quando si incatenano draw B/N).
inline void GxEPD2_SOLUM_097c_960x672::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write)
  {
    _cleanAccentIfDirty(0x26, _color_dirty);
    _cleanAccentIfDirty(0x28, _yellow_dirty);
  }
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

inline void GxEPD2_SOLUM_097c_960x672::_writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (_initial_write) writeScreenBuffer(); // initial full screen buffer clean
  // Note: delay(1) yield WDT rimosso (target ESP32 task WDT 5s, refresh <30ms)
  uint16_t wb = (w + 7) / 8; // width bytes, bitmaps are padded
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  // Bulk SPI: invece di chiamare _transfer(byte) per-byte (overhead ~1.5us
  // per byte su ESP32 a 10 MHz), riempiamo un buffer riga (max WIDTH/8 =
  // 120 byte) e lo flushiamo via _pSPIx->writeBytes(), che usa la FIFO
  // 64-byte e raggiunge il limite teorico del clock SPI (~0.8us/byte).
  // Su 8 page x 2 canali x 10080 byte = 161.280 byte per refresh: da ~242 ms
  // a ~129 ms, saving ~113 ms (~225 ms sul refresh BWRY completo da 322.560
  // byte). Buffer dimensionato per la WIDTH max del pannello.
  const int16_t rowBytes = w1 / 8;
  uint8_t rowBuf[120]; // WIDTH(960)/8 = 120
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < rowBytes; j++)
    {
      uint8_t data;
      // use wb, h of bitmap for index!
      uint32_t idx = mirror_y ? j + dx / 8 + uint32_t((h - 1 - (i + dy))) * wb : j + dx / 8 + uint32_t(i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      rowBuf[j] = data;
    }
    _pSPIx->writeBytes(rowBuf, rowBytes);
  }
  _endTransfer();
}

// Allineato al fratello writeImage(bitmap[], ...): pulisce gli accent dirty
// prima di scrivere il piano BW, altrimenti red/yellow residui di un draw
// colorato precedente trasparirebbero sotto la zona BW disegnata in part.
inline void GxEPD2_SOLUM_097c_960x672::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write)
  {
    _cleanAccentIfDirty(0x26, _color_dirty);
    _cleanAccentIfDirty(0x28, _yellow_dirty);
  }
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

inline void GxEPD2_SOLUM_097c_960x672::_writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (_initial_write) writeScreenBuffer(); // initial full screen buffer clean
  // Note: delay(1) yield WDT rimosso (target ESP32 task WDT 5s, refresh <30ms)
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  uint16_t wb_bitmap = (w_bitmap + 7) / 8; // width bytes, bitmaps are padded
  x_part -= x_part % 8; // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w; // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h; // limit
  x -= x % 8; // byte boundary
  w = 8 * ((w + 7) / 8); // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  // Bulk SPI: stesso pattern di _writeImage (vedi commento sopra). Buffer
  // di riga di max WIDTH/8 = 120 byte, flush via writeBytes una volta per
  // riga invece di per-byte transfer().
  // Nota: nel progetto attuale il template GxEPD2_3C usa solo full-window
  // mode (setFullWindow), quindi questo overload non è in hot path. Lo
  // refactoriamo per simmetria con _writeImage.
  const int16_t rowBytes = w1 / 8;
  uint8_t rowBuf[120];
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < rowBytes; j++)
    {
      uint8_t data;
      // use wb_bitmap, h_bitmap of bitmap for index!
      uint32_t idx = mirror_y ? x_part / 8 + j + dx / 8 + uint32_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + uint32_t(y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      rowBuf[j] = data;
    }
    _pSPIx->writeBytes(rowBuf, rowBytes);
  }
  _endTransfer();
}

// HOT PATH (paged full-window): GxEPD2_3C::nextPage() in modalità full-window
// chiama questa overload (non writeImagePart) - vedi GxEPD2_3C.h:368.
// Modalità 3-colori: puliamo il canale giallo se dirty (residuo di un
// precedente draw BWRY) per evitare pixel gialli fantasma sul nuovo frame.
// Eccezione: se _preserve_yellow è true il chiamante sta proteggendo un
// giallo iniettato out-of-band (slider Weather, o pre-firstPage write); in
// tal caso NON puliamo, identico al path writeImagePart sotto.
inline void GxEPD2_SOLUM_097c_960x672::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write && !_preserve_yellow) _cleanAccentIfDirty(0x28, _yellow_dirty);
  if (black) _writeImage(0x24, black, x, y, w, h, invert, mirror_y, pgm);
  if (color)
  {
    _writeImage(0x26, color, x, y, w, h, !invert, mirror_y, pgm);
    _color_dirty = true;
  }
  // GxEPD2_3C::nextPage() ha appena flushato la page corrente sul controller
  // chiamando questo overload (vedi commento sopra). Avanza il page-hint per
  // allinearlo alla prossima iterazione del loop paged: showImage usa il hint
  // per skippare le righe sorgente fuori dalla page corrente.
  _show_image_page_hint++;
}

// HOT PATH (paged): chiamato 8 volte per refresh dal template GxEPD2_3C
// durante nextPage(). Pulisce 0x28 solo se non siamo in modalità paged BWRY
// (preserveYellow(true)) per non distruggere il canale giallo iniettato
// manualmente prima di firstPage().
inline void GxEPD2_SOLUM_097c_960x672::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write && !_preserve_yellow) _cleanAccentIfDirty(0x28, _yellow_dirty);
  if (black) _writeImagePart(0x24, black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  if (color)
  {
    _writeImagePart(0x26, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, !invert, mirror_y, pgm);
    _color_dirty = true;
  }
}

inline void GxEPD2_SOLUM_097c_960x672::writeNative(const uint8_t* data1, const uint8_t* /*data2*/, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1)
  {
    writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

inline void GxEPD2_SOLUM_097c_960x672::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

// Il reset di _preserve_yellow dopo refresh è centralizzato in _Update_Full()
// (vedi sotto). Entrambi gli overload di refresh chiamano _Update_Full, quindi
// non serve duplicare il reset qui.
inline void GxEPD2_SOLUM_097c_960x672::refresh(bool /*partial_update_mode*/)
{
  _Update_Full(); // always uses full window refresh
}

inline void GxEPD2_SOLUM_097c_960x672::refresh(int16_t /*x*/, int16_t /*y*/, int16_t /*w*/, int16_t /*h*/)
{
  _Update_Full(); // always uses full window refresh
}

inline void GxEPD2_SOLUM_097c_960x672::powerOff()
{
  _PowerOff();
}

// Porta il controller in deep sleep. Protetto contro chiamate multiple:
// se è già _hibernating la funzione non invia nuovamente la sequenza 0x10.
// I flag dirty vengono azzerati perchè al prossimo wake _InitDisplay()
// invocherà SWRESET che riporta la RAM del controller a uno stato noto.
inline void GxEPD2_SOLUM_097c_960x672::hibernate()
{
  if (_hibernating) return;
  _PowerOff();
  if (_rst >= 0)
  {
    /** Deep sleep. Il datasheet SSD1677 Rev 1.0 definisce per 0x10 solo
     *  A[1:0]=00 (normale) e A[1:0]=11 (deep sleep), e dice che in deep sleep
     *  il BUSY resta alto. Da qui 0x03, che è anche quello che usa
     *  OpenEPaperLink sulla stessa famiglia; il precedente 0x11 ha A[1:0]=01,
     *  che nella tabella non c'è. panel_diagnostic misura quale dei due alza
     *  il BUSY. Per uscire serve un HW reset, che _InitDisplay() fa già quando
     *  _hibernating è alto. */
    _writeCommand(0x10);
    _writeData(0x03);
    _hibernating = true;
    _init_display_done = false;
    _color_dirty = false;
    _yellow_dirty = false;
    // Simmetria con refresh(): se l'ibernazione interrompe un loop paged
    // BWRY prima del refresh finale, il flag rimarrebbe alto e al wake il
    // primo writeImage(black,color) salterebbe il cleanup di 0x28 anche se
    // ormai è fisicamente azzerato dal SWRESET. Reset esplicito.
    _preserve_yellow = false;
  }
}

// Imposta l'area RAM parziale del controller SSD1677.
// L'entry mode (comando 0x11) non è più inviato qui: è configurato una sola
// volta in _InitDisplay() per evitare scritture SPI ridondanti ad ogni draw.
inline void GxEPD2_SOLUM_097c_960x672::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  _writeCommand(0x44);
  _writeData(x % 256);
  _writeData(x / 256);
  _writeData((x + w - 1) % 256);
  _writeData((x + w - 1) / 256);
  _writeCommand(0x45);
  _writeData(y % 256);
  _writeData(y / 256);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeCommand(0x4e);
  _writeData(x % 256);
  _writeData(x / 256);
  _writeCommand(0x4f);
  _writeData(y % 256);
  _writeData(y / 256);
}

inline void GxEPD2_SOLUM_097c_960x672::_PowerOn()
{
  if (!_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xc0);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

inline void GxEPD2_SOLUM_097c_960x672::_PowerOff()
{
  if (_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xc3);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
}

inline void GxEPD2_SOLUM_097c_960x672::_InitDisplay()
{
  if (_hibernating) _reset();
  delay(10);
  //_waitWhileBusy("_InitDisplay", power_on_time);
  _writeCommand(0x12); //SWRESET
  delay(200); // SSD1677 needs ~100-300 ms after SWRESET before accepting commands
  //_waitWhileBusy("_InitDisplay", power_on_time);
  _writeCommand(0x0C);  // Soft start setting
  _writeData(0xAE);
  _writeData(0xC7);
  _writeData(0xC3);
  _writeData(0xC0);
  _writeData(0x80);
  /** MUX = 671 -> 672 gate lines, quante il pannello SOLUM ne ha davvero.
   *  L'upstream GDEM133Z91 programmava 679 (680 linee) perchè quel pannello
   *  è 960x680: erano 8 gate line inesistenti scandite a ogni refresh. */
  _writeCommand(0x01);  // Set MUX as 671
  _writeData(0x9F);
  _writeData(0x02);
  _writeData(0x00);
  _writeCommand(0x3C); // VBD
  _writeData(0x01); // LUT1, for white
  _writeCommand(0x18);
  _writeData(0x80);
  // Entry mode x/y increase: impostato una sola volta in init,
  // _setPartialRamArea non deve più riscriverlo ad ogni draw.
  _writeCommand(0x11);
  _writeData(0x03);
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _init_display_done = true;
}

// Esegue il ciclo di refresh elettroforetico full-window (~22 s). Il byte
// 0xF7 al cmd 0x22 attiva clock + analog + load temp + load LUT + disable
// analog + disable clock: include power-on/off implicito, perciò non serve
// chiamare _PowerOn() prima nè _PowerOff() dopo (oltre a settare il flag).
//
// Reset di _preserve_yellow: il "ciclo di rendering" termina qui (sia che
// refresh sia chiamato dal template GxEPD2_3C al fine del paged loop, sia
// che venga chiamato dal sketch per scritture single-channel). Centralizzare
// il reset qui evita di duplicarlo nei due overload di refresh().
inline void GxEPD2_SOLUM_097c_960x672::_Update_Full()
{
  _writeCommand(0x22); // Display Update Sequence Options
  _writeData(0xF7);    //
  _writeCommand(0x20); // Master Activation
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
  _preserve_yellow = false;
  _show_image_page_hint = 0;   // simmetrico al ciclo di rendering
}

// ---------------------------------------------------------------------------
// API siblings single-channel: scrivono un solo canale del controller
// (black/red/yellow) con la stessa shape. Non chiamano refresh. Convenzione
// bitmap identica a writeImage(black, color): bit=1 = pixel NON in quel
// canale; il driver applica ~data (invert=true) per gli accent rosso/giallo
// in modo che la polarity nativa SSD1677 (bit=1 = accent ON) combaci.
// ---------------------------------------------------------------------------
inline void GxEPD2_SOLUM_097c_960x672::writeImageBlack(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x24, bitmap, x, y, w, h, false, false, pgm);
  // canale black non ha dirty flag: viene sempre riscritto a ogni frame.
}

inline void GxEPD2_SOLUM_097c_960x672::writeImageRed(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x26, bitmap, x, y, w, h, true, false, pgm);
  _color_dirty = true;
}

/** Canale giallo mai verificato sul pannello: nel datasheet SSD1677 Rev 1.0 la
 *  posizione 0x28 è VCOM Sense e non un piano immagine. Vedi la nota in cima
 *  al file; l'esito lo dà panel_diagnostic. */
inline void GxEPD2_SOLUM_097c_960x672::writeImageYellow(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x28, bitmap, x, y, w, h, true, false, pgm);
  _yellow_dirty = true;
}

// ---------------------------------------------------------------------------
// Pulizia selettiva di un canale accent.
//
// SSD1677 RAM polarity (datasheet Rev 1.0, tabella comandi, verbatim):
//   cmd 0x24 Write RAM (Black White): bit=1 -> pixel white, bit=0 -> black
//   cmd 0x26 Write RAM (RED):         bit=1 -> red, bit=0 -> non-red
//   cmd 0x28 nel datasheet è VCOM Sense e non un piano: il cleanup su 0x28
//            pulisce una RAM solo se il modulo ne ha davvero una lì.
//
// Le bitmap in input alle API writeImage* adottano convenzione inversa
// (bit=1 = NOT color) per comodità visiva e compatibilità con il formato
// dello script python / image2cpp; il driver applica ~data prima del
// transfer SPI negli accent. Il cleanup scrive invece DIRETTAMENTE 0x00 =
// polarity nativa "accent spento", senza invert. Era 0xFF in versioni
// precedenti -> equivaleva a "accent ON ovunque" (bug latente mascherato
// dal SWRESET a ogni wake hibernate).
// ---------------------------------------------------------------------------
inline void GxEPD2_SOLUM_097c_960x672::_cleanAccentIfDirty(uint8_t command, bool& dirty_flag)
{
  if (dirty_flag)
  {
    _writeScreenBuffer(command, 0x00);
    dirty_flag = false;
  }
}

#endif
