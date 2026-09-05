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
//   - Colori: bianco, nero e rosso, tutti e tre misurati sul pannello e
//     pilotati dai comandi 0x24 (BW plane) e 0x26 (accent) del controller
//     SSD1677. Un quarto colore non esiste: vedi la sezione sotto.
//   - Refresh: solo full refresh, 24007 ms di BUSY misurati a temperatura
//     ambiente e di più al freddo, niente fast partial.
//   - Controller: SSD1677, indirizzamento full-window a finestra parziale.
//   - Alimentazione: 3.3V su VCC e su tutte le data line (non 5V-tolerant).
//
// PARAMETRI MISURATI, DA NON RITOCCARE A OCCHIO.
//   Tutti i numeri di questo driver vengono da examples/097c/panel_diagnostic,
//   e i conti sono già stati fatti: rifarli senza una nuova misura è tempo
//   perso.
//     - full_refresh_time 60000 ms: è solo il delay() di fallback per un
//       display costruito senza pin BUSY, e deve coprire il banco freddo.
//     - _busy_timeout 120 s, passato al costruttore. Non è sovradimensionato:
//       la passata con temperatura forzata a 0 °C non si è chiusa entro 40 s,
//       cioè entro il valore che stava qui prima, e allo scadere del timeout il
//       frame usciva troncato. È una guardia, non un'attesa.
//     - Banchi di waveform per temperatura: l'OTP ne ha più di uno, ma nel
//       range utile il guadagno è ~1 s su 24 (40 °C danno 23739 ms contro i
//       24795 di 20 °C) mentre verso il freddo la waveform si allunga. Forzare
//       la temperatura con 0x18/0x1A non è una leva: 0x18 = 0x80, sensore
//       interno, resta la scelta giusta.
//     - Blocchi SPI da 120 byte per riga in _writeImage: 0,879 us/byte, il 91%
//       del limite teorico a 10 MHz. A blocchi da 1024 si arriva al 92%, cioè
//       0,6 ms su 71: irrilevante, e costerebbe un buffer otto volte più grande.
//     - Pattern hardware 0x47/0x46 per riempire un piano: 8-9 ms contro i
//       70-72 del push SPI, 7,8x.
//     - Il refresh non si accorcia in nessun modo: vedi _Update_Full().
//     - hasFastPartialUpdate = false non è prudenza: il differenziale dell'OTP
//       non esiste, misurato. La strada della LUT breve scritta dall'MCU via
//       0x32 è invece ACCETTATA dal silicio — 640 ms di BUSY con il bit 4 di
//       0x22 spento, e 4055 ms nel probe dei livelli, quindi l'OTP non viene
//       ricaricato sopra la LUT custom — ma se quella waveform dipinga davvero
//       si legge solo sul vetro, e finchè quella lettura non c'è resta false.
//   Una cosa che il log dice e che vale fuori da qui: l'init di fabbrica SOLUM
//   riempie i pattern in 1 ms invece di 8-9, ma usa entry mode 0x02 e finestra
//   X da 959 a 0. Sono 16 ms per frame contro il rischio di specchiare
//   l'immagine: non conviene.
//
// TRE COLORI, MISURATI: NESSUN CANALE GIALLO.
//   Il driver pilota due piani, 0x24 (BW) e 0x26 (accent), e non ne esiste un
//   terzo. La questione è stata chiusa da examples/097c/panel_diagnostic, che
//   ha esercitato tutte e quattro le combinazioni dei due piani sotto la
//   waveform di produzione, e sei evidenze indipendenti concordano:
//
//     1. Codice modello dell'unità, letto sul case: EL097R2CRN (pratica FCC
//        2AFWN-EL097R2CRN, certificazione KC R-R-SLU-EL097R2CRN). Il campo
//        colore display è R, che nella nomenclatura SOLUM (docs/fonti_esterne.md)
//        vale BWR; la linea PRO a quattro colori porta invece la cifra 4.
//        Non è il donor EL097F5C4C: è la generazione R2, precedente.
//     2. Le quattro LUT della Table 6-4, tutte viste sul vetro: (0,0) nero
//        LUT0, (0,1) bianco LUT1, (1,0) e (1,1) ENTRAMBE ROSSE, cioè LUT2 e
//        LUT3 rendono lo stesso colore come la tabella dichiara. Con due bit
//        per pixel le combinazioni sono esaurite: non c'è un quinto stato.
//     3. 0x28 è VCOM Sense, non un piano immagine: alla scrittura alza il
//        BUSY per ~10 s (misurati 9953-9968 ms) e non dipinge niente. Il
//        datasheet SSD1677 Rev 1.0 lo dà per tale, la misura lo conferma.
//     4. OpenEPaperLink (docs/openepaperlink/) cataloga il modulo come
//        SOLUM_M3_BWR_97, e la tabella UICR dei tag di fabbrica
//        (nrf52811_tag_fw/tagtype_db.cpp) dà la 9.7" con terzo colore = 0x01,
//        che in quella scala significa BWR (0x02 = giallo, 0x03 = BWRY).
//     5. Sulla linea grande di Good Display, a parità di risoluzione e
//        connettore, i 3 colori stanno su SSD1677 e i 4 su SSD2677
//        (GDEM102Z91 BWR contro GDEM102F91 BWRY, entrambi 960x640 su FPC 24
//        pin).
//     6. I pannelli a 4 colori non usano tre piani da 1 bit: scrivono un solo
//        stream 0x10 a 2 bit per pixel, come il path epdvarbwry di OEPL. Su
//        SSD1677 quel codice è invece Deep Sleep, quindi quella strada qui è
//        fisicamente esclusa.
//
//   Conseguenza pratica per chi compone immagini: poichè LUT2 = LUT3, sotto un
//   pixel di accent il valore del piano BW è INDIFFERENTE. Scrivere l'accent
//   non richiede di mascherare 0x24, e writeImageRed() non lo fa.
//
//   Un residuo da non riaprire per errore: il datasheet SOLUM della linea PRO
//   dichiara PIXEL COLORS = BWRY per la taglia 9.7", ma riguarda la linea PRO
//   (campo colore 4) e non questa unità. Restano fuori portata la LUT4 del
//   silicio, che due bit di RAM non sanno indirizzare, e una waveform custom
//   via 0x32 che separi i due rossi per livello di sorgente: quest'ultima è
//   l'unico test residuo, e la sonda la esegue.
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
//     descrittori BW / BWR e va chiamata dentro un loop paged
//     firstPage()/nextPage() del template GxEPD2_3C. Un descrittore a tre
//     piani è accettato e ne rende i primi due.
//   - 2 API siblings writeImageBlack / writeImageRed per scrittura
//     single-channel diretta sul controller (no GFX), per compositing manuale
//     fuori dal loop paged.
//
// display.drawPixel(x, y, GxEPD_YELLOW) FINISCE SUL ROSSO, ED È CORRETTO:
//   il template upstream GxEPD2_3C tratta GxEPD_YELLOW come GxEPD_RED, in
//   GxEPD2_3C.h il drawPixel ha la condizione
//     else if ((color == GxEPD_RED) || (color == GxEPD_YELLOW))
//       _color_buffer[i] = ... // scrive nel piano red
//   e su questo pannello è l'unico esito possibile, perchè un terzo colore non
//   c'è. Non è più una trappola da aggirare: chi scrive GxEPD_YELLOW ottiene
//   il solo accent che il film ha.
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

// Pinout uniforme fra i driver della libreria.
#include "GxEPD2_SOLUM_Pins.h"

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
    /** Refresh pieno misurato sul pannello: 24007 ms di BUSY a temperatura
     *  ambiente, vedi examples/097c/panel_diagnostic.
     *  Attenzione: _waitWhileBusy usa questo valore soltanto come delay() di
     *  fallback quando il pin BUSY non è cablato (busy < 0). Con il BUSY
     *  presente il timeout che conta è _busy_timeout, passato al costruttore.
     *  60000 perchè il fallback deve coprire lo stesso banco di waveform freddo
     *  del _busy_timeout: a 0 °C il refresh non si chiude entro 40 s. Il tipo è
     *  uint16_t, quindi 65535 è il massimo esprimibile. */
    static const uint16_t full_refresh_time = 60000;
    // Nessun fast partial update: il refresh su finestra ripassa la stessa
    // waveform completa, misurata identica su una fascia di 168 righe, sulla
    // stessa fascia ristretta anche in X e su 48 righe con Mode 1.
    static const uint16_t partial_refresh_time = 60000;
    // constructor
    GxEPD2_SOLUM_097c_960x672(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    /**
     * Costruttore a pinout uniforme: accetta la struct comune ai driver della
     * libreria e legge i quattro campi che servono a questo pannello. Di cs2 e
     * busy2 non ha nulla da fare (single-controller) e i pin del bus li apre
     * lo sketch via selectSPI(), quindi anche quelli vengono ignorati.
     * È la firma che permette a uno sketch di cambiare pannello senza
     * riscrivere la riga di costruzione del display.
     */
    explicit GxEPD2_SOLUM_097c_960x672(const GxEPD2_SOLUM_Pins& pins);
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void clearScreen(uint8_t black_value, uint8_t color_value); // init controller memory and screen
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value); // init controller memory
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
    // Stesso shape per i due piani del controller:
    //   writeImageBlack -> cmd 0x24 (black/white plane, no invert)
    //   writeImageRed   -> cmd 0x26 (accent, invert applicato)
    //
    // Convenzione bitmap input: bit=1 dove il pixel NON appartiene a quel
    // canale (stesso formato prodotto da epd_image_converter.pyw e
    // image2cpp). Il driver applica ~data prima del transfer per l'accent
    // per allinearsi alla polarity del controller (bit=1 nativo = accent ON).
    //
    // Servono al compositing manuale fuori dal loop paged, dove il template
    // GxEPD2_3C non arriva. NON chiamano refresh: è responsabilità del
    // chiamante.
    // ------------------------------------------------------------------
    void writeImageBlack (const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);
    void writeImageRed   (const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);

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
    // Pulizia del piano accent: se _color_dirty è attivo scrive 0x00 ovunque
    // (polarity nativa SSD1677 = "accent spento") e resetta il flag.
    // Centralizza la semantica "clean accent" per evitare il bug latente
    // 0xFF (= accent ON ovunque) che esisteva in versioni precedenti.
    void _cleanColorIfDirty();

    void _writeScreenBuffer(uint8_t command, uint8_t value);

    /** Riempimento di un piano immagine tramite i comandi Auto Write RAM for
     *  Regular Pattern del SSD1677 (0x47 per 0x24, 0x46 per 0x26): il pattern
     *  lo genera il controller, quindi sul bus va un solo byte invece di
     *  80.640 e il piano si riempie in 7-8 ms invece di 70-72, misurati sul
     *  pannello. È la stessa coppia di comandi che il firmware SOLUM di
     *  fabbrica usa in init.
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

    // Dirty flag del piano accent: traccia quando la RAM 0x26 contiene dati
    // non puliti dall'ultima writeScreenBuffer(). Permette di saltare il
    // clean pre-draw quando non serve, tipicamente in una catena di immagini
    // B/N consecutive. Il clean costa 8-9 ms misurati, perchè lo fa il
    // pattern hardware del controller e non il bus.
    bool _color_dirty = false;   // 0x26 (accent)

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
  /** Il sesto argomento di GxEPD2_EPD è _busy_timeout in MICROSECONDI, non una
   *  frequenza SPI: è il tempo oltre il quale _waitWhileBusy smette di attendere
   *  il BUSY, stampa "Busy Timeout!" e prosegue. Allo scadere _Update_Full torna
   *  con il pannello ancora in pilotaggio, e il powerOff() o l'hibernate() che
   *  seguono mandano 0x22 o 0x10 a metà waveform: frame troncato.
   *
   *  Il refresh pieno a temperatura ambiente misura 24007 ms di BUSY, ma la
   *  waveform non è una sola: l'OTP tiene un banco per range di temperatura, e
   *  verso il freddo si ALLUNGA. La sonda con la temperatura forzata a 0 °C non
   *  si è chiusa entro 40 s, quindi i 40 s che stavano qui non coprivano il
   *  pannello che lavora al freddo, che è il caso d'uso di una dashboard meteo.
   *  120 s sono una guardia e non un'attesa: se il BUSY scende prima
   *  _waitWhileBusy esce sul pin e il valore non costa niente. La durata vera
   *  del banco freddo la darà il prossimo run della sonda, che ora la misura
   *  invece di scadere. */
  GxEPD2_EPD(cs, dc, rst, busy, HIGH, 120000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

inline GxEPD2_SOLUM_097c_960x672::GxEPD2_SOLUM_097c_960x672(const GxEPD2_SOLUM_Pins& pins) :
  GxEPD2_SOLUM_097c_960x672(pins.cs, pins.dc, pins.rst, pins.busy)
{
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t value)
{
  clearScreen(value, 0x00);
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t black_value, uint8_t color_value)
{
  writeScreenBuffer(black_value, color_value);
  refresh(false);
}

inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t value)
{
  writeScreenBuffer(value, 0x00);
}

// Init dei due buffer del controller: B/N (0x24) e accent (0x26). Sono i soli
// piani immagine che il pannello ha, quindi qui finisce tutta la RAM che il
// driver conosce. Entrambi si riempiono col pattern hardware, 8-9 ms per piano
// invece dei 70-72 del bus.
inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t black_value, uint8_t color_value)
{
  if (!_init_display_done) _InitDisplay();
  _writeScreenBuffer(0x24, black_value);   // set black/white
  _writeScreenBuffer(0x26, color_value);   // set accent
  _initial_write = false; // initial full screen buffer clean done
  // Dopo una pulizia completa dei buffer il piano accent è "clean" per definizione.
  _color_dirty = false;
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
  // costante e lo scarichiamo a chunk via writeBytes(). Misurato: 0,89 us/byte
  // a blocchi di 256, cioè 70-72 ms per piano, il 91% del limite teorico del
  // clock a 10 MHz. Entrambi i piani del driver hanno il pattern hardware,
  // quindi questo percorso serve solo a un valore che non sia 0x00 o 0xFF: nel
  // firmware non capita mai, ed è qui perchè writeScreenBuffer è pubblica.
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
  // I due piani immagine, cioè tutti quelli del driver, hanno un comando Auto
  // Write Pattern dedicato. Il ramo di uscita resta come guardia: qualunque
  // altro comando non è un piano e non va riempito per pattern.
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

// Scrive una bitmap B/W sul canale nero (0x24) lasciando l'accent pulito.
// Al primo write _writeImage richiama writeScreenBuffer() che azzera già
// tutto; nei draw successivi 0x26 si pulisce SOLO se il flag dirty è attivo
// (risparmio SPI quando si incatenano draw B/N).
inline void GxEPD2_SOLUM_097c_960x672::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write) _cleanColorIfDirty();
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

// Allineato al fratello writeImage(bitmap[], ...): pulisce l'accent dirty
// prima di scrivere il piano BW, altrimenti il rosso residuo di un draw
// colorato precedente trasparirebbe sotto la zona BW disegnata in part.
inline void GxEPD2_SOLUM_097c_960x672::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write) _cleanColorIfDirty();
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
// Qui non serve nessun cleanup preliminare: il template passa entrambi i piani
// del pannello a ogni page, quindi 0x26 viene riscritto per intero e un
// residuo di accent non può sopravvivere.
inline void GxEPD2_SOLUM_097c_960x672::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
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
// durante nextPage() in modalità partial window. Come il fratello a full
// window non fa cleanup: entrambi i piani arrivano dal template.
inline void GxEPD2_SOLUM_097c_960x672::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
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

// Entrambi gli overload passano da _Update_Full, che dichiara la finestra
// piena: la finestra RAM limita l'area ridipinta ma non accorcia la waveform —
// misurata identica su una fascia di 168 righe, sulla stessa ristretta anche in
// X e su 48 righe con Mode 1 — quindi un refresh d'area non ha niente da
// guadagnare, e l'overload con le coordinate ridipinge tutto lo schermo.
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
// Il flag dirty viene azzerato perchè al prossimo wake _InitDisplay()
// invocherà SWRESET che riporta la RAM del controller a uno stato noto.
inline void GxEPD2_SOLUM_097c_960x672::hibernate()
{
  if (_hibernating) return;
  _PowerOff();
  if (_rst >= 0)
  {
    /** Deep sleep, verificato sul pannello end-to-end: il controller diventa
     *  sordo anche a un refresh intero, il risveglio costa 252 ms di reset più
     *  init e il frame successivo stampa. Il datasheet SSD1677 Rev 1.0
     *  definisce per 0x10 solo A[1:0]=00 (normale) e A[1:0]=11 (deep sleep),
     *  e dice che in deep sleep il BUSY resta alto: da qui 0x03, che è anche
     *  quello che usa OpenEPaperLink sulla stessa famiglia. Il precedente 0x11
     *  ha A[1:0]=01, che nella tabella non c'è, e alzava il BUSY come 0x03
     *  senza però essere un valore definito. Per uscire serve un HW reset, che
     *  _InitDisplay() fa già quando _hibernating è alto. */
    _writeCommand(0x10);
    _writeData(0x03);
    _hibernating = true;
    _init_display_done = false;
    _color_dirty = false;
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
  /** Il SWRESET si attende sul BUSY, che il pannello alza e riabbassa in 2 ms
   *  misurati: al posto dei 200 ms fissi che c'erano prima, in gran parte
   *  attesa a vuoto. I 10 ms che seguono sono il margine dopo la discesa, ed
   *  è la stessa forma che usa il driver GDEM133T91 di GxEPD2 sullo stesso
   *  silicio. Il timeout passato qui vale solo se il BUSY non è cablato. */
  _waitWhileBusy("_InitDisplay SWRESET", 200);
  delay(10);
  /** Soft start. L'ultimo byte è 0x80 e non 0x40 come in GxEPD2 GDEH116T91:
   *  0x80 è il valore dell'init di fabbrica SOLUM ed è anche quello che Good
   *  Display scrive sui propri pannelli BWR con questo controller
   *  (docs/097c/gooddisplay_GDEM102Z91_arduino/). */
  _writeCommand(0x0C);  // Soft start setting
  _writeData(0xAE);
  _writeData(0xC7);
  _writeData(0xC3);
  _writeData(0xC0);
  _writeData(0x80);
  /** MUX = 671 -> 672 gate lines, quante il pannello SOLUM ne ha davvero.
   *  L'upstream GDEM133Z91 programmava 679 (680 linee) perchè quel pannello
   *  è 960x680: erano 8 gate line inesistenti scandite a ogni refresh.
   *  680 è anche il massimo assoluto del controller, e in commercio viene usato
   *  tutto: il Good Display GDEM133T91 è 960x680 con un solo SSD1677 e
   *  programma MUX = 679. */
  _writeCommand(0x01);  // Set MUX as 671
  _writeData(0x9F);
  _writeData(0x02);
  _writeData(0x00);
  _writeCommand(0x3C); // VBD
  // LUT1 = bianco nella Table 6-4 del datasheet; la stessa numerazione la
  // commenta così il demo Good Display per un BWR su questo controller.
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

// Esegue il ciclo di refresh elettroforetico full-window, 24007 ms di BUSY
// misurati a temperatura ambiente. Il byte 0xF7 al cmd 0x22 attiva clock +
// analog + load temp + load LUT + DISPLAY Mode 1 + disable analog + disable
// clock: include power-on/off implicito, perciò non serve chiamare _PowerOn()
// prima nè _PowerOff() dopo (oltre a settare il flag).
//
// La durata è quella della waveform in OTP e non si comprime: misurate
// identiche a 23.8-24.0 s anche Mode 2 (0xFF), display senza ricarica di LUT
// (0xCF, 0xC7) e il differenziale 0xFC, quest'ultimo con scarto di 2 ms fra
// piani identici e piani opposti. Il controller non confronta i due piani.
//
// La finestra piena viene dichiarata qui, e non ereditata: la sonda ha misurato
// che i registri 0x44/0x45 confinano l'area ridipinta con 0x22=0xFC, e
// all'ingresso la finestra è quella lasciata dall'ultima scrittura — in un loop
// paged del template GxEPD2_3C è l'ultima page. Che anche Mode 1, cioè la
// sequenza usata qui, confini o no non è ancora stato letto sul vetro: il
// driver di riferimento upstream GxEPD2_1330c_GDEM133Z91 non dichiara la
// finestra e stampa correttamente, e così faceva questo prima. È quindi
// prudenza e non la correzione di un guasto osservato, ma toglie di mezzo la
// dipendenza da un comportamento non documentato. Costa 250 us su 24 s.
inline void GxEPD2_SOLUM_097c_960x672::_Update_Full()
{
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x22); // Display Update Sequence Options
  _writeData(0xF7);    //
  _writeCommand(0x20); // Master Activation
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
  _show_image_page_hint = 0;   // simmetrico al ciclo di rendering
}

// ---------------------------------------------------------------------------
// API siblings single-channel: scrivono un solo piano del controller con la
// stessa shape. Non chiamano refresh. Convenzione bitmap identica a
// writeImage(black, color): bit=1 = pixel NON in quel canale; il driver
// applica ~data (invert=true) sull'accent in modo che la polarity nativa
// SSD1677 (bit=1 = accent ON) combaci.
// ---------------------------------------------------------------------------
inline void GxEPD2_SOLUM_097c_960x672::writeImageBlack(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x24, bitmap, x, y, w, h, false, false, pgm);
  // canale black non ha dirty flag: viene sempre riscritto a ogni frame.
}

/** Scrive il piano accent. Non maschera 0x24 sotto i pixel accesi, e non
 *  serve: la Table 6-4 dà LUT2 = LUT3 e la misura sul pannello lo conferma,
 *  cioè con l'accent a 1 il valore del piano BW non cambia il colore reso. */
inline void GxEPD2_SOLUM_097c_960x672::writeImageRed(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x26, bitmap, x, y, w, h, true, false, pgm);
  _color_dirty = true;
}

// ---------------------------------------------------------------------------
// Pulizia del piano accent.
//
// SSD1677 RAM polarity (datasheet Rev 1.0, tabella comandi, verbatim):
//   cmd 0x24 Write RAM (Black White): bit=1 -> pixel white, bit=0 -> black
//   cmd 0x26 Write RAM (RED):         bit=1 -> red, bit=0 -> non-red
//
// Le bitmap in input alle API writeImage* adottano convenzione inversa
// (bit=1 = NOT color) per comodità visiva e compatibilità con il formato
// dello script python / image2cpp; il driver applica ~data prima del
// transfer SPI sull'accent. Il cleanup scrive invece DIRETTAMENTE 0x00 =
// polarity nativa "accent spento", senza invert. Era 0xFF in versioni
// precedenti -> equivaleva a "accent ON ovunque" (bug latente mascherato
// dal SWRESET a ogni wake hibernate).
// ---------------------------------------------------------------------------
inline void GxEPD2_SOLUM_097c_960x672::_cleanColorIfDirty()
{
  if (_color_dirty)
  {
    _writeScreenBuffer(0x26, 0x00);
    _color_dirty = false;
  }
}

#endif
