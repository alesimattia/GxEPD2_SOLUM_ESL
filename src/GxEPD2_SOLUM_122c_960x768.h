// =============================================================================
// Driver custom per pannello e-paper SOLUM 12.2" 960x768 BWR su ESP32.
//
// Origine:
//   - Libreria base: GxEPD2 (https://github.com/ZinggJM/GxEPD2).
//   - Scheletro strutturale dual-controller: GxEPD2_1248c (ScreenPart,
//     _writeCommandAll, _waitWhileAnyBusy, dispatch outer-class). Per
//     maggiori informazioni: src/epd3c/GxEPD2_1248c.h e GxEPD2_1248c.cpp.
//   - Command set SSD16xx: GxEPD2_SOLUM_097c_960x672.h (driver SOLUM 9.7"
//     del progetto) e lo stock GxEPD2_1160c_GDEY116Z91, che concordano su
//     init, piani RAM, addressing, power e refresh.
//   - Custom features (showImage page-hint, bulk-SPI writeBytes,
//     _cleanAccentIfDirty): portate dal driver SOLUM 9.7".
//
// Pannello pilotato:
//   - Produttore: SOLUM, modulo ESL 12.2" riusato. La diagonale esiste in due
//     linee, Newton PRO (modello EL122H6W4A, etichetta di fabbrica
//     "Newton PRO 12.2" BWR normal") e Newton Core / M3 (etichetta
//     "M3 12.2" NEWTON BWR Normal"), con lo stesso pannello: cambiano scheda
//     tag e guscio. Quale sia un dato esemplare lo dice l'etichetta sul vetro.
//   - Risoluzione: 768x960 nativi, pilotati in landscape come 960x768.
//   - Colori: 3 colori nativi (bianco / nero / rosso). Niente giallo,
//     a differenza del driver SOLUM 9.7" del progetto.
//   - Connettività: 2 code FFC, una per controller, al centro dei due bordi
//     lunghi del pannello.
//   - Refresh: solo full refresh, misurato in 18.3 s di BUSY e 19.1 s dalla
//     master activation alla fine del ciclo. Niente fast partial update, e
//     nemmeno partial d'area: vedi hasFastPartialUpdate.
//
// Controller e geometria (misurati, vedi docs/122c/identificazione_pannello.md):
//   Ogni controller pilota 960x384 e lo split cade sull'asse corto del
//   pannello: in coordinate driver sono due bande orizzontali, righe 0..383
//   al master e 384..767 allo slave. Il silicio è SSD16xx: i 960 source
//   dell'SSD1677 coincidono con l'asse lungo, i suoi 680 gate non bastano per
//   768 linee e da qui i due controller da 384 gate ciascuno. Che 680 sia un
//   tetto vero, e non un valore prudenziale, lo conferma il mercato: il Good
//   Display GDEM133T91 è 960x680 con UN SOLO SSD1677 e una sola coda, e
//   programma MUX = 679. Con due soli controller l'UC8179 (800x600) non copre
//   il pannello in nessuna spartizione, quindi il suo command set non è
//   utilizzabile.
//
// !!! ORIENTAMENTO DELLE DUE BANDE (da verificare al bring-up):
//   Le due code escono da bordi opposti, quindi il secondo controller è
//   presumibilmente ruotato di 180° rispetto al primo. Qui il ribaltamento vive
//   nel data path: ordine delle righe e dei byte invertito, bit invertiti dentro
//   il byte, finestra RAM riposizionata di conseguenza. Default: master normale,
//   slave ruotato di 180°. Se l'immagine esce specchiata su una delle due bande,
//   le quattro combinazioni si provano dallo sketch con setMasterMirror() /
//   setSlaveMirror() senza toccare questo file. Se poi le misure dicessero che la
//   seconda banda ha lo stesso verso della prima, il default corretto diventa
//   setSlaveMirror(false, false).
//
//   Precisazione su cosa il controller può fare da sè, perchè le due cose
//   venivano confuse: è vero che non esiste una reverse scan delle GATE (cmd
//   0x01, bit TB = 1 è dichiarato Reserved), ma il CONTATORE DI INDIRIZZO sì,
//   si può far decrementare su entrambi gli assi — cmd 0x11, A[1:0]: 00 = Y e X
//   decrement, 01 = Y decrement X increment, 10 = Y increment X decrement, 11 =
//   entrambi increment (POR). È così che GxEPD2 upstream specchia una delle due
//   metà del Good Display GDEY0579Z93, che è un pannello a due controller della
//   stessa famiglia: entry mode diverso per chip e coordinate rimappate, senza
//   toccare i dati. Se il bring-up conferma quella strada si risparmiano il
//   reverse dei byte e dei bit per ogni riga; resta da verificare sul pannello
//   che l'ordine dei bit dentro il byte torni da sè, come là.
//
// Requisiti build:
//   - HW SPI (HSPI su ESP32 tramite la Waveshare E-Paper ESP32 Driver Board
//     o cablaggio equivalente).
//   - Target ESP32 (Arduino core): le ottimizzazioni bulk-SPI usano
//     SPI.writeBytes() che è specifica ESP32.
//   - Adafruit_GFX opzionale (se ENABLE_GxEPD2_GFX=0 il footprint è minore).
//
// Aggiunte custom rispetto alla base GxEPD2 (riprese dal driver SOLUM 9.7"):
//   - GxEPDImage::showImage(display, descriptor) come UNICO entry-point
//     pubblico per stampare un'immagine. Free function template (vive nel
//     namespace GxEPDImage del .h, non come metodo classe) che accetta
//     descrittori BW / BWR e va chiamata dentro un loop paged
//     firstPage()/nextPage() del template GxEPD2_3C.
//   - 2 API siblings writeImageBlack / writeImageRed per scrittura
//     single-channel diretta sul controller (no GFX).
//   - Bulk-SPI con writeBytes() invece di SPI.transfer() per-byte: saving
//     ~290 ms per refresh full-screen (vedi commento _writeScreenBuffer
//     e _writeImage).
//   - Page-tracking di showImage tramite hint counter: skip a priori delle
//     righe sorgente fuori dalla page corrente del template GxEPD2_3C.
//
// CONVIVENZA CON GLI ALTRI DRIVER DELLA LIBRERIA:
//   Il namespace GxEPDImage sta in src/GxEPDImage.h, incluso da tutti i
//   driver: includere due header driver nella stessa translation unit non
//   ridefinisce niente. Il contratto che quel namespace chiede sono due soli
//   metodi, setPaged() e showImagePageHint(): showImage() compone i piani
//   black e red e non tocca un eventuale terzo piano, quindi nessun driver
//   deve dichiarare primitive che non gli servono. Il namespace condiviso
//   dichiara anche il formato FORMAT_BWRY_1BPP, di cui showImage() rende i
//   primi due piani: un descrittore a tre piani è quindi stampabile qui
//   senza rami condizionali, con il terzo ignorato.
//
// !!! STATO DEL BRING-UP:
//   Una sola banda è stata validata sul pannello: con un solo FFC cablato si
//   stampa correttamente un rettangolo 960x384, ed è la metà che il firmware di
//   produzione già disegna. Delle due code del pannello risponde la LUNGA, e la
//   metà che stampa è quella dal suo lato; la corta, infilata nello stesso
//   connettore e con lo stesso codice, non stampa niente. "Master" in questo
//   file è quindi il controller della coda lunga. Da qui vengono il command
//   set e la geometria di questo file. Non è ancora validato niente di ciò che
//   riguarda le due bande insieme: la coda corta non risponde, quindi il
//   dispatch a due controller, l'orientamento relativo delle bande e la
//   giunzione fra loro restano da provare sul pannello.
//
//   !!! IL MODELLO A DUE CHIP SELECT NON È PIÙ CABLATO NEL FILE.
//   Le evidenze raccolte (docs/fonti_esterne.md §4 e
//   docs/122c/identificazione_pannello.md §5-§6) dicono che i due controller
//   sono con ogni probabilità una coppia in CASCADE: un solo chip select, lo
//   slave indirizzato sommando 0x80 all'opcode, il master messo in cascade da
//   0x21 con B[4] = 1, e soprattutto lo slave senza oscillatore nè booster, che
//   riceve clock e tensioni dal master. Se examples/12_2c/dual_panel_finder lo
//   conferma, di questo file cambiano il dispatch (niente _cs_s, niente
//   _writeCommandAll con due CS bassi) e il modo di indirizzare lo slave, e la
//   base di riscrittura diventa GxEPD2_579c_GDEY0579Z93 di GxEPD2 invece dello
//   scheletro 1248c. Per non dover scegliere prima della misura, il modello di
//   indirizzamento è ora RUNTIME: setAddressingMode(ADDRESSING_CASCADE) fa
//   passare il driver a un solo chip select con lo slave a opcode|0x80 e il
//   master messo in cascade da 0x21, senza toccare nè la geometria nè il data
//   path. Il default resta ADDRESSING_DUAL_CS, cioè il comportamento storico:
//   la misura decide quale dei due tenere, non questo file.
//
//   Quanto il modello sia in bilico lo dice il confronto con un pannello a due
//   code di catalogo, il 12.48" GDEY1248Z51 su cui è modellato lo scheletro di
//   questo file: lì i controller sono quattro e hanno QUATTRO chip select
//   (CSB_M1/M2/S1/S2), quattro BUSY, due DC, due RST, due BS e due sezioni di
//   boost indipendenti, una per coda. Il tag di fabbrica della 12.2" ha un solo
//   chip select e una sola sezione analogica, con il secondo connettore nudo
//   alimentato da un fascio di piste che arriva dal primo. Un pannello a
//   controller indipendenti quel conto dei pin non lo fa: è l'argomento più
//   forte a favore della cascade, e quindi contro il modello a due CS di questo
//   file.
//
// BUS SPI:
//   le primitive di bus passano da _pSPIx / _spi_settings della base
//   GxEPD2_EPD, comprese quelle delle ScreenPart, che ne tengono un
//   riferimento. Il default che i costruttori impostano è l'oggetto SPI
//   globale a 20 MHz, cioè quello che il driver usava da sempre; uno
//   selectSPI(hspi, SPISettings(...)) dello sketch lo sostituisce, come sul
//   driver 9.7". Va chiamato prima di init(), perchè è init() che apre il bus.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef _GxEPD2_SOLUM_122c_960x768_H_
#define _GxEPD2_SOLUM_122c_960x768_H_

#include <GxEPD2_EPD.h>

// Sistema di descrittori immagine e showImage(): condivisi con gli altri driver
// della libreria, vedi src/GxEPDImage.h. Il ramo FORMAT_BWRY_1BPP non scatta su
// questo pannello, che ha due soli piani: lo escludono il formato del
// descrittore e le no-op del giallo dichiarate da questa classe.
#include "GxEPDImage.h"

// Pinout uniforme fra i driver della libreria.
#include "GxEPD2_SOLUM_Pins.h"

// ===========================================================================
// Classe driver.
// ===========================================================================
class GxEPD2_SOLUM_122c_960x768 : public GxEPD2_EPD
{
  public:
    // attributi
    static const uint16_t WIDTH = 960;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 768;
    /** Identificatore preso a prestito, non una dichiarazione di modello: il
     *  pannello è un SOLUM 12.2" 960x768 e non esiste un enum per lui.
     *  GxEPD2.h è upstream e non si tocca, quindi si riusa il valore di un
     *  pannello SSD1677, come fa già il driver 9.7" della libreria.
     *  GxEPD2::Panel non ha usi funzionali nei template (l'unico confronto è
     *  su GDEW0154Z04), quindi la scelta non ha effetti sul comportamento. */
    static const GxEPD2::Panel panel = GxEPD2::GDEM133Z91;
    static const bool hasColor = true;
    static const bool hasPartialUpdate = true; // partial window addressing, full window refresh
    /** Falso per misura, non per prudenza. examples/12_2c/dual_panel_finder ha
     *  cronometrato le passate su finestra: 0x22 = 0xFC su 64 righe 18167 ms e
     *  su 24 righe 18170 ms, 0x22 = 0xF4 su 32 righe 18158 ms, contro i 18308 ms
     *  di un frame intero. La durata della waveform non dipende dalle gate line
     *  coinvolte, quindi un refresh d'area non comprerebbe tempo e i due
     *  overload di refresh() restano giustamente su _Update_Full. */
    static const bool hasFastPartialUpdate = false;
    /** Tempi misurati sul pannello, sul controller che risponde: BUSY alto per
     *  82 ms sul power on (0x22 = 0xC0) e 221 ms sul power off (0x22 = 0xC3).
     *  Sono gli stessi valori annotati nel driver SOLUM 9.7" della libreria,
     *  che gira sullo stesso silicio. Contano quando il pin BUSY non c'è,
     *  perchè allora sono l'unica attesa: vanno arrotondati per eccesso. */
    static const uint16_t power_on_time = 100;       // ms, misurato 82
    static const uint16_t power_off_time = 250;      // ms, misurato 221
    /** Refresh pieno misurato: 18.3 s di BUSY, 19.1 s di ciclo. Il margine fino
     *  a 30 s copre l'allungamento della waveform a bassa temperatura, ed è lo
     *  stesso che porta il driver 9.7". Sopra ci sta il busy_timeout passato ai
     *  costruttori, 40 s, che è il tetto oltre il quale _waitWhileAnyBusy
     *  rinuncia: con 30 s il margine sul misurato era solo 1.6x.
     *
     *  partial_refresh_time è pari al pieno perchè su questo pannello una
     *  passata su finestra costa come un frame intero, vedi
     *  hasFastPartialUpdate. */
    static const uint16_t full_refresh_time = 30000; // ms
    static const uint16_t partial_refresh_time = 30000;

    /**
     * Modello di indirizzamento dei due controller. Non è una preferenza di
     * stile: sono due cablaggi fisici diversi, e quale sia quello vero non è
     * ancora misurato (vedi l'intestazione del file e
     * examples/12_2c/dual_panel_finder).
     *
     *   ADDRESSING_DUAL_CS  due chip select indipendenti, un opcode solo. È la
     *                       topologia dei pannelli commerciali a più
     *                       controller (12.48" GDEY1248Z51: quattro CS, quattro
     *                       BUSY, due boost) ed è il default storico di questo
     *                       driver.
     *   ADDRESSING_CASCADE  un solo chip select condiviso; lo slave si
     *                       indirizza sommando 0x80 agli opcode che lo
     *                       riguardano, e il master va messo in cascade con
     *                       0x21 B[4] = 1 perchè emetta il clock CL. È il
     *                       modello del firmware di fabbrica SOLUM
     *                       (docs/openepaperlink/nrf52811_tag_fw/dualssd.cpp) e
     *                       quello che il conteggio dei pin del tag rende più
     *                       probabile.
     */
    enum AddressingMode : uint8_t
    {
      ADDRESSING_DUAL_CS,
      ADDRESSING_CASCADE
    };

    /** Offset sommato agli opcode diretti allo slave in ADDRESSING_CASCADE.
     *  Il nome non è SLAVE_CMD_OFFSET perchè quel simbolo è già una macro
     *  nello sketch examples/12_2c/dual_panel_finder, che lo usa per la stessa
     *  cosa nella fase probe: una macro con lo stesso nome romperebbe la
     *  qualificazione di classe. */
    static const uint8_t CASCADE_CMD_OFFSET = 0x80;

    // Split master/slave: larghezza piena per ogni controller, metà altezza.
    // I 960 px sono l'asse source, i 768 l'asse gate, spartito 384 + 384 fra i
    // due controller. Le costanti sono usate sia dalla classe outer per il
    // dispatch delle scritture sia dalle ScreenPart per l'addressing locale.
    static const uint16_t PART_WIDTH = WIDTH;       // 960 source per controller
    static const uint16_t PART_HEIGHT = HEIGHT / 2; // master righe 0..383, slave 384..767

    // ----- Costruttori -----
#if defined(ESP32)
    // Costruttore completo ESP32 con SPI espliciti (es. Waveshare ESP32 driver board).
    GxEPD2_SOLUM_122c_960x768(int16_t sck, int16_t miso, int16_t mosi,
                              int16_t cs_m, int16_t cs_s,
                              int16_t dc, int16_t rst,
                              int16_t busy_m, int16_t busy_s);
#endif
    // Costruttore standard SPI (default SCK / MISO / MOSI).
    GxEPD2_SOLUM_122c_960x768(int16_t cs_m, int16_t cs_s,
                              int16_t dc, int16_t rst,
                              int16_t busy_m, int16_t busy_s);
    // Costruttore "compat single-CS" per bring-up con un solo controller cablato
    // (il secondo CS viene passato come -1, le scritture vanno solo al master).
    GxEPD2_SOLUM_122c_960x768(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    /**
     * Costruttore a pinout uniforme: accetta la struct comune ai driver della
     * libreria. Legge cs2 e busy2 per il secondo controller e sck/miso/mosi
     * per il bus, che questo driver apre da sè; i campi a -1 valgono
     * "assente", quindi lo stesso pinout con cs2 = -1 dà il bring-up
     * single-CS. È la firma che permette a uno sketch di cambiare pannello
     * senza riscrivere la riga di costruzione del display.
     */
    explicit GxEPD2_SOLUM_122c_960x768(const GxEPD2_SOLUM_Pins& pins);

    // ----- API pubbliche (pattern GxEPD2 standard, BWR-only) -----
    // Override di init(): il base GxEPD2_EPD::init() configura solo i pin
    // del master (_cs / _rst / _busy / SPI). Per il dual-controller serve
    // estendere con pinMode su _cs_s e _busy_s. Pattern preso da 1248c.
    void init(uint32_t serial_diag_bitrate = 0);
    void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 20, bool pulldown_rst_mode = false);

    void clearScreen(uint8_t value = 0xFF);
    void clearScreen(uint8_t black_value, uint8_t color_value);
    void writeScreenBuffer(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value);

    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);

    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);

    void refresh(bool partial_update_mode = false);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
    void powerOff();
    void hibernate();

    // ------------------------------------------------------------------
    // API siblings per scrittura single-channel (senza refresh).
    // Stessa shape per i 2 piani RAM del controller SSD16xx:
    //   writeImageBlack -> cmd 0x24 (black/white plane, no invert)
    //   writeImageRed   -> cmd 0x26 (red accent, invert applicato)
    // Convenzione bitmap input: bit=1 dove il pixel NON appartiene al canale
    // (stesso formato prodotto da epd_image_converter.pyw e image2cpp).
    // ------------------------------------------------------------------
    void writeImageBlack(const uint8_t* bitmap, int16_t x, int16_t y,
                         int16_t w, int16_t h, bool pgm = true);
    void writeImageRed  (const uint8_t* bitmap, int16_t x, int16_t y,
                         int16_t w, int16_t h, bool pgm = true);

    // Hook virtual chiamato da GxEPD2_3C::firstPage() all'inizio di ogni
    // loop paged. Reset del page-hint per allinearlo a _current_page del
    // template (privato senza getter pubblico).
    void setPaged() override { _show_image_page_hint = 0; }

    // Getter del page-hint usato da GxEPDImage::showImage come surrogato
    // di _current_page del template GxEPD2_3C: permette a showImage di
    // skippare a priori le righe sorgente fuori dalla page corrente.
    int16_t showImagePageHint() const { return _show_image_page_hint; }

    /**
     * Primitive del terzo piano, per ora senza corpo. Non sono parte del
     * contratto di src/GxEPDImage.h, che chiede i soli setPaged() e
     * showImagePageHint(): showImage() compone black e red e un terzo piano
     * non lo tocca mai. Restano dichiarate perchè su questo pannello il
     * quarto colore è una questione ancora aperta — il codice modello
     * EL122H6W4A ha campo colore 4, cioè BWRY nominale, mentre il vetro
     * porta serigrafato "Newton PRO 12.2" BWR normal" — e il bring-up è
     * fermo alla seconda coda muta, quindi non c'è modo di misurarlo.
     * Quando lo si saprà: o prendono un corpo vero, o vanno rimosse.
     * Il 9.7" ha chiuso la stessa domanda e non le dichiara affatto.
     */
    void preserveYellow(bool /*preserve*/) {}
    bool isYellowPreserved() const { return true; }
    void writeImageYellow(const uint8_t* /*bitmap*/, int16_t /*x*/, int16_t /*y*/,
                          int16_t /*w*/, int16_t /*h*/, bool /*pgm*/ = true) {}

    /**
     * Orientamento delle due bande. Le due code del pannello escono da bordi
     * opposti, quindi il secondo controller è presumibilmente ruotato di 180°
     * rispetto al primo: default master (false, false), slave (true, true).
     * L'SSD1677 non ha una reverse scan hardware, quindi il ribaltamento è nel
     * data path e queste chiamate lo governano senza ricompilare il driver.
     * Da chiamare prima del primo write; servono a provare le combinazioni al
     * bring-up quando una banda esce specchiata.
     */
    void setMasterMirror(bool mirror_x, bool mirror_y) { M.setMirror(mirror_x, mirror_y); }
    void setSlaveMirror(bool mirror_x, bool mirror_y)  { S.setMirror(mirror_x, mirror_y); }

    /**
     * Sceglie il modello di indirizzamento dei due controller, vedi il commento
     * di AddressingMode. Va chiamata PRIMA di init(), perchè init() decide in
     * base al modo quali pin configurare: in cascade il secondo chip select non
     * esiste e non va pilotato.
     *
     * In ADDRESSING_CASCADE la ScreenPart slave passa a scrivere sul chip
     * select del master con gli opcode offset, quindi diventa attiva anche se
     * il costruttore ha ricevuto cs2 = -1: in cascade un secondo CS non serve.
     */
    void setAddressingMode(AddressingMode mode);
    AddressingMode addressingMode() const { return _addressing; }

  private:
    // ------------------------------------------------------------------
    // ScreenPart: gestisce un singolo controller (master o slave).
    // Pattern preso da GxEPD2_1248c::ScreenPart, semplificato per 2
    // controller invece di 4.
    // ------------------------------------------------------------------
    class ScreenPart
    {
      public:
        const uint16_t WIDTH;
        const uint16_t HEIGHT;
        ScreenPart(uint16_t width, uint16_t height, bool mirror_x, bool mirror_y, int16_t cs, int16_t dc,
                   SPIClass*& pSPIx, SPISettings& spi_settings);
        void writeScreenBuffer(uint8_t command, uint8_t value);
        void writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                            int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm);
        void writeCommand(uint8_t c);
        void writeData(uint8_t d);
        bool isActive() const { return _cs >= 0; }
        /** Riassegna chip select e offset degli opcode di questa ScreenPart.
         *  In cascade lo slave scrive sul CS del master e somma 0x80 ai propri
         *  comandi; in dual-CS torna al proprio CS con offset nullo. */
        void setAddressing(int16_t cs, uint8_t cmd_offset) { _cs = cs; _cmd_offset = cmd_offset; }
        // Ribaltamento della banda gestita da questa ScreenPart, vedi
        // setSlaveMirror() nella classe outer.
        void setMirror(bool mirror_x, bool mirror_y) { _mirror_x = mirror_x; _mirror_y = mirror_y; }
      private:
        bool    _mirror_x;
        bool    _mirror_y;
        int16_t _cs;
        int16_t _dc;
        // Sommato all'opcode di ogni comando di questa ScreenPart: 0x00 in
        // dual-CS, 0x80 sullo slave in cascade. Non tocca i dati, che non
        // hanno opcode.
        uint8_t _cmd_offset;
        // Riferimenti allo stato SPI del driver, non copie: così un
        // selectSPI() dello sketch vale anche per le ScreenPart.
        SPIClass*&   _pSPIx;
        SPISettings& _spi_settings;
        void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
        // Inverte l'ordine degli 8 bit di un byte: serve quando la banda è
        // specchiata lungo X, perchè il ribaltamento dei soli byte lascerebbe
        // i pixel invertiti dentro ogni gruppo di 8.
        static uint8_t _reverseBits(uint8_t b);
    };

    // ------------------------------------------------------------------
    // Helpers privati outer-class.
    // ------------------------------------------------------------------
    void _cleanAccentIfDirty(uint8_t command, bool& dirty_flag);
    void _writeScreenBuffer(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t* bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm);
    void _writeImagePart(uint8_t command, const uint8_t* bitmap, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm);
    void _resetDual();
    void _initSPI();
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();
    void _writeCommandMaster(uint8_t c);
    void _writeDataMaster(uint8_t d);
    void _writeCommandAll(uint8_t c);
    void _writeDataAll(uint8_t d);
    void _waitWhileAnyBusy(const char* comment, uint16_t busy_time);

    // ------------------------------------------------------------------
    // Stato.
    // ------------------------------------------------------------------
    int16_t _sck, _miso, _mosi;
    int16_t _cs_m, _cs_s;
    int16_t _dc_pin;
    int16_t _rst_pin;
    int16_t _busy_m, _busy_s;

    ScreenPart M;
    ScreenPart S;

    // Modello di indirizzamento attivo, vedi setAddressingMode().
    AddressingMode _addressing = ADDRESSING_DUAL_CS;

    // Dirty flag canale rosso (cmd 0x26). Permette di saltare la pulizia
    // pre-draw quando non serve (catena di immagini B/N consecutive).
    bool _color_dirty = false;

    // Counter usato da GxEPDImage::showImage per dedurre la page corrente
    // del template GxEPD2_3C.
    int16_t _show_image_page_hint = 0;
};

// =============================================================================
// IMPLEMENTAZIONI INLINE
// =============================================================================

// ----- Costruttori outer-class -----

#if defined(ESP32)
inline GxEPD2_SOLUM_122c_960x768::GxEPD2_SOLUM_122c_960x768(
    int16_t sck, int16_t miso, int16_t mosi,
    int16_t cs_m, int16_t cs_s,
    int16_t dc, int16_t rst,
    int16_t busy_m, int16_t busy_s) :
  GxEPD2_EPD(cs_m, dc, rst, busy_m, HIGH, 40000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate),
  _sck(sck), _miso(miso), _mosi(mosi),
  _cs_m(cs_m), _cs_s(cs_s), _dc_pin(dc), _rst_pin(rst),
  _busy_m(busy_m), _busy_s(busy_s),
  M(PART_WIDTH, PART_HEIGHT, false, false, cs_m, dc, _pSPIx, _spi_settings),
  S(PART_WIDTH, PART_HEIGHT, true,  true,  cs_s, dc, _pSPIx, _spi_settings)
{
  // Default: SPI globale a 10 MHz. Passa dai membri della base invece di essere
  // cablato nelle primitive, così un selectSPI() dello sketch lo sostituisce.
  // Era 20 MHz: alcuni SSD1677 non tollerano clock superiori a 10 MHz, e con
  // una sola banda validata e la seconda coda muta un clock fuori specifica
  // sarebbe una variabile in più nel bring-up. Il costo è ~130 ms in più per
  // refresh full-screen, su un refresh che ne dura 25.000.
  selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
}
#endif

inline GxEPD2_SOLUM_122c_960x768::GxEPD2_SOLUM_122c_960x768(
    int16_t cs_m, int16_t cs_s,
    int16_t dc, int16_t rst,
    int16_t busy_m, int16_t busy_s) :
  GxEPD2_EPD(cs_m, dc, rst, busy_m, HIGH, 40000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate),
  _sck(SCK), _miso(MISO), _mosi(MOSI),
  _cs_m(cs_m), _cs_s(cs_s), _dc_pin(dc), _rst_pin(rst),
  _busy_m(busy_m), _busy_s(busy_s),
  M(PART_WIDTH, PART_HEIGHT, false, false, cs_m, dc, _pSPIx, _spi_settings),
  S(PART_WIDTH, PART_HEIGHT, true,  true,  cs_s, dc, _pSPIx, _spi_settings)
{
  // Default: SPI globale a 10 MHz. Passa dai membri della base invece di essere
  // cablato nelle primitive, così un selectSPI() dello sketch lo sostituisce.
  // Era 20 MHz: alcuni SSD1677 non tollerano clock superiori a 10 MHz, e con
  // una sola banda validata e la seconda coda muta un clock fuori specifica
  // sarebbe una variabile in più nel bring-up. Il costo è ~130 ms in più per
  // refresh full-screen, su un refresh che ne dura 25.000.
  selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
}

// Variante single-CS: utile per bring-up con un solo controller cablato
// fisicamente. Lo slave riceve cs=-1, quindi le scritture verso S sono no-op.
// La banda bassa del pannello (righe 384..767) non si aggiorna, ma il bring-up
// del master si può validare in isolamento.
inline GxEPD2_SOLUM_122c_960x768::GxEPD2_SOLUM_122c_960x768(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_EPD(cs, dc, rst, busy, HIGH, 40000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate),
  _sck(SCK), _miso(MISO), _mosi(MOSI),
  _cs_m(cs), _cs_s(-1), _dc_pin(dc), _rst_pin(rst),
  _busy_m(busy), _busy_s(-1),
  M(PART_WIDTH, PART_HEIGHT, false, false, cs, dc, _pSPIx, _spi_settings),
  S(PART_WIDTH, PART_HEIGHT, true,  true,  -1, dc, _pSPIx, _spi_settings)
{
  // Default: SPI globale a 10 MHz. Passa dai membri della base invece di essere
  // cablato nelle primitive, così un selectSPI() dello sketch lo sostituisce.
  // Era 20 MHz: alcuni SSD1677 non tollerano clock superiori a 10 MHz, e con
  // una sola banda validata e la seconda coda muta un clock fuori specifica
  // sarebbe una variabile in più nel bring-up. Il costo è ~130 ms in più per
  // refresh full-screen, su un refresh che ne dura 25.000.
  selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
}

/**
 * Delega al costruttore che corrisponde al pinout ricevuto. I pin del bus a
 * -1 significano "quelli di default della board"; cs2 a -1 dà il bring-up
 * single-CS, con la metà slave del pannello non pilotata.
 */
inline GxEPD2_SOLUM_122c_960x768::GxEPD2_SOLUM_122c_960x768(const GxEPD2_SOLUM_Pins& pins) :
#if defined(ESP32)
  GxEPD2_SOLUM_122c_960x768(pins.sck  >= 0 ? pins.sck  : int16_t(SCK),
                            pins.miso >= 0 ? pins.miso : int16_t(MISO),
                            pins.mosi >= 0 ? pins.mosi : int16_t(MOSI),
                            pins.cs, pins.cs2,
                            pins.dc, pins.rst,
                            pins.busy, pins.busy2)
#else
  GxEPD2_SOLUM_122c_960x768(pins.cs, pins.cs2, pins.dc, pins.rst,
                            pins.busy, pins.busy2)
#endif
{
}

// ----- API pubbliche outer-class -----

inline void GxEPD2_SOLUM_122c_960x768::setAddressingMode(AddressingMode mode)
{
  _addressing = mode;
  if (mode == ADDRESSING_CASCADE) S.setAddressing(_cs_m, CASCADE_CMD_OFFSET);
  else                            S.setAddressing(_cs_s, 0x00);
}

inline void GxEPD2_SOLUM_122c_960x768::clearScreen(uint8_t value)
{
  clearScreen(value, 0x00);
}

inline void GxEPD2_SOLUM_122c_960x768::clearScreen(uint8_t black_value, uint8_t color_value)
{
  writeScreenBuffer(black_value, color_value);
  refresh(false);
}

inline void GxEPD2_SOLUM_122c_960x768::writeScreenBuffer(uint8_t value)
{
  writeScreenBuffer(value, 0x00);
}

// Init dei buffer del controller: B/N (cmd 0x24) e rosso (cmd 0x26).
// SSD16xx polarity: cmd 0x24 bit=1=white bit=0=black; cmd 0x26 bit=1=red.
// I parametri sono valori RAW da scrivere nel piano RAM (no inversione).
// Esempi pratici per i 3 colori puri:
//   (black=0xFF, color=0x00) -> bianco totale
//   (black=0x00, color=0x00) -> nero totale
//   (black=0xFF, color=0xFF) -> rosso totale
// Per i bitmap usare invece writeImage(black, color, ...) che applica
// l'inversione necessaria sul color plane (convenzione bit=1=NOT color
// dello script python e di image2cpp).
inline void GxEPD2_SOLUM_122c_960x768::writeScreenBuffer(uint8_t black_value, uint8_t color_value)
{
  if (!_init_display_done) _InitDisplay();
  _writeScreenBuffer(0x24, black_value);
  _writeScreenBuffer(0x26, color_value);
  _initial_write = false;
  _color_dirty = false;
}

inline void GxEPD2_SOLUM_122c_960x768::_writeScreenBuffer(uint8_t command, uint8_t value)
{
  M.writeScreenBuffer(command, value);
  if (S.isActive()) S.writeScreenBuffer(command, value);
}

// HOT PATH: chiamato dal template GxEPD2_3C in modalità BW (paged).
// Pulisce il canale rosso se dirty e poi scrive il piano BW.
inline void GxEPD2_SOLUM_122c_960x768::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write) _cleanAccentIfDirty(0x26, _color_dirty);
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

inline void GxEPD2_SOLUM_122c_960x768::_writeImage(uint8_t command, const uint8_t* bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (_initial_write) writeScreenBuffer();
  if (!_init_display_done) _InitDisplay();
  if (!bitmap) return;
  // Dispatch master/slave: master gestisce le Y 0..M.HEIGHT-1, slave le Y
  // M.HEIGHT..HEIGHT-1. La ScreenPart::writeImagePart fa il clipping interno,
  // e la Y negativa passata allo slave diventa l'offset con cui pesca le righe
  // sorgente della propria banda.
  M.writeImagePart(command, bitmap, 0, 0, w, h, x, y, w, h, invert, mirror_y, pgm);
  if (S.isActive())
    S.writeImagePart(command, bitmap, 0, 0, w, h, x, y - int16_t(M.HEIGHT), w, h, invert, mirror_y, pgm);
}

inline void GxEPD2_SOLUM_122c_960x768::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write) _cleanAccentIfDirty(0x26, _color_dirty);
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

inline void GxEPD2_SOLUM_122c_960x768::_writeImagePart(uint8_t command, const uint8_t* bitmap, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (_initial_write) writeScreenBuffer();
  if (!_init_display_done) _InitDisplay();
  if (!bitmap) return;
  M.writeImagePart(command, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  if (S.isActive())
    S.writeImagePart(command, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y - int16_t(M.HEIGHT), w, h, invert, mirror_y, pgm);
}

// HOT PATH (paged full-window): GxEPD2_3C::nextPage() in modalità full-window
// chiama questa overload una volta per page. Avanza il page-hint dopo aver
// scritto la page corrente sul controller, così GxEPDImage::showImage
// nella prossima iterazione skippa le righe già scritte.
inline void GxEPD2_SOLUM_122c_960x768::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) _writeImage(0x24, black, x, y, w, h, invert, mirror_y, pgm);
  if (color)
  {
    _writeImage(0x26, color, x, y, w, h, !invert, mirror_y, pgm);
    _color_dirty = true;
  }
  _show_image_page_hint++;
}

inline void GxEPD2_SOLUM_122c_960x768::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) _writeImagePart(0x24, black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  if (color)
  {
    _writeImagePart(0x26, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, !invert, mirror_y, pgm);
    _color_dirty = true;
  }
}

inline void GxEPD2_SOLUM_122c_960x768::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(data1, data2, x, y, w, h, invert, mirror_y, pgm);
}

inline void GxEPD2_SOLUM_122c_960x768::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_122c_960x768::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_122c_960x768::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_122c_960x768::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_122c_960x768::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_122c_960x768::refresh(bool /*partial_update_mode*/)
{
  _Update_Full(); // sempre full window
}

inline void GxEPD2_SOLUM_122c_960x768::refresh(int16_t /*x*/, int16_t /*y*/, int16_t /*w*/, int16_t /*h*/)
{
  _Update_Full();
}

inline void GxEPD2_SOLUM_122c_960x768::powerOff()
{
  _PowerOff();
}

/** Deep sleep SSD16xx: cmd 0x10 con A[1:0] = 11. Il datasheet SSD1677 per 0x10
 *  definisce solo 00 (normale) e 11 (deep sleep), da cui 0x03; è anche il
 *  valore che usa OpenEPaperLink sulla stessa famiglia. Per uscirne serve un
 *  HW reset, che _InitDisplay() fa già quando _hibernating è alto.
 *  Protetto contro chiamate multiple. */
inline void GxEPD2_SOLUM_122c_960x768::hibernate()
{
  if (_hibernating) return;
  _PowerOff();
  if (_rst >= 0)
  {
    _writeCommandAll(0x10);
    _writeDataAll(0x03);
    _hibernating = true;
    _init_display_done = false;
    _color_dirty = false;
  }
}

// ----- API single-channel -----

inline void GxEPD2_SOLUM_122c_960x768::writeImageBlack(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x24, bitmap, x, y, w, h, false, false, pgm);
}

inline void GxEPD2_SOLUM_122c_960x768::writeImageRed(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x26, bitmap, x, y, w, h, true, false, pgm);
  _color_dirty = true;
}

// ----- Helper privati outer-class -----

// Pulizia selettiva di un canale accent: scrive 0x00 ovunque (polarity nativa
// SSD16xx = "accent spento") e resetta il flag dirty.
inline void GxEPD2_SOLUM_122c_960x768::_cleanAccentIfDirty(uint8_t command, bool& dirty_flag)
{
  if (dirty_flag)
  {
    _writeScreenBuffer(command, 0x00);
    dirty_flag = false;
  }
}

inline void GxEPD2_SOLUM_122c_960x768::_resetDual()
{
  if (_rst_pin >= 0)
  {
    digitalWrite(_rst_pin, LOW);
    delay(_reset_duration);
    digitalWrite(_rst_pin, HIGH);
    delay(_reset_duration);
  }
  _hibernating = false;
}

// Override di init(): pattern 1248c. Configura pinMode di tutti i pin
// (master + slave) e poi avvia SPI con i pin custom se ESP32 lo richiede.
// NON chiama GxEPD2_EPD::init() base, perchè il base assume single-CS.
inline void GxEPD2_SOLUM_122c_960x768::init(uint32_t serial_diag_bitrate)
{
  init(serial_diag_bitrate, true, 20, false);
}

inline void GxEPD2_SOLUM_122c_960x768::init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode)
{
  _initial_write = initial;
  _initial_refresh = initial;
  _pulldown_rst_mode = pulldown_rst_mode;
  _power_is_on = false;
  _using_partial_mode = false;
  _hibernating = false;
  _init_display_done = false;
  _reset_duration = reset_duration;
  if (serial_diag_bitrate > 0)
  {
    Serial.begin(serial_diag_bitrate);
    _diag_enabled = true;
  }
  // Pin master + slave. Ogni pin è guardato da >= 0 come fa
  // GxEPD2_EPD::init() della base: -1 significa assente e non deve arrivare a
  // pinMode(). È il valore che GxEPD2_SOLUM_Pins usa per i campi non
  // valorizzati, quindi è raggiungibile dal costruttore a pinout uniforme.
  if (_cs_m >= 0)
  {
    pinMode(_cs_m, OUTPUT);
    digitalWrite(_cs_m, HIGH);
  }
  // In cascade il secondo chip select non esiste: il pin resta libero e non va
  // pilotato, altrimenti si tiene alto un CS che sul pannello non arriva a
  // nessuno.
  if ((_addressing == ADDRESSING_DUAL_CS) && (_cs_s >= 0))
  {
    pinMode(_cs_s, OUTPUT);
    digitalWrite(_cs_s, HIGH);
  }
  if (_dc_pin >= 0)
  {
    pinMode(_dc_pin, OUTPUT);
    digitalWrite(_dc_pin, HIGH);
  }
  if (_rst_pin >= 0)
  {
    pinMode(_rst_pin, OUTPUT);
    digitalWrite(_rst_pin, HIGH);
  }
  if (_busy_m >= 0) pinMode(_busy_m, INPUT);
  /** Pull-down sul BUSY dello slave, non su quello del master, che è pilotato e
   *  non va toccato. Il BUSY è attivo alto: un pin non contattato che segue il
   *  pull viene letto "non occupato" e lascia passare le attese, mentre un
   *  ingresso flottante che si assesta alto le manda tutte al busy_timeout
   *  senza che niente lo segnali. Su una linea davvero pilotata il pull interno
   *  da ~45 kohm è irrilevante. Sui GPIO 34..39 dell'ESP32 la direttiva è
   *  inerte, perchè quei pin non hanno pull: lì l'unico rimedio è portare il
   *  BUSY su un pin che ne abbia. */
#if defined(ESP32)
  if (_busy_s >= 0) pinMode(_busy_s, INPUT_PULLDOWN);
#else
  if (_busy_s >= 0) pinMode(_busy_s, INPUT);
#endif
  _initSPI();
  _resetDual();
}

inline void GxEPD2_SOLUM_122c_960x768::_initSPI()
{
#if defined(ESP32)
  if ((SCK != _sck) || (MISO != _miso) || (MOSI != _mosi))
  {
    _pSPIx->end();
    _pSPIx->begin(_sck, _miso, _mosi, _cs_m);
  }
  else _pSPIx->begin();
#else
  _pSPIx->begin();
#endif
}

inline void GxEPD2_SOLUM_122c_960x768::_PowerOn()
{
  if (!_power_is_on)
  {
    // SSD16xx: display update sequence "solo power on" + master activation.
    // Va a entrambi i controller, che alimentano ciascuno la propria banda.
    _writeCommandAll(0x22);
    _writeDataAll(0xc0);
    _writeCommandAll(0x20);
    _waitWhileAnyBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

inline void GxEPD2_SOLUM_122c_960x768::_PowerOff()
{
  if (_power_is_on)
  {
    _writeCommandAll(0x22);
    _writeDataAll(0xc3); // sequence "solo power off"
    _writeCommandAll(0x20);
    _waitWhileAnyBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
}

/** Init SSD16xx, identico per i due controller e quindi mandato in broadcast
 *  con _writeCommandAll: ogni controller pilota la stessa geometria
 *  (960 source x 384 gate) e differisce solo per l'orientamento della banda,
 *  che vive nel data path e non nei registri.
 *
 *  La sequenza è quella del driver 9.7" della libreria, che gira su questo
 *  stesso silicio; lo stock GxEPD2_1160c_GDEY116Z91 concorda sui comandi e si
 *  limita a un init più corto (SWRESET + VBD, tutto il resto dai default OTP).
 *
 *  Nessun power on qui: su SSD16xx è la display update sequence di
 *  _Update_Full (0x22 = 0xF7) a fare power on, refresh e power off.
 *
 *  MUX derivato da PART_HEIGHT: sono le gate line che ogni controller pilota
 *  davvero, oggi 384, contro un POR del registro di 680. Si scrive per avere la
 *  mappatura gate corretta, non per guadagnare tempo: dual_panel_finder ha
 *  cronometrato lo stesso refresh con il MUX scritto a 383 e con il registro
 *  lasciato al default, e la durata coincide entro 13 ms su 18.3 s. Le linee in
 *  più, se vengono scandite, non costano niente di misurabile.
 *
 *  Il valore NON va cablato: era scritto qui come `0x7F 0x01` e la stessa
 *  informazione stava anche in PART_HEIGHT, quindi un conteggio gate diverso
 *  misurato al bring-up avrebbe richiesto due modifiche coordinate, e
 *  dimenticarne una dà un pannello che scandisce il numero sbagliato di linee
 *  senza che niente lo segnali. Ora la sola PART_HEIGHT decide. */
inline void GxEPD2_SOLUM_122c_960x768::_InitDisplay()
{
  if (_hibernating) _resetDual();
  delay(10);
  _writeCommandAll(0x12);  // SWRESET
  delay(200);              // SSD16xx: ~100-300 ms prima di accettare comandi
  _writeCommandAll(0x0C);  // soft start
  _writeDataAll(0xAE);
  _writeDataAll(0xC7);
  _writeDataAll(0xC3);
  _writeDataAll(0xC0);
  _writeDataAll(0x80);
  // driver output control: MUX = (gate line - 1) su 10 bit, little endian,
  // più un terzo byte di direzione di scansione a 0 (GD/SM/TB ai default: sul
  // SSD1677 TB = 1 è dichiarato Reserved, quindi la reverse scan hardware non
  // esiste ed è per questo che il ribaltamento sta nel data path)
  const uint16_t mux = PART_HEIGHT - 1;
  _writeCommandAll(0x01);
  _writeDataAll(uint8_t(mux & 0xFF));
  _writeDataAll(uint8_t(mux >> 8));
  _writeDataAll(0x00);
  _writeCommandAll(0x3C);  // border waveform
  _writeDataAll(0x01);     // LUT1, bianco
  _writeCommandAll(0x18);  // temperatura dal sensore interno
  _writeDataAll(0x80);
  // Entry mode x/y increase: la finestra RAM la riscrive ogni write, ma il
  // verso di avanzamento del contatore è uguale per tutti i write.
  _writeCommandAll(0x11);
  _writeDataAll(0x03);
  if (_addressing == ADDRESSING_CASCADE)
  {
    /** Display update control 1. Il secondo byte esiste dal SSD1683 in poi e
     *  porta B[4] "ckouten": a 1 mette il master in cascade e gli fa emettere
     *  il clock CL verso lo slave, che di suo non ha oscillatore. Senza questo
     *  bit lo slave resta muto anche se cablato bene. 0x08 sul primo byte è il
     *  valore dell'init di fabbrica SOLUM, che scrive 0x21 = 08 10 in cascade e
     *  08 00 sui pannelli a chip singolo. */
    _writeCommandAll(0x21);
    _writeDataAll(0x08);
    _writeDataAll(0x10);
  }
  _init_display_done = true;
}

/** Esegue il refresh elettroforetico full-window: 18.3 s di BUSY misurati,
 *  19.1 s dalla master activation alla fine del ciclo. Su SSD16xx la
 *  display update sequence 0xF7 comprende power on, load LUT dalla OTP, scan e
 *  power off: da qui _power_is_on = false all'uscita. */
inline void GxEPD2_SOLUM_122c_960x768::_Update_Full()
{
  _writeCommandAll(0x22);
  _writeDataAll(0xF7);
  _writeCommandAll(0x20);  // master activation
  _waitWhileAnyBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
  _show_image_page_hint = 0;
}

// ----- Dispatch comandi master/slave (pattern 1248c semplificato) -----
//
// Le guardie >= 0 sui pin sono quelle di GxEPD2_EPD::_writeCommand della base:
// -1 significa pin assente ed è un valore legale di GxEPD2_SOLUM_Pins.

inline void GxEPD2_SOLUM_122c_960x768::_writeCommandMaster(uint8_t c)
{
  _pSPIx->beginTransaction(_spi_settings);
  if (_dc_pin >= 0) digitalWrite(_dc_pin, LOW);
  if (_cs_m >= 0) digitalWrite(_cs_m, LOW);
  _pSPIx->transfer(c);
  if (_cs_m >= 0) digitalWrite(_cs_m, HIGH);
  if (_dc_pin >= 0) digitalWrite(_dc_pin, HIGH);
  _pSPIx->endTransaction();
}

inline void GxEPD2_SOLUM_122c_960x768::_writeDataMaster(uint8_t d)
{
  _pSPIx->beginTransaction(_spi_settings);
  if (_cs_m >= 0) digitalWrite(_cs_m, LOW);
  _pSPIx->transfer(d);
  if (_cs_m >= 0) digitalWrite(_cs_m, HIGH);
  _pSPIx->endTransaction();
}

/** Abbassa entrambi i CS e manda il byte una volta sola: i due controller lo
 *  ricevono in parallelo. Va bene perchè CS è un ingresso e MOSI è condiviso,
 *  quindi non c'è contesa.
 *
 *  Vale solo in scrittura: se un giorno si cablasse la linea di lettura del
 *  pannello (SDO) su entrambe le code, con due CS bassi i due controller
 *  piloterebbero insieme lo stesso filo. Una lettura va fatta selezionando un
 *  solo controller.
 *
 *  È il broadcast che usa GxEPD2_1248c upstream su un pannello a chip select
 *  separati, ed è corretto solo su quella topologia: se i due controller sono
 *  una coppia in cascade il broadcast non serve, perchè il chip select è uno e
 *  a distinguere i due chip è l'offset 0x80 sull'opcode. */
inline void GxEPD2_SOLUM_122c_960x768::_writeCommandAll(uint8_t c)
{
  _pSPIx->beginTransaction(_spi_settings);
  const bool dual = (_addressing == ADDRESSING_DUAL_CS);
  if (_dc_pin >= 0) digitalWrite(_dc_pin, LOW);
  if (_cs_m >= 0) digitalWrite(_cs_m, LOW);
  if (dual && (_cs_s >= 0)) digitalWrite(_cs_s, LOW);
  _pSPIx->transfer(c);
  if (_cs_m >= 0) digitalWrite(_cs_m, HIGH);
  if (dual && (_cs_s >= 0)) digitalWrite(_cs_s, HIGH);
  if (_dc_pin >= 0) digitalWrite(_dc_pin, HIGH);
  _pSPIx->endTransaction();
}

inline void GxEPD2_SOLUM_122c_960x768::_writeDataAll(uint8_t d)
{
  const bool dual = (_addressing == ADDRESSING_DUAL_CS);
  _pSPIx->beginTransaction(_spi_settings);
  if (_cs_m >= 0) digitalWrite(_cs_m, LOW);
  if (dual && (_cs_s >= 0)) digitalWrite(_cs_s, LOW);
  _pSPIx->transfer(d);
  if (_cs_m >= 0) digitalWrite(_cs_m, HIGH);
  if (dual && (_cs_s >= 0)) digitalWrite(_cs_s, HIGH);
  _pSPIx->endTransaction();
}

/** Attende che entrambi i controller (master + slave) abbiano rilasciato il pin
 *  BUSY. Pattern OR-degli-AND-negati: usciamo solo quando NON è busy alcuno dei
 *  due.
 *
 *  Lo slave viene ignorato se manca il suo BUSY (`_busy_s < 0`) **oppure se
 *  manca il suo CS** (`_cs_s < 0`): un controller che non viene mai selezionato
 *  non può essere occupato, e nel bring-up con una sola coda cablata il suo pin
 *  BUSY è flottante. Senza la guardia su `_cs_s` un pinout con `cs2 = -1` e
 *  `busy2` valorizzato manderebbe ogni attesa al timeout, perchè un input-only
 *  senza pull (GPIO35 sulla board Waveshare) può restare letto come occupato.
 *
 *  In ADDRESSING_CASCADE lo slave viene ignorato sempre: il tag di fabbrica ha
 *  un solo BUSY, e in cascade è il master a scandire per entrambi. */
inline void GxEPD2_SOLUM_122c_960x768::_waitWhileAnyBusy(const char* comment, uint16_t busy_time)
{
  if (_busy_m >= 0)
  {
    delay(1);
    unsigned long start = micros();
    while (true)
    {
      delay(1);
      bool nb_m = (_busy_level != digitalRead(_busy_m));
      bool nb_s = ((_addressing == ADDRESSING_DUAL_CS) && (_cs_s >= 0) && (_busy_s >= 0))
                  ? (_busy_level != digitalRead(_busy_s)) : true;
      if (nb_m && nb_s) break;
      if (micros() - start > _busy_timeout)
      {
        if (_diag_enabled) Serial.println("Busy Timeout!");
        break;
      }
    }
    if (comment && _diag_enabled)
    {
      Serial.print(comment); Serial.print(" : "); Serial.println(micros() - start);
    }
  }
  else delay(busy_time);
}

// =============================================================================
// IMPLEMENTAZIONI INLINE — ScreenPart (controller singolo: master o slave)
// =============================================================================

inline GxEPD2_SOLUM_122c_960x768::ScreenPart::ScreenPart(uint16_t width, uint16_t height, bool mirror_x, bool mirror_y, int16_t cs, int16_t dc,
                                                         SPIClass*& pSPIx, SPISettings& spi_settings) :
  WIDTH(width), HEIGHT(height), _mirror_x(mirror_x), _mirror_y(mirror_y), _cs(cs), _dc(dc),
  _cmd_offset(0x00), _pSPIx(pSPIx), _spi_settings(spi_settings)
{
}

// Bulk-SPI: invece di chiamare SPI.transfer(value) WIDTH*HEIGHT/8 volte
// (=46080 byte per controller = 0.36s a 1.5us/byte), pre-riempiamo un buffer
// di stack e lo scarichiamo a chunk via writeBytes(). Saving ~290 ms per
// refresh full-screen rispetto al pattern per-byte del 1248c originale.
// La finestra RAM va sempre riscritta prima del piano: su SSD16xx il contatore
// di indirizzo resta dov'era finito il write precedente.
inline void GxEPD2_SOLUM_122c_960x768::ScreenPart::writeScreenBuffer(uint8_t command, uint8_t value)
{
  if (_cs < 0) return; // ScreenPart non attiva
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  writeCommand(command);
  uint8_t buf[256];
  memset(buf, value, sizeof(buf));
  uint32_t remaining = uint32_t(WIDTH) * uint32_t(HEIGHT) / 8;
  _pSPIx->beginTransaction(_spi_settings);
  digitalWrite(_cs, LOW);
  while (remaining > 0)
  {
    uint32_t chunk = remaining > sizeof(buf) ? (uint32_t)sizeof(buf) : remaining;
    _pSPIx->writeBytes(buf, chunk);
    remaining -= chunk;
  }
  digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

/** Scrittura partial della banda di pannello pertinente a questa ScreenPart.
 *  Bulk-SPI per riga: buffer di max WIDTH/8 byte (120 byte a WIDTH=960), flush
 *  via writeBytes una volta per riga invece di per-byte transfer().
 *
 *  L'addressing è quello SSD16xx di _setPartialRamArea (0x44/0x45 finestra,
 *  0x4E/0x4F contatore), che vale per un solo write: va riscritto ogni volta.
 *
 *  Se la banda è specchiata, il ribaltamento è qui e non nei registri, perchè
 *  l'SSD1677 non ha una reverse scan hardware: le righe si leggono in ordine
 *  inverso dentro la banda, i byte in ordine inverso dentro la riga e i bit in
 *  ordine inverso dentro il byte. Il flag mirror_y del chiamante è un'altra
 *  cosa e si compone con questo: quello ribalta il bitmap sorgente, questo la
 *  banda fisica. */
inline void GxEPD2_SOLUM_122c_960x768::ScreenPart::writeImagePart(uint8_t command, const uint8_t bitmap[],
    int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (_cs < 0) return;
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int32_t wb_bitmap = (w_bitmap + 7) / 8;
  x_part -= x_part % 8;
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w;
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h;
  x -= x % 8;
  w = 8 * ((w + 7) / 8);
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;

  _setPartialRamArea(x1, y1, w1, h1);
  writeCommand(command);

  const int16_t rowBytes = w1 / 8;
  uint8_t rowBuf[PART_WIDTH / 8]; // 120 byte: una riga piena a 960 px
  _pSPIx->beginTransaction(_spi_settings);
  digitalWrite(_cs, LOW);
  for (int16_t i = 0; i < h1; i++)
  {
    // Riga e byte sorgente: il ribaltamento della banda si applica dentro la
    // banda stessa, quindi sugli indici locali i e j.
    const int16_t si = _mirror_y ? (h1 - 1 - i) : i;
    for (int16_t j = 0; j < rowBytes; j++)
    {
      const int16_t sj = _mirror_x ? (rowBytes - 1 - j) : j;
      uint8_t data;
      int32_t idx = mirror_y
          ? x_part / 8 + sj + dx / 8 + ((h_bitmap - 1 - (y_part + si + dy))) * wb_bitmap
          : x_part / 8 + sj + dx / 8 + (y_part + si + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else data = bitmap[idx];
      if (invert) data = ~data;
      if (_mirror_x) data = _reverseBits(data);
      rowBuf[j] = data;
    }
    _pSPIx->writeBytes(rowBuf, rowBytes);
  }
  digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

inline void GxEPD2_SOLUM_122c_960x768::ScreenPart::writeCommand(uint8_t c)
{
  if (_cs < 0) return;
  _pSPIx->beginTransaction(_spi_settings);
  if (_dc >= 0) digitalWrite(_dc, LOW);
  digitalWrite(_cs, LOW);
  // In cascade l'opcode porta l'offset dello slave; i comandi comuni li manda
  // la classe outer con _writeCommandAll, senza offset.
  _pSPIx->transfer(uint8_t(c | _cmd_offset));
  digitalWrite(_cs, HIGH);
  if (_dc >= 0) digitalWrite(_dc, HIGH);
  _pSPIx->endTransaction();
}

inline void GxEPD2_SOLUM_122c_960x768::ScreenPart::writeData(uint8_t d)
{
  if (_cs < 0) return;
  _pSPIx->beginTransaction(_spi_settings);
  digitalWrite(_cs, LOW);
  _pSPIx->transfer(d);
  digitalWrite(_cs, HIGH);
  _pSPIx->endTransaction();
}

/** Imposta la finestra RAM del controller: 0x44/0x45 per gli estremi,
 *  0x4E/0x4F per il contatore di indirizzo. Coordinate in pixel, non in byte
 *  (il POR di XEnd è 0x3BF = 959).
 *
 *  Se la banda è specchiata, i dati arrivano già ribaltati dal data path e la
 *  finestra va riposizionata in modo simmetrico, altrimenti l'immagine
 *  risulterebbe ribaltata ma nel posto sbagliato. */
inline void GxEPD2_SOLUM_122c_960x768::ScreenPart::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  if (_mirror_x) x = WIDTH - w - x;
  if (_mirror_y) y = HEIGHT - h - y;
  writeCommand(0x44);
  writeData(x % 256);
  writeData(x / 256);
  writeData((x + w - 1) % 256);
  writeData((x + w - 1) / 256);
  writeCommand(0x45);
  writeData(y % 256);
  writeData(y / 256);
  writeData((y + h - 1) % 256);
  writeData((y + h - 1) / 256);
  writeCommand(0x4E);
  writeData(x % 256);
  writeData(x / 256);
  writeCommand(0x4F);
  writeData(y % 256);
  writeData(y / 256);
}

// Inversione dell'ordine dei bit dentro il byte, per la banda specchiata su X.
inline uint8_t GxEPD2_SOLUM_122c_960x768::ScreenPart::_reverseBits(uint8_t b)
{
  b = uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
  b = uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
  b = uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
  return b;
}

#endif
