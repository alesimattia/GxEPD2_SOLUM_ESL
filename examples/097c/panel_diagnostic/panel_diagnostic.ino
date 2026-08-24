/**
 * panel_diagnostic - strumento di misura: determina sul pannello SOLUM ESL 9.7"
 * (672w x 960h native portrait, controller SSD1677) QUALI colori e QUALI
 * combinazioni dei piani RAM il display rende davvero, e raccoglie le altre
 * misure diagnostiche utili a tarare il driver.
 *
 * Non decide niente: mette il pannello nelle condizioni di rispondere e
 * riporta cosa si vede. Le conclusioni si tirano dopo, guardando lo schermo,
 * e da quelle si correggono il driver e il firmware.
 *
 * Vive in examples/ di GxEPD2_SOLUM_ESL perchè serve a costruire il driver, non
 * a far funzionare un progetto che lo usa: è lo strumento con cui si stabilisce
 * cosa il driver deve pilotare.
 *
 * Sketch autonomo: non usa GxEPD2, non usa il driver custom, non usa
 * Adafruit_GFX. Parla direttamente al controller in SPI, così l'esito non
 * dipende da nessuno degli strati sopra, driver compreso.
 *
 * ---------------------------------------------------------------------------
 * COSA MISURA
 *
 * 1. Le combinazioni dei due piani RAM documentati, tutte e quattro. Con due
 *    piani a 1 bit le combinazioni per pixel sono esattamente 4 e qui ci sono
 *    tutte, una banda alta 168 px per ognuna:
 *
 *      banda 1  y=  0..167   0x24=0xFF  0x26=0x00
 *      banda 2  y=168..335   0x24=0x00  0x26=0x00
 *      banda 3  y=336..503   0x24=0xFF  0x26=0xFF
 *      banda 4  y=504..671   0x24=0x00  0x26=0xFF
 *
 *    Le prime tre sono le combinazioni che il firmware genera oggi; la quarta
 *    non la genera mai. Ogni banda porta il proprio numero disegnato in basso
 *    a sinistra, così non serve contare le fasce dall'alto.
 *
 * 2. Il canale 0x28, che il driver custom usa oggi per il giallo. Viene
 *    scritto a 0xFF sulla metà alta della banda 1, che è bianca: qualunque
 *    cosa appaia lì è il canale 0x28 che funziona. Insieme al colore si misura
 *    il BUSY subito dopo il comando, perchè distingue una scrittura in RAM da
 *    un comando analogico.
 *
 * 3. La waveform alternativa. Dopo la prima passata di refresh il test rifà il
 *    refresh con 0x22=0xFF invece di 0xF7, cioè DISPLAY Mode 2 invece di Mode
 *    1: stessa RAM, banco di waveform diverso preso da OTP. Fra le due
 *    passate il test si ferma per dare tempo di guardare e annotare i colori
 *    di Mode 1, che la seconda passata sovrascrive.
 *
 * 4. Il refresh differenziale, candidato a sostituire i 22 s con ~600 ms.
 *    Il fratello monocromatico dello stesso silicio (GxEPD2_1330_GDEM133T91,
 *    960x680, SSD1677) dichiara partial_refresh_time = 600 ms tenendo il frame
 *    precedente nella RAM 0x26 e lanciando 0x22=0xFC. Qui la stessa cosa: si
 *    porta il pannello a nero su entrambi i piani, così 0x26 è un frame
 *    precedente coerente, poi si riscrive solo 0x24 e si misurano due
 *    inversioni piene di seguito.
 *
 * 5. Il refresh parziale d'area: se restringendo la finestra RAM il pannello
 *    aggiorna davvero solo quella porzione e a che prezzo in tempo. La sonda
 *    del punto 4 misura la waveform a schermo pieno, questa misura
 *    l'indirizzamento. Il discriminante è una fascia di trappola scritta in
 *    0x24 che nessuna finestra di refresh comprende: se resta bianca le
 *    finestre sono rispettate, se compare il refresh ha ripassato tutto il
 *    pannello. Vengono provate finestre di altezza diversa, una ristretta
 *    anche lungo X, una riscrittura ripetuta della stessa area, i due valori
 *    di bordo 0x3C e infine 0x22=0xF4, cioè Mode 1 su finestra, la strada che
 *    resta se il banco differenziale non c'è.
 *
 *    Il prezzo è dichiarato e accettato in partenza: con 0x26 usata come
 *    frame precedente l'accent non esiste più. Un pannello che passa questa
 *    sonda è un pannello bianco e nero.
 *
 * 6. Il parametro di deep sleep di 0x10, con il BUSY come testimone.
 *
 * Non viene esercitato di proposito: il motore di dithering (0x25). Scrive lui
 * stesso nella BW RAM a partire dal cursore, quindi un tentativo alla cieca
 * rischia di sporcare le bande e invalidare la misura principale. Se servono i
 * livelli intermedi va fatto in una sonda separata.
 *
 * Le finestre RAM della misura dei colori variano solo lungo Y a larghezza
 * piena: è lo stesso indirizzamento che il firmware usa a ogni page, quindi
 * terreno noto. Le finestre ristrette anche lungo X vivono solo nella sonda
 * del partial d'area, dove sono l'oggetto della misura.
 * ---------------------------------------------------------------------------
 * ATTESE DALLA DOCUMENTAZIONE, da confermare o smentire col pannello
 *
 * Servono solo a riconoscere una divergenza quando la si vede, non a decidere
 * l'esito in anticipo.
 *
 * Datasheet SSD1677 Rev 1.0 (docs/SSD1677_Rev1.0_2018-11_Solomon-Systech.pdf),
 * tabella comandi:
 *   0x24  Write RAM (Black White). "For White pixel: = 1. For Black pixel: = 0"
 *   0x25  Write RAM (Dithering): entra nel motore di dithering, non è un piano
 *   0x26  Write RAM (RED). "For Red pixel: = 1. For non-Red pixel = 0"
 *   0x28  VCOM Sense: entra in condizione di misura del VCOM, richiede CLKEN=1
 *         e ANALOGEN=1, alza il BUSY, non accetta parametri. Cioè secondo il
 *         datasheet NON è un piano immagine
 *   0x2E / 0x2F  User ID Read da OTP / Status Bit Read (POR 0x01, chip ID 01)
 *   0x46 / 0x47  Auto Write RED / B/W RAM for Regular Pattern
 *   0x7F  NOP, comando vuoto senza effetti
 * Il datasheet elenca due soli piani immagine e non contiene la parola
 * "yellow" nè una modalità a 4 colori. Mode 2 è il banco waveform
 * differenziale e usa la seconda RAM come frame precedente (0x37 F[6] RAM
 * ping-pong, dichiarato non supportato in Mode 1): su questo pannello la
 * seconda RAM è l'accent, quindi da Mode 2 non è atteso un colore nuovo.
 *
 * OpenEPaperLink (docs/openepaperlink/): cataloga il 9.7" SOLUM come
 * SOLUM_M3_BWR_97, e il suo init dedicato al 960x672 aborta se i colori non
 * sono 3. I pannelli catalogati a 4 colori sono UltraChip e si alimentano con
 * un frame a 4 bit per pixel, non con piani 1bpp.
 *
 * Dall'altra parte, il datasheet SOLUM del modulo donor dichiara per la 9.7"
 * PIXEL COLORS = BWRY, e il driver custom stampa correttamente bianco, nero e
 * rosso, quindi init, MUX, entry mode, finestre, pattern e refresh sono già
 * validati sul campo. La domanda aperta è solo cosa risponda il film e cosa
 * risponda il silicio oltre le tre combinazioni già in uso: la risolve questo
 * test, non la documentazione.
 * ---------------------------------------------------------------------------
 *
 * Diagnostica stampata sul monitor seriale, oltre all'esito dei colori:
 *   - ambiente di misura: chip, clock CPU e APB, clock SPI effettivo, heap
 *   - init spacchettata: reset hardware, durata reale del BUSY dopo lo
 *     SWRESET, blocco di configurazione, finestra RAM piena
 *   - benchmark del bus a blocchi crescenti, per separare l'overhead fisso
 *     per transazione dal costo per byte, con i due blocchi che il driver usa
 *     davvero: 120 byte per riga in _writeImage, 256 byte in _writeScreenBuffer
 *   - costo delle transazioni a singolo byte, la via di writeCommand/writeData
 *   - costo per operazione di ogni fascia scritta, con MB/s effettivi e il
 *     percorso usato: blocchi da 256 come _writeScreenBuffer per le fasce
 *     uniformi, righe da 120 come _writeImage per quelle con la cifra
 *   - pattern hardware 0x46/0x47: latenza, durata, guadagno sul push SPI
 *   - registri in lettura: status 0x2F (che ha un POR noto e fa da prova di
 *     validità del percorso), User ID 0x2E da OTP, temperatura 0x1B. Sulla
 *     Waveshare V3 il FPC non porta fuori la linea dati in uscita, quindi
 *     è atteso che non tornino valori: il test lo dichiara invece di
 *     stampare numeri senza senso
 *   - comportamento del BUSY dopo 0x28
 *   - refresh: latenza di salita del BUSY, durata, rilevamento di più fasi,
 *     per ogni passata, differenziale compresa
 *   - refresh differenziale: durata delle due passate con 0x22=0xFC, confronto
 *     col refresh pieno e stima del ciclo B/N che ne risulterebbe
 *   - partial d'area: per ogni finestra i byte spinti, i microsecondi di push
 *     e i millisecondi di refresh, più il confronto fra finestre di altezza
 *     diversa, che dice se la durata scala con le gate line coinvolte
 *   - deep sleep: quale parametro di 0x10 fa davvero dormire il controller
 *   - riepilogo tempi e stima del ciclo di aggiornamento completo
 *   - scheda di osservazione finale: cosa guardare sul pannello e, per ogni
 *     esito possibile, la conseguenza sul driver
 */

#include <SPI.h>

// Pin del pannello, identici a Layout_097c.h
static const int PIN_CS   = 15;
static const int PIN_DC   = 27;
static const int PIN_RST  = 26;
static const int PIN_BUSY = 25;

// Bus HSPI della Waveshare E-Paper ESP32 Driver Board
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;

static const uint32_t SPI_HZ = 10000000;

// Geometria nativa: 960 source sull'asse RAM X, 672 gate sull'asse RAM Y
static const uint16_t SRC       = 960;
static const uint16_t GATE      = 672;
static const uint16_t ROW_BYTES = SRC / 8;   // 120

// Byte di un piano a schermo pieno: quanto il driver spinge per canale
static const uint32_t PLANE_BYTES = (uint32_t)ROW_BYTES * GATE;   // 80.640

static const uint16_t BAND_H = GATE / 4;     // 168
static const uint16_t CTRL_H = BAND_H / 2;   // 84, fascia di controllo su 0x28

/**
 * Cifra della banda, disegnata dentro la banda stessa per non dover contare
 * le fasce dall'alto quando si legge il pannello. Font 5x7 scalato per un
 * fattore multiplo di 8 e offset X allineato al byte: ogni pixel del font
 * copre byte RAM interi, quindi la composizione delle righe non richiede
 * nessun mascheramento a livello di bit.
 *
 * Posizione: in basso nella banda, con un margine dal bordo inferiore. Sulla
 * prima banda questo tiene la cifra fuori dalla fascia di controllo 0x28,
 * che occupa la metà alta: la fascia resta uniforme e leggibile.
 */
static const uint8_t  GLYPH_W = 5;
static const uint8_t  GLYPH_H = 7;
static const uint8_t  GLYPH_SCALE = 8;             // 40x56 px a schermo
static const uint16_t GLYPH_X = 32;                // offset dal bordo sinistro
static const uint16_t GLYPH_BOTTOM_MARGIN = 20;    // distanza dal fondo della banda

// Cifre 1..4, una riga per elemento, bit 4 = colonna più a sinistra
static const uint8_t GLYPHS[4][GLYPH_H] =
{
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
  { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },   // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
};

// Sul SSD1677 il BUSY è attivo alto, come il busy_level=HIGH del driver
static const int BUSY_ACTIVE = HIGH;

/**
 * Pausa fra la passata Mode 1 e la passata Mode 2, per dare il tempo di
 * guardare il pannello: la seconda passata sovrascrive la prima.
 */
static const uint32_t MODE2_PAUSE_MS = 45000;

// Pausa prima della sonda differenziale, che sovrascrive il risultato di Mode 2
static const uint32_t PARTIAL_PAUSE_MS = 45000;

// Pausa prima della sonda d'area, che azzera lo schermo e lo riscrive da capo
static const uint32_t AREA_PAUSE_MS = 45000;

/**
 * Pausa dentro la sonda d'area, fra le passate 0xFC e la passata Mode 1.
 * La fascia di trappola va letta prima della passata Mode 1: se Mode 1
 * ripassasse tutto il pannello la trappola comparirebbe comunque, e a sonda
 * finita non si saprebbe più quale delle due cose l'ha fatta comparire.
 */
static const uint32_t AREA_TRAP_PAUSE_MS = 30000;

/**
 * Mappa verticale della sonda del partial d'area: fasce disgiunte sui 672
 * gate, così a sonda finita si leggono tutte insieme sullo stesso schermo.
 * Le altezze sono multipli comodi e le X del riquadro sono multiple di 8,
 * perchè la finestra lavora per byte sull'asse source e un bordo a metà byte
 * non è rappresentabile.
 */
static const uint16_t AREA_P1_Y   = 0;         // 0..167,   passata 1 e poi 4
static const uint16_t AREA_P1_H   = BAND_H;
static const uint16_t AREA_TRAP_Y = 176;       // 176..215, fascia di trappola
static const uint16_t AREA_TRAP_H = 40;
static const uint16_t AREA_THIN_Y = 224;       // 224..247, finestra sottile
static const uint16_t AREA_THIN_H = 24;
static const uint16_t AREA_M1_Y   = 264;       // 264..311, passata Mode 1
static const uint16_t AREA_M1_H   = 48;
static const uint16_t AREA_P2_Y   = 336;       // 336..503, passata 2
static const uint16_t AREA_P2_H   = BAND_H;
static const uint16_t AREA_BOX_X  = 256;       // 504..671 x 256..511, riquadro
static const uint16_t AREA_BOX_W  = 256;
static const uint16_t AREA_BOX_Y  = 504;
static const uint16_t AREA_BOX_H  = BAND_H;

SPIClass hspi(HSPI);
static SPISettings spiSettings(SPI_HZ, MSBFIRST, SPI_MODE0);

/**
 * Clock ridotto per i tentativi di lettura registro: in lettura il
 * controller è molto più lento che in scrittura.
 */
static SPISettings spiReadSettings(2500000, MSBFIRST, SPI_MODE0);

// Contatori cumulativi alimentati dalle primitive: base del riepilogo finale
static uint32_t totalSpiBytes = 0;
static uint32_t totalSpiMicros = 0;
static uint32_t totalBusyMillis = 0;
static uint32_t totalCommands = 0;
static uint32_t totalParamBytes = 0;

// Costi per byte misurati dal benchmark, usati per le stime di frame
static double bulkUsPerByte = 0.0;   // blocco più grande, limite del bus
static double rowUsPerByte = 0.0;    // blocchi da 120, la riga immagine del driver

// Durata dei pattern hardware, -1 se non supportati
static int32_t patternMs24 = -1;
static int32_t patternMs26 = -1;
static int32_t diffMs1 = -1;
static int32_t diffMs2 = -1;

// Durate della sonda d'area, -1 se la passata non si è conclusa o non è stata fatta
static int32_t areaMsFirst = -1;   // prima passata 0xFC, finestra alta BAND_H
static int32_t areaMsThin  = -1;   // passata 0xFC su 24 righe, per la scala con l'altezza
static int32_t areaMsMode1 = -1;   // passata 0xF4, Mode 1 su finestra

/**
 * Invia un byte di comando: D/C basso. Ordine delle operazioni identico a
 * GxEPD2_EPD::_writeCommand, D/C riportato alto in coda incluso, così il
 * costo misurato è quello che paga il driver.
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
  ++totalCommands;
}

// Invia un byte di parametro: D/C alto, come GxEPD2_EPD::_writeData
static void writeData(uint8_t d)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  hspi.transfer(d);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  ++totalParamBytes;
}

/**
 * Riempie la finestra RAM corrente con un byte costante, a blocchi da 256:
 * è il percorso di _writeScreenBuffer del driver, stessa dimensione di chunk.
 * Ritorna i microsecondi del solo transfer: serve a misurare il costo reale
 * per byte del bus invece di dedurlo dal clock.
 */
static uint32_t writeConst(uint8_t value, uint32_t count)
{
  uint8_t buf[256];
  memset(buf, value, sizeof(buf));
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  const uint32_t total = count;
  const uint32_t t0 = micros();
  while (count > 0)
  {
    uint32_t chunk = count > sizeof(buf) ? (uint32_t)sizeof(buf) : count;
    hspi.writeBytes(buf, chunk);
    count -= chunk;
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes += total;
  totalSpiMicros += dt;
  return dt;
}

/** Attende la discesa del BUSY. Ritorna i ms attesi, oppure -1 al timeout. */
static int32_t waitBusy(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
  {
    if ((millis() - t0) > timeout_ms)
    {
      totalBusyMillis += (millis() - t0);
      return -1;
    }
    delay(1);
  }
  const uint32_t dt = millis() - t0;
  totalBusyMillis += dt;
  return (int32_t)dt;
}

/**
 * Porta il BUSY a riposo prima di una misura che lo usa come testimone. Senza
 * questa guardia un BUSY rimasto alto da un comando precedente verrebbe letto
 * come "il comando appena inviato è stato accettato", e si misurerebbe la coda
 * dell'operazione precedente invece dell'operazione nuova.
 */
static void ensureBusyLow(const char* dove)
{
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
    return;
  const int32_t ms = waitBusy(3000);
  if (ms < 0)
    Serial.printf("  ATTENZIONE: BUSY ancora alto entrando in %s, misura non attendibile\n", dove);
  else
    Serial.printf("  BUSY era alto entrando in %s: attesi %ld ms prima di misurare\n",
                  dove, (long)ms);
}

/**
 * Costo per byte di riferimento per le stime: il migliore misurato dal
 * benchmark, con fallback al limite teorico del clock se il benchmark non ha
 * ancora girato.
 */
static double usPerByteReference()
{
  if (bulkUsPerByte > 0.0)
    return bulkUsPerByte;
  return 8.0 * 1000000.0 / (double)SPI_HZ;
}

/**
 * Impulso di reset. Il firmware chiama display.init(..., 2, false), quindi
 * GxEPD2_EPD::_reset tiene RST alto 10 ms, basso 2 ms e alto altri 10 ms: qui
 * il livello basso dura 10 ms invece di 2, più margine e nessun effetto sulla
 * misura.
 */
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

/**
 * Init minima: la stessa del driver custom, MUX incluso a 672 gate line.
 * Ogni passo è cronometrato. Il BUSY dopo lo SWRESET viene osservato dentro
 * la finestra di 200 ms che il driver spende comunque a delay: la durata
 * reale del reset interno diventa un dato invece di una stima, e sul bus non
 * cambia niente perchè in quella finestra non si inviano comandi.
 */
static void initPanel()
{
  const uint32_t tReset = micros();
  resetPanel();
  delay(10);   // stesso delay di cortesia di _InitDisplay dopo il reset
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
  // il controller ignora i comandi per ~100-300 ms: la finestra resta piena
  const uint32_t swElapsed = millis() - tSw;
  if (swElapsed < 200)
    delay(200 - swElapsed);

  const uint32_t tCfg = micros();
  writeCommand(0x0C);   // soft start
  writeData(0xAE);
  writeData(0xC7);
  writeData(0xC3);
  writeData(0xC0);
  writeData(0x80);
  writeCommand(0x01);   // MUX 671 -> 672 gate line
  writeData(0x9F);
  writeData(0x02);
  writeData(0x00);
  writeCommand(0x3C);   // border waveform: LUT1, bianco
  writeData(0x01);
  writeCommand(0x18);   // sensore di temperatura interno
  writeData(0x80);
  writeCommand(0x11);   // entry mode: X e Y crescenti
  writeData(0x03);
  const uint32_t usCfg = micros() - tCfg;

  const uint32_t tWin = micros();
  setRamWindow(0, 0, SRC, GATE);
  const uint32_t usWin = micros() - tWin;

  Serial.printf("  reset hardware      %6lu us\n", (unsigned long)usReset);
  if (!rose)
    Serial.println(F("  SWRESET             BUSY non è salito entro 50 ms (il driver non lo verifica)"));
  else if (swBusy < 0)
    Serial.printf("  SWRESET             BUSY salito dopo %lu ms, ancora alto a 1000 ms\n",
                  (unsigned long)riseMs);
  else
    Serial.printf("  SWRESET             BUSY salito dopo %lu ms, alto per %ld ms (il driver attende 200 ms fissi)\n",
                  (unsigned long)riseMs, (long)swBusy);
  Serial.printf("  config 0x0C..0x11   %6lu us  (5 comandi, 9 parametri)\n", (unsigned long)usCfg);
  Serial.printf("  finestra RAM piena  %6lu us  (4 comandi, 12 parametri)\n", (unsigned long)usWin);
}

/**
 * Misura il costo del bus separando l'overhead fisso per transazione dal
 * costo per byte: scrive sempre lo stesso totale cambiando la dimensione del
 * blocco passato a writeBytes. I due blocchi che contano per il driver sono
 * 120 byte, una riga immagine di _writeImage, e 256 byte, il chunk di
 * _writeScreenBuffer; gli altri servono a vedere dove la curva si appiattisce.
 *
 * Lavora sul piano 0x26 dentro una finestra di una sola gate row: gli
 * indirizzi ciclano dentro la finestra, e il piano viene comunque riscritto
 * per intero subito dopo dal pattern hardware, quindi il test non lascia
 * traccia sull'immagine. Non usa 0x28 di proposito: il datasheet lo dà come
 * VCOM Sense, cioè un comando che alza il BUSY, e un BUSY lasciato alto in
 * eredità falserebbe la validazione del pattern che viene subito dopo.
 */
static void benchmarkBus()
{
  static const uint16_t chunk_sizes[] = { 1, 8, 32, 120, 256, 1024, 4096 };
  static const uint32_t BENCH_BYTES = 8192;
  static uint8_t buf[4096];   // statico: 4 KB sullo stack del task Arduino sono troppi
  memset(buf, 0x00, sizeof(buf));

  // il benchmark non deve inquinare il profilo delle operazioni del test
  const uint32_t cmdSnapshot = totalCommands;
  const uint32_t paramSnapshot = totalParamBytes;

  const double theoretical = (double)SPI_HZ / 8.0 / 1000000.0;   // MB/s al clock nominale
  Serial.println(F("\nbenchmark del bus: 8192 byte per riga, blocco crescente"));
  Serial.printf("  limite teorico a %lu Hz: %.2f MB/s (%.3f us/byte)\n",
                (unsigned long)SPI_HZ, theoretical, 1.0 / theoretical);
  Serial.println(F("  blocco  chiamate      us   us/byte     MB/s  %teorico  note"));
  for (uint8_t i = 0; i < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); ++i)
  {
    const uint16_t chunk = chunk_sizes[i];
    const uint32_t calls = BENCH_BYTES / chunk;
    const uint32_t written = calls * chunk;   // 8192 non è multiplo di 120
    setRamWindow(0, 0, SRC, 1);
    writeCommand(0x26);
    digitalWrite(PIN_DC, HIGH);
    hspi.beginTransaction(spiSettings);
    digitalWrite(PIN_CS, LOW);
    const uint32_t t0 = micros();
    for (uint32_t n = 0; n < calls; ++n)
      hspi.writeBytes(buf, chunk);
    const uint32_t dt = micros() - t0;
    digitalWrite(PIN_CS, HIGH);
    hspi.endTransaction();

    const double upb = (double)dt / (double)written;
    const double mbs = (double)written / (double)dt;
    const char* note = "";
    if (chunk == 120)
      note = "riga immagine _writeImage";
    else if (chunk == 256)
      note = "chunk _writeScreenBuffer";
    Serial.printf("  %6u %9lu %7lu %9.3f %8.2f %8.0f%%  %s\n",
                  (unsigned)chunk, (unsigned long)written / chunk, (unsigned long)dt,
                  upb, mbs, mbs / theoretical * 100.0, note);
    if (chunk == 120)
      rowUsPerByte = upb;
    if (upb > 0.0 && (bulkUsPerByte == 0.0 || upb < bulkUsPerByte))
      bulkUsPerByte = upb;
  }

  /**
   * Costo di una transazione per singolo byte: è la via di writeData e
   * writeCommand, quella che il driver usa per comandi e parametri e che
   * evita nei hot path proprio per questo scarto.
   */
  const uint32_t SINGLE = 512;
  setRamWindow(0, 0, SRC, 1);
  writeCommand(0x26);
  const uint32_t tData = micros();
  for (uint32_t n = 0; n < SINGLE; ++n)
    writeData(0x00);
  const uint32_t usData = micros() - tData;

  /**
   * Costo della transazione di comando. Si ripete 0x7F, che il datasheet
   * SSD1677 definisce NOP: comando vuoto senza alcun effetto sul modulo,
   * utilizzabile anche per terminare una scrittura o lettura di frame memory.
   * È quindi la ripetizione che non può alterare lo stato del controller.
   */
  const uint32_t tCmd = micros();
  for (uint32_t n = 0; n < SINGLE; ++n)
    writeCommand(0x7F);
  const uint32_t usCmd = micros() - tCmd;

  const double singleUsPerByte = (double)usData / (double)SINGLE;
  Serial.printf("  writeData()    %.2f us/byte, %.0f volte il blocco grande\n",
                singleUsPerByte, singleUsPerByte / usPerByteReference());
  Serial.printf("  writeCommand() %.2f us/comando\n",
                (double)usCmd / (double)SINGLE);
  Serial.printf("  un piano da %lu byte: %.0f ms a blocchi da 120, %.0f ms al limite del bus,\n",
                (unsigned long)PLANE_BYTES,
                (rowUsPerByte > 0.0 ? rowUsPerByte : usPerByteReference()) * PLANE_BYTES / 1000.0,
                usPerByteReference() * PLANE_BYTES / 1000.0);
  Serial.printf("                       %.0f ms se passasse da writeData()\n",
                singleUsPerByte * PLANE_BYTES / 1000.0);

  totalCommands = cmdSnapshot;
  totalParamBytes = paramSnapshot;
}

/**
 * Legge n byte da un registro del controller. Sulla Waveshare E-Paper ESP32
 * Driver Board la linea di uscita dati del pannello non è cablata al MISO,
 * quindi l'esito è credibile solo su hardware che la porta fuori: una lettura
 * tutta 0x00 o tutta 0xFF va interpretata come "non cablato".
 */
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
 * Legge lo Status Bit Read (0x2F) e lo usa come prova di validità del
 * percorso di lettura: il datasheet dà POR 0x01, con A[1:0] = chip ID = 01 e
 * A[2] = busy flag. Se il valore letto ha il chip ID a 01 e il busy flag
 * coerente col pin, allora la linea di uscita dati è cablata e anche le altre
 * letture valgono qualcosa. 0x00 o 0xFF pieni dicono il contrario.
 * Ritorna true se la lettura è credibile.
 */
static bool reportStatus()
{
  uint8_t st = 0;
  readRegister(0x2F, &st, 1);
  const uint8_t chipId = st & 0x03;
  const bool busyFlag = (st & 0x04) != 0;
  const bool pinBusy = digitalRead(PIN_BUSY) == BUSY_ACTIVE;
  const bool credible = (st != 0x00 && st != 0xFF && chipId == 0x01);
  /**
   * A[5] e A[4] sono attivi bassi: il datasheet dà 0 = Ready / Normal e 1 =
   * Not Ready / VCI sotto soglia. E avverte che dopo il reset non sono validi
   * finchè non li si inizializza con 0x14 e 0x15, che questo test non invia:
   * vanno letti come indeterminati.
   */
  Serial.printf("status 0x2F        0x%02X  chip ID %u, busy flag %d (pin BUSY %d), HV %s, VCI %s\n",
                st, (unsigned)chipId, (int)busyFlag, (int)pinBusy,
                (st & 0x20) ? "NON pronta" : "pronta",
                (st & 0x10) ? "sotto soglia" : "normale");
  Serial.println(F("                   HV e VCI non inizializzati (0x14 / 0x15 non inviati): indeterminati"));
  if (credible)
    Serial.println(F("                   chip ID atteso: la linea di lettura è cablata, letture valide"));
  else
    Serial.println(F("                   chip ID non atteso: linea di lettura non cablata, letture da ignorare"));
  return credible;
}

/**
 * Legge i 10 byte di User ID da OTP (0x2E). Su un pannello recuperato da ESL
 * è l'unico modo di farsi dire dal controller quale modulo sta pilotando,
 * quindi vale il tentativo anche se serve la linea di lettura cablata.
 */
static void reportUserId()
{
  uint8_t id[10];
  memset(id, 0, sizeof(id));
  readRegister(0x2E, id, sizeof(id));
  Serial.print(F("user ID 0x2E       "));
  for (uint8_t i = 0; i < sizeof(id); ++i)
    Serial.printf("%02X ", id[i]);
  Serial.print(F(" ascii \""));
  for (uint8_t i = 0; i < sizeof(id); ++i)
    Serial.print((char)(id[i] >= 0x20 && id[i] < 0x7F ? id[i] : '.'));
  Serial.println(F("\""));
}

/**
 * Legge il registro di temperatura (0x1B) e prova a decodificarlo: 12 bit in
 * complemento a due, 1 LSB = 1/16 di grado. Il valore è significativo solo
 * dopo che un update ha caricato la temperatura, cioè dopo il bit load-temp
 * del comando 0x22: per questo viene letto anche a refresh concluso. È la
 * temperatura che seleziona la LUT, quindi determina la durata del refresh.
 */
static void reportTemperature(const char* when)
{
  uint8_t raw[2] = { 0, 0 };
  readRegister(0x1B, raw, 2);
  const uint16_t reg = ((uint16_t)raw[0] << 8) | raw[1];
  int16_t value = (int16_t)(reg >> 4);
  if (value & 0x0800)
    value -= 4096;
  Serial.printf("temperatura %-18s raw 0x%02X%02X", when, raw[0], raw[1]);
  if (reg == 0x0000 || reg == 0xFFFF)
    Serial.println(F("  -> linea di lettura non cablata o sensore non campionato"));
  else
    Serial.printf("  -> %.1f gradi\n", (double)value / 16.0);
}

/**
 * Riempie la finestra RAM corrente riga per riga sovraimprimendo la cifra
 * della banda: fondo a bg, pixel della cifra a fg. È il percorso di
 * _writeImage del driver, blocchi da ROW_BYTES, quindi misura l'altro dei due
 * modi in cui il driver spinge i dati. La finestra resta a larghezza piena:
 * la cifra si ottiene componendo le righe, non restringendo la finestra.
 * Ritorna i microsecondi del solo transfer.
 */
static uint32_t writeRowsWithDigit(uint8_t bg, uint16_t h, uint8_t digit, uint8_t fg)
{
  uint8_t row[ROW_BYTES];
  const int16_t glyphPx = (int16_t)GLYPH_H * GLYPH_SCALE;
  const int16_t top = (int16_t)h - glyphPx - (int16_t)GLYPH_BOTTOM_MARGIN;
  const uint8_t* glyph = GLYPHS[digit - 1];
  const uint8_t colBytes = GLYPH_SCALE / 8;   // byte coperti da un pixel del font
  const uint16_t x0 = GLYPH_X / 8;            // primo byte della cifra nella riga

  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  const uint32_t t0 = micros();
  for (uint16_t r = 0; r < h; ++r)
  {
    memset(row, bg, sizeof(row));
    const int16_t dy = (int16_t)r - top;
    if (dy >= 0 && dy < glyphPx)
    {
      const uint8_t bits = glyph[dy / GLYPH_SCALE];
      for (uint8_t c = 0; c < GLYPH_W; ++c)
      {
        if (bits & (0x10 >> c))
          memset(row + x0 + (uint16_t)c * colBytes, fg, colBytes);
      }
    }
    hspi.writeBytes(row, sizeof(row));
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes += (uint32_t)ROW_BYTES * h;
  totalSpiMicros += dt;
  return dt;
}

/**
 * Scrive una fascia a larghezza piena su un piano, con valore costante.
 * Separa il costo dell'indirizzamento da quello dei dati: la finestra sono 4
 * comandi più 12 parametri a singolo byte, e su fasce piccole pesa.
 *
 * Con digit fra 1 e 4 la cifra viene sovraimpressa al valore fg e le righe
 * passano per il percorso _writeImage; senza cifra si usa il percorso a
 * blocchi da 256 di _writeScreenBuffer. Le due chiamate per banda, una per
 * piano, devono passare la stessa cifra: sono i due piani insieme a
 * determinare il colore dei pixel della cifra, e su un piano fg può
 * coincidere col fondo (scrittura senza effetto, ma simmetrica).
 */
static void fillBand(uint8_t plane, uint16_t y, uint16_t h, uint8_t value,
                     uint8_t digit = 0, uint8_t fg = 0x00)
{
  const uint32_t bytes = (uint32_t)ROW_BYTES * h;
  const bool withDigit = (digit >= 1 && digit <= 4);
  const uint32_t tWin = micros();
  setRamWindow(0, y, SRC, h);
  writeCommand(plane);
  const uint32_t usWin = micros() - tWin;
  const uint32_t us = withDigit ? writeRowsWithDigit(value, h, digit, fg)
                                : writeConst(value, bytes);
  Serial.printf("  0x%02X  y=%3u..%3u  val 0x%02X  %6lu byte  finestra %4lu us  dati %6lu us  %.2f us/byte  %.2f MB/s  %s\n",
                plane, (unsigned)y, (unsigned)(y + h - 1), value,
                (unsigned long)bytes, (unsigned long)usWin, (unsigned long)us,
                (double)us / (double)bytes, (double)bytes / (double)us,
                withDigit ? "righe da 120 con cifra" : "blocchi da 256");
}

/**
 * Riempie un rettangolo di un piano con un valore costante, finestra
 * ristretta anche lungo X. x e w devono essere multipli di 8: sull'asse
 * source la finestra lavora per byte e un bordo a metà byte non è
 * rappresentabile. Con l'entry mode 0x03 l'indirizzo avanza dentro la
 * finestra e va a capo da solo, quindi basta spingere (w/8)*h byte di seguito.
 *
 * Serve alla sonda del partial d'area, l'unica parte del test che restringe
 * la finestra su entrambi gli assi: le bande della misura dei colori sono a
 * larghezza piena per definizione e continuano a passare da fillBand.
 * Ritorna i microsecondi del solo transfer.
 */
static uint32_t fillRect(uint8_t plane, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint8_t value)
{
  setRamWindow(x, y, w, h);
  writeCommand(plane);
  return writeConst(value, (uint32_t)(w / 8) * h);
}

/**
 * Riempie un piano immagine col generatore di pattern del controller: 0x47
 * per il piano 0x24, 0x46 per il piano 0x26. Il parametro codifica A[7] =
 * valore del primo step, A[6:4]=111 step height 680, A[2:0]=111 step width
 * 960, cioè un unico step su tutta la RAM nativa. La finestra piena viene
 * impostata prima come fa _fillPlaneByPattern, anche se il pattern la ignora:
 * serve a lasciare area e cursore nello stesso stato del percorso SPI. Se
 * BUSY non sale il comando non è supportato e il driver deve restare sul
 * transfer SPI. Ritorna i ms del riempimento, -1 se non praticabile.
 */
static int32_t fillByPattern(uint8_t pattern_command, uint8_t value)
{
  const uint8_t param = value ? 0xF7 : 0x77;
  ensureBusyLow("fillByPattern");
  setRamWindow(0, 0, SRC, GATE);
  writeCommand(pattern_command);
  writeData(param);
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - t0) < 200)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    Serial.printf("  0x%02X  param 0x%02X  BUSY MAI SALITO: pattern non supportato\n",
                  pattern_command, param);
    return -1;
  }
  const uint32_t riseMs = millis() - t0;
  const int32_t ms = waitBusy(2000);
  if (ms < 0)
  {
    Serial.printf("  0x%02X  param 0x%02X  timeout BUSY a 2000 ms\n", pattern_command, param);
    return -1;
  }
  // confronto col push SPI che il pattern sostituisce: 80.640 byte sul bus
  const double spiMs = usPerByteReference() * PLANE_BYTES / 1000.0;
  Serial.printf("  0x%02X  param 0x%02X  BUSY su dopo %lu ms, riempito in %ld ms  (push SPI equivalente %.0f ms, %.1fx)\n",
                pattern_command, param, (unsigned long)riseMs, (long)ms,
                spiMs, spiMs / (ms > 0 ? (double)ms : 1.0));
  return ms;
}

/**
 * Attende la fine del refresh stampando l'avanzamento e rilevando eventuali
 * passate successive: un pannello multi-fase riabbassa e rialza il BUSY, e
 * saperlo cambia il timeout che il driver deve tenere. Ritorna i ms totali
 * dalla master activation, -1 al timeout.
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
        Serial.printf("    fase %d in corso da %lu ms\n", phase, (unsigned long)(millis() - tPhase));
        nextLog += 2500;
      }
      delay(1);
    }
    Serial.printf("    fase %d conclusa: BUSY alto per %lu ms\n",
                  phase, (unsigned long)(millis() - tPhase));
    // se il BUSY si rialza entro 800 ms il refresh procede in più passate
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
    Serial.printf("    BUSY risalito dopo %lu ms: il refresh è multi-fase\n",
                  (unsigned long)(millis() - tIdle));
  }
  const uint32_t dt = millis() - t0;
  totalBusyMillis += dt;
  Serial.printf("    fasi rilevate: %d\n", phase);
  return (int32_t)dt;
}

/**
 * Esegue una passata di refresh con la sequenza di update indicata e ne
 * misura i tempi. 0xF7 e 0xFF differiscono solo per il Display Mode, 1 e 2:
 * stessa RAM, waveform diversa presa da OTP; 0xFC è Mode 2 senza il power
 * down finale, la sequenza del refresh differenziale. Verifica anche che il
 * BUSY salga, cioè che il controller abbia davvero preso il comando.
 *
 * attesa e timeout_ms sono parametri perchè una passata differenziale, se il
 * banco esiste, dura tre ordini di grandezza meno di una piena: annunciare
 * "una ventina di secondi" e attendere 40 s non avrebbe senso.
 *
 * Ritorna i ms del BUSY, -1 se la passata non si è conclusa.
 */
static int32_t runRefresh(uint8_t updateSequence, const char* label,
                          const char* attesa = "una ventina di secondi",
                          uint32_t timeout_ms = 40000)
{
  Serial.printf("\nrefresh %s (0x22=0x%02X), %s\n",
                label, updateSequence, attesa);
  ensureBusyLow("runRefresh");
  writeCommand(0x22);   // clock, analog, load temp, load LUT, display, power off
  writeData(updateSequence);
  writeCommand(0x20);   // master activation

  /**
   * Il BUSY deve salire quasi subito. Se non sale, il controller non ha preso
   * il comando: cablaggio o alimentazione, e tutto il resto non ha valore.
   */
  const uint32_t tAct = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - tAct) < 1000)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    Serial.println(F("ATTENZIONE: BUSY non è mai salito, il controller non risponde"));
    return -1;
  }
  Serial.printf("BUSY salito dopo %lu ms\n", (unsigned long)(millis() - tAct));

  const int32_t ms = waitRefresh(timeout_ms);
  if (ms < 0)
    Serial.printf("ATTENZIONE: timeout BUSY a %lu ms, refresh non concluso\n",
                  (unsigned long)timeout_ms);
  else
    Serial.printf("refresh %s concluso in %ld ms\n", label, (long)ms);
  return ms;
}

/**
 * Pausa di osservazione con conto alla rovescia sul seriale. Il pannello
 * mostra un risultato che la fase successiva sovrascrive e il test non può
 * vederlo: l'unico modo di non perderlo è fermarsi.
 */
static void observePause(uint32_t ms, const char* cosaSuccede)
{
  Serial.printf("\nGUARDA IL PANNELLO E ANNOTA: fra %lu s %s\n",
                (unsigned long)(ms / 1000), cosaSuccede);
  for (uint32_t left = ms / 1000; left >= 5; left -= 5)
  {
    Serial.printf("  %lu s\n", (unsigned long)left);
    delay(5000);
  }
}

/**
 * Sonda del refresh differenziale: la strada per un aggiornamento da ~600 ms
 * invece dei 22 s del refresh pieno.
 *
 * Da dove viene l'ipotesi. Il fratello monocromatico dello stesso silicio,
 * GxEPD2_1330_GDEM133T91 (13.3", 960x680, SSD1677), dichiara
 * partial_refresh_time = 600 ms e lo ottiene così: scrive il frame precedente
 * nella RAM 0x26 e quello corrente nella 0x24, poi lancia 0x22=0xFC invece di
 * 0xF7. Su questo pannello 0x26 è l'accent, quindi accent e partial non
 * possono coesistere; ma se il banco di waveform differenziale esiste in OTP,
 * un frame senza accent può usarlo.
 *
 * Decodifica di 0x22 dalla tabella del datasheet: bit7 enable clock, bit6
 * enable analog, bit5 load temperature, bit4 load LUT, bit3 seleziona DISPLAY
 * Mode 2, bit2 display, bit1 disable analog, bit0 disable OSC. Quindi
 * 0xF7 = Mode 1 con power down finale, 0xFF = Mode 2 con power down,
 * 0xFC = Mode 2 lasciando clock e analogico accesi, che è il modo in cui una
 * catena di partial resta veloce.
 *
 * Perchè serve una baseline tutta nera: il differenziale calcola la
 * transizione fra 0x26 e 0x24, quindi 0x26 deve contenere quello che il
 * pannello sta davvero mostrando. Dopo le bande non lo contiene, e l'unico
 * stato coerente ottenibile al prezzo di un solo refresh è "tutto nero" su
 * entrambi i piani. Da lì le due passate fanno due inversioni piene, il caso
 * peggiore per una waveform differenziale: se le regge in 600 ms il partial su
 * questo pannello è reale.
 *
 * Riempie diffMs1 e diffMs2 e ritorna i ms della prima passata, -1 se non si
 * è conclusa.
 */
static int32_t probeDifferentialRefresh(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del refresh differenziale, candidato ~600 ms ==="));

  /**
   * Baseline coerente: entrambi i piani a 0, cioè BW nero e accent spento, col
   * pattern hardware. Dopo il refresh il pannello è nero e la RAM 0x26 vale 0
   * ovunque, che come frame precedente si legge "tutto nero": coerente con
   * quello che si vede.
   */
  Serial.println(F("baseline: entrambi i piani a nero col pattern, poi refresh pieno Mode 1"));
  fillByPattern(0x47, 0x00);   // 0x24 tutto nero
  fillByPattern(0x46, 0x00);   // 0x26 accent spento, cioè frame precedente nero
  if (runRefresh(0xF7, "baseline nera") < 0)
  {
    Serial.println(F("baseline non riuscita: sonda differenziale abbandonata"));
    return -1;
  }

  /**
   * Prima passata: tutto bianco con la cifra 1 in nero, scritto SOLO su 0x24.
   * 0x26 resta la baseline nera, cioè il frame precedente.
   */
  Serial.println(F("\npassata 1: nero -> bianco, riscritto solo 0x24"));
  fillBand(0x24, 0, GATE, 0xFF, 1, 0x00);
  diffMs1 = runRefresh(0xFC, "differenziale Mode 2",
                       "atteso sotto il secondo se il banco esiste", 30000);

  if (diffMs1 > 0)
  {
    /**
     * Seconda passata: prima si allinea il frame precedente scrivendo su 0x26
     * lo stesso contenuto appena mostrato, come fa writeImageAgain del driver
     * monocromatico, poi si scrive su 0x24 l'inversione con la cifra 2. Una
     * catena di due passate distingue un colpo di fortuna da un meccanismo.
     */
    Serial.println(F("\npassata 2: allineo il precedente su 0x26, poi bianco -> nero"));
    fillBand(0x26, 0, GATE, 0xFF, 1, 0x00);   // precedente = quello che si vede
    fillBand(0x24, 0, GATE, 0x00, 2, 0xFF);   // corrente = inversione, cifra 2 bianca
    diffMs2 = runRefresh(0xFC, "differenziale Mode 2, seconda",
                         "atteso come la prima", 30000);
  }

  // 0xFC lascia clock e analogico accesi: si spengono come fa _PowerOff del driver
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  waitBusy(2000);

  Serial.println(F("\nesito della sonda differenziale:"));
  if (diffMs1 < 0)
    Serial.println(F("  la passata non si è conclusa: 0xFC non è praticabile su questo pannello"));
  else
  {
    Serial.printf("  passata 1  %ld ms\n", (long)diffMs1);
    if (diffMs2 > 0)
      Serial.printf("  passata 2  %ld ms\n", (long)diffMs2);
    if (fullMs > 0)
      Serial.printf("  riferimenti: %ld ms il refresh pieno qui, 600 ms il partial del\n"
                    "               GDEM133T91 monocromatico sullo stesso silicio\n",
                    (long)fullMs);
    if (diffMs1 < 3000)
      Serial.println(F("  durata da partial: se il pannello mostra davvero il frame nuovo,\n"
                       "  il banco differenziale esiste in OTP"));
    else if (fullMs > 0 && diffMs1 > fullMs - 3000)
      Serial.println(F("  durata da refresh pieno: 0xFC ricade sulla waveform completa"));
    else
      Serial.println(F("  durata intermedia: annotarla, non è nessuno dei due casi noti"));
  }
  return diffMs1;
}

/**
 * Esito di una passata della sonda d'area, tenuto per il riepilogo finale:
 * il confronto fra passate serve più del valore singolo.
 */
struct AreaPass
{
  const char* label;
  uint16_t x, y, w, h;
  uint8_t  sequence;   // parametro di 0x22 usato per aggiornare
  uint32_t bytes;      // byte spinti su 0x24
  uint32_t pushUs;     // microsecondi del solo transfer
  int32_t  ms;         // millisecondi di BUSY, -1 se non conclusa
};

static AreaPass areaPasses[6];
static uint8_t  areaPassCount = 0;

/**
 * Una passata della sonda d'area: scrive la finestra su 0x24, aggiorna con la
 * sequenza indicata e poi riallinea la stessa finestra su 0x26, perchè la
 * passata successiva trovi come frame precedente quello che il pannello sta
 * davvero mostrando. È lo stesso schema di writeImagePartToPrevious del
 * driver monocromatico GxEPD2_1330_GDEM133T91.
 *
 * La finestra viene reimpostata subito prima della master activation: è
 * quella presente in quel momento nei registri 0x44/0x45 a definire l'area
 * che il refresh percorre, ed è il punto in prova.
 *
 * Con digit fra 1 e 4 la fascia porta la cifra e passa dal percorso a righe,
 * che lavora solo a larghezza piena: su finestre più strette la cifra viene
 * ignorata e la fascia esce uniforme. Ritorna i ms del refresh, -1 se non si
 * è concluso.
 */
static int32_t areaPass(const char* label, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint8_t value, uint8_t digit, uint8_t fg,
                        uint8_t updateSequence, uint32_t timeout_ms)
{
  const bool withDigit = (digit >= 1 && digit <= 4) && (w == SRC);
  const uint32_t bytes = (uint32_t)(w / 8) * h;

  Serial.printf("\n-- %s\n   finestra x=%u..%u  y=%u..%u  %u righe  %lu byte\n",
                label, (unsigned)x, (unsigned)(x + w - 1),
                (unsigned)y, (unsigned)(y + h - 1), (unsigned)h, (unsigned long)bytes);

  uint32_t pushUs;
  if (withDigit)
  {
    setRamWindow(x, y, w, h);
    writeCommand(0x24);
    pushUs = writeRowsWithDigit(value, h, digit, fg);
  }
  else
    pushUs = fillRect(0x24, x, y, w, h, value);
  Serial.printf("   0x24 scritto in %lu us (%.2f us/byte)\n",
                (unsigned long)pushUs, (double)pushUs / (double)bytes);

  setRamWindow(x, y, w, h);
  const char* attesa = (updateSequence == 0xF4)
                       ? "Mode 1 su finestra: fra meno di un secondo e una ventina, si misura"
                       : "atteso sotto il secondo se finestra e banco differenziale valgono";
  const int32_t ms = runRefresh(updateSequence, label, attesa, timeout_ms);

  // frame precedente allineato a quello che si vede adesso, nella stessa finestra
  if (withDigit)
  {
    setRamWindow(x, y, w, h);
    writeCommand(0x26);
    writeRowsWithDigit(value, h, digit, fg);
  }
  else
    fillRect(0x26, x, y, w, h, value);

  if (areaPassCount < (uint8_t)(sizeof(areaPasses) / sizeof(areaPasses[0])))
  {
    AreaPass& p = areaPasses[areaPassCount++];
    p.label = label;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.sequence = updateSequence;
    p.bytes = bytes;
    p.pushUs = pushUs;
    p.ms = ms;
  }
  return ms;
}

/**
 * Riepilogo della sonda d'area: la tabella delle passate e le letture che
 * contano, cioè se la durata scala con l'altezza della finestra e quanto
 * costerebbe un aggiornamento B/N fatto per finestre.
 */
static void reportAreaPasses(int32_t fullMs)
{
  // stesso costo per byte del riepilogo finale: righe da 120, il percorso _writeImage
  const double usbRow = rowUsPerByte > 0.0 ? rowUsPerByte : usPerByteReference();
  Serial.println(F("\nesito della sonda del partial d'area:"));
  if (areaPassCount == 0)
  {
    Serial.println(F("  nessuna passata eseguita"));
    return;
  }

  Serial.println(F("  finestra                    righe  0x22    byte   push us   refresh"));
  for (uint8_t k = 0; k < areaPassCount; ++k)
  {
    const AreaPass& p = areaPasses[k];
    Serial.printf("  x=%3u..%3u y=%3u..%3u    %5u  0x%02X  %6lu  %8lu  ",
                  (unsigned)p.x, (unsigned)(p.x + p.w - 1),
                  (unsigned)p.y, (unsigned)(p.y + p.h - 1), (unsigned)p.h,
                  p.sequence, (unsigned long)p.bytes, (unsigned long)p.pushUs);
    if (p.ms < 0)
      Serial.println(F("non conclusa"));
    else
      Serial.printf("%ld ms\n", (long)p.ms);
  }

  /**
   * Scala con l'altezza. Se la durata è proporzionale alle gate line, al
   * driver conviene passare la finestra minima; se è costante, il costo è
   * tutto della waveform e restringere la finestra fa risparmiare solo bus.
   */
  if (areaMsFirst > 0 && areaMsThin > 0)
  {
    const double ratioTempi = (double)areaMsFirst / (double)areaMsThin;
    const double ratioRighe = (double)AREA_P1_H / (double)AREA_THIN_H;
    Serial.printf("  %u righe %ld ms contro %u righe %ld ms: tempi 1:%.2f, righe 1:%.2f\n",
                  (unsigned)AREA_P1_H, (long)areaMsFirst,
                  (unsigned)AREA_THIN_H, (long)areaMsThin, ratioTempi, ratioRighe);
    if (ratioTempi > 1.5)
      Serial.println(F("  la durata scala con le gate line coinvolte: al driver conviene\n"
                       "  passare la finestra minima che contiene il disegno cambiato"));
    else
      Serial.println(F("  la durata non dipende dall'altezza: il costo è della waveform e\n"
                       "  restringere la finestra fa guadagnare solo sul push SPI"));
  }

  if (areaMsFirst > 0)
  {
    if (fullMs > 0)
      Serial.printf("  passata d'area %ld ms contro %ld ms del refresh pieno: %.1fx\n",
                    (long)areaMsFirst, (long)fullMs, (double)fullMs / (double)areaMsFirst);
    const double pushMs = usbRow * (double)ROW_BYTES * AREA_P1_H / 1000.0;
    Serial.printf("  ciclo su una fascia di %u righe: %.0f ms di push + %ld ms di refresh = %.2f s\n",
                  (unsigned)AREA_P1_H, pushMs, (long)areaMsFirst,
                  (pushMs + (double)areaMsFirst) / 1000.0);
    Serial.printf("  partial_refresh_time da mettere nel driver: %ld ms con margine\n",
                  (long)(areaMsFirst * 2 + 200));
  }

  if (areaMsMode1 > 0)
  {
    Serial.printf("  Mode 1 su finestra (0x22=0xF4) %ld ms su %u righe",
                  (long)areaMsMode1, (unsigned)AREA_M1_H);
    if (fullMs > 0)
      Serial.printf(", contro %ld ms del pieno", (long)fullMs);
    Serial.println();
    if (fullMs > 0 && areaMsMode1 * 2 < fullMs)
      Serial.println(F("  Mode 1 si accorcia con la finestra: anche senza banco differenziale\n"
                       "  un refresh d'area costa meno di uno pieno, e non consuma 0x26,\n"
                       "  quindi resterebbe compatibile con l'accent"));
    else if (fullMs > 0)
      Serial.println(F("  Mode 1 dura come il pieno: la finestra non accorcia la sua waveform"));
  }

  Serial.println(F("  le passate 0xFC valgono per frame in bianco e nero: 0x26 lì fa da\n"
                   "  frame precedente, quindi in quella modalità l'accent non esiste"));
}

/**
 * Sonda del partial d'area: verifica se restringendo la finestra RAM il
 * pannello aggiorna davvero solo quella porzione, e a che prezzo in tempo.
 *
 * È la domanda che resta aperta dopo la sonda differenziale. Quella misura la
 * waveform, a schermo pieno; questa misura l'indirizzamento. Sul fratello
 * monocromatico dello stesso silicio, GxEPD2_1330_GDEM133T91, refresh(x,y,w,h)
 * fa esattamente questo: _setPartialRamArea seguito da 0x22=0xFC, oppure 0xF4
 * quando il banco differenziale non c'è. Il driver 097c invece manda sempre
 * _Update_Full a schermo pieno da entrambi gli overload di refresh, quindi qui
 * non c'è niente di già verificato sul campo.
 *
 * COME SI DISTINGUE UNA FINESTRA RISPETTATA DA UNA IGNORATA. Il contenuto
 * finale non basta: la RAM accumula le scritture, quindi un refresh che
 * ripassa tutto il pannello mostra la stessa identica immagine di uno che
 * ripassa solo la finestra. Serve una discordanza voluta fra RAM e schermo,
 * ed è la fascia di trappola: nera in 0x24 a y=176..215, e nessuna finestra
 * di refresh la comprende. Se resta bianca le finestre sono rispettate, se
 * compare il refresh ha percorso il pannello intero.
 *
 * Le passate sono disgiunte lungo Y, così a sonda finita si leggono tutte
 * insieme sullo stesso schermo:
 *     0..167   passata 1, nera con la cifra 1, poi passata 4 la riporta a
 *              bianca con la cifra 3: due scritture sulla stessa area dicono
 *              se una catena di partial regge
 *   176..215   fascia di trappola, scritta in RAM e mai refreshata
 *   224..247   finestra sottile di 24 righe: dice se la durata scala con
 *              l'altezza o è tutta della waveform
 *   264..311   passata Mode 1 (0x22=0xF4), la strada che resta se 0xFC non va
 *   336..503   passata 2, nera con la cifra 2, con bordo 0x3C=0x80
 *   504..671   riquadro ristretto anche in X, x=256..511: l'unico posto del
 *              test dove la finestra non è a larghezza piena
 *
 * Il prezzo è già accettato: nelle passate 0xFC la 0x26 fa da frame
 * precedente e non da accent, quindi quello che si misura lì vale per un
 * pannello bianco e nero.
 */
static void probePartialWindowRefresh(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del partial d'area: la finestra RAM limita il refresh? ==="));
  Serial.println(F("nelle passate 0xFC si misura un pannello B/N: 0x26 fa da frame precedente"));

  // baseline bianca, col pattern hardware: fondo su cui il nero si legge subito
  Serial.println(F("\nbaseline: schermo bianco col pattern, poi refresh pieno Mode 1"));
  fillByPattern(0x47, 0xFF);   // 0x24 tutto bianco
  fillByPattern(0x46, 0x00);   // 0x26 accent spento: il refresh pieno lo legge come accent
  if (runRefresh(0xF7, "baseline bianca") < 0)
  {
    Serial.println(F("baseline non riuscita: sonda del partial d'area abbandonata"));
    return;
  }

  /**
   * Ora che il pannello è bianco, 0x26 va portato a bianco: da questo punto
   * non è più l'accent ma il frame precedente, e deve contenere quello che si
   * vede. Il pattern lo fa senza spingere 80.640 byte sul bus.
   */
  fillByPattern(0x46, 0xFF);

  Serial.printf("\ntrappola: fascia nera scritta in 0x24 a y=%u..%u, che nessuna finestra di\n"
                "refresh comprende. Se compare sul pannello, la finestra non è rispettata\n",
                (unsigned)AREA_TRAP_Y, (unsigned)(AREA_TRAP_Y + AREA_TRAP_H - 1));
  fillRect(0x24, 0, AREA_TRAP_Y, SRC, AREA_TRAP_H, 0x00);

  const bool differenzialeVivo = (diffMs1 > 0);
  if (!differenzialeVivo)
    Serial.println(F("\n0xFC non si è concluso nella sonda precedente: le passate 0xFC qui\n"
                     "sarebbero solo cinque timeout, si va diritti a Mode 1 su finestra\n"
                     "(0x22=0xF4), che è la strada che resta"));

  if (differenzialeVivo)
  {
    // bordo come lo lascia l'init, cioè come lo tiene il driver oggi
    writeCommand(0x3C);
    writeData(0x01);
    areaMsFirst = areaPass("passata 1: striscia alta a nero, cifra 1, bordo 0x3C=0x01",
                           0, AREA_P1_Y, SRC, AREA_P1_H, 0x00, 1, 0xFF, 0xFC, 30000);

    /**
     * Bordo su VCOM invece che sulla LUT: su questa famiglia di controller è
     * il valore che tiene ferma la cornice durante un partial. Le due passate
     * differiscono solo per questo, quindi il confronto è pulito.
     */
    writeCommand(0x3C);
    writeData(0x80);
    areaPass("passata 2: striscia centrale a nero, cifra 2, bordo 0x3C=0x80",
             0, AREA_P2_Y, SRC, AREA_P2_H, 0x00, 2, 0xFF, 0xFC, 30000);

    areaPass("passata 3: riquadro ristretto anche in X",
             AREA_BOX_X, AREA_BOX_Y, AREA_BOX_W, AREA_BOX_H, 0x00, 0, 0x00, 0xFC, 30000);

    // seconda scrittura sulla stessa area della passata 1: la catena regge?
    areaPass("passata 4: la striscia alta torna bianca, cifra 3",
             0, AREA_P1_Y, SRC, AREA_P1_H, 0xFF, 3, 0x00, 0xFC, 30000);

    areaMsThin = areaPass("passata 5: finestra sottile di 24 righe",
                          0, AREA_THIN_Y, SRC, AREA_THIN_H, 0x00, 0, 0x00, 0xFC, 30000);

    observePause(AREA_TRAP_PAUSE_MS,
                 "parte la passata Mode 1, che se ignora la finestra fa comparire la\n"
                 "fascia di trappola: GUARDA ORA se y=176..215 è ancora bianca");
  }

  /**
   * Mode 1 su finestra. 0xF4 è 0xF7 senza il power down finale, ed è quello
   * che il driver monocromatico manda quando hasFastPartialUpdate è false:
   * waveform piena, ma sempre delimitata dalla finestra. Vale la misura in
   * ogni caso: se 0xFC non funziona è l'unica strada che resta, e a
   * differenza di 0xFC non consuma 0x26, quindi resterebbe compatibile con
   * l'accent.
   */
  writeCommand(0x3C);
  writeData(0x01);
  areaMsMode1 = areaPass("passata Mode 1 su finestra (0x22=0xF4)",
                         0, AREA_M1_Y, SRC, AREA_M1_H, 0x00, 0, 0x00, 0xF4, 40000);

  // 0xFC e 0xF4 lasciano clock e analogico accesi: si spengono come fa _PowerOff
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  waitBusy(2000);

  reportAreaPasses(fullMs);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);
  const uint32_t tSetup = millis();
  const uint32_t heapStart = ESP.getFreeHeap();

  Serial.println();
  Serial.println(F("=== panel_diagnostic: colori e combinazioni, SOLUM ESL 9.7 pollici ==="));
  Serial.printf("chip  %s rev %d, %d core, CPU %lu MHz, APB %lu Hz\n",
                ESP.getChipModel(), (int)ESP.getChipRevision(), (int)ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)getApbFrequency());
  Serial.printf("heap  %lu byte liberi, blocco max %lu byte\n",
                (unsigned long)heapStart, (unsigned long)ESP.getMaxAllocHeap());
  Serial.printf("pin   CS=%d DC=%d RST=%d BUSY=%d (attivo %s)\n",
                PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, BUSY_ACTIVE == HIGH ? "alto" : "basso");
  Serial.printf("HSPI  SCK=%d MISO=%d MOSI=%d\n", PIN_SCK, PIN_MISO, PIN_MOSI);

  /**
   * Il clock SPI dell'ESP32 nasce dividendo l'APB per un intero: il valore
   * richiesto non sempre è ottenibile e il divisore effettivo determina il
   * limite superiore del throughput misurato più sotto.
   */
  const uint32_t apb = getApbFrequency();
  const uint32_t div = (apb + SPI_HZ / 2) / SPI_HZ;
  Serial.printf("bus   %lu Hz richiesti, MSBFIRST, SPI_MODE0 -> stimati %lu Hz (APB/%lu)\n",
                (unsigned long)SPI_HZ, (unsigned long)(apb / (div ? div : 1)), (unsigned long)div);
  Serial.printf("RAM   %u source (X) x %u gate (Y), %u byte per riga, %lu byte per piano (%.1f KiB)\n",
                (unsigned)SRC, (unsigned)GATE, (unsigned)ROW_BYTES,
                (unsigned long)PLANE_BYTES, (double)PLANE_BYTES / 1024.0);
  Serial.printf("piani 0x24 (BW) e 0x26 (accent) documentati, 0x28 in prova -> %lu byte\n"
                "      per frame se i canali scrivibili sono 3, %lu se sono 2\n",
                (unsigned long)(3UL * PLANE_BYTES), (unsigned long)(2UL * PLANE_BYTES));
  Serial.printf("bande alte %u px, fascia di controllo 0x28 alta %u px\n",
                (unsigned)BAND_H, (unsigned)CTRL_H);

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_BUSY, INPUT);
  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  Serial.printf("\nBUSY prima del reset: %d (atteso 0)\n", digitalRead(PIN_BUSY));

  Serial.println(F("\ninit del controller, spacchettata:"));
  const uint32_t tInit = millis();
  initPanel();
  Serial.printf("  totale              %6lu ms, BUSY=%d\n",
                (unsigned long)(millis() - tInit), digitalRead(PIN_BUSY));

  /**
   * Letture di registro. Lo status 0x2F va per primo perchè ha un POR noto e
   * quindi dice se il percorso di lettura vale qualcosa: senza quello, User
   * ID e temperatura sono numeri senza significato.
   */
  Serial.println(F("\nregistri in lettura:"));
  const bool readsValid = reportStatus();
  reportUserId();
  // prima del refresh la temperatura non è ancora stata caricata: baseline
  reportTemperature("prima del refresh");
  if (!readsValid)
    Serial.println(F("  (le tre letture sopra non sono attendibili: MISO del pannello non cablato)"));

  benchmarkBus();

  /**
   * Validazione dei comandi Auto Write Pattern, quelli con cui il driver
   * riempie un piano senza passare dal bus: un byte invece di 80.640. Sta
   * prima di tutto il resto di proposito, così i piani vengono comunque
   * riscritti per intero dopo e la misura delle combinazioni resta valida
   * anche se il controller non accettasse il comando.
   */
  Serial.println(F("\nvalido il pattern hardware sui due piani immagine"));
  patternMs24 = fillByPattern(0x47, 0xFF);   // 0x24 tutto bianco
  patternMs26 = fillByPattern(0x46, 0x00);   // 0x26 accent spento
  /**
   * Se 0x28 raccogliesse dati, dopo lo SWRESET il suo contenuto sarebbe
   * indefinito: azzerarlo tutto evita che eventuale spazzatura si mescoli
   * alla lettura delle bande. È anche la misura del canale che non ha pattern
   * hardware e passa sempre dal bus, cioè il caso peggiore del driver.
   */
  Serial.println(F("azzero il canale 0x28 a schermo pieno (nessun pattern hardware su 0x28)"));
  const uint32_t tYellow = millis();
  fillBand(0x28, 0, GATE, 0x00);
  Serial.printf("  piano 0x28 completo in %lu ms\n", (unsigned long)(millis() - tYellow));
  /**
   * Se 0x28 ha avviato una misura analogica invece di aprire una scrittura in
   * RAM, il BUSY è alto e i comandi di finestra delle bande verrebbero
   * ignorati: le bande sono la misura principale, quindi si aspetta.
   */
  ensureBusyLow("scrittura delle bande");

  // Le 4 combinazioni dei due piani, dall'alto verso il basso
  static const uint8_t v24[4] = { 0xFF, 0x00, 0xFF, 0x00 };
  static const uint8_t v26[4] = { 0x00, 0x00, 0xFF, 0xFF };
  /**
   * Etichette: cosa significa la combinazione secondo la polarità documentata
   * dei due piani. Il colore che ne esce lo dice il pannello, non questa
   * tabella: per le prime tre è quello che il firmware produce già oggi.
   */
  static const char* atteso[4] =
  {
    "BW=bianco, accent spento",
    "BW=nero, accent spento",
    "BW=bianco, accent acceso  <- combinazione in uso oggi",
    "BW=nero, accent acceso    <- mai generata dal firmware"
  };

  /**
   * Valore dei due piani sui pixel della cifra: combinazione (BW=1, accent=0)
   * su tutte le bande tranne la prima, che quella combinazione la ha già come
   * fondo e per la cifra usa (0,0). Così il numero stacca dal fondo qualunque
   * colore le due combinazioni rendano.
   */
  static const uint8_t d24[4] = { 0x00, 0xFF, 0xFF, 0xFF };
  static const uint8_t d26[4] = { 0x00, 0x00, 0x00, 0x00 };

  Serial.println(F("\nscrivo le 4 bande, ognuna col proprio numero in basso a sinistra:"));
  const uint32_t tBands = millis();
  for (int k = 0; k < 4; ++k)
  {
    const uint16_t y = (uint16_t)k * BAND_H;
    const uint8_t digit = (uint8_t)(k + 1);
    Serial.printf(" banda %d  y=%3u..%3u  0x24=0x%02X 0x26=0x%02X  atteso %s  cifra %s\n",
                  digit, (unsigned)y, (unsigned)(y + BAND_H - 1),
                  v24[k], v26[k], atteso[k], d24[k] ? "bianca" : "nera");
    fillBand(0x24, y, BAND_H, v24[k], digit, d24[k]);
    fillBand(0x26, y, BAND_H, v26[k], digit, d26[k]);
  }
  Serial.printf("  8 fasce scritte in %lu ms\n", (unsigned long)(millis() - tBands));

  /**
   * Prova del canale 0x28, quello che il driver custom usa per il giallo.
   * Scritto a 0xFF sulla metà alta della banda 1, che ha fondo (BW=1,
   * accent=0): qualunque differenza visibile lì dentro è 0x28 che scrive
   * davvero. Il BUSY subito dopo il comando è il secondo indizio: una
   * scrittura in RAM non lo alza, un comando analogico sì.
   */
  Serial.println(F("\nprova del canale 0x28 sulla metà alta della banda 1"));
  setRamWindow(0, 0, SRC, CTRL_H);
  writeCommand(0x28);
  delay(2);
  const bool busyAfter28 = digitalRead(PIN_BUSY) == BUSY_ACTIVE;
  Serial.printf("  BUSY subito dopo 0x28: %d -> %s\n", (int)busyAfter28,
                busyAfter28 ? "il comando avvia un'operazione: non è una scrittura RAM"
                            : "nessuna attività sul BUSY: compatibile con una scrittura RAM");
  const uint32_t bytes28 = (uint32_t)ROW_BYTES * CTRL_H;
  const uint32_t us28 = writeConst(0xFF, bytes28);
  Serial.printf("  %lu byte inviati in %lu us\n",
                (unsigned long)bytes28, (unsigned long)us28);
  // se 0x28 ha avviato una misura analogica va attesa la fine prima del refresh
  if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
  {
    const int32_t ms28 = waitBusy(2000);
    if (ms28 < 0)
      Serial.println(F("  BUSY ancora alto a 2000 ms: controller in misura, refresh a rischio"));
    else
      Serial.printf("  BUSY tornato basso dopo %ld ms\n", (long)ms28);
  }

  // prima passata: la waveform che il driver usa oggi
  const int32_t busy_ms = runRefresh(0xF7, "DISPLAY Mode 1");
  if (busy_ms > 0)
    Serial.printf("  il driver dichiara full_refresh_time = 22000 ms\n");

  // ora il registro è stato caricato dal bit load-temp del comando 0x22
  reportTemperature("dopo Mode 1");

  /**
   * Pausa prima della seconda passata: il pannello mostra adesso il risultato
   * di Mode 1 e la passata successiva lo sovrascrive, quindi va guardato ora.
   */
  Serial.printf("\nGUARDA IL PANNELLO E ANNOTA I COLORI: fra %lu s parte la\n"
                "seconda passata con DISPLAY Mode 2, che sovrascrive il risultato\n",
                (unsigned long)(MODE2_PAUSE_MS / 1000));
  for (uint32_t left = MODE2_PAUSE_MS / 1000; left > 0; left -= 5)
  {
    Serial.printf("  %lu s\n", (unsigned long)left);
    delay(5000);
  }

  /**
   * Seconda passata con 0x22=0xFF: identica alla prima tranne il Display
   * Mode, 2 invece di 1. RAM intatta, waveform differenziale presa da OTP che
   * tratta la RAM rosso come frame precedente. Non può produrre un colore
   * nuovo: serve a documentare cosa fa Mode 2 su questo pannello.
   */
  const int32_t busy_ms2 = runRefresh(0xFF, "DISPLAY Mode 2");
  reportTemperature("dopo Mode 2");


  /**
   * Sonda del partial. Sta per ultima perchè azzera i piani e riscrive lo
   * schermo da capo: tutto quello che le due passate precedenti hanno prodotto
   * va guardato prima.
   */
  observePause(PARTIAL_PAUSE_MS,
               "parte la sonda differenziale, che azzera lo schermo e lo riscrive");
  probeDifferentialRefresh(busy_ms);

  /**
   * Sonda del partial d'area. Viene dopo la differenziale perchè si appoggia
   * al suo esito: se 0xFC non si è concluso lì, qui le passate 0xFC vengono
   * saltate. Riparte da una baseline bianca, quindi quello che la
   * differenziale ha lasciato a schermo va guardato prima.
   */
  observePause(AREA_PAUSE_MS,
               "parte la sonda del partial d'area, che riporta lo schermo a bianco");
  probePartialWindowRefresh(busy_ms);

  /**
   * Deep sleep. Il datasheet definisce per 0x10 solo A[1:0]=00 (normale) e
   * A[1:0]=11 (deep sleep), e dice che in deep sleep il BUSY resta alto.
   * Il driver custom manda 0x11, che ha A[1:0]=01, un valore non definito:
   * qui si prova prima quello e, se il BUSY non sale, si manda 0x03 che
   * A[1:0]=11 lo ha davvero. Il BUSY dice quale dei due funziona.
   */
  Serial.println(F("\ndeep sleep: verifica del parametro di 0x10"));
  writeCommand(0x10);
  writeData(0x11);
  delay(10);
  const bool sleep11 = digitalRead(PIN_BUSY) == BUSY_ACTIVE;
  Serial.printf("  0x10=0x11 (A[1:0]=01, non definito): BUSY=%d -> %s\n",
                (int)sleep11, sleep11 ? "dorme" : "NON dorme");
  if (!sleep11)
  {
    writeCommand(0x10);
    writeData(0x03);
    delay(10);
    const bool sleep03 = digitalRead(PIN_BUSY) == BUSY_ACTIVE;
    Serial.printf("  0x10=0x03 (A[1:0]=11, da datasheet):  BUSY=%d -> %s\n",
                  (int)sleep03, sleep03 ? "dorme: il driver deve usare 0x03" : "NON dorme");
  }

  /**
   * Riepilogo: i totali dicono quanto di un aggiornamento è bus, quanto è
   * attesa del pannello e quanto costerebbe un frame pieno.
   */
  const double usbBulk = usPerByteReference();
  const double usbRow = rowUsPerByte > 0.0 ? rowUsPerByte : usbBulk;
  Serial.println(F("\n--- riepilogo tempi ---"));
  if (totalSpiBytes > 0 && totalSpiMicros > 0)
    Serial.printf("dati sul bus     %lu byte in %lu us -> %.2f us/byte, %.2f MB/s\n",
                  (unsigned long)totalSpiBytes, (unsigned long)totalSpiMicros,
                  (double)totalSpiMicros / (double)totalSpiBytes,
                  (double)totalSpiBytes / (double)totalSpiMicros);
  Serial.printf("transazioni      %lu comandi, %lu parametri a singolo byte\n",
                (unsigned long)totalCommands, (unsigned long)totalParamBytes);
  Serial.printf("attese BUSY      %lu ms in totale\n", (unsigned long)totalBusyMillis);
  Serial.printf("setup completo   %lu ms\n", (unsigned long)(millis() - tSetup));
  Serial.printf("heap             %lu byte liberi (%ld byte consumati dal test)\n",
                (unsigned long)ESP.getFreeHeap(),
                (long)heapStart - (long)ESP.getFreeHeap());

  Serial.println(F("\n--- stima di un aggiornamento a schermo pieno ---"));
  Serial.printf("push 2 piani (BW+RED) %lu byte -> %.0f ms a blocchi da 120 byte\n",
                (unsigned long)(2UL * PLANE_BYTES), usbRow * 2.0 * PLANE_BYTES / 1000.0);
  if (patternMs24 >= 0 && patternMs26 >= 0)
    Serial.printf("clear dei 2 piani col pattern hardware: %ld ms invece di %.0f ms\n",
                  (long)(patternMs24 + patternMs26), usbBulk * 2.0 * PLANE_BYTES / 1000.0);
  if (busy_ms > 0)
    Serial.printf("ciclo completo        %.1f s: %.0f ms di push + %ld ms di refresh\n",
                  (usbRow * 2.0 * PLANE_BYTES / 1000.0 + (double)busy_ms) / 1000.0,
                  usbRow * 2.0 * PLANE_BYTES / 1000.0, (long)busy_ms);
  if (busy_ms > 0 && busy_ms2 > 0)
    Serial.printf("refresh Mode 1 %ld ms, Mode 2 %ld ms: %s\n", (long)busy_ms, (long)busy_ms2,
                  busy_ms2 > busy_ms + 500 || busy_ms2 < busy_ms - 500
                    ? "durate diverse, quindi Mode 2 usa un altro banco di waveform"
                    : "durate uguali, probabilmente lo stesso banco");
  if (diffMs1 > 0)
  {
    Serial.printf("refresh differenziale (0x22=0xFC) %ld ms", (long)diffMs1);
    if (diffMs2 > 0)
      Serial.printf(", seconda passata %ld ms", (long)diffMs2);
    if (busy_ms > 0)
      Serial.printf(" -> %.1fx più veloce del pieno", (double)busy_ms / (double)diffMs1);
    Serial.println();
    if (busy_ms > 0 && diffMs1 * 4 < busy_ms)
    {
      const double pushMs = usbRow * PLANE_BYTES / 1000.0;
      Serial.printf("se il frame nuovo si vede, un aggiornamento B/N costerebbe %.1f s\n"
                    "  (%.0f ms per il solo piano 0x24 + %ld ms di refresh) invece di %.1f s\n",
                    (pushMs + (double)diffMs1) / 1000.0, pushMs, (long)diffMs1,
                    (usbRow * 2.0 * PLANE_BYTES / 1000.0 + (double)busy_ms) / 1000.0);
    }
  }
  if (areaMsFirst > 0 || areaMsMode1 > 0)
  {
    Serial.println(F("partial d'area, valido solo se la fascia di trappola è rimasta bianca:"));
    if (areaMsFirst > 0)
    {
      const double pushBand = usbRow * (double)ROW_BYTES * AREA_P1_H / 1000.0;
      Serial.printf("  0xFC su %u righe: %.0f ms di push + %ld ms di refresh = %.2f s\n",
                    (unsigned)AREA_P1_H, pushBand, (long)areaMsFirst,
                    (pushBand + (double)areaMsFirst) / 1000.0);
    }
    if (areaMsThin > 0)
      Serial.printf("  0xFC su %u righe: %ld ms di refresh\n",
                    (unsigned)AREA_THIN_H, (long)areaMsThin);
    if (areaMsMode1 > 0)
      Serial.printf("  0xF4 su %u righe: %ld ms di refresh\n",
                    (unsigned)AREA_M1_H, (long)areaMsMode1);
  }

  /**
   * Scheda di osservazione. Il test non può vedere il pannello, quindi si
   * ferma qui e lascia le domande e la mappa esito -> conseguenza. Le
   * combinazioni misurate sono tutte quelle che due piani a 1 bit possono
   * esprimere, quindi le risposte a questi punti chiudono la questione.
   */
  Serial.println(F("\n============ SCHEDA DI OSSERVAZIONE ============"));
  Serial.println(F("Guarda il pannello e annota. Ogni banda porta il proprio numero"));
  Serial.println(F("disegnato in basso a sinistra."));
  Serial.println(F(""));
  Serial.println(F("  banda 1  (BW=1 RED=0)  colore ....................."));
  Serial.println(F("  banda 2  (BW=0 RED=0)  colore ....................."));
  Serial.println(F("  banda 3  (BW=1 RED=1)  colore ....................."));
  Serial.println(F("  banda 4  (BW=0 RED=1)  colore ....................."));
  Serial.println(F("  metà alta della banda 1, dove è stato scritto 0x28:"));
  Serial.println(F("           uguale al resto della banda?   SI / NO"));
  Serial.println(F("  dopo la passata Mode 2, cosa è cambiato ..........."));
  Serial.println(F(""));
  Serial.println(F("--- conseguenze per il driver, secondo cosa hai osservato ---"));
  Serial.println(F(""));
  Serial.println(F("BANDA 3, il colore dell'accent con i due piani a 1:"));
  Serial.println(F("  rossa   -> l'accent del film è rosso, il piano 0x26 è il rosso:"));
  Serial.println(F("             il driver è già giusto su questo punto"));
  Serial.println(F("  gialla  -> l'accent del film è giallo: 0x26 pilota il GIALLO, e nel"));
  Serial.println(F("             driver il giallo va mappato su 0x26, non su 0x28"));
  Serial.println(F("  bianca o nera -> l'accent non si accende: guarda banda 2, se anche"));
  Serial.println(F("             quella è sbagliata init o cablaggio non funzionano e"));
  Serial.println(F("             nessun'altra riga di questa scheda ha valore"));
  Serial.println(F(""));
  Serial.println(F("BANDA 4, la combinazione che il firmware non genera mai:"));
  Serial.println(F("  colore diverso da banda 2 E da banda 3 -> la coppia (BW=0, RED=1)"));
  Serial.println(F("             è un TERZO stato renderizzabile: esiste un 4o colore, e"));
  Serial.println(F("             va codificato nel driver come coppia di bit sui due piani"));
  Serial.println(F("             esistenti, non come terzo piano"));
  Serial.println(F("  uguale a banda 3 -> vince il piano RED: la coppia non aggiunge"));
  Serial.println(F("             stati, e nel driver conviene forzare BW=1 dove c'è"));
  Serial.println(F("             accent, risparmiando la coerenza fra i due piani"));
  Serial.println(F("  uguale a banda 2 -> vince il piano BW: idem al contrario"));
  Serial.println(F(""));
  Serial.println(F("METÀ ALTA BANDA 1, il canale 0x28 del driver:"));
  Serial.println(F("  diversa dal resto della banda -> 0x28 su questo modulo scrive"));
  Serial.println(F("             davvero un piano: writeImageYellow e preserveYellow"));
  Serial.println(F("             restano come sono, e il colore che vedi è il 4o colore"));
  Serial.println(F("  identica al resto -> 0x28 non produce nulla (il datasheet lo dà"));
  Serial.println(F("             come VCOM Sense): nel driver writeImageYellow,"));
  Serial.println(F("             preserveYellow, _yellow_dirty e il terzo _writeScreenBuffer"));
  Serial.println(F("             sono codice che costa 80.640 byte per chiamata senza"));
  Serial.println(F("             effetto, e il giallo va cercato altrove"));
  Serial.println(F(""));
  Serial.println(F("PASSATA MODE 2:"));
  Serial.println(F("  niente cambia -> Mode 1 e Mode 2 rendono uguale, hasFastPartialUpdate"));
  Serial.println(F("             = false resta la scelta giusta"));
  Serial.println(F("  cambia qualcosa -> annota cosa: Mode 2 è il banco differenziale e"));
  Serial.println(F("             tratta la seconda RAM come frame precedente, quindi una"));
  Serial.println(F("             differenza va interpretata prima di chiamarla colore"));
  Serial.println(F(""));
  Serial.println(F("SONDA DIFFERENZIALE, le due passate con 0x22=0xFC:"));
  Serial.println(F("  la cifra 1 su bianco e poi la cifra 2 su nero appaiono, ognuna in"));
  Serial.println(F("             meno di un secondo -> il banco di waveform differenziale"));
  Serial.println(F("             esiste in OTP: sui frame senza accent il driver può usare"));
  Serial.println(F("             0xFC e scendere da 22 s a meno di uno, riscrivendo 0x26"));
  Serial.println(F("             come frame precedente al posto del rosso."));
  Serial.println(F("             hasFastPartialUpdate resta false per i frame con accent"));
  Serial.println(F("  appaiono, ma lente come il refresh pieno -> 0xFC ricade su Mode 1:"));
  Serial.println(F("             niente da guadagnare, il driver resta com'è"));
  Serial.println(F("  la passata 2 esce ROSSA -> 0xFC non seleziona Mode 2: il piano"));
  Serial.println(F("             0x26, che la sonda ha riempito di bianco come frame"));
  Serial.println(F("             precedente, è stato riletto come accent. Il partial"));
  Serial.println(F("             differenziale non esiste su questo pannello"));
  Serial.println(F("  non appaiono, o appaiono sporche di ghosting -> il banco non c'è"));
  Serial.println(F("             oppure vuole una LUT caricata via 0x32: strada chiusa"));
  Serial.println(F("             senza la waveform del produttore"));
  Serial.println(F("  NB: qualunque ghosting lasciato dalla sonda lo cancella il primo"));
  Serial.println(F("      refresh pieno successivo, cioè il prossimo frame del firmware"));
  Serial.println(F(""));
  Serial.println(F("SONDA DEL PARTIAL D'AREA, le passate su finestra:"));
  Serial.println(F("  fascia di trappola a y=176..215, scritta in 0x24 e mai compresa in"));
  Serial.println(F("  nessuna finestra di refresh. Dopo le passate 0xFC era: BIANCA / NERA"));
  Serial.println(F("  (te l'ha chiesto la pausa prima della passata Mode 1)"));
  Serial.println(F("  bianca -> la finestra RAM limita davvero il refresh: il partial d'area"));
  Serial.println(F("             esiste. In GxEPD2_SOLUM_097c_960x672.h: hasFastPartialUpdate"));
  Serial.println(F("             a true, partial_refresh_time alla durata misurata sopra,"));
  Serial.println(F("             refresh(x,y,w,h) che fa _setPartialRamArea sull'area e un"));
  Serial.println(F("             nuovo _Update_Part con 0x22=0xFC invece di chiamare"));
  Serial.println(F("             _Update_Full, e writeImageAgain che scrive su 0x26 il frame"));
  Serial.println(F("             precedente al posto dell'accent"));
  Serial.println(F("  nera   -> il refresh ripassa il pannello intero qualunque finestra sia"));
  Serial.println(F("             impostata: il partial d'area non esiste, hasPartialUpdate"));
  Serial.println(F("             resta vero solo come indirizzamento e i due refresh del"));
  Serial.println(F("             driver restano su _Update_Full come sono oggi"));
  Serial.println(F(""));
  Serial.println(F("  riquadro a x=256..511 y=504..671, l'unica finestra ristretta in X:"));
  Serial.println(F("           i bordi verticali sono netti?   SI / NO"));
  Serial.println(F("  no     -> la finestra lungo X non viene rispettata: nel driver il"));
  Serial.println(F("             partial va allargato d'ufficio a x=0 w=WIDTH, cioè si"));
  Serial.println(F("             aggiornano fasce alte quanto serve ma larghe tutto lo schermo"));
  Serial.println(F(""));
  Serial.println(F("  striscia alta y=0..167, riscritta due volte: alla fine porta la cifra 3"));
  Serial.println(F("  nera su fondo bianco?   SI / NO"));
  Serial.println(F("  sì     -> la catena di partial sulla stessa area regge: il driver può"));
  Serial.println(F("             aggiornare ripetutamente la stessa zona senza refresh pieno"));
  Serial.println(F("  no, resta la cifra 1 o è sporca -> la seconda scrittura non passa"));
  Serial.println(F("             pulita: serve un refresh pieno periodico, cioè un contatore"));
  Serial.println(F("             di partial nel driver che forzi _Update_Full ogni N"));
  Serial.println(F(""));
  Serial.println(F("  cornice del pannello durante le passate: ha lampeggiato?"));
  Serial.println(F("           la passata 1 usa 0x3C=0x01 (bordo sulla LUT), la 2 e la 3"));
  Serial.println(F("           usano 0x3C=0x80 (bordo su VCOM)"));
  Serial.println(F("  la 1 lampeggia e le altre no -> il driver deve mandare 0x3C=0x80 prima"));
  Serial.println(F("             di un partial e rimettere 0x01 prima di un refresh pieno"));
  Serial.println(F(""));
  Serial.println(F("  passata Mode 1 su finestra (y=264..311, 0x22=0xF4), l'ultima:"));
  Serial.println(F("  la trappola è diventata nera SOLO adesso -> Mode 1 ripassa il pannello"));
  Serial.println(F("             intero e solo 0xFC rispetta la finestra: il partial esiste"));
  Serial.println(F("             ma costa i colori, non c'è la via di mezzo"));
  Serial.println(F("  la striscia esce nera, la trappola resta bianca e la durata è molto"));
  Serial.println(F("  sotto il refresh pieno -> anche Mode 1 rispetta la finestra: si può"));
  Serial.println(F("             fare partial d'area SENZA perdere l'accent, perchè 0xF4 non"));
  Serial.println(F("             usa 0x26 come frame precedente. È l'esito migliore"));
  Serial.println(F(""));
  Serial.println(F("  PREZZO GIÀ ACCETTATO SULLA STRADA 0xFC: con 0x26 usata come frame"));
  Serial.println(F("             precedente l'accent non è disponibile. Un frame aggiornato"));
  Serial.println(F("             in partial è un frame bianco e nero, e le due cose non"));
  Serial.println(F("             possono convivere nello stesso frame: il driver dovrà"));
  Serial.println(F("             scegliere per frame, non per pixel."));
  Serial.println(F(""));
  Serial.println(F("DEEP SLEEP: vedi sopra quale dei due parametri di 0x10 alza il BUSY;"));
  Serial.println(F("il driver deve mandare quello."));
  Serial.println(F("================================================"));
}

void loop()
{
  delay(60000);
}
