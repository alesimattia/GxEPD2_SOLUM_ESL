/**
 * dual_panel_finder - unico sketch per testare e diagnosticare il pannello
 * SOLUM 12.2" (768w x 960h nativi, pilotato in landscape come 960x768, due
 * controller SSD16xx). Lo stesso pannello si trova nelle linee Newton PRO e
 * Newton Core / M3: cambiano scheda tag e guscio, non il vetro.
 *
 * Non decide niente: mette il pannello nelle condizioni di rispondere e riporta
 * cosa si vede e quanto ci mette. Le conclusioni si tirano dopo, guardando lo
 * schermo, e da quelle si corregge src/GxEPD2_SOLUM_122c_960x768.h.
 *
 * DUE FASI, indipendenti e separabili con RUN_PROBE_PHASE / RUN_DRIVER_PHASE.
 *
 *   FASE PROBE, a SPI diretta su UNA coda alla volta. Non usa nè GxEPD2 nè il
 *   driver custom nè Adafruit_GFX: parla al controller e basta, come
 *   examples/097c/panel_diagnostic. Misura il SILICIO, e l'esito non dipende da
 *   nessuno strato software.
 *
 *   FASE DRIVER, che costruisce GxEPD2_SOLUM_DRIVER_CLASS dall'ombrello della
 *   libreria e lo esercita su ENTRAMBE le code: clearScreen() per i tre colori
 *   pieni, poi un frame di tile che verifica il dispatch per righe e la
 *   giunzione fra le due bande. Misura il DRIVER.
 *
 * Il senso di averle nello stesso sketch è la distinzione che serve al
 * bring-up: se il probe stampa e la fase driver no, il difetto è nel driver e
 * non nel pannello. (Questo sketch assorbe anche quello che faceva il vecchio
 * examples/12_2c/color_cycle, che non esiste più.)
 *
 * ---------------------------------------------------------------------------
 * COSA SI SA GIÀ, e non va rimisurato
 *
 * - Una coda stampa: con l'ESP32 su un solo FFC si ottiene un rettangolo
 *   960x384, quindi il controller risponde al command set SSD16xx e ogni
 *   controller pilota 960 source x 384 gate.
 * - L'altra coda non risponde. Le ipotesi (ordine dei pin ribaltato, rail di
 *   boost non portati, BUSY o RST fuori posizione) sono in
 *   docs/122c/identificazione_pannello.md §6.
 * - Lo split cade sull'asse corto del pannello: due bande da 960x384, non due
 *   metà per colonne.
 * - L'UC8179 è escluso dall'aritmetica: 800x600 non copre nessuna delle
 *   spartizioni possibili con due controller.
 *
 * COSA MISURA QUESTO TEST
 *
 * 1. Quale sequenza di init vuole il pannello, cioè su quale driver conviene
 *    basarsi. Tre candidate, selezionabili a compile time, tutte SSD16xx:
 *
 *      CAND_MINIMAL  come GxEPD2_1160c_GDEY116Z91: SWRESET, border waveform,
 *                    tutto il resto ai default POR. È l'init più corto che sia
 *                    già noto funzionare su questo pannello.
 *      CAND_SOLUM    come il driver 9.7" di questa libreria e come lo stock
 *                    GxEPD2_1330c_GDEM133Z91: soft start, MUX esplicito,
 *                    sensore di temperatura interno, entry mode.
 *      CAND_OEPL     l'init di fabbrica che OpenEPaperLink usa sul 9.7" SOLUM
 *                    (docs/openepaperlink/oepl_display_driver_unissd.c): parte
 *                    dai pattern hardware 0x46/0x47 e chiude con 0x21, che in
 *                    quel codice serve a raddrizzare un'immagine che altrimenti
 *                    esce ribaltata. Su un pannello SOLUM è la candidata con
 *                    più pedigree, ed è l'unica che tocca il verso.
 *
 *    Si confrontano tre cose: se il BUSY reagisce, quanto dura il refresh, e
 *    come esce il pattern.
 *
 * 2. Quante gate line pilota davvero questo controller. Il pattern porta un
 *    righello verticale con l'etichetta numerica ogni 64 righe: la riga più
 *    alta ancora visibile è il conteggio, letto invece che dedotto. Serve
 *    perchè MUX_LINES nel driver decide quante righe vengono scandite a ogni
 *    refresh, e programmarne più di quante esistono costa tempo a ogni frame.
 *
 * 3. Il verso della banda, su entrambi gli assi. Blocco nero nell'origine RAM
 *    (0,0), blocco rosso nell'angolo opposto in X, righelli numerati su
 *    entrambi gli assi: se i numeri crescono verso il basso e verso destra il
 *    verso è quello atteso, altrimenti la banda è specchiata e va detto al
 *    driver con setMasterMirror() / setSlaveMirror().
 *
 * 4. Quale metà fisica del pannello dipende da questa coda: la lettera grande
 *    al centro identifica la coda sotto test, e dove appare sul pannello dice
 *    se questa coda serve la banda alta o quella bassa.
 *
 * 5. Cosa fa l'altra coda mentre questa lavora. Il BUSY dell'altra coda viene
 *    letto a riposo e durante il refresh: se si muove, i due controller
 *    condividono qualcosa oltre al bus, e questo pesa sull'ipotesi
 *    dell'alimentazione. Il CS dell'altra coda resta alto per tutto il test,
 *    così quel controller non ascolta.
 *
 * 6. Se le alte tensioni salgono. Il controller ha due rilevatori interni,
 *    HV Ready Detection (0x14) e VCI Detection (0x15), che il datasheet
 *    descrive così: alzano il BUSY per la durata della misura e "la detection
 *    si conclude quando HV è pronta". Quindi la DURATA del BUSY è la risposta,
 *    e la si legge anche senza linea di lettura: se HV non arriva mai, la
 *    detection cicla per tutto il tempo massimo programmato. È la misura che
 *    separa "silicio morto o non cablato" da "silicio vivo senza
 *    elettroforesi" sulla coda che non stampa, e non richiede di vedere un
 *    pixel.
 *
 * 7. I registri in lettura, se questa coda porta fuori la linea dati. Status
 *    0x2F ha un POR noto (0x01) e fa da prova di validità del percorso; i suoi
 *    bit 5 e 4 sono i flag di HV Ready e VCI, cioè l'esito esplicito del punto
 *    6. Poi User ID 0x2E da OTP, che identificherebbe il modulo, e temperatura
 *    0x1B.
 *
 *    **Il connettore interno della board non ha SDO**, confermato sullo
 *    schematico Waveshare V3 (docs/E-Paper_ESP32_Driver_Board_V3.pdf): sul FPC
 *    24 pin il pin dati è solo SDI. Quindi sulla coda corta la lettura dei
 *    registri dirà sempre "nessuna linea dati", ed è corretto che lo dica.
 *
 *    Sulla coda lunga, che è cablata a mano su breakout, c'è invece
 *    un'opportunità: se la coda a 21 pin porta fuori un SDO, collegandolo a
 *    GPIO12 la lettura diventa possibile, e **0x2E (User ID da OTP)
 *    chiuderebbe la questione del part number del controller** — l'ultima cosa
 *    che manca per il driver. Vale la pena cercare quel pin col multimetro
 *    mentre si mappa la coda.
 *
 *    Attenzione però: **GPIO12 è un pin di strapping** (MTDI: al reset decide
 *    la tensione della flash). Su questa board è usato come MISO dummy anche
 *    dall'example upstream di GxEPD2, e con la linea flottante o a livello
 *    basso al boot non succede niente. Se invece ci si collega davvero un SDO,
 *    verificare che al reset non sia forzato alto: in quel caso l'ESP32
 *    potrebbe non avviarsi.
 *
 * 8. I colori, e tutte le combinazioni dei due piani. Dopo il pattern di
 *    identificazione il test stampa un frame a 4 bande numerate con le quattro
 *    combinazioni possibili dei piani 0x24 e 0x26, e poi i colori pieni. Serve
 *    a stabilire quale colore rende l'accent su QUESTO film (rosso o giallo:
 *    la famiglia SOLUM ha entrambe le varianti) e se la quarta combinazione,
 *    quella che il driver non genera mai, è un colore a sè.
 *
 * 9. L'addressing a finestra parziale con x diverso da zero. Un frame scrive
 *    tre box da 64x64 a x = 0, 448, 896 con la propria finestra ciascuno, più
 *    un box accent sfalsato: è il percorso di _setPartialRamArea e
 *    writeImagePart del driver, e nessun altro frame lo tocca perchè tutti gli
 *    altri scrivono a larghezza piena. Se questa parte è rotta, ogni scrittura
 *    del driver che non parta dal bordo sinistro è sbagliata.
 *
 * 10. I due tempi di power on e power off (0x22 = 0xC0 / 0xC3), che tarano
 *    power_on_time e power_off_time del driver, e il refresh, che tara
 *    full_refresh_time.
 *
 * 11. Quale parametro di deep sleep accetta il modulo: 0x10 = 0x03 (A[1:0]=11,
 *    l'unico che il datasheet definisce) contro 0x11 (A[1:0]=01, quello del
 *    driver stock 1160c), con il BUSY come testimone e un reset hardware in
 *    mezzo. Decide il byte che hibernate() deve mandare.
 *
 * 12. Se il bus tiene i 20 MHz che il driver usa per default: l'ultimo frame
 *    riscrive il pattern di identificazione a SPI_HZ_FAST. Se esce identico al
 *    primo il default è buono, se esce sporco il driver deve stare più basso.
 *
 * 13. Il livello del BUSY **a riposo, prima di qualunque comando**. È la prima
 *    riga del report e la prima da guardare: su un pin flottante il livello può
 *    restare alto, e allora ogni attesa va in timeout e ogni detection sembra
 *    fallita anche senza che il controller abbia ricevuto niente.
 *
 * NON misurato di proposito: il DISPLAY Mode 2 (0x22 = 0xFF / 0xFC), cioè il
 * banco waveform differenziale. Sul 9.7", stesso silicio, è già stabilito che
 * usa la seconda RAM come frame precedente e che su un pannello a 3 colori
 * quella RAM è l'accent, quindi non è utilizzabile per il fast partial update:
 * hasFastPartialUpdate = false nel driver non è un'assunzione ma una
 * conseguenza. Idem le varianti di 0x3C (border) e la LUT via 0x32, che si
 * toccano solo se i colori pieni mostrano ghosting.
 *
 * SEQUENZA DI UNA ESECUZIONE
 *   fase probe:  init -> prova di vita (pattern hardware, power on, HV/VCI
 *                detect, registri) -> frame 1: pattern di identificazione
 *                (righelli, angoli, lettera) -> frame 2: 4 bande numerate con
 *                le combinazioni dei due piani -> frame 3..5: colori pieni, se
 *                SHOW_SOLID_COLORS -> deep sleep -> riepilogo
 *   fase driver: init del driver (che riparte da un reset hardware, quindi
 *                sveglia il pannello) -> clearScreen bianco / nero / accent ->
 *                frame di 5 tile, uno a cavallo della giunzione -> hibernate
 *   in coda:     scheda di osservazione, comune alle due fasi
 *   Fra un frame e l'altro il test si ferma: il refresh successivo cancella il
 *   precedente e il test non può vedere il pannello al posto tuo.
 *
 * PROCEDURA
 *   1. TEST_TARGET = TEST_CODA_CORTA, INIT_CANDIDATE = CAND_MINIMAL. Flasha,
 *      guarda il pannello, annota. È la configurazione che già stampa, quindi
 *      serve da riferimento per tutte le altre.
 *   2. Ripeti con le altre due candidate di init, sulla stessa coda. Il
 *      confronto dice quale sequenza tenere nel driver.
 *   3. Ripeti con MUX_LINES = 680 invece di 384: se il refresh dura
 *      sensibilmente di più a parità di pixel visibili, la durata è
 *      proporzionale alle gate line programmate e il conteggio del punto 2 è
 *      confermato da un secondo lato.
 *   4. TEST_TARGET = TEST_CODA_LUNGA, ripeti dal punto 1. Se non si vede
 *      niente, la sezione "altra coda" del report dice se il silicio è vivo.
 *
 * HARDWARE
 *   - Board: Waveshare E-Paper ESP32 Driver Board (HSPI: SCK=13, MISO=12,
 *     MOSI=14). La coda corta è quella nel connettore interno della board, ed
 *     è quella che risponde.
 *   - Coda lunga: breakout esterno cablato come da docs/122c/connessioni.html,
 *     con CS=GPIO33 e BUSY=GPIO35.
 *   - SPI a 4 MHz: margine per il bring-up, il driver poi userà 20 MHz.
 * ---------------------------------------------------------------------------
 */

#include <SPI.h>

// #####################################################################
// ##                                                                 ##
// ##                        I M P O S T A Z I O N I                  ##
// ##                                                                 ##
// ##   Tutto quello che si cambia per una prova sta in questo blocco. ##
// ##   Sotto la riga FINE IMPOSTAZIONI ci sono solo costanti derivate ##
// ##   e codice: lì non c'è niente da regolare.                      ##
// ##                                                                 ##
// ##   Ogni sezione è marcata [OBBLIGATORIO] o [OPZIONALE] e porta il ##
// ##   suo costo in tempo. Un refresh su questo pannello è dell'ordine ##
// ##   dei 20 s, una pausa di osservazione dura OBSERVE_MS.           ##
// ##                                                                 ##
// ##   DURATA CON I DEFAULT: circa 11 minuti, così composti           ##
// ##     fase probe     7 refresh + 6 pause .............. ~6:50      ##
// ##     passaggio fra le due fasi, 1 pausa .............. ~0:45      ##
// ##     fase driver    4 refresh + 3 pause .............. ~3:35      ##
// ##                                                                 ##
// ##   COME ACCORCIARLA                                               ##
// ##     SHOW_SOLID_COLORS 0 ....... -3 refresh, -3 pause  -3:15      ##
// ##     SHOW_FAST_CLOCK_FRAME 0 ... -1 refresh, -1 pause  -1:05      ##
// ##     OBSERVE_MS 0 .............. via tutte le pause    -7:30      ##
// ##     RUN_DRIVER_PHASE 0 ........                       -4:20      ##
// ##     RUN_PROBE_PHASE 0 .........                       -6:50      ##
// ##   Giro minimo utile: solo probe, senza colori pieni, senza frame ##
// ##   a 20 MHz e senza pause = 4 refresh, circa 1:20.                ##
// ##                                                                 ##
// #####################################################################

// ---------------------------------------------------------------------
// 1) FASI DA ESEGUIRE            [OBBLIGATORIO: almeno una a 1]  [0 | 1]
// ---------------------------------------------------------------------
/**
 * Le due fasi sono indipendenti e misurano cose diverse.
 *
 *   RUN_PROBE_PHASE   diagnostica a SPI diretta su UNA coda alla volta: non usa
 *                     nè GxEPD2 nè il driver custom, quindi misura il SILICIO.
 *                     Costo: 7 refresh + 6 pause, ~6:50 con i default.
 *   RUN_DRIVER_PHASE  verifica del driver custom su ENTRAMBE le code:
 *                     clearScreen(), dispatch master/slave, geometria dello
 *                     split. Misura il DRIVER, non il pannello.
 *                     Costo: 4 refresh + 3 pause, ~3:35 con i default.
 *
 * Tenerle nello stesso sketch serve alla distinzione che conta durante il
 * bring-up: se il probe stampa e la fase driver no, il difetto è nel driver.
 */
#define RUN_PROBE_PHASE    1
#define RUN_DRIVER_PHASE   1

/**
 * DRIVER_DUAL governa come la fase driver costruisce il driver. [OBBLIGATORIO]
 *   1 = entrambe le code, ruoli fissi (master = connettore interno).
 *   0 = una coda sola, e il master diventa quella scelta da TEST_TARGET.
 * Con una coda sola cablata va tenuto a 0: vedi il punto 7c.
 */
#define DRIVER_DUAL        1

// ---------------------------------------------------------------------
// 2) CODA FFC SOTTO TEST                                 [OBBLIGATORIO]
//    valori: TEST_CODA_CORTA | TEST_CODA_LUNGA
// ---------------------------------------------------------------------
// I due valori ammessi; non si toccano, si sceglie con TEST_TARGET.
#define TEST_CODA_CORTA   1   // connettore interno della board: quella che risponde
#define TEST_CODA_LUNGA   2   // breakout esterno cablato a mano

#define TEST_TARGET       TEST_CODA_CORTA

// ---------------------------------------------------------------------
// 3) SEQUENZA DI INIT                                    [OBBLIGATORIO]
//    valori: CAND_MINIMAL | CAND_SOLUM | CAND_OEPL
//    Da ripetere una volta per candidata: è il confronto che dice su
//    quale driver conviene basare il custom. Tre giri, non uno.
// ---------------------------------------------------------------------
#define CAND_MINIMAL      1   // stile GxEPD2_1160c_GDEY116Z91: SWRESET + border
#define CAND_SOLUM        2   // stile driver 9.7" della libreria / 1330c
#define CAND_OEPL         3   // init di fabbrica OEPL per il 9.7" SOLUM

#define INIT_CANDIDATE    CAND_MINIMAL

// ---------------------------------------------------------------------
// 4) GATE LINE PROGRAMMATE NEL MUX     [OPZIONALE: il default va bene]
//    valori utili: 384 (misurato) | 680 (POR del registro)
//    Il secondo giro a 680 costa un'esecuzione intera e serve solo a
//    confermare che la durata del refresh scala con le linee programmate.
// ---------------------------------------------------------------------
/**
 * Gate line scritte nel MUX (cmd 0x01). 384 è quanto il pannello ha mostrato di
 * avere per controller. CAND_MINIMAL non scrive il MUX di proposito, quindi in
 * quella candidata questo valore vale solo per la finestra RAM.
 */
static const uint16_t MUX_LINES = 384;

// ---------------------------------------------------------------------
// 5) FRAME OPZIONALI E DURATA                              [OPZIONALE]
// ---------------------------------------------------------------------
/**
 * Colori pieni: bianco, nero, accent su tutta la banda. [OPZIONALE, +3:15]
 *
 * ABILITARE SOLO PER VALUTARE LA RESA CROMATICA. Servono a giudicare uniformità
 * e ghosting, che il frame a bande non mostra; le bande da sole danno già i
 * colori e le quattro combinazioni dei piani. Con la fase driver attiva gli
 * stessi tre colori vengono ristampati da clearScreen(), quindi qui la
 * ridondanza è sacrificabile.
 */
#define SHOW_SOLID_COLORS   1

/**
 * Ripetizione del pattern al clock del driver. [OPZIONALE, +1:05]
 *
 * Verifica una cosa sola, ma che nessun altro frame verifica: che il bus tenga
 * i 20 MHz che il driver usa per default. Una volta accertato, nei giri
 * successivi si può spegnere.
 */
#define SHOW_FAST_CLOCK_FRAME  1

/**
 * Pausa di osservazione fra due frame. [OPZIONALE, ma vedi sotto]
 *
 * Il refresh successivo cancella il precedente e il test non può guardare il
 * pannello al posto tuo: con 0 non si ferma, e va bene solo se stai cronometrando
 * e non guardando. Con i default le pause sono 10 e pesano 7:30 sugli 11 minuti.
 */
static const uint32_t OBSERVE_MS = 45000;

// ---------------------------------------------------------------------
// 6) CLOCK SPI                          [OPZIONALE: i default vanno bene]
// ---------------------------------------------------------------------
// Clock di lavoro del test: margine per il bring-up, ed è anche quello che usa
// l'example board-specific di GxEPD2 upstream.
static const uint32_t SPI_HZ = 4000000;

/**
 * Clock del frame di verifica finale, usato solo se SHOW_FAST_CLOCK_FRAME. Il
 * driver usa 20 MHz per default e il datasheet SSD1677 dichiara 20 MHz come
 * massimo in scrittura: se il pattern riscritto a questo clock resta integro, il
 * default del driver è verificato invece che assunto.
 */
static const uint32_t SPI_HZ_FAST = 20000000;

// ---------------------------------------------------------------------
// 7) PIN — Waveshare E-Paper ESP32 Driver Board V3        [OBBLIGATORIO]
//    Da cambiare solo se il tuo cablaggio è diverso da quello documentato
//    in docs/122c/connessioni.html.
// ---------------------------------------------------------------------
// 7a. Segnali condivisi da tutte le code. Sono il pinout della board: la board
//     usa i pin HSPI con SCK e MOSI scambiati, da cui il remap obbligatorio in
//     hspi.begin(). MISO è un dummy, il FPC interno non ha SDO (punto 7 in testa
//     al file).
static const int PIN_DC   = 27;
static const int PIN_RST  = 26;
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;

// 7b. La coda sotto test e l'altra, che seguono TEST_TARGET. Etichetta e lettera
//     del pattern seguono la stessa scelta, così il report e il pannello dicono
//     la stessa cosa.
//
//     ATTENZIONE su GPIO33: nello schematico della board non compare fra le net
//     (ci sono 32, 34, 35 su pin consecutivi dell'header), quindi molto
//     probabilmente non è portato fuori. Se la coda lunga non risponde, questa è
//     la prima cosa da verificare, prima di sospettare il pannello: in tal caso
//     il candidato è GPIO32.
#if TEST_TARGET == TEST_CODA_CORTA
  static const int  PIN_CS         = 15;
  static const int  PIN_BUSY       = 25;
  static const int  PIN_CS_OTHER   = 33;
  static const int  PIN_BUSY_OTHER = 35;
  static const char TARGET_LABEL[] = "CODA CORTA (connettore interno, CS=15, BUSY=25)";
  static const char TARGET_GLYPH   = 'C';
#elif TEST_TARGET == TEST_CODA_LUNGA
  static const int  PIN_CS         = 33;
  static const int  PIN_BUSY       = 35;
  static const int  PIN_CS_OTHER   = 15;
  static const int  PIN_BUSY_OTHER = 25;
  static const char TARGET_LABEL[] = "CODA LUNGA (breakout esterno, CS=33, BUSY=35)";
  static const char TARGET_GLYPH   = 'L';
#else
  #error "TEST_TARGET deve essere TEST_CODA_CORTA o TEST_CODA_LUNGA"
#endif

/**
 * 7c. Pin della fase driver, che seguono DRIVER_DUAL.
 *
 * Con DRIVER_DUAL si pilotano entrambe le code insieme, quindi i ruoli sono
 * fissi: master è la coda nel connettore interno della board, slave quella sul
 * breakout esterno.
 *
 * Senza DRIVER_DUAL è cablata una coda sola, e il master diventa quella scelta
 * da TEST_TARGET: così le due fasi parlano allo stesso silicio. Con il master
 * fisso a 15, provando la sola coda lunga, il probe misurerebbe una coda e la
 * fase driver ne piloterebbe un'altra non collegata, e il driver sembrerebbe
 * rotto quando invece è muto il cablaggio.
 */
#if DRIVER_DUAL
static const int PIN_CS_MASTER   = 15;
static const int PIN_BUSY_MASTER = 25;
static const int PIN_CS_SLAVE    = 33;
static const int PIN_BUSY_SLAVE  = 35;
#else
static const int PIN_CS_MASTER   = PIN_CS;
static const int PIN_BUSY_MASTER = PIN_BUSY;
#endif

// #####################################################################
// ##                     F I N E   I M P O S T A Z I O N I           ##
// #####################################################################

// ===== Dipendenze che seguono le impostazioni ========================

#if RUN_DRIVER_PHASE
  // L'ombrello include il driver giusto e ne espone il nome come
  // GxEPD2_SOLUM_DRIVER_CLASS: lo sketch non nomina mai la classe concreta.
  #define SOLUM_PANEL_122C
  #include <GxEPD2_SOLUM.h>
#endif

// ===== Etichette derivate dalle impostazioni =========================

#if INIT_CANDIDATE == CAND_MINIMAL
  static const char CAND_LABEL[] = "CAND_MINIMAL (stile GxEPD2_1160c_GDEY116Z91)";
#elif INIT_CANDIDATE == CAND_SOLUM
  static const char CAND_LABEL[] = "CAND_SOLUM (stile driver 9.7\" / GxEPD2_1330c)";
#elif INIT_CANDIDATE == CAND_OEPL
  static const char CAND_LABEL[] = "CAND_OEPL (init di fabbrica OEPL 9.7\")";
#else
  #error "INIT_CANDIDATE deve essere CAND_MINIMAL, CAND_SOLUM o CAND_OEPL"
#endif

// ===== Costanti del pannello, non impostazioni =======================
// Sono fatti del silicio e della geometria misurata: cambiarli qui non
// riconfigura la prova, la falsa.

static const uint16_t SRC       = 960;           // source sull'asse RAM X
static const uint16_t ROW_BYTES = SRC / 8;       // 120 byte per riga piena
static const uint16_t PART_ROW_MAX = ROW_BYTES;  // buffer di riga più grande possibile

// Sul SSD16xx il BUSY è attivo alto, come il busy_level = HIGH del driver
static const int BUSY_ACTIVE = HIGH;

// Banda attesa per controller: il pattern la marca per renderla riconoscibile
static const uint16_t EXPECTED_BAND = 384;

// ===== Oggetti di bus ================================================

SPIClass hspi(HSPI);
// Non const: il frame di verifica la riassegna a SPI_HZ_FAST.
static SPISettings spiSettings(SPI_HZ, MSBFIRST, SPI_MODE0);

// Tempi raccolti lungo il test, riassunti in coda
static uint32_t planeMs24 = 0;
static uint32_t planeMs26 = 0;
static int32_t  refreshMs = -1;
static bool     otherBusyMoved = false;
static int32_t  patternMs47 = -1;   // pattern hardware sul piano B/N
static int32_t  patternMs46 = -1;   // pattern hardware sull'accent
static int32_t  hvDetectMs = -1;    // BUSY della HV Ready Detection
static int32_t  vciDetectMs = -1;   // BUSY della VCI Detection
static int16_t  statusRead = -1;    // status 0x2F, se la lettura è valida
static int32_t  bandsRefreshMs = -1;         // refresh del frame a bande
static int32_t  solidRefreshMs[3] = { -1, -1, -1 };  // bianco, nero, accent
static int32_t  driverMs[3] = { -1, -1, -1 };        // clearScreen dalla fase driver
static int32_t  driverTilesMs = -1;                  // refresh del frame dei tile
static int32_t  powerOnMs = -1;       // BUSY del power on 0xC0
static int32_t  powerOffMs = -1;      // BUSY del power off 0xC3
static int32_t  partialMs = -1;       // refresh del frame a finestre parziali
static int32_t  fastMs = -1;          // refresh del frame riscritto a 20 MHz
static uint32_t fastPlaneMs = 0;      // push di un piano a 20 MHz
static bool     busyStuckAtRest = false;  // BUSY già alto prima di ogni comando
static bool     sleepBusy03 = false;  // BUSY dopo 0x10 = 0x03
static bool     sleepBusy11 = false;  // BUSY dopo 0x10 = 0x11

/**
 * Font 5x7 per le etichette dei righelli e per la lettera della coda. Una riga
 * per elemento, bit 4 = colonna più a sinistra. Scala e offset X sono sempre
 * multipli di 8, così ogni colonna del font copre byte RAM interi e la
 * composizione delle righe non richiede mascheramento a livello di bit.
 */
static const uint8_t GLYPH_W = 5;
static const uint8_t GLYPH_H = 7;

static const uint8_t GLYPHS[12][GLYPH_H] =
{
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
  { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },   // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   // 5
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   // 6
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   // 7
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   // 9
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   // C
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   // L
};

static const uint8_t GLYPH_C = 10;
static const uint8_t GLYPH_L = 11;

// --- primitive di bus ------------------------------------------------

/**
 * Invia un byte di comando: D/C basso. Ordine delle operazioni identico a
 * GxEPD2_EPD::_writeCommand, D/C riportato alto in coda incluso.
 */
static void writeCommand(uint8_t c)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  hspi.transfer(c);
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  hspi.endTransaction();
}

// Invia un byte di parametro: D/C alto, come GxEPD2_EPD::_writeData
static void writeData(uint8_t d)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  hspi.transfer(d);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/** Attende la discesa del BUSY. Ritorna i ms attesi, oppure -1 al timeout. */
static int32_t waitBusy(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
  {
    if ((millis() - t0) > timeout_ms)
      return -1;
    delay(1);
  }
  return (int32_t)(millis() - t0);
}

static void resetPanel()
{
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
}

// Finestra RAM e cursore. Stessa sequenza di comandi del driver custom.
static void setRamWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
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

// --- init: le tre candidate -----------------------------------------

/**
 * Blocco di configurazione comune alle candidate che lo usano. Il MUX
 * programma MUX_LINES gate line: il registro vuole (linee - 1) su 10 bit, in
 * due byte little endian, più un terzo byte di direzione di scansione a 0.
 */
static void writeSoftStartAndMux()
{
  writeCommand(0x0C);   // soft start
  writeData(0xAE);
  writeData(0xC7);
  writeData(0xC3);
  writeData(0xC0);
  writeData(0x80);
  const uint16_t mux = MUX_LINES - 1;
  writeCommand(0x01);   // driver output control
  writeData(mux % 256);
  writeData(mux / 256);
  writeData(0x00);
}

/**
 * Esegue la sequenza di init della candidata selezionata e cronometra ogni
 * passo. Il BUSY dopo lo SWRESET viene osservato: la durata reale del reset
 * interno diventa un dato invece di una stima.
 */
static void initPanel()
{
  const uint32_t tReset = micros();
  resetPanel();
  const uint32_t usReset = micros() - tReset;

  writeCommand(0x12);   // SWRESET
  const uint32_t tSw = millis();
  uint32_t riseMs = 0;
  bool rose = false;
  while ((millis() - tSw) < 50)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      rose = true;
      riseMs = millis() - tSw;
      break;
    }
  }
  int32_t swBusy = -1;
  if (rose)
    swBusy = waitBusy(1000);
  // il controller ignora i comandi per ~100-300 ms dopo lo SWRESET
  const uint32_t swElapsed = millis() - tSw;
  if (swElapsed < 200)
    delay(200 - swElapsed);

  const uint32_t tCfg = micros();

#if INIT_CANDIDATE == CAND_MINIMAL
  /**
   * Init corta: solo il border waveform, tutto il resto ai default POR. Il MUX
   * resta a 680 gate line, quindi il controller scandisce anche le righe che il
   * pannello non ha.
   */
  writeCommand(0x3C);
  writeData(0x01);      // LUT1, bianco

#elif INIT_CANDIDATE == CAND_SOLUM
  writeSoftStartAndMux();
  writeCommand(0x3C);   // border waveform
  writeData(0x01);
  writeCommand(0x18);   // sensore di temperatura interno
  writeData(0x80);
  writeCommand(0x11);   // entry mode: X e Y crescenti
  writeData(0x03);

#elif INIT_CANDIDATE == CAND_OEPL
  /**
   * Init di fabbrica del 9.7" SOLUM secondo OEPL. Parte dai due pattern
   * hardware, che riempiono le RAM senza passare dal bus, e chiude con
   * 0x21 = 0x08 0x00: in quel codice è il fix di un'immagine che altrimenti
   * esce ribaltata, quindi è la candidata che tocca il verso della banda.
   */
  writeCommand(0x46);   // auto write RED RAM for regular pattern
  writeData(0xF7);
  waitBusy(2000);
  writeCommand(0x47);   // auto write B/W RAM for regular pattern
  writeData(0xF7);
  waitBusy(2000);
  writeSoftStartAndMux();
  writeCommand(0x11);   // entry mode
  writeData(0x02);
  setRamWindow(0, 0, SRC, MUX_LINES);
  writeCommand(0x3C);   // border waveform
  writeData(0x01);
  writeCommand(0x18);   // sensore di temperatura interno
  writeData(0x80);
  writeCommand(0x22);   // display update sequence, caricata ma non attivata
  writeData(0xF7);
  writeCommand(0x21);   // display update control 1
  writeData(0x08);
  writeData(0x00);
#endif

  const uint32_t usCfg = micros() - tCfg;

  Serial.printf("  reset hardware      %6lu us\n", (unsigned long)usReset);
  if (!rose)
    Serial.println(F("  SWRESET             BUSY non è salito entro 50 ms"));
  else if (swBusy < 0)
    Serial.printf("  SWRESET             BUSY salito dopo %lu ms, ancora alto a 1000 ms\n",
                  (unsigned long)riseMs);
  else
    Serial.printf("  SWRESET             BUSY salito dopo %lu ms, alto per %ld ms\n",
                  (unsigned long)riseMs, (long)swBusy);
  Serial.printf("  config candidata    %6lu us\n", (unsigned long)usCfg);
}

// --- composizione del pattern ---------------------------------------

/**
 * Dipinge un intervallo orizzontale di byte interi dentro la riga. x0 e w
 * devono essere multipli di 8: tutti gli elementi del pattern sono allineati al
 * byte, quindi qui non serve mascheramento.
 */
static void paintSpan(uint8_t* row, int32_t x0, int32_t w, uint8_t value)
{
  if (w <= 0) return;
  int32_t from = x0 / 8;
  int32_t to   = (x0 + w) / 8;
  if (from < 0) from = 0;
  if (to > ROW_BYTES) to = ROW_BYTES;
  for (int32_t i = from; i < to; ++i)
    row[i] = value;
}

/**
 * Dipinge la riga y di un glifo scalato, se quella riga lo attraversa. scale
 * multiplo di 8: ogni colonna del font diventa un numero intero di byte.
 */
static void paintGlyph(uint8_t* row, int16_t y, uint8_t idx,
                       int32_t gx, int32_t gy, int32_t scale, uint8_t value)
{
  const int32_t h = (int32_t)GLYPH_H * scale;
  if (y < gy || y >= gy + h) return;
  const uint8_t bits = GLYPHS[idx][(y - gy) / scale];
  for (uint8_t c = 0; c < GLYPH_W; ++c)
  {
    if (bits & (0x10 >> c))
      paintSpan(row, gx + (int32_t)c * scale, scale, value);
  }
}

/**
 * Dipinge un numero decimale come sequenza di glifi. Ritorna la larghezza in
 * px occupata, così il chiamante sa dove finisce l'etichetta.
 */
static int32_t paintNumber(uint8_t* row, int16_t y, uint16_t n,
                           int32_t gx, int32_t gy, int32_t scale, uint8_t value)
{
  uint8_t digits[5];
  uint8_t nd = 0;
  do {
    digits[nd++] = n % 10;
    n /= 10;
  } while (n > 0 && nd < sizeof(digits));
  const int32_t advance = (int32_t)(GLYPH_W + 1) * scale;
  for (uint8_t i = 0; i < nd; ++i)
    paintGlyph(row, y, digits[nd - 1 - i], gx + i * advance, gy, scale, value);
  return nd * advance;
}

// Geometria del pattern, tutta allineata al byte
static const int32_t CORNER      = 96;   // lato dei blocchi d'angolo
static const int32_t LABEL_SCALE = 8;    // glifi dei righelli: 40x56 px
static const int32_t BIG_SCALE   = 16;   // lettera della coda: 80x112 px
static const int32_t YRULE_TICK  = 64;   // lunghezza del tick del righello Y
static const int32_t YLABEL_X    = 104;  // oltre il blocco d'angolo
static const int32_t XLABEL_Y    = 8;

/**
 * Compone la riga y del piano B/N. Polarità SSD16xx del cmd 0x24: bit 1 =
 * bianco, bit 0 = nero, quindi si parte da 0xFF e si spegne dove serve nero.
 */
static void composeRowBW(int16_t y, uint8_t* row, uint16_t gate)
{
  memset(row, 0xFF, ROW_BYTES);

  // Cornice di 4 px: delimita l'area programmata. Il bordo inferiore si vede
  // solo se il controller pilota davvero tutte le gate line programmate.
  if (y < 4 || y >= (int16_t)gate - 4)
  {
    memset(row, 0x00, ROW_BYTES);
    return;
  }
  paintSpan(row, 0, 8, 0x00);
  paintSpan(row, SRC - 8, 8, 0x00);

  // Blocco nero nell'origine RAM (0,0): ancora del verso su entrambi gli assi.
  if (y < CORNER)
    paintSpan(row, 0, CORNER, 0x00);

  // Scaletta diagonale, un byte per riga: uno shift di stride la spezza.
  paintSpan(row, ((int32_t)y * ROW_BYTES / gate) * 8, 8, 0x00);

  // Righello Y: tick di 4 righe ogni 64, con etichetta numerica. La riga più
  // alta ancora leggibile sul pannello è il numero di gate line reali.
  for (int32_t ty = 0; ty < (int32_t)gate; ty += 64)
  {
    if (y >= ty && y < ty + 4)
      paintSpan(row, 0, YRULE_TICK, 0x00);
    if (ty > 0)
      paintNumber(row, y, (uint16_t)ty, YLABEL_X, ty, LABEL_SCALE, 0x00);
  }

  // Righello X: tick ogni 128 px con etichetta, nella fascia alta.
  for (int32_t tx = 128; tx < SRC; tx += 128)
  {
    if (y >= XLABEL_Y && y < XLABEL_Y + 24)
      paintSpan(row, tx, 8, 0x00);
    paintNumber(row, y, (uint16_t)tx, tx + 16, XLABEL_Y + 32, LABEL_SCALE, 0x00);
  }

  // Lettera della coda sotto test, al centro della banda attesa.
  paintGlyph(row, y, (TARGET_GLYPH == 'C') ? GLYPH_C : GLYPH_L,
             SRC / 2 - 40, EXPECTED_BAND / 2 - 56, BIG_SCALE, 0x00);
}

/**
 * Compone la riga y del piano accent. Polarità del cmd 0x26: bit 1 = accent
 * acceso, quindi si parte da 0x00.
 *
 * Due elementi soltanto, entrambi fuori dai righelli per non sovrapporre
 * accent e nero sugli stessi pixel: la combinazione dei due piani a 1 non è
 * ciò che questo test vuole misurare.
 */
static void composeRowRED(int16_t y, uint8_t* row, uint16_t gate)
{
  memset(row, 0x00, ROW_BYTES);

  // Blocco accent nell'angolo opposto in X, alla stessa altezza dell'origine:
  // resta visibile qualunque sia il numero di gate line reali.
  if (y < CORNER)
    paintSpan(row, SRC - CORNER, CORNER, 0xFF);

  // Barra accent sulle ultime 64 righe della banda attesa: se il controller
  // pilota esattamente 384 gate, questa barra chiude il bordo inferiore.
  if (y >= (int16_t)EXPECTED_BAND - 64 && y < (int16_t)EXPECTED_BAND)
    paintSpan(row, 256, SRC - 256, 0xFF);
}

/**
 * Riversa un piano riga per riga dentro la finestra RAM piena. Blocchi da una
 * riga, 120 byte, che è il percorso di _writeImage del driver. Ritorna i ms
 * spesi, che sono il costo per piano di un frame.
 */
static uint32_t writePlane(uint8_t planeCommand, uint16_t gate,
                           void (*compose)(int16_t, uint8_t*, uint16_t))
{
  uint8_t row[ROW_BYTES];
  setRamWindow(0, 0, SRC, gate);
  writeCommand(planeCommand);

  const uint32_t t0 = millis();
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (int16_t y = 0; y < (int16_t)gate; ++y)
  {
    compose(y, row, gate);
    hspi.writeBytes(row, ROW_BYTES);
  }
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  return millis() - t0;
}

// --- refresh ---------------------------------------------------------

/**
 * Attende la fine del refresh riportando le fasi: il BUSY che si rialza entro
 * 800 ms dice che il refresh procede in più passate.
 */
static int32_t waitRefresh(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  int phase = 0;
  while (true)
  {
    ++phase;
    const uint32_t tPhase = millis();
    uint32_t nextLog = 2500;
    while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      if ((millis() - t0) > timeout_ms)
        return -1;
      if ((millis() - tPhase) >= nextLog)
      {
        Serial.printf("    fase %d in corso da %lu ms\n",
                      phase, (unsigned long)(millis() - tPhase));
        nextLog += 2500;
      }
      // il BUSY dell'altra coda è un testimone: se si muove, i due
      // controller condividono qualcosa oltre al bus
      if (digitalRead(PIN_BUSY_OTHER) == BUSY_ACTIVE)
        otherBusyMoved = true;
      delay(1);
    }
    Serial.printf("    fase %d conclusa: BUSY alto per %lu ms\n",
                  phase, (unsigned long)(millis() - tPhase));
    const uint32_t tIdle = millis();
    bool again = false;
    while ((millis() - tIdle) < 800)
    {
      if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
      {
        again = true;
        break;
      }
      delay(1);
    }
    if (!again)
      break;
    Serial.printf("    BUSY risalito dopo %lu ms: refresh multi-fase\n",
                  (unsigned long)(millis() - tIdle));
  }
  Serial.printf("    fasi rilevate: %d\n", phase);
  return (int32_t)(millis() - t0);
}

/**
 * Lancia la display update sequence e ne misura i tempi. Verifica prima di
 * tutto che il BUSY salga: se non sale, il controller non ha preso il comando e
 * niente di quanto segue ha valore.
 */
static int32_t runRefresh()
{
  Serial.println(F("\nrefresh (0x22 = 0xF7 + 0x20), una ventina di secondi"));
  writeCommand(0x22);
  writeData(0xF7);
  writeCommand(0x20);   // master activation

  const uint32_t tAct = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - tAct) < 1000)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    Serial.println(F("ATTENZIONE: BUSY non è mai salito, il controller non risponde"));
    return -1;
  }
  Serial.printf("BUSY salito dopo %lu ms\n", (unsigned long)(millis() - tAct));

  const int32_t ms = waitRefresh(40000);
  if (ms < 0)
    Serial.println(F("ATTENZIONE: timeout BUSY a 40000 ms, refresh non concluso"));
  else
    Serial.printf("refresh concluso in %ld ms\n", (long)ms);
  return ms;
}

// --- registri in lettura --------------------------------------------
/**
 * Clock ridotto per la lettura: in lettura il controller è molto più lento che
 * in scrittura.
 */
static SPISettings spiReadSettings(2500000, MSBFIRST, SPI_MODE0);

// Legge n byte dopo un comando. Stessa convenzione di panel_diagnostic.
static void readRegister(uint8_t cmd, uint8_t* out, uint8_t n)
{
  writeCommand(cmd);
  hspi.beginTransaction(spiReadSettings);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t i = 0; i < n; ++i)
    out[i] = hspi.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Tenta i tre registri in lettura. Lo status 0x2F ha un POR noto (0x01, chip
 * ID 01) e fa da prova di validità del percorso: se non torna, gli altri due
 * vengono dichiarati inattendibili invece di stampare numeri senza senso. I
 * suoi bit 5 e 4 sono i flag di HV Ready e VCI, cioè l'esito esplicito dei
 * rilevatori che la sezione precedente misura col BUSY.
 *
 * Sul FPC 24 pin del 9.7" la linea dati in uscita non c'è, quindi l'esito
 * atteso è "nessuna risposta"; su questa coda a 21 pin non si sa, e se invece
 * risponde il modulo si identifica senza ambiguità.
 */
static void reportRegisters()
{
  uint8_t st = 0xFF;
  readRegister(0x2F, &st, 1);
  const bool plausible = (st != 0x00) && (st != 0xFF);
  Serial.printf("  status 0x2F         0x%02X  %s\n", st,
                plausible ? "plausibile" : "NON plausibile: nessuna linea dati in lettura");
  if (!plausible)
  {
    Serial.println(F("    -> 0x2E e 0x1B non vengono interpretati: senza 0x2F valido"));
    Serial.println(F("       qualunque byte letto è rumore del bus"));
    return;
  }
  statusRead = st;
  Serial.printf("    bit5 HV Ready     %s\n", (st & 0x20) ? "1 = NON pronta" : "0 = pronta");
  Serial.printf("    bit4 VCI          %s\n", (st & 0x10) ? "1 = fuori norma" : "0 = normale");
  Serial.printf("    bit1:0 chip ID    %u\n", (unsigned)(st & 0x03));

  uint8_t id[10];
  memset(id, 0, sizeof(id));
  readRegister(0x2E, id, sizeof(id));
  Serial.print(F("  user ID 0x2E        "));
  for (uint8_t i = 0; i < sizeof(id); ++i) Serial.printf("%02X ", id[i]);
  Serial.print(F(" ascii \""));
  for (uint8_t i = 0; i < sizeof(id); ++i)
    Serial.print((char)(id[i] >= 0x20 && id[i] < 0x7F ? id[i] : '.'));
  Serial.println(F("\""));

  uint8_t t[2] = { 0, 0 };
  readRegister(0x1B, t, sizeof(t));
  const int16_t raw = (int16_t)(((uint16_t)t[0] << 4) | (t[1] >> 4));
  Serial.printf("  temperatura 0x1B    raw 0x%03X = %d C\n",
                (unsigned)(raw & 0x0FFF), (int)(raw / 16));
}

// --- prova di vita e alte tensioni ----------------------------------

/**
 * Riempie un piano col pattern hardware (0x47 per il B/N, 0x46 per l'accent):
 * il controller scrive la propria RAM da sè, senza passare dal bus. Serve a due
 * cose: è una prova di vita che non dipende da 46 KB di push SPI, e alza il
 * BUSY per una durata misurabile. Ritorna i ms di BUSY, -1 al timeout.
 */
static int32_t patternFill(uint8_t patternCommand, uint8_t value, const char* label)
{
  writeCommand(patternCommand);
  writeData(value);
  const uint32_t tAct = millis();
  bool rose = false;
  while ((millis() - tAct) < 200)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { rose = true; break; }
  }
  if (!rose)
  {
    Serial.printf("  pattern %s (0x%02X)  BUSY non è salito: comando non accettato\n",
                  label, patternCommand);
    return -1;
  }
  const int32_t ms = waitBusy(5000);
  if (ms < 0)
    Serial.printf("  pattern %s (0x%02X)  BUSY ancora alto a 5000 ms\n", label, patternCommand);
  else
    Serial.printf("  pattern %s (0x%02X)  BUSY alto per %ld ms\n", label, patternCommand, (long)ms);
  return ms;
}

/**
 * Prova di vita del controller e stato delle alte tensioni, senza guardare un
 * solo pixel. È la sezione che conta sulla coda che non stampa.
 *
 * I due rilevatori interni vogliono CLKEN=1 e ANALOGEN=1, che si ottengono con
 * la sequenza di solo power on (0x22 = 0xC0 + 0x20). Poi:
 *
 *   0x14 HV Ready Detection con A = 0x77: cool down 10ms x (7+1) = 80 ms,
 *        7 cicli, quindi durata massima 560 ms. Il datasheet dice che "la
 *        detection si conclude quando HV è pronta": un BUSY molto più corto del
 *        massimo significa HV arrivata presto, un BUSY che arriva al massimo
 *        significa che non è mai arrivata.
 *   0x15 VCI Detection al livello POR (2.3 V). Qui il datasheet non promette
 *        una conclusione anticipata, quindi la durata dice meno; quello che
 *        conta è che il BUSY reagisca, cioè che il blocco analogico sia vivo.
 *
 * L'esito esplicito di entrambi sta nei bit 5 e 4 dello status 0x2F, che si
 * legge solo se questa coda porta fuori la linea dati: per questo la lettura
 * dei registri viene subito dopo, con la detection ancora fresca.
 */
static void probeLifeAndHighVoltage()
{
  Serial.println(F("\nprova di vita e alte tensioni:"));

  patternMs47 = patternFill(0x47, 0xF7, "B/N   ");
  patternMs46 = patternFill(0x46, 0xF7, "accent");

  writeCommand(0x22);
  writeData(0xC0);        // solo power on: abilita clock e blocco analogico
  writeCommand(0x20);
  powerOnMs = waitBusy(5000);
  if (powerOnMs < 0)
    Serial.println(F("  power on 0xC0       BUSY ancora alto a 5000 ms"));
  else
    Serial.printf("  power on 0xC0       BUSY alto per %ld ms\n", (long)powerOnMs);

  writeCommand(0x14);
  writeData(0x77);        // cool down 80 ms x 7 cicli -> massimo 560 ms
  hvDetectMs = waitBusy(3000);
  if (hvDetectMs < 0)
    Serial.println(F("  HV detect 0x14      BUSY ancora alto a 3000 ms: HV non pronta"));
  else
    Serial.printf("  HV detect 0x14      BUSY alto per %ld ms (massimo programmato 560)\n",
                  (long)hvDetectMs);

  writeCommand(0x15);
  writeData(0x04);        // livello POR, 2.3 V
  vciDetectMs = waitBusy(3000);
  if (vciDetectMs < 0)
    Serial.println(F("  VCI detect 0x15     BUSY ancora alto a 3000 ms"));
  else
    Serial.printf("  VCI detect 0x15     BUSY alto per %ld ms\n", (long)vciDetectMs);

  Serial.println(F("  registri in lettura:"));
  reportRegisters();

  writeCommand(0x22);
  writeData(0xC3);        // solo power off
  writeCommand(0x20);
  powerOffMs = waitBusy(5000);
  if (powerOffMs < 0)
    Serial.println(F("  power off 0xC3      BUSY ancora alto a 5000 ms"));
  else
    Serial.printf("  power off 0xC3      BUSY alto per %ld ms\n", (long)powerOffMs);
}

// --- frame ----------------------------------------------------------

/**
 * Pausa di osservazione con conto alla rovescia sul seriale. Il pannello mostra
 * un risultato che il frame successivo sovrascrive, e il test non può vederlo:
 * l'unico modo di non perderlo è fermarsi.
 */
static void observePause(uint32_t ms, const char* cosaSuccede)
{
  if (ms == 0) return;
  Serial.printf("\nGUARDA IL PANNELLO E ANNOTA: fra %lu s %s\n",
                (unsigned long)(ms / 1000), cosaSuccede);
  for (uint32_t left = ms / 1000; left >= 5; left -= 5)
  {
    Serial.printf("  %lu s\n", (unsigned long)left);
    delay(5000);
  }
}

/**
 * Scrive i due piani e lancia un refresh, cronometrando tutto. Ritorna i ms del
 * refresh, -1 se non è concluso.
 */
static int32_t showFrame(const char* label,
                         void (*composeBW)(int16_t, uint8_t*, uint16_t),
                         void (*composeRED)(int16_t, uint8_t*, uint16_t))
{
  Serial.printf("\n--- frame: %s ---\n", label);
  planeMs24 = writePlane(0x24, MUX_LINES, composeBW);
  planeMs26 = writePlane(0x26, MUX_LINES, composeRED);
  Serial.printf("  piani 0x24 + 0x26   %lu + %lu ms  (%lu byte per piano)\n",
                (unsigned long)planeMs24, (unsigned long)planeMs26,
                (unsigned long)ROW_BYTES * MUX_LINES);
  return runRefresh();
}

/**
 * Frame a bande: le quattro combinazioni dei due piani, una per banda, con la
 * cifra della banda disegnata dentro. Le prime tre sono quelle che il driver
 * genera (bianco, nero, accent); la quarta, BW=0 con accent acceso, non la
 * genera mai, e se rende un colore distinto dalle altre tre allora sul film
 * esiste un quarto stato.
 *
 * La cifra è disegnata sul piano B/N col valore opposto a quello della banda,
 * così resta leggibile sia su fondo bianco sia su fondo nero.
 */
static void composeRowBandsBW(int16_t y, uint8_t* row, uint16_t gate)
{
  const uint16_t bandH = gate / 4;
  const uint8_t  band  = bandH ? (uint8_t)(y / bandH) : 0;
  const bool     bwOn  = (band == 0) || (band == 2);   // bande 1 e 3: BW = 1
  memset(row, bwOn ? 0xFF : 0x00, ROW_BYTES);
  const int32_t gy = (int32_t)band * bandH + bandH - GLYPH_H * LABEL_SCALE - 16;
  paintGlyph(row, y, (uint8_t)(band + 1), 32, gy, LABEL_SCALE, bwOn ? 0x00 : 0xFF);
}

static void composeRowBandsRED(int16_t y, uint8_t* row, uint16_t gate)
{
  const uint16_t bandH = gate / 4;
  const uint8_t  band  = bandH ? (uint8_t)(y / bandH) : 0;
  const bool     redOn = (band == 2) || (band == 3);   // bande 3 e 4: accent = 1
  memset(row, redOn ? 0xFF : 0x00, ROW_BYTES);
}

// Colori pieni: i valori dei due piani li imposta il chiamante.
static uint8_t solidBW  = 0xFF;
static uint8_t solidRED = 0x00;

static void composeRowSolidBW(int16_t, uint8_t* row, uint16_t)
{
  memset(row, solidBW, ROW_BYTES);
}

static void composeRowSolidRED(int16_t, uint8_t* row, uint16_t)
{
  memset(row, solidRED, ROW_BYTES);
}

// --- finestre parziali e box -----------------------------------------

/**
 * Scrive un rettangolo di valore costante passando da una finestra RAM
 * parziale. x e w multipli di 8: la RAM è organizzata a byte, e allinearsi
 * evita di dover mascherare i bit ai bordi.
 *
 * Serve a esercitare l'addressing con **x diverso da zero**, che è il percorso
 * di _setPartialRamArea + writeImagePart del driver e che nessun altro frame di
 * questo test tocca: tutti gli altri scrivono finestre a larghezza piena. Se
 * questa parte non funziona, il driver sbaglia ogni scrittura che non parta dal
 * bordo sinistro, e senza questa prova non si saprebbe.
 */
static void writeBoxConst(uint8_t planeCommand, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h, uint8_t value)
{
  setRamWindow(x, y, w, h);
  writeCommand(planeCommand);
  const uint16_t rowBytes = w / 8;
  uint8_t row[PART_ROW_MAX];
  memset(row, value, rowBytes);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint16_t i = 0; i < h; ++i)
    hspi.writeBytes(row, rowBytes);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Frame delle finestre parziali: fondo bianco a larghezza piena, poi tre box
 * neri da 64x64 scritti uno per uno con la propria finestra, a x = 0, 448, 896,
 * e un quarto box accent a x = 224. Le posizioni sono equispaziate di proposito:
 * tre quadrati allineati e equidistanti dicono che l'addressing X funziona; se
 * scivolano, si sovrappongono o si smarginano, il difetto è nella finestra
 * parziale e il driver va corretto lì prima di ogni altra cosa.
 */
static int32_t showPartialWindowFrame()
{
  Serial.println(F("\n--- frame: finestre parziali (box a x = 0 / 448 / 896) ---"));
  const uint32_t t0 = millis();
  // fondo: bianco sul piano B/N, accent spento
  solidBW = 0xFF; solidRED = 0x00;
  writePlane(0x24, MUX_LINES, composeRowSolidBW);
  writePlane(0x26, MUX_LINES, composeRowSolidRED);
  // tre box neri equispaziati: 0x24 a 0x00 = nero
  writeBoxConst(0x24, 0,   160, 64, 64, 0x00);
  writeBoxConst(0x24, 448, 160, 64, 64, 0x00);
  writeBoxConst(0x24, 896, 160, 64, 64, 0x00);
  // un box accent sfalsato in Y, per provare le due finestre insieme
  writeBoxConst(0x26, 224, 256, 64, 64, 0xFF);
  Serial.printf("  scrittura fondo + 4 box  %lu ms\n", (unsigned long)(millis() - t0));
  return runRefresh();
}

// --- deep sleep: quale parametro accetta il modulo -------------------

/**
 * Prova entrambi i parametri di 0x10 con il BUSY come testimone. Il datasheet
 * SSD1677 definisce solo A[1:0] = 00 (normale) e 11 (deep sleep), cioè 0x03, e
 * dice che in deep sleep il BUSY resta alto; 0x11 ha A[1:0] = 01, che nella
 * tabella non c'è, ed è il valore che usa il driver stock 1160c. Quale dei due
 * il modulo accetti davvero decide il byte che hibernate() deve mandare.
 *
 * Fra le due prove serve un reset hardware: dal deep sleep non si esce
 * altrimenti, e senza il reset la seconda misura leggerebbe la coda della prima.
 */
static void probeDeepSleep()
{
  Serial.println(F("\ndeep sleep, quale parametro di 0x10 fa dormire il controller:"));

  writeCommand(0x10);
  writeData(0x03);
  delay(50);
  sleepBusy03 = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  Serial.printf("  0x10 = 0x03 (A[1:0]=11, da datasheet)  BUSY %s\n",
                sleepBusy03 ? "ALTO -> dorme" : "basso -> non dorme");

  resetPanel();
  writeCommand(0x12);   // SWRESET, per ripartire da uno stato noto
  delay(200);

  writeCommand(0x10);
  writeData(0x11);
  delay(50);
  sleepBusy11 = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  Serial.printf("  0x10 = 0x11 (A[1:0]=01, fuori tabella)  BUSY %s\n",
                sleepBusy11 ? "ALTO -> dorme" : "basso -> non dorme");
}

// =====================================================================
// FASE PROBE: diagnostica a SPI diretta, una coda alla volta
// =====================================================================
#if RUN_PROBE_PHASE

static void runProbePhase()
{
  Serial.println(F("\n=================================================="));
  Serial.println(F(" FASE PROBE — SPI diretta, nessuno strato software"));
  Serial.print  (F(" coda      : ")); Serial.println(TARGET_LABEL);
  Serial.print  (F(" init      : ")); Serial.println(CAND_LABEL);
  Serial.printf (" MUX       : %u gate line programmate\n", MUX_LINES);
  Serial.printf (" pattern   : %u source x %u gate, banda attesa %u\n",
                 SRC, MUX_LINES, EXPECTED_BAND);
  Serial.println(F("=================================================="));

  Serial.printf("ambiente: CPU %lu MHz, APB %lu MHz, SPI %lu MHz, heap %lu B\n",
                (unsigned long)(getCpuFrequencyMhz()),
                (unsigned long)(getApbFrequency() / 1000000UL),
                (unsigned long)(SPI_HZ / 1000000UL),
                (unsigned long)ESP.getFreeHeap());

  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_BUSY, INPUT);

  /**
   * L'altra coda: CS tenuto alto per tutto il test, così quel controller non
   * ascolta il bus; BUSY letto come testimone. RST, SCK, MOSI e DC sono in
   * parallelo per costruzione del cablaggio, quindi l'altro controller subisce
   * comunque il reset: è voluto, parte da uno stato noto.
   */
  pinMode(PIN_CS_OTHER, OUTPUT);
  digitalWrite(PIN_CS_OTHER, HIGH);
  pinMode(PIN_BUSY_OTHER, INPUT);
  Serial.printf("altra coda a riposo: BUSY = %s\n",
                digitalRead(PIN_BUSY_OTHER) == BUSY_ACTIVE ? "ALTO (busy)" : "basso (idle)");

  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  /**
   * BUSY della coda sotto test **prima di qualunque comando**. Serve a non
   * interpretare male tutto il resto: su un pin flottante — e con una coda
   * scollegata lo è — il livello può restare alto, e allora ogni attesa va in
   * timeout e ogni detection sembra fallita anche se il controller non ha mai
   * ricevuto niente. Se questa riga dice ALTO, le misure che seguono valgono
   * solo dopo aver sistemato il cablaggio del BUSY.
   */
  busyStuckAtRest = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  Serial.printf("BUSY della coda sotto test, a riposo: %s\n",
                busyStuckAtRest ? "ALTO -> sospetto pin flottante o controller occupato"
                                : "basso -> livello di riposo corretto");

  Serial.println(F("\ninit:"));
  initPanel();

  /**
   * Prima di stampare qualunque cosa: il controller è vivo? e le alte tensioni
   * salgono? Sono le due domande che si possono rispondere senza vedere un
   * pixel, quindi valgono anche sulla coda che non stampa. Questa fase sporca
   * le RAM col pattern hardware, per questo viene prima dei frame.
   */
  probeLifeAndHighVoltage();

  // Frame 1: identificazione. Righelli numerati, angoli, lettera della coda.
  refreshMs = showFrame("pattern di identificazione", composeRowBW, composeRowRED);
  Serial.printf("\naltra coda durante il refresh: BUSY %s\n",
                otherBusyMoved ? "SI È MOSSO" : "non si è mosso");
  observePause(OBSERVE_MS, "il frame a bande sovrascrive questo pattern");

  /**
   * Frame 2: le quattro combinazioni dei due piani, una per banda numerata.
   * Da qui si legge quale colore rende l'accent su questo film e se la quarta
   * combinazione è un colore a sè.
   */
  bandsRefreshMs = showFrame("4 bande, combinazioni dei piani",
                             composeRowBandsBW, composeRowBandsRED);

  observePause(OBSERVE_MS, "arriva il frame delle finestre parziali");

  /**
   * Frame delle finestre parziali: è l'unico che scrive con x diverso da zero,
   * cioè il percorso di addressing che il driver usa per ogni writeImagePart.
   */
  partialMs = showPartialWindowFrame();

#if SHOW_SOLID_COLORS
  observePause(OBSERVE_MS, "cominciano i colori pieni");

  /**
   * Frame 3..5: colori pieni. Le bande danno i colori, ma non l'uniformità su
   * tutta la banda nè il ghosting fra un colore e il successivo, che sono
   * proprio quello che si giudica guardando un fondo pieno.
   */
  struct SolidTest { const char* nome; uint8_t bw; uint8_t red; };
  static const SolidTest SOLIDI[] = {
    { "BIANCO", 0xFF, 0x00 },
    { "NERO",   0x00, 0x00 },
    { "ACCENT", 0xFF, 0xFF },
  };
  for (uint8_t i = 0; i < sizeof(SOLIDI) / sizeof(SOLIDI[0]); ++i)
  {
    solidBW  = SOLIDI[i].bw;
    solidRED = SOLIDI[i].red;
    solidRefreshMs[i] = showFrame(SOLIDI[i].nome, composeRowSolidBW, composeRowSolidRED);
    if (i + 1 < sizeof(SOLIDI) / sizeof(SOLIDI[0]))
      observePause(OBSERVE_MS, "arriva il colore successivo");
  }
#endif

#if SHOW_FAST_CLOCK_FRAME
  observePause(OBSERVE_MS, "l'ultimo frame ripete il pattern a 20 MHz");

  /**
   * Ultimo frame: lo stesso pattern di identificazione riscritto col clock che
   * il driver usa per default. Se esce identico al primo, i 20 MHz sono buoni;
   * se esce con righe sporche o byte spostati, il driver deve stare più basso.
   */
  Serial.printf("\nripetizione del pattern a %lu MHz\n",
                (unsigned long)(SPI_HZ_FAST / 1000000UL));
  spiSettings = SPISettings(SPI_HZ_FAST, MSBFIRST, SPI_MODE0);
  const uint32_t tFast = millis();
  writePlane(0x24, MUX_LINES, composeRowBW);
  fastPlaneMs = millis() - tFast;
  writePlane(0x26, MUX_LINES, composeRowRED);
  Serial.printf("  piano 0x24 a %lu MHz     %lu ms (contro %lu ms a %lu MHz)\n",
                (unsigned long)(SPI_HZ_FAST / 1000000UL), (unsigned long)fastPlaneMs,
                (unsigned long)planeMs24, (unsigned long)(SPI_HZ / 1000000UL));
  fastMs = runRefresh();
  spiSettings = SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0);
#endif  // SHOW_FAST_CLOCK_FRAME

  probeDeepSleep();

  Serial.println(F("\n---------------- RIEPILOGO ----------------"));
  Serial.printf("piani        %lu + %lu ms per frame\n",
                (unsigned long)planeMs24, (unsigned long)planeMs26);
  Serial.printf("pattern HW   0x47 %ld ms, 0x46 %ld ms\n",
                (long)patternMs47, (long)patternMs46);
  Serial.printf("HV detect    %ld ms su 560 massimi%s\n", (long)hvDetectMs,
                (hvDetectMs >= 0 && hvDetectMs < 500) ? "  -> HV arrivata prima del massimo"
                                                      : "  -> ciclo completo o timeout");
  Serial.printf("VCI detect   %ld ms\n", (long)vciDetectMs);
  Serial.printf("power on/off %ld / %ld ms  -> tarano power_on_time / power_off_time\n",
                (long)powerOnMs, (long)powerOffMs);
  if (statusRead >= 0)
    Serial.printf("status 0x2F  0x%02X (HV %s, VCI %s)\n", (unsigned)statusRead,
                  (statusRead & 0x20) ? "NON pronta" : "pronta",
                  (statusRead & 0x10) ? "fuori norma" : "normale");
  else
    Serial.println(F("status 0x2F  non leggibile: nessuna linea dati su questa coda"));
  if (refreshMs >= 0)
    Serial.printf("refresh      %ld ms con %u gate programmate (%.2f ms per gate line)\n",
                  (long)refreshMs, MUX_LINES, (double)refreshMs / (double)MUX_LINES);
  else
    Serial.println(F("refresh      non concluso"));
  Serial.printf("frame bande  %ld ms\n", (long)bandsRefreshMs);
  Serial.printf("finestre parz %ld ms\n", (long)partialMs);
#if SHOW_FAST_CLOCK_FRAME
  Serial.printf("pattern 20MHz %ld ms refresh, piano in %lu ms\n",
                (long)fastMs, (unsigned long)fastPlaneMs);
#endif
  Serial.printf("deep sleep   0x03 %s, 0x11 %s\n",
                sleepBusy03 ? "dorme" : "no", sleepBusy11 ? "dorme" : "no");
#if SHOW_SOLID_COLORS
  Serial.printf("colori pieni bianco %ld ms, nero %ld ms, accent %ld ms\n",
                (long)solidRefreshMs[0], (long)solidRefreshMs[1], (long)solidRefreshMs[2]);
#endif

}

#endif  // RUN_PROBE_PHASE

// =====================================================================
// FASE DRIVER: verifica del driver custom, non del silicio
// =====================================================================
#if RUN_DRIVER_PHASE

/**
 * Tile 64x64 a scacchi, in PROGMEM: 8 byte per riga, 512 byte in tutto. Serve a
 * esercitare il dispatch del driver con una bitmap vera invece che con un
 * riempimento uniforme, senza allocare i 92160 byte di un frame intero.
 *
 * Il bordo del tile è pieno e un angolo è marcato, così un tile ruotato o
 * specchiato si riconosce a occhio.
 */
static const uint8_t TILE[64 * 8] PROGMEM = {
#define TILE_ROW_FULL   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
#define TILE_ROW_EDGE   0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00
#define TILE_ROW_CHECK  0x00,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0x00
#define TILE_ROW_MARK   0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00
  // 8 righe piene: bordo superiore
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  // 8 righe con l'angolo marcato in alto a sinistra
  TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK,
  TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK,
  // 32 righe a scacchi
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  // 16 righe piene: bordo inferiore
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
};

/**
 * Pinout del driver nella struct uniforme della libreria: l'ordine dei campi è
 * cs, dc, rst, busy, cs2, busy2, sck, miso, mosi. Con DRIVER_DUAL a 0 il
 * secondo controller resta a -1, cioè assente, e le scritture verso la sua
 * banda vengono saltate: è il modo giusto per provare il driver quando è
 * cablata una sola coda.
 */
#if DRIVER_DUAL
static const GxEPD2_SOLUM_Pins DRIVER_PINS{ PIN_CS_MASTER, PIN_DC, PIN_RST, PIN_BUSY_MASTER,
                                            PIN_CS_SLAVE, PIN_BUSY_SLAVE,
                                            PIN_SCK, PIN_MISO, PIN_MOSI };
#else
static const GxEPD2_SOLUM_Pins DRIVER_PINS{ PIN_CS_MASTER, PIN_DC, PIN_RST, PIN_BUSY_MASTER,
                                            -1, -1,
                                            PIN_SCK, PIN_MISO, PIN_MOSI };
#endif

static GxEPD2_SOLUM_DRIVER_CLASS driverDisplay(DRIVER_PINS);

/**
 * Esercita il driver custom sul pannello: gli stessi tre colori pieni della
 * fase probe, ma passando da clearScreen() e dal dispatch a due controller, poi
 * un frame di tile per verificare la geometria dello split.
 *
 * Il senso di avere le due fasi nello stesso sketch: la fase probe misura il
 * SILICIO e non dipende da nessuno strato software, questa misura il DRIVER. Se
 * il probe stampa e questa no, il difetto è nel driver e non nel pannello, che
 * è esattamente la distinzione che serve durante il bring-up.
 *
 * I tile sono piazzati in modo da coprire i quattro angoli e, quello centrale,
 * la giunzione fra le due bande a y = PART_HEIGHT: se il dispatch per righe è
 * giusto quel tile esce intero, se è sbagliato esce tagliato o sdoppiato.
 */
static void runDriverPhase()
{
  Serial.println(F("\n=================================================="));
  Serial.println(F(" FASE DRIVER — GxEPD2_SOLUM_122c_960x768"));
  Serial.printf ("  geometria driver   %ux%u, bande da %ux%u\n",
                 GxEPD2_SOLUM_DRIVER_CLASS::WIDTH, GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT,
                 GxEPD2_SOLUM_DRIVER_CLASS::PART_WIDTH,
                 GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT);
#if DRIVER_DUAL
  Serial.printf ("  modo               DUAL: CS_M=%d BUSY_M=%d, CS_S=%d BUSY_S=%d\n",
                 PIN_CS_MASTER, PIN_BUSY_MASTER, PIN_CS_SLAVE, PIN_BUSY_SLAVE);
#else
  Serial.printf ("  modo               SINGLE: CS=%d BUSY=%d (banda slave non pilotata)\n",
                 PIN_CS_MASTER, PIN_BUSY_MASTER);
#endif
  Serial.println(F("=================================================="));

  /**
   * init() del driver rifà pinMode e riapre il bus, e chiude con un reset
   * hardware: basta a svegliare il pannello dal deep sleep in cui la fase probe
   * lo ha lasciato. selectSPI prima di init() per tenere il clock di bring-up
   * invece dei 20 MHz di default del driver.
   */
  driverDisplay.selectSPI(hspi, SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  driverDisplay.init(115200);

  struct DriverColor { const char* nome; uint8_t black; uint8_t color; };
  static const DriverColor COLORI[] = {
    { "BIANCO", 0xFF, 0x00 },
    { "NERO",   0x00, 0x00 },
    { "ACCENT", 0xFF, 0xFF },
  };
  for (uint8_t i = 0; i < sizeof(COLORI) / sizeof(COLORI[0]); ++i)
  {
    Serial.printf("\nclearScreen %s (black=0x%02X, color=0x%02X)\n",
                  COLORI[i].nome, COLORI[i].black, COLORI[i].color);
    const uint32_t t0 = millis();
    driverDisplay.clearScreen(COLORI[i].black, COLORI[i].color);
    driverMs[i] = (int32_t)(millis() - t0);
    Serial.printf("  concluso in %ld ms\n", (long)driverMs[i]);
    if (i + 1 < sizeof(COLORI) / sizeof(COLORI[0]))
      observePause(OBSERVE_MS, "arriva il colore successivo");
  }

  observePause(OBSERVE_MS, "arriva il frame dei tile");

  /**
   * Frame dei tile: cinque tile da 64x64 sul piano nero, di cui uno a cavallo
   * della giunzione fra le due bande. writeImage scrive senza refresh, quindi
   * si accumulano e si fa un refresh solo alla fine.
   */
  Serial.println(F("\nframe dei tile (writeImage x5 + refresh)"));
  const int16_t H = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT;
  const int16_t W = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::WIDTH;
  const int16_t SPLIT = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT;
  driverDisplay.writeScreenBuffer(0xFF, 0x00);          // fondo bianco, accent spento
  driverDisplay.writeImage(TILE, 0, 0, 64, 64, false, false, true);            // angolo alto sx
  driverDisplay.writeImage(TILE, W - 64, 0, 64, 64, false, false, true);       // angolo alto dx
  driverDisplay.writeImage(TILE, 0, H - 64, 64, 64, false, false, true);       // angolo basso sx
  driverDisplay.writeImage(TILE, W - 64, H - 64, 64, 64, false, false, true);  // angolo basso dx
  driverDisplay.writeImage(TILE, W / 2 - 32, SPLIT - 32, 64, 64, false, false, true); // giunzione
  const uint32_t t0 = millis();
  driverDisplay.refresh(false);
  driverTilesMs = (int32_t)(millis() - t0);
  Serial.printf("  refresh in %ld ms\n", (long)driverTilesMs);

  driverDisplay.hibernate();
  Serial.println(F("driver in hibernate"));
}

#endif  // RUN_DRIVER_PHASE

/**
 * Scheda di osservazione. Il test non può vedere il pannello, quindi si ferma
 * qui e lascia le domande e la mappa esito -> conseguenza.
 */
static void printObservationSheet()
{
  Serial.println(F("\n============ SCHEDA DI OSSERVAZIONE ============"));
  Serial.println(F("Guarda il pannello e annota."));
  Serial.println(F(""));
  Serial.println(F("  si vede qualcosa?                        SI / NO"));
  Serial.println(F("  quale metà del pannello ha reagito ..............."));
  Serial.println(F("  ultima etichetta del righello Y leggibile ........"));
  Serial.println(F("  blocco NERO: in quale angolo ....................."));
  Serial.println(F("  blocco ACCENT: in quale angolo ..................."));
  Serial.println(F("  colore dell'accent (rosso / giallo / altro) ......"));
  Serial.println(F("  i numeri dei righelli sono diritti?      SI / NO"));
  Serial.println(F("  la barra accent chiude il bordo della banda? SI / NO"));
  Serial.println(F(""));
  Serial.println(F("  frame a bande, colore di ognuna:"));
  Serial.println(F("    banda 1  (BW=1 RED=0) ......................."));
  Serial.println(F("    banda 2  (BW=0 RED=0) ......................."));
  Serial.println(F("    banda 3  (BW=1 RED=1) ......................."));
  Serial.println(F("    banda 4  (BW=0 RED=1) ......................."));
  Serial.println(F("  colori pieni: uniformi?                  SI / NO"));
  Serial.println(F("  ghosting fra un colore e il successivo?   SI / NO"));
  Serial.println(F(""));
  Serial.println(F("--- conseguenze, secondo cosa hai osservato ---"));
  Serial.println(F(""));
  Serial.println(F("BUSY GIÀ ALTO A RIPOSO (vedi la riga in testa al report):"));
  Serial.println(F("  è la prima cosa da guardare, perchè invalida il resto: un BUSY"));
  Serial.println(F("  alto prima di ogni comando manda in timeout ogni attesa e fa"));
  Serial.println(F("  sembrare fallita ogni detection. Cause tipiche: il pin non è"));
  Serial.println(F("  cablato (input-only senza pull, quindi flotta), la coda non è"));
  Serial.println(F("  alimentata, oppure BUSY è sul pin sbagliato del FFC. Fino a che"));
  Serial.println(F("  questa riga non dice basso, le altre misure non valgono."));
  Serial.println(F(""));
  Serial.println(F("NON SI VEDE NIENTE, e il BUSY non è mai salito:"));
  Serial.println(F("  il controller non ha preso i comandi. Con l'altra coda che"));
  Serial.println(F("  invece stampa, la differenza è nel cablaggio di QUESTA coda:"));
  Serial.println(F("  ordine dei pin, rail di boost, BUSY o RST. Prima del"));
  Serial.println(F("  multimetro non c'è niente da provare via software."));
  Serial.println(F(""));
  Serial.println(F("NON SI VEDE NIENTE, ma il BUSY è salito e il refresh è durato:"));
  Serial.println(F("  il silicio è vivo e ha fatto un ciclo di refresh: manca"));
  Serial.println(F("  l'elettroforesi, cioè le alte tensioni. Punta ai rail di boost"));
  Serial.println(F("  non portati su questo attacco."));
  Serial.println(F(""));
  Serial.println(F("ULTIMA ETICHETTA Y LEGGIBILE:"));
  Serial.println(F("  320 con la barra accent che chiude il bordo -> il controller"));
  Serial.println(F("           pilota 384 gate line: MUX_LINES = 384 nel driver è"));
  Serial.println(F("           giusto e PART_HEIGHT = 384 pure"));
  Serial.println(F("  un valore diverso -> quello è il conteggio reale: PART_HEIGHT"));
  Serial.println(F("           del driver va portato a (etichetta + 64) e HEIGHT al"));
  Serial.println(F("           doppio"));
  Serial.println(F(""));
  Serial.println(F("ANGOLI E NUMERI, il verso della banda:"));
  Serial.println(F("  nero in alto a sinistra, accent in alto a destra, numeri"));
  Serial.println(F("           diritti e crescenti verso il basso -> verso atteso,"));
  Serial.println(F("           mirror (false, false) per questa coda"));
  Serial.println(F("  nero e accent scambiati di lato -> asse X specchiato"));
  Serial.println(F("  blocchi in basso e numeri capovolti -> asse Y specchiato"));
  Serial.println(F("  entrambe le cose -> banda ruotata di 180 gradi, che è il"));
  Serial.println(F("           default del driver per la coda slave"));
  Serial.println(F("  Il verso si dice al driver con setMasterMirror(x, y) /"));
  Serial.println(F("  setSlaveMirror(x, y), senza ricompilare il driver."));
  Serial.println(F(""));
  Serial.println(F("CONFRONTO FRA LE TRE CANDIDATE DI INIT:"));
  Serial.println(F("  se stampano tutte e tre -> tieni CAND_MINIMAL come base del"));
  Serial.println(F("           driver: meno registri scritti, meno cose che possono"));
  Serial.println(F("           divergere fra i due controller. Il MUX però va scritto"));
  Serial.println(F("           comunque, altrimenti si scandiscono gate inesistenti"));
  Serial.println(F("  se solo CAND_SOLUM stampa -> il pannello vuole soft start e MUX"));
  Serial.println(F("           espliciti: la base è il driver 9.7\" di questa libreria"));
  Serial.println(F("  se solo CAND_OEPL stampa -> conta la sequenza dei pattern"));
  Serial.println(F("           hardware e 0x21: porta nel driver anche quelli"));
  Serial.println(F("  se CAND_OEPL raddrizza un'immagine che le altre danno"));
  Serial.println(F("           ribaltata -> 0x21 = 0x08 0x00 fa il lavoro che il"));
  Serial.println(F("           driver fa nel data path: mettilo nell'init e togli il"));
  Serial.println(F("           mirror software"));
  Serial.println(F(""));
  Serial.println(F("MS PER GATE LINE, dal riepilogo:"));
  Serial.println(F("  rilancia con MUX_LINES = 680 e confronta. Se il refresh cresce"));
  Serial.println(F("  in proporzione alle gate programmate, ogni riga programmata in"));
  Serial.println(F("  più è tempo buttato a ogni frame, e il MUX del driver va tenuto"));
  Serial.println(F("  al conteggio reale. Se non cambia, il MUX non pesa sul tempo e"));
  Serial.println(F("  full_refresh_time del driver può restare conservativo."));
  Serial.println(F(""));
  Serial.println(F("FRAME A BANDE, cosa rende l'accent:"));
  Serial.println(F("  banda 3 rossa -> l'accent del film è rosso e il driver è"));
  Serial.println(F("           già giusto: piano 0x26 = rosso"));
  Serial.println(F("  banda 3 gialla -> questo esemplare è BWRY e non BWR: il piano"));
  Serial.println(F("           0x26 pilota il GIALLO. Vale la pena rileggere"));
  Serial.println(F("           l'etichetta sul vetro, perchè la famiglia SOLUM ha"));
  Serial.println(F("           entrambe le varianti"));
  Serial.println(F("  banda 4 diversa da banda 2 E da banda 3 -> la coppia"));
  Serial.println(F("           (BW=0, RED=1) è un TERZO stato renderizzabile: esiste"));
  Serial.println(F("           un quarto colore, codificato come coppia di bit sui"));
  Serial.println(F("           due piani esistenti e non come terzo piano"));
  Serial.println(F("  banda 4 uguale a banda 3 -> vince il piano accent: nel driver"));
  Serial.println(F("           conviene forzare BW=1 dove c'è accent"));
  Serial.println(F("  bande 1 e 2 uguali -> il piano 0x24 non arriva: init o"));
  Serial.println(F("           cablaggio, e nessun'altra riga di questa scheda vale"));
  Serial.println(F(""));
  Serial.println(F("COLORI PIENI:"));
  Serial.println(F("  fondo non uniforme, più chiaro verso un bordo -> le tensioni"));
  Serial.println(F("           cedono sulla lunghezza della banda: guarda il soft"));
  Serial.println(F("           start (0x0C) e prova CAND_SOLUM se eri su CAND_MINIMAL"));
  Serial.println(F("  ghosting che resta fra un colore e l'altro -> la LUT OTP non"));
  Serial.println(F("           basta per questo film: il driver dovrà caricare una LUT"));
  Serial.println(F("           via 0x32, oppure fare due passate di refresh"));
  Serial.println(F(""));
  Serial.println(F("HV DETECT (0x14), la misura che vale sulla coda muta:"));
  Serial.println(F("  BUSY molto più corto di 560 ms -> le alte tensioni salgono:"));
  Serial.println(F("           l'alimentazione di questa coda è a posto, e se non si"));
  Serial.println(F("           vede niente il problema è altrove (dati, finestra RAM,"));
  Serial.println(F("           oppure stai guardando la metà sbagliata del pannello)"));
  Serial.println(F("  BUSY che arriva a 560 ms o va in timeout -> la detection ha"));
  Serial.println(F("           ciclato senza mai trovare HV pronta: manca"));
  Serial.println(F("           l'elettroforesi. Su questa coda i rail di boost non"));
  Serial.println(F("           arrivano, ed è un problema di cablaggio, non di init"));
  Serial.println(F("  BUSY che non sale affatto -> il comando non è stato accettato:"));
  Serial.println(F("           il controller non parla, torna al multimetro"));
  Serial.println(F(""));
  Serial.println(F("STATUS 0x2F, se leggibile:"));
  Serial.println(F("  è la risposta esplicita invece che dedotta dai tempi: bit5 dice"));
  Serial.println(F("  se HV è pronta, bit4 se VCI è nella norma. Se torna valido,"));
  Serial.println(F("  vale anche la lettura di 0x2E (User ID da OTP), che identifica"));
  Serial.println(F("  il modulo senza ambiguità: annotalo, chiude la questione del"));
  Serial.println(F("  part number del controller."));
  Serial.println(F(""));
  Serial.println(F("PATTERN HARDWARE 0x46/0x47:"));
  Serial.println(F("  BUSY che reagisce a entrambi -> il controller esegue e le due"));
  Serial.println(F("           RAM esistono, indipendentemente da come vada il push"));
  Serial.println(F("           SPI dei 46 KB per piano"));
  Serial.println(F("  nessuna reazione, ma SWRESET aveva risposto -> il controller"));
  Serial.println(F("           prende i comandi corti e non questi: sospetta il"));
  Serial.println(F("           parametro, non il cablaggio"));
  Serial.println(F(""));
  Serial.println(F("FINESTRE PARZIALI, i tre box a x = 0 / 448 / 896:"));
  Serial.println(F("  tre quadrati allineati ed equidistanti, più un box accent"));
  Serial.println(F("           sfalsato -> l'addressing con x diverso da zero funziona,"));
  Serial.println(F("           e con esso ogni writeImagePart del driver"));
  Serial.println(F("  box spostati, sovrapposti o smarginati -> la finestra parziale"));
  Serial.println(F("           non fa quello che crediamo: prima di ogni altra cosa va"));
  Serial.println(F("           corretto _setPartialRamArea nel driver, perchè tutte le"));
  Serial.println(F("           scritture non a tutta larghezza passano da lì"));
  Serial.println(F("  box presenti ma il fondo sporco -> il fondo bianco è stato"));
  Serial.println(F("           scritto a larghezza piena e i box no: se il fondo ha"));
  Serial.println(F("           strisce, il problema è nel push dei piani, non nelle"));
  Serial.println(F("           finestre"));
  Serial.println(F(""));
  Serial.println(F("PATTERN RIPETUTO A 20 MHz:"));
  Serial.println(F("  identico al primo -> il default a 20 MHz del driver è buono"));
  Serial.println(F("  righe sporche, byte spostati, pixel mancanti -> il bus non"));
  Serial.println(F("           tiene: nel driver abbassa il clock in selectSPI (il"));
  Serial.println(F("           driver 9.7\" del progetto sta a 10 MHz)"));
  Serial.println(F(""));
  Serial.println(F("DEEP SLEEP, quale parametro:"));
  Serial.println(F("  BUSY alto solo con 0x03 -> hibernate() deve mandare 0x03, come"));
  Serial.println(F("           fa ora il driver"));
  Serial.println(F("  BUSY alto solo con 0x11 -> cambia il byte in hibernate()"));
  Serial.println(F("  alto con entrambi -> il modulo accetta anche il valore fuori"));
  Serial.println(F("           tabella, tieni quello del datasheet"));
  Serial.println(F("  basso con entrambi -> il deep sleep non si osserva sul BUSY su"));
  Serial.println(F("           questo modulo: misura la corrente prima di fidarti di"));
  Serial.println(F("           hibernate() come risparmio"));
  Serial.println(F(""));
  Serial.println(F("ALTRA CODA, dal report sopra:"));
  Serial.println(F("  BUSY si è mosso durante il refresh di questa -> i due"));
  Serial.println(F("           controller condividono più del bus, e il refresh"));
  Serial.println(F("           simultaneo va verificato prima di darlo per buono"));
  Serial.println(F("  BUSY fermo -> le due code sono indipendenti sul BUSY, e"));
  Serial.println(F("           _waitWhileAnyBusy del driver misura due cose distinte"));
  Serial.println(F("================================================"));
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F(" dual_panel_finder — SOLUM 12.2\" 960x768 BWR"));
  Serial.print  (F(" coda      : ")); Serial.println(TARGET_LABEL);
  Serial.print  (F(" init      : ")); Serial.println(CAND_LABEL);
  Serial.printf (" MUX       : %u gate line programmate\n", MUX_LINES);
  Serial.printf (" pattern   : %u source x %u gate, banda attesa %u\n",
                 SRC, MUX_LINES, EXPECTED_BAND);
  Serial.printf (" fasi      : probe %s, driver %s (dual %s)\n",
                 RUN_PROBE_PHASE ? "SI" : "no", RUN_DRIVER_PHASE ? "SI" : "no",
                 DRIVER_DUAL ? "SI" : "no");
  Serial.printf (" opzionali : colori pieni %s, frame a 20 MHz %s, pause %lu s\n",
                 SHOW_SOLID_COLORS ? "SI" : "no", SHOW_FAST_CLOCK_FRAME ? "SI" : "no",
                 (unsigned long)(OBSERVE_MS / 1000));
  Serial.printf ("ambiente: CPU %lu MHz, APB %lu MHz, SPI %lu MHz, heap %lu B\n",
                 (unsigned long)(getCpuFrequencyMhz()),
                 (unsigned long)(getApbFrequency() / 1000000UL),
                 (unsigned long)(SPI_HZ / 1000000UL),
                 (unsigned long)ESP.getFreeHeap());
  Serial.println(F("=================================================="));

#if RUN_PROBE_PHASE
  runProbePhase();
#endif

#if RUN_DRIVER_PHASE
#if RUN_PROBE_PHASE
  observePause(OBSERVE_MS, "comincia la fase driver, che riparte da un reset");
#endif
  runDriverPhase();
#endif

  printObservationSheet();
}

void loop()
{
  delay(60000);
}
