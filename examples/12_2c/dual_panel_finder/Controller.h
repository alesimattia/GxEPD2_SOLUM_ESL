// =============================================================================
// Controller.h — il bus, l'init, il refresh: tutto ciò che parla al silicio.
//
// SEMPRE A SPI DIRETTA, mai attraverso il driver custom: il driver è l'oggetto
// della misura, non lo strumento, e passare da lui farebbe ereditare a ogni
// misura le sue assunzioni. Del driver si riproducono le SEQUENZE, comando per
// comando, e l'ordine delle operazioni delle primitive è identico a quello di
// GxEPD2_EPD::_writeCommand / _writeData — D/C riportato alto in coda compreso —
// così il costo misurato è quello che il driver paga.
//
// QUI NON STA NESSUNA SONDA. Questo file offre il vocabolario; a decidere cosa
// chiedere al pannello pensano i file Probes*.h. Il confine tiene le coordinate
// del bus fuori dalle sonde e le domande fuori dal bus.
//
// I DUE MODI DEL BUS. Il datasheet dà il pin BS1 come selettore fra 4 fili e
// 3 fili a 9 bit, ed è un ingresso che sul FFC della board non è cablato. Le
// primitive parlano quindi entrambi i modi, scelti da cfg.busMode: in 3 fili il
// D/C# resta BASSO per tutta la transazione e il suo valore logico diventa il
// PRIMO dei nove bit del frame (§6.1.3, "the bit shifting sequence is D/C# bit,
// D7, D6 to D0"). L'ESP32 manda nove bit in hardware con transferBits(), quindi
// non serve nessun bit-bang.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_CONTROLLER_H
#define DUAL_PANEL_FINDER_CONTROLLER_H

#include <Arduino.h>
#include <SPI.h>
#include "Config.h"
#include "Report.h"
#include "Graphics.h"

// ===== Oggetti di bus ================================================

SPIClass hspi(HSPI);
// Non const: lo sweep dei clock e il frame di verifica la riassegnano.
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

// ===== Stato dello sweep, scritto dall'orchestrazione ================
// Sono le variabili che prima erano #define: la candidata di init in corso e il
// MUX che initPanel() programma.
static uint8_t  g_cand = CAND_DRIVER;
static uint16_t g_mux  = 384;

// --- primitive di bus ------------------------------------------------

/**
 * Offset sommato a ogni opcode. Vale 0 per tutto il test tranne che dentro la
 * sonda del secondo controller, dove diventa 0x80: è il modo in cui il
 * firmware di fabbrica del 5.85" distingue i due controller di una coppia in
 * CASCADE sullo stesso bus. Su questo pannello la cascade è esclusa — estende
 * le sorgenti e non i gate — quindi la sonda serve a verificare se il bit 7
 * indirizza qualcosa, non ad assumere che indirizzi. Va sempre riportato a 0
 * prima di uscire da quella sonda, altrimenti anche le letture di registro
 * partirebbero offset.
 */
static uint8_t cmdOffset = 0x00;

/**
 * Con csBoth attivo il chip select dell'altra coda segue quello della coda
 * sotto test, così i due controller vedono lo stesso bus. Fuori dalla sonda
 * resta false e l'altra coda sta alta, cioè muta.
 */
static bool csBoth = false;

static inline void csAssert()
{
  digitalWrite(PIN_CS, LOW);
  if (csBoth)
    digitalWrite(PIN_CS_OTHER, LOW);
}

static inline void csRelease()
{
  digitalWrite(PIN_CS, HIGH);
  if (csBoth)
    digitalWrite(PIN_CS_OTHER, HIGH);
}

/**
 * Manda un byte in 3 fili: nove bit, il primo dei quali è il D/C#. Il pin D/C
 * resta basso, come il datasheet pretende in questo modo ("the D/C# pin is not
 * used and it must be tied to LOW").
 */
static void write9bit(bool dato, uint8_t b)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_DC, LOW);
  csAssert();
  hspi.transferBits((uint32_t)((dato ? 0x100u : 0x000u) | b), nullptr, 9);
  csRelease();
  hspi.endTransaction();
}

/**
 * Invia un byte di comando: D/C basso in 4 fili, primo bit a 0 in 3 fili.
 * Ordine delle operazioni identico a GxEPD2_EPD::_writeCommand, D/C riportato
 * alto in coda incluso.
 */
static void writeCommand(uint8_t c)
{
  if (cfg.busMode == BUS_3WIRE) { write9bit(false, (uint8_t)(c | cmdOffset)); return; }
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_DC, LOW);
  csAssert();
  hspi.transfer(c | cmdOffset);
  csRelease();
  digitalWrite(PIN_DC, HIGH);
  hspi.endTransaction();
}

/** Invia un byte di parametro: D/C alto in 4 fili, primo bit a 1 in 3 fili. */
static void writeData(uint8_t d)
{
  if (cfg.busMode == BUS_3WIRE) { write9bit(true, d); return; }
  hspi.beginTransaction(spiSettings);
  csAssert();
  hspi.transfer(d);
  csRelease();
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

/**
 * Reset hardware, con la durata dell'impulso e il numero di tentativi presi da
 * cfg. Un tentativo solo con impulso da 10 ms è quello che fa il driver; il
 * firmware di fabbrica OEPL invece cicla con attese CRESCENTI finchè il chip
 * non dà segno di vita, e su una coda che non risponde quella differenza è una
 * delle spiegazioni possibili del silenzio. Ritorna il numero del tentativo in
 * cui il BUSY si è mosso, 0 se non si è mosso mai.
 */
static uint8_t resetPanelTentativi(uint8_t tentativi, uint8_t lowMs)
{
  for (uint8_t k = 1; k <= (tentativi ? tentativi : 1); ++k)
  {
    const uint16_t attesa = (uint16_t)(lowMs + 20 * (k - 1));
    digitalWrite(PIN_RST, HIGH);
    delay(attesa);
    digitalWrite(PIN_RST, LOW);
    delay(attesa);
    digitalWrite(PIN_RST, HIGH);
    delay(attesa);
    // Il BUSY alto subito dopo il rilascio del reset è il segno che il
    // controller ha iniziato la propria sequenza interna.
    const uint32_t t0 = millis();
    while ((millis() - t0) < 20)
      if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) return k;
  }
  return 0;
}

/** Il reset del caso normale: quello che il driver fa. */
static void resetPanel()
{
  resetPanelTentativi(cfg.resetTentativi, cfg.resetLowMs);
}

// Finestra RAM e cursore. Stessa sequenza di comandi del driver custom.
/**
 * Finestra RAM in vigore. Il riquadro col numero della schermata la restringe
 * per un istante e deve rimetterla com'era: è la finestra presente alla master
 * activation a definire l'area che il refresh percorre.
 */
static uint16_t ramWinX = 0, ramWinY = 0, ramWinW = SRC, ramWinH = 384;

/**
 * Imposta finestra e cursore. L'indirizzo X va in PIXEL e non in byte: §8.3 dà
 * XEA <= 3BFh, cioè 959, che è il conteggio delle source. Sul SSD1683, che
 * indirizza X in byte, la stessa sequenza vorrebbe x/8 — è una delle differenze
 * che rendono i driver delle due famiglie non interscambiabili.
 */
static void setRamWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  ramWinX = x;
  ramWinY = y;
  ramWinW = w;
  ramWinH = h;
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
 * Finestra RAM con il cursore posizionato secondo l'ENTRY MODE, che è la parte
 * non ovvia. I bit ID[1:0] di 0x11 dicono se il contatore di indirizzo
 * incrementa o decrementa su ciascun asse (§8.2: "00 Y decrement X decrement,
 * 01 Y decrement X increment, 10 Y increment X decrement, 11 Y increment X
 * increment [POR]"), e un contatore che decrementa deve partire dall'ESTREMO
 * OPPOSTO della finestra, altrimenti scrive fuori.
 *
 * È la tecnica con cui GxEPD2 specchia una delle due metà del GDEY0579Z93 senza
 * rovesciare nè byte nè bit: se funziona anche qui, il reverse nel data path
 * del driver 12.2" — righe, byte e bit — diventa cancellabile. Il datasheet non
 * dice cosa accada agli otto bit DENTRO un byte scritto, quindi la sonda usa un
 * pattern con struttura sotto il byte: è l'unico modo di distinguere una
 * specchiatura di byte da una di bit.
 */
static void setRamWindowEntry(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              uint8_t entryMode)
{
  const bool xDec = ((entryMode & 0x01) == 0x00);   // ID0 = 0 -> X decrementa
  const bool yDec = ((entryMode & 0x02) == 0x00);   // ID1 = 0 -> Y decrementa

  ramWinX = x; ramWinY = y; ramWinW = w; ramWinH = h;

  writeCommand(0x11);
  writeData(entryMode);

  // La finestra si dichiara sempre come start/end crescenti: sono i registri
  // del CURSORE a dire da quale estremo il contatore parte.
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

  const uint16_t cx = xDec ? (uint16_t)(x + w - 1) : x;
  const uint16_t cy = yDec ? (uint16_t)(y + h - 1) : y;
  writeCommand(0x4E);
  writeData(cx % 256);
  writeData(cx / 256);
  writeCommand(0x4F);
  writeData(cy % 256);
  writeData(cy / 256);
}

static void pushBadgePiano(uint8_t plane, uint16_t bx, uint16_t by,
                           uint8_t bw, uint8_t bh, bool cifre)
{
  static const uint8_t spento[BADGE_MAX_BYTES] = { 0 };
  const uint8_t nbyte = (uint8_t)(bw / 8);

  setRamWindow(bx, by, bw, bh);
  writeCommand(plane);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  csAssert();
  for (uint8_t r = 0; r < bh; ++r)
    hspi.writeBytes(cifre ? badgeRiga(r) : spento, nbyte);
  csRelease();
  hspi.endTransaction();
}
/**
 * Disegna il riquadro in alto a destra della finestra RAM corrente e rimette la
 * finestra com'era: è quella a decidere l'area ridipinta. Chiamata da
 * runRefresh() prima della master activation di ogni frame.
 *
 * Con BADGE_PIANI_UGUALI i due piani ricevono lo STESSO pattern invece che il
 * riquadro nel BW e zeri nell'accent. Serve alla passata a differenza zero della
 * differenziale: scritto identico, il riquadro non introduce nessuna differenza
 * fra i piani e quella misura resta valida pur avendo il numero sul vetro.
 */
static void disegnaBadge(uint8_t numero, ModoBadge modo)
{
  uint8_t bw = 0, bh = 0;
  if (!componiBadge(numero, ramWinW, ramWinH, bwByteFor(true), bwByteFor(false), &bw, &bh))
  {
    Serial.printf("   finestra %ux%u troppo piccola per il riquadro della schermata %u:\n"
                  "   questa passata la riconosci dalla cifra sulla fascia\n",
                  (unsigned)ramWinW, (unsigned)ramWinH, (unsigned)numero);
    return;
  }

  // in alto a destra della finestra, allineato al byte sull'asse source
  uint16_t bx = (uint16_t)((ramWinX + ramWinW - bw - BADGE_MARGINE) & ~7);
  if (bx < ramWinX) bx = ramWinX;
  const uint16_t scarto = (uint16_t)(ramWinH - bh);
  const uint16_t by = (uint16_t)(ramWinY + (scarto > 8 ? 8 : scarto));

  const uint16_t salvaX = ramWinX, salvaY = ramWinY;
  const uint16_t salvaW = ramWinW, salvaH = ramWinH;
  pushBadgePiano(0x24, bx, by, bw, bh, true);
  pushBadgePiano(0x26, bx, by, bw, bh, modo == BADGE_PIANI_UGUALI);
  setRamWindow(salvaX, salvaY, salvaW, salvaH);
}

// --- init: le candidate, scelte a runtime ----------------------------

/**
 * Soft start del booster, cmd 0x0C. I primi quattro byte sono comuni a tutti i
 * driver della famiglia; il quinto è il LIVELLO, e il datasheet ne dà due:
 * 0x40 = Level 1 (il più debole), 0x80 = Level 2. L'init di fabbrica SOLUM e il
 * driver 12.2" usano il Level 2, GxEPD2_1160_T91 il Level 1. Su una coda che
 * non parte la forza dell'avvio del booster è una variabile da provare, non da
 * assumere, quindi il livello viene da cfg.
 */
static void writeSoftStart()
{
  writeCommand(0x0C);
  writeData(0xAE);
  writeData(0xC7);
  writeData(0xC3);
  writeData(0xC0);
  writeData(cfg.softLevel);
}

/**
 * Programma il MUX, cioè quante gate line il controller scandisce. Il registro
 * 0x01 vuole (linee - 1) su 10 bit in due byte little endian, più un terzo byte
 * con GD, SM e TB.
 *
 * TB (B[0]) è il bit interessante: sul SSD1677 il datasheet lo dà **Reserved**
 * e la scansione va solo da G0 a G679; sul SSD1683 vale "scan from G299 to G0",
 * cioè un ribaltamento verticale IN HARDWARE. Se questo die si comporta da 1683
 * — e sul secondo byte di 0x21 lo fa già — allora la specchiatura della banda
 * capovolta si risolve in un registro, e il reverse di righe, byte e bit nel
 * data path del driver diventa cancellabile. Da qui TB come parametro.
 *
 * Con mux == MUX_NOT_WRITTEN il registro NON viene toccato e resta al default
 * della OTP: è quello che fa CAND_MINIMAL, ed è uno dei valori dello sweep.
 */
static void writeMux(uint16_t mux)
{
  if (mux == MUX_NOT_WRITTEN) return;
  const uint16_t v = mux - 1;
  writeCommand(0x01);
  writeData(v % 256);
  writeData(v / 256);
  writeData(cfg.tbReverse ? 0x01 : 0x00);
}

/**
 * Scrive 0x21 se la configurazione dice di scriverlo in questo punto.
 *
 * Il registro fa DUE cose insieme. A[7:4] è l'opzione della RAM Red e A[3:0]
 * quella della RAM BW — Normal, Bypass-as-0, Inverse — quindi è anche il posto
 * da cui si governa la polarità apparente del piano, ed è per questo che OEPL
 * scrive 0x08 commentandolo "fix reversed image with stock setup".
 *
 * Il SECONDO byte invece non esiste nella Rev 1.0 dell'SSD1677: lo definisce il
 * SSD1683, dove B[4] è *ckouten*, "0: Single chip application, 1: Cascade
 * application". Che l'init di fabbrica SOLUM lo mandi è uno degli indizi che il
 * die sia più recente della Rev 1.0. Su questo pannello però la cascade è
 * esclusa, quindi mandarlo a 1 è probabilmente un errore da togliere dal
 * driver: la sonda lo verifica invece di assumerlo, e la posizione conta perchè
 * upstream varia — il gemello BW del 5.79" lo manda nel refresh, la variante a
 * tre colori non lo manda affatto.
 *
 * `dove`: 1 = dentro l'init, 2 = subito prima di 0x22. Vale per ogni candidata
 * tranne CAND_OEPL, dove `0x21 = 08 00` è parte della sequenza di fabbrica che
 * quella candidata riproduce e sovrascriverlo la snaturerebbe.
 */
static void writeReg21(uint8_t dove)
{
  if (cfg.reg21Dove != dove) return;
  writeCommand(0x21);
  writeData(cfg.reg21A);
  if (cfg.reg21Due) writeData(cfg.reg21B);
}

/**
 * Reset hardware più la sequenza di init della candidata `cand`, programmando
 * `mux` gate line, cronometrando ogni passo. Il BUSY dopo lo SWRESET viene
 * osservato: la durata reale del reset interno diventa un dato invece di una
 * stima, ed è anche la prima prova che il controller ha preso un comando — il
 * datasheet dà "During operation, BUSY pad will output high" e sul 9.7" quella
 * durata è stata misurata in 2 ms, contro i 200 che il driver aspetta a occhio.
 *
 * Candidata e MUX sono parametri e non #define: erano una ricompilazione per
 * combinazione, e ora sono due cicli dentro la stessa esecuzione. Chi chiama
 * passa anche per g_cand e g_mux, che restano lo stato corrente per le funzioni
 * che compongono i pattern e per il report.
 *
 * Ritorna i ms di BUSY dello SWRESET, -1 se il BUSY non si è mosso.
 */
static int32_t initPanel(uint8_t cand, uint16_t mux)
{
  g_cand = cand;
  g_mux  = mux;

  const uint32_t tReset = micros();
  const uint8_t tentativo = resetPanelTentativi(cfg.resetTentativi, cfg.resetLowMs);
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

  switch (cand)
  {
    case CAND_MINIMAL:
      /**
       * Init corta: solo il border waveform, tutto il resto ai default POR. Non
       * scrive nemmeno il MUX, di proposito: è la candidata che dice cosa fa il
       * silicio quando gli si tocca il minimo indispensabile.
       */
      writeCommand(0x3C);
      writeData(0x01);      // LUT1, bianco
      writeReg21(1);
      break;

    case CAND_SOLUM:
      // È la sequenza che il driver custom implementa oggi in _InitDisplay().
      writeSoftStart();
      writeMux(mux);
      writeCommand(0x3C);   // border waveform
      writeData(0x01);
      writeCommand(0x18);   // sensore di temperatura interno
      writeData(0x80);
      writeCommand(0x11);   // entry mode
      writeData(cfg.entryMode);
      writeReg21(1);
      break;

    case CAND_OEPL:
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
      writeSoftStart();
      writeMux(mux);
      writeCommand(0x11);   // entry mode
      writeData(cfg.entryMode);
      setRamWindow(0, 0, SRC, cfg.gate);
      writeCommand(0x3C);   // border waveform
      writeData(0x01);
      writeCommand(0x18);   // sensore di temperatura interno
      writeData(0x80);
      writeCommand(0x22);   // display update sequence, caricata ma non attivata
      writeData(0xF7);
      writeCommand(0x21);   // in questa candidata 0x21 è parte della sequenza
      writeData(0x08);
      writeData(0x00);
      break;

    case CAND_1330C:
      /**
       * GxEPD2_1330c_GDEM133Z91: il gemello a tre colori dell'unico driver
       * upstream che gira davvero a 960x680 con il MUX programmato, quindi la
       * forma canonica del command set SSD1677 a 960 source. Rispetto a
       * CAND_SOLUM non scrive l'entry mode, perchè la finestra RAM lo riscrive
       * a ogni write, e non scrive 0x21.
       */
      writeSoftStart();
      writeMux(mux);
      writeCommand(0x3C);
      writeData(0x01);
      writeCommand(0x18);
      writeData(0x80);
      writeReg21(1);
      setRamWindow(0, 0, SRC, cfg.gate);
      break;

    case CAND_1160T91:
      /**
       * GxEPD2_1160_T91: come sopra, più il ciclo 0x22 = 0xB1 + 0x20 che carica
       * temperatura e waveform dall'OTP PRIMA del primo frame. È l'unico driver
       * a 960 source che lo fa in init, ed è anche l'unico con un _Init_Part:
       * se il primo frame di una candidata esce sbiadito mentre il secondo è
       * pieno, è questo carico che manca.
       */
      writeSoftStart();
      writeMux(mux);
      writeCommand(0x3C);
      writeData(0x01);
      writeCommand(0x18);
      writeData(0x80);
      writeCommand(0x22);   // load temperature + load LUT, Display Mode 1
      writeData(0xB1);
      writeCommand(0x20);   // master activation: qui non dipinge, carica
      waitBusy(2000);
      writeReg21(1);
      setRamWindow(0, 0, SRC, cfg.gate);
      break;

    case CAND_PERCS:
      /**
       * Init a controller INDIPENDENTI: la stessa sequenza di CAND_1330C, ma
       * mandata due volte, una per chip select, invece che in broadcast. È la
       * topologia che il datasheet sostiene per uno split sull'asse gate —
       * la cascade estende le sorgenti, non i gate — ed è quella che il driver
       * dovrà tenere se la sonda dell'impronta dice che il secondo chip ha un
       * oscillatore proprio.
       *
       * Con un solo connettore cablato la seconda passata non arriva a nessuno
       * e la candidata degenera in CAND_1330C: non è uno spreco, è il caso di
       * controllo che dice che il doppio init non disturba il chip che risponde.
       */
      for (uint8_t giro = 0; giro < 2; ++giro)
      {
        const bool secondo = (giro == 1);
        if (secondo && !cfg.secondFfc) break;
        if (secondo)
        {
          digitalWrite(PIN_CS, HIGH);          // il primo esce dal bus
          pinMode(PIN_CS_OTHER, OUTPUT);
          digitalWrite(PIN_CS_OTHER, HIGH);
        }
        writeSoftStart();
        writeMux(mux);
        writeCommand(0x3C);
        writeData(0x01);
        writeCommand(0x18);
        writeData(0x80);
        writeReg21(1);
        setRamWindow(0, 0, SRC, cfg.gate);
      }
      break;
  }

  const uint32_t usCfg = micros() - tCfg;

  measure("SWRESET BUSY", 0, rose ? swBusy : -1);
  logDetail("init %s, reset %lu us (tentativo %u), config %lu us",
            CAND_LABEL[cand], (unsigned long)usReset, (unsigned)tentativo,
            (unsigned long)usCfg);
  logDetail("MUX %s, entry mode %02X, TB %u, soft start %02X",
            (mux == MUX_NOT_WRITTEN) ? "non scritto" : "scritto",
            cfg.entryMode, cfg.tbReverse ? 1u : 0u, cfg.softLevel);
  if (!rose)
    logLine("SWRESET: BUSY non e' salito entro 50 ms");
  else
    logDetail("SWRESET: BUSY salito dopo %lu ms", (unsigned long)riseMs);
  return rose ? swBusy : -1;
}
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

  /**
   * Glifo grande al centro della banda: il numero della candidata di init che ha
   * prodotto questo frame, 1 = MINIMAL, 2 = SOLUM, 3 = OEPL. Le tre passate
   * stampano lo stesso pattern e una cancella l'altra, quindi sul vetro devono
   * restare distinguibili senza contare i frame.
   */
  paintGlyph(row, y, (uint8_t)(g_cand + 1),
             SRC / 2 - 40, cfg.gate / 2 - 56, BIG_SCALE, 0x00);
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
  if (y >= (int16_t)cfg.gate - 64 && y < (int16_t)cfg.gate)
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
  /**
   * I push bulk passano da hspi.writeBytes(), cioè da frame di OTTO bit: in
   * modo 3 fili ogni byte vorrebbe nove bit e questa strada scriverebbe
   * spazzatura. La sonda del modo bus è elettrica di proposito e non arriva
   * qui, ma se ci si arrivasse per un'altra strada va detto.
   */
  if (cfg.busMode == BUS_3WIRE)
    logLine("push bulk in modo 3 fili: non esprimibile, piano non scritto");
  setRamWindow(0, 0, SRC, gate);
  writeCommand(planeCommand);

  const uint32_t t0 = millis();
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  csAssert();
  for (int16_t y = 0; y < (int16_t)gate; ++y)
  {
    compose(y, row, gate);
    hspi.writeBytes(row, ROW_BYTES);
  }
  csRelease();
  hspi.endTransaction();
  return millis() - t0;
}
// --- frame ----------------------------------------------------------

/**
 * Le pause di osservazione non stanno più qui: le fa il gate dentro
 * runRefresh(), che chiude il frame sul vetro prima di dipingere il successivo.
 * Vedi chiudiFrame(). Una funzione di pausa a sè sarebbe una seconda strada per
 * fermarsi, quindi un secondo posto da cui dimenticarsi di farlo.
 */

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
  // x = 160 e non 32: la colonna sinistra ospita i box a finestra parziale che
  // questo frame scrive sopra le bande, e le due cose non devono sovrapporsi.
  paintGlyph(row, y, (uint8_t)(band + 1), 160, gy, LABEL_SCALE, bwOn ? 0x00 : 0xFF);
}

static void composeRowBandsRED(int16_t y, uint8_t* row, uint16_t gate)
{
  const uint16_t bandH = gate / 4;
  const uint8_t  band  = bandH ? (uint8_t)(y / bandH) : 0;
  const bool     redOn = (band == 2) || (band == 3);   // bande 3 e 4: accent = 1
  memset(row, redOn ? 0xFF : 0x00, ROW_BYTES);
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
  csAssert();
  for (uint16_t i = 0; i < h; ++i)
    hspi.writeBytes(row, rowBytes);
  csRelease();
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

/**
 * Valore di `numero` che significa "fascia uniforme, senza cifra": serve alle
 * passate ristrette anche in X, dove una cifra non entrerebbe.
 */
static const uint16_t STRIPE_SENZA_NUMERO = 0xFFFF;

/**
 * Scrive una fascia a larghezza piena su un piano: fondo uniforme con la cifra
 * della passata sovraimpressa, per riconoscere a colpo d'occhio quale passata
 * ha dipinto cosa. Riusa paintGlyph, cioè lo stesso font dei righelli del
 * pattern di identificazione. La cifra viene disegnata solo se la fascia è
 * abbastanza alta da contenerla: sotto quell'altezza la fascia esce uniforme e
 * la si riconosce dalla posizione.
 */
static void writeStripeWithDigit(uint8_t planeCommand, uint16_t y, uint16_t h,
                                 uint8_t bg, uint16_t numero, uint8_t fg)
{
  uint8_t row[ROW_BYTES];
  const int32_t glyphH = (int32_t)GLYPH_H * LABEL_SCALE;
  const bool fits = (numero != STRIPE_SENZA_NUMERO) && ((int32_t)h >= glyphH + 8);
  const int32_t gy = fits ? ((int32_t)h - glyphH) / 2 : -glyphH;

  setRamWindow(0, y, SRC, h);
  writeCommand(planeCommand);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  csAssert();
  for (int16_t r = 0; r < (int16_t)h; ++r)
  {
    memset(row, bg, ROW_BYTES);
    if (fits)
      paintNumber(row, r, numero, 32, gy, LABEL_SCALE, fg);
    hspi.writeBytes(row, ROW_BYTES);
  }
  csRelease();
  hspi.endTransaction();
}

// --- refresh ---------------------------------------------------------

/**
 * Attende la fine del refresh riportando le fasi: il BUSY che si rialza entro
 * 800 ms dice che il refresh procede in più passate, e l'attesa fra due fasi
 * NON entra nella durata riportata.
 *
 * I progressivi stanno sotto logDetail: su un refresh da 19 s una riga ogni
 * 2,5 s sono otto righe che dicono "sta ancora andando", e servono solo quando
 * una misura non torna.
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
        logDetail("fase %d in corso da %lu ms", phase,
                  (unsigned long)(millis() - tPhase));
        nextLog += 2500;
      }
      // il BUSY dell'altra coda è un testimone: se si muove, i due
      // controller condividono qualcosa oltre al bus
      if (cfg.secondFfc && digitalRead(PIN_BUSY_OTHER) == BUSY_ACTIVE)
        otherBusyMoved = true;
      delay(1);
    }
    logDetail("fase %d conclusa: BUSY alto per %lu ms", phase,
              (unsigned long)(millis() - tPhase));
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
    logDetail("BUSY risalito dopo %lu ms", (unsigned long)(millis() - tIdle));
  }
  if (phase > 1)
    verdict("refresh multi-fase: %d fasi", phase);
  return (int32_t)(millis() - t0);
}

/**
 * Lancia la display update sequence e ne misura i tempi. Verifica prima di
 * tutto che il BUSY salga: se non sale, il controller non ha preso il comando e
 * niente di quanto segue ha valore.
 *
 * IL PARAMETRO DI 0x22 È UN BITMASK DI STADI, non un enum di sequenze: il
 * datasheet ne dà la tabella (0x80 enable clock, 0xC0 clock + analog, 0x91/0x99
 * load LUT in Display Mode 1/2, 0xB1/0xB9 load temperatura + LUT, 0xC7/0xCF
 * display, 0xF7/0xFF temperatura + LUT + display). I valori 0xCC, 0xFC e 0xF4
 * che le due suite usano NON sono code point documentati: sono costruzioni su
 * quel bitmask, ed è per questo che vale spazzarlo uno stadio per volta.
 *
 * QUI PASSA LA NUMERAZIONE, ed è l'unico posto in cui può stare: `frame` non
 * nullo dice che la passata è una schermata da guardare, e allora la funzione
 * chiude il frame precedente sul gate, prende il numero successivo, lo disegna
 * sul vetro e lo mette in testa alle righe. Con `frame` nullo la passata è solo
 * cronometrata e non consuma nè un numero nè una pausa.
 *
 * Se una passata ha già PRENOTATO il proprio numero con prenotaNumero() — le
 * sonde che scrivono la cifra dentro l'immagine devono farlo, per avere lo
 * stesso numero sulla fascia e nel riquadro — la prenotazione pendente viene
 * usata invece di prenderne una nuova.
 *
 * L'offset degli opcode del riquadro è indipendente da quello in vigore per la
 * master activation: nella sonda dello slave i piani si scrivono a opcode|0x80
 * mentre l'attivazione va al master, e senza questo il riquadro finirebbe sul
 * master sporcando l'esperimento.
 */
static int32_t runRefresh(uint8_t updateSequence = 0xF7,
                          const char* attesa = nullptr,
                          uint32_t timeout_ms = 0,     // 0 = cfg.timeoutMs
                          const Frame* frame = nullptr)
{
  if (timeout_ms == 0) timeout_ms = cfg.timeoutMs;

  /**
   * Il numero si prenota prima, perchè va scritto nel riquadro; il gate scatta
   * subito dopo, quando la RAM è pronta ma il vetro mostra ancora la schermata
   * precedente. Scatta prima di OGNI refresh, anche di una passata non
   * osservabile: anche quella cambia il vetro, quindi cancellerebbe il frame
   * precedente senza che nessuno lo abbia guardato.
   */
  uint8_t numero = numeroInPrenotazione();
  if (frame && !numero) numero = registraFrame(*frame);
  chiudiFrame(frame ? frame->etichetta : nullptr);

  const char* caption = frame ? frame->etichetta : "REFRESH";
  if (attesa) logDetail("atteso: %s", attesa);

  // il riquadro va scritto in RAM prima della master activation, o non si vede
  if (numero && frame && !frame->senzaBadge)
  {
    const uint8_t salvaOffset = cmdOffset;
    cmdOffset = frame->opcodeOffset;
    disegnaBadge(numero, frame->badge);
    cmdOffset = salvaOffset;
    numeroRiquadro = numero;
  }

  writeReg21(2);
  writeCommand(0x22);
  writeData(updateSequence);
  writeCommand(0x20);   // master activation

  const uint32_t tAct = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - tAct) < 1000)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    /**
     * Il frame prenotato non arriva sul vetro, quindi non lo si attiva: le
     * righe che seguono continuano a portare il numero della schermata che sta
     * ancora sul vetro, che è la verità.
     */
    measure(caption, updateSequence, REFRESH_NON_ESEGUITO);
    logLine("BUSY mai salito: il controller non ha preso 0x22 = 0x%02X",
            updateSequence);
    annullaPrenotazione();
    return -1;
  }
  logDetail("BUSY salito dopo %lu ms", (unsigned long)(millis() - tAct));

  const int32_t ms = waitRefresh(timeout_ms);
  attivaFramePrenotato();
  measure(caption, updateSequence, ms);
  if (ms < 0)
    logLine("timeout a %lu ms: refresh non concluso", (unsigned long)timeout_ms);

  /**
   * Ripiego per il riferimento di durata: lo fissa lo sweep del MUX, ma se una
   * sonda viene lanciata da sola dal menu quello sweep può non essere stato
   * eseguito, e senza riferimento metà dei confronti del report resta muta. Un
   * 0xF7 su finestra piena è un refresh pieno per definizione, quindi vale come
   * riferimento; le sequenze ridotte e le passate su finestra no.
   */
  if (refreshMs <= 0 && ms > 0 && updateSequence == 0xF7
      && ramWinW == SRC && ramWinH == cfg.gate)
  {
    refreshMs = ms;
    logDetail("preso come riferimento di durata: %ld ms", (long)refreshMs);
  }

  durataFrame(ms);
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
    measure(label, 0, REFRESH_NON_ESEGUITO);
    logLine("pattern 0x%02X: BUSY non e' salito, comando non accettato", patternCommand);
    return -1;
  }
  const int32_t ms = waitBusy(5000);
  measure(label, 0, ms);
  return ms;
}

/**
 * Prova di vita del controller e stato delle alte tensioni, senza guardare un
 * pixel: è la sezione che conta sulla coda che non stampa. I due rilevatori
 * vogliono CLKEN=1 e ANALOGEN=1, cioè il solo power on (0x22 = 0xC0 + 0x20).
 *
 *   0x14 HV Ready Detection, A = 0x77: cool down 10 ms x 8, 7 cicli, massimo
 *        560 ms. Il datasheet dà la detection per conclusa quando HV è pronta,
 *        quindi un BUSY molto più corto del massimo dice HV arrivata presto e
 *        uno che arriva al massimo dice che non è mai arrivata.
 *   0x15 VCI Detection al POR (2.3 V): qui il datasheet non promette una
 *        conclusione anticipata, e conta solo che il BUSY reagisca.
 *
 * L'esito esplicito sta nei bit 5 e 4 dello status 0x2F, leggibile solo se
 * questa coda porta fuori la linea dati: per questo i registri si leggono
 * subito dopo, con la detection ancora fresca.
// --- waveform LUT: partial e quarto colore ---------------------------
 */

/**
 * Layout della waveform LUT, dal datasheet SSD1677 §6.7 Figure 6-6. Il comando
 * 0x32 scrive i byte 0..104, cioè tutto tranne le tensioni:
 *
 *   byte   0.. 9   LUT0, dieci gruppi da quattro fasi, 2 bit per fase
 *   byte  10..19   LUT1        byte  20..29   LUT2
 *   byte  30..39   LUT3        byte  40..49   LUT4
 *   byte  50..99   dieci gruppi da { TP[nA], TP[nB], TP[nC], TP[nD], RP[n] }
 *   byte 100..104  frame rate
 *   byte 105..109  VGH, VSH1, VSH2, VSL, VCOM: NON scritti da 0x32, arrivano
 *                  da 0x03 / 0x04 / 0x2C e restano quelli dell'OTP.
 *
 * È il motivo per cui queste due sonde sono a rischio contenuto: cambiano la
 * sequenza delle fasi, non le tensioni con cui il film viene pilotato.
 *
 * I 2 bit di VS[nX-LUTm], Table 6-6: 00 = VSS, 01 = VSH1, 10 = VSL, 11 = VSH2.
 * TP[nX] = durata della fase in frame, 0 = fase saltata. RP[n] = ripetizioni
 * del gruppo meno una.
 */
static const uint16_t LUT_BYTES = 105;

/**
 * LUT di partial update del GDEH116T91, copiata da GxEPD2 1.6.9,
 * src/epd/GxEPD2_1160_T91.cpp (GPL-3.0, come questa libreria).
 *
 * Perchè proprio questa: quel pannello è 960x640 su SSD1677, cioè stesso
 * command set e stessi 960 source di questo, e con essa dichiara e ottiene
 * partial_refresh_time = 700 ms contro i 6200 del refresh pieno.
 *
 * Decodificata col layout sopra, e il conto torna:
 *
 *   LUT0, LUT3, LUT4  tutte a zero
 *   LUT1              0D = VSH1, poi 1B 1C 1D = VSL
 *   LUT2              0C 0D = VSL, poi 1A 1B 1C 1D = VSH1
 *   gruppo 0          TP = 0, 0, 5, 5   RP = 0 (una passata)
 *   gruppo 1          TP = 5, 3, 5, 5   RP = 0
 *
 * Su un pannello monocromatico in Mode 2 le due RAM sono (frame precedente,
 * frame nuovo): LUT1 e LUT2 sono le due transizioni, una per verso, e si vede
 * che sono simmetriche. LUT0 e LUT3 a zero sono i pixel che non cambiano, e
 * non venendo pilotati non consumano tempo.
 *
 * La verifica che il layout è interpretato bene: 10 + 18 = 28 frame in tutto,
 * che a un frame rate di ~50 Hz fanno ~560 ms, lo stesso ordine di grandezza
 * dei 700 ms dichiarati con il margine che ci si aspetta.
 */
static const uint8_t LUT_PARTIAL_1160[LUT_BYTES] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0
  0x01, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1
  0x0A, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x03, 0x05, 0x05, 0x00, // TP/RP gruppi 0-1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 2-3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 4-5
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 6-7
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 8-9
  0x22, 0x22, 0x22, 0x22, 0x22                                // frame rate
};

/**
 * La stessa waveform, riassegnata alle LUT che la Table 6-4 usa su un pannello
 * a 3 colori.
 *
 * Serve perchè qui le due RAM non sono (precedente, nuovo) ma (accent,
 * bianco/nero), e l'indice di LUT esce dalla Table 6-4: (0,0) nero = LUT0,
 * (0,1) bianco = LUT1, (1,x) accent = LUT2 e LUT3. La LUT del 1160 lascia LUT0
 * a zero, quindi con essa i pixel neri non verrebbero pilotati affatto: qui
 * LUT0 prende la waveform che nel 1160 stava in LUT2, cioè quella che spinge
 * nel verso opposto a LUT1, e LUT2 / LUT3 restano a zero perchè un frame
 * aggiornato in partial è per forza senza accent.
 *
 * I valori sono gli stessi, spostati di posto: niente è inventato.
 */
static const uint8_t LUT_PARTIAL_T64[LUT_BYTES] =
{
  0x0A, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0 nero
  0x01, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1 bianco
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2 accent
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3 accent
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x03, 0x05, 0x05, 0x00, // TP/RP gruppi 0-1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

/**
 * LUT del probe del quarto colore: separa LUT2 e LUT3 per livello di sorgente.
 *
 * LUT2 pilota a VSH1 (VS = 01), LUT3 a VSH2 (VS = 11), stessi tempi. Nero e
 * bianco restano fermi (LUT0 e LUT1 a zero), così l'unica differenza fra le
 * due metà stampate è quale delle due tensioni positive tocca il film.
 *
 * Su QUESTO pannello è la misura che conta più di tutte. Il codice modello
 * EL122H6W4A ha campo colore 4, cioè BWRY nominale, mentre il vetro dice
 * "Newton PRO 12.2" BWR normal": il frame a bande da solo non scioglie la
 * contraddizione, perchè se il film avesse quattro pigmenti ma l'OTP aliasasse
 * LUT3 su LUT2 — che è esattamente quello che la Table 6-4 descrive — le bande
 * 3 e 4 uscirebbero identiche e si concluderebbe "tre colori" per un difetto
 * della waveform, non del film.
 *
 * Tempi: quattro fasi da 50 frame, gruppo non ripetuto, cioè 200 frame in
 * tutto. Un ordine di grandezza sotto la waveform di produzione: abbastanza
 * per muovere il pigmento, non abbastanza per sovra-pilotarlo.
 */
static const uint8_t LUT_LEVELS[LUT_BYTES] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0 nero, fermo
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1 bianco, fermo
  0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2 = VSH1 x4 fasi
  0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3 = VSH2 x4 fasi
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x32, 0x32, 0x32, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // TP 50x4, RP 1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

/** Varianti della sonda con LUT custom: le due diagonali della matrice. */
static const uint8_t VARIANTI_LUT = 2;
/** Durate del BUSY delle varianti di partial con LUT custom. */
static int32_t lutPartialMs[VARIANTI_LUT] = { -1, -1 };
/** Durata del BUSY del probe dei livelli di sorgente. */
static int32_t levelsMs = -1;

/**
 * Carica una waveform LUT via 0x32 e imposta il border.
 *
 * Per un partial il border va a 0xC0, HiZ, e non al valore di produzione 0x01
 * che aggancia la cornice a LUT1: agganciata, verrebbe pilotata a ogni passata
 * e lampeggerebbe. È quello che fa _Init_Part() del GDEH116T91.
 */
static void loadWaveformLut(const uint8_t* lut, uint8_t border)
{
  /**
   * L'unica leva del test che può fare un danno permanente è alzare VSH1 sopra
   * i 15 V, e la guardia la riporta al POR prima di ogni carico: il menu clampa
   * già, ma una sonda che compone una LUT byte per byte potrebbe arrivarci per
   * un'altra strada.
   */
  if (!waveformGuard())
    logLine("tensioni oltre il POR riportate al POR: VSH1 %02X VSH2 %02X VSL %02X",
            cfg.lutVsh1, cfg.lutVsh2, cfg.lutVsl);
  /**
   * UN WAVEFORM SETTING È DI 110 BYTE E 0x32 NE SCRIVE 105. I cinque che
   * restano sono le TENSIONI, e hanno comandi propri: byte 105 VGH via 0x03,
   * 106..108 VSH1/VSH2/VSL via 0x04, 109 VCOM via 0x2C. Chi scrive solo 0x32
   * eredita le tensioni dall'ultimo load dall'OTP — quelle della waveform di
   * produzione, tarate su un refresh pieno — e la stessa LUT rende un nero
   * VISIBILMENTE PIÙ CHIARO. Sul 9.7" è stato un bug reale, e la diagnosi è
   * venuta dal confronto fra due misure con la stessa LUT: la sonda gira dopo
   * un reset, quindi a POR, e vedeva nero pieno; il driver partiva dopo un
   * refresh, quindi con le tensioni dell'OTP, e vedeva nero pallido.
   *
   * Due strade, e nessuna delle due scrive una tensione FUORI dal POR:
   *
   *   cfg.lutSwreset   uno SWRESET prima del carico. Il datasheet dà lo SWRESET
   *                    per "resets the commands and parameters to their S/W
   *                    Reset default values except R10h" e non tocca la RAM,
   *                    quindi le tensioni tornano ai POR senza scriverle. È la
   *                    strada più sicura, ma un driver a metà vita non può
   *                    permettersela senza rifare tutto l'init.
   *   cfg.lutTensioni  0x04 coi soli POR dopo 0x32, che è quello che il driver
   *                    dovrà fare. VGH e VCOM restano dell'OTP di proposito: il
   *                    primo pilota i transistor e non il pigmento, il secondo
   *                    governa il bilanciamento DC ed è tarato di fabbrica.
   *
   * Se le due rendono lo stesso nero, il driver può usare la seconda.
   */
  if (cfg.lutSwreset)
  {
    writeCommand(0x12);
    const int32_t sw = waitBusy(1000);
    delay(200);
    logDetail("SWRESET prima del carico: tensioni ai POR senza scriverle, BUSY %ld ms",
              (long)sw);
  }

  writeCommand(0x3C);
  writeData(border);
  writeCommand(0x32);
  for (uint16_t i = 0; i < LUT_BYTES; ++i)
    writeData(lut[i]);

  if (cfg.lutTensioni)
  {
    writeCommand(0x04);
    writeData(cfg.lutVsh1);
    writeData(cfg.lutVsh2);
    writeData(cfg.lutVsl);
    logDetail("0x04 = %02X %02X %02X (i POR: VSH1 15 V, VSH2 5 V, VSL -15 V)",
              cfg.lutVsh1, cfg.lutVsh2, cfg.lutVsl);
  }
}

/**
 * Riporta a Normal le due opzioni di contenuto RAM di 0x21.
 *
 * Serve perchè initPanel con CAND_SOLUM riproduce l'init di fabbrica, che
 * scrive 0x21 = 0x08 0x00: A[3:0] = 1000 è **BW Inverse**, cioè il controller
 * legge il complemento del piano 0x24. Con quel registro attivo un byte 0xFF
 * scritto in 0x24 vale BW = 0 e non BW = 1, e la Table 6-4 si applica a
 * rovescio: le due sonde qui sotto ragionano sulle coppie di bit, quindi la
 * prima cosa che fanno è togliere l'inversione. Così l'esito non dipende dalla
 * candidata di init con cui girano, e i due bit sono quelli che la tabella
 * nomina.
 *
 * A[7:4] = opzione Red, A[3:0] = opzione BW: 0000 Normal per entrambe. Il
 * driver in uso normale non scrive mai 0x21 e resta al POR, che è questo.
 */
static void forceRamOptionsNormal()
{
  writeCommand(0x21);
  writeData(0x00);
  writeData(0x00);
}

/** Power on esplicito, come lo fa il GDEH116T91 prima di ogni _Update_Part(). */
static int32_t powerOnExplicit()
{
  writeCommand(0x22);
  writeData(0xC0);
  writeCommand(0x20);
  const int32_t ms = waitBusy(2000);
  powerOnMs = ms;
  return ms;
}

/**
 * Solo power off, 0x22 = 0xC3. La coppia con powerOnExplicit() serve alle sonde
 * elettriche: HV Ready e VCI Detection pretendono CLKEN = 1 e ANALOGEN = 1,
 * cioè il power on, e lasciare l'analogico acceso dopo una misura falserebbe
 * la successiva.
 */
static int32_t powerOffExplicit()
{
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  const int32_t ms = waitBusy(5000);
  powerOffMs = ms;
  return ms;
}

/**
 * Scrive un piano a due clock diversi: la metà alta della banda a cfg.spiBase,
 * la metà bassa a cfg.spiFast. Due finestre RAM, due transazioni.
 *
 * Serve a togliere un frame e una pausa dal test: prima l'integrità del bus al
 * clock alto costava un frame intero da confrontare a memoria con il
 * precedente, e il confronto a memoria fra due schermi visti a un minuto di
 * distanza è la parte più debole di una prova visiva. Qui le due metà stanno
 * sullo stesso schermo, con lo stesso pattern e la stessa candidata: la
 * scaletta diagonale e i righelli attraversano entrambe, quindi un byte
 * spostato nella metà bassa si vede confrontandolo con la metà alta.
 */
static void writePlaneDualClock(uint8_t planeCommand,
                                void (*compose)(int16_t, uint8_t*, uint16_t))
{
  const uint32_t CLOCKS[2] = { cfg.spiBase, cfg.spiFast };
  const uint16_t half = cfg.gate / 2;
  uint8_t row[ROW_BYTES];

  for (uint8_t k = 0; k < 2; ++k)
  {
    const uint16_t y0 = k ? half : 0;
    const uint16_t h  = k ? (uint16_t)(cfg.gate - half) : half;
    spiSettings = SPISettings(CLOCKS[k], MSBFIRST, SPI_MODE0);
    setRamWindow(0, y0, SRC, h);
    writeCommand(planeCommand);
    digitalWrite(PIN_DC, HIGH);
    hspi.beginTransaction(spiSettings);
    csAssert();
    for (uint16_t y = y0; y < y0 + h; ++y)
    {
      compose((int16_t)y, row, cfg.gate);
      hspi.writeBytes(row, ROW_BYTES);
    }
    csRelease();
    hspi.endTransaction();
  }
  spiSettings = SPISettings(cfg.spiBase, MSBFIRST, SPI_MODE0);
}

#endif // DUAL_PANEL_FINDER_CONTROLLER_H
