// =============================================================================
// Controller.h — il vocabolario della suite: SPI diretta verso l'SSD1677.
//
// PERCHE' TUTTO E' RAW. Il driver custom src/GxEPD2_SOLUM_097c_960x672.h è
// l'OGGETTO della misura, non lo strumento: nasce da un driver upstream di
// GxEPD2 e nè la scelta di quella base nè il suo partial refresh sono
// verificati. Se la sonda parlasse al pannello attraverso il driver, ogni
// misura erediterebbe le sue assunzioni. Qui si parla al controller e basta;
// del driver si riproducono le SEQUENZE, comando per comando, e la tabella
// nelle primitive del partial dice quale metodo ciascuna rispecchia.
//
// L'unica cosa che arriva dalla libreria è la costante della waveform del
// partial, GxEPD2_SOLUM_097c_lut_partial: la taratura deve confrontare
// esattamente i byte che andrebbero in produzione, non una copia che può
// divergere. Nessuna istanza del driver viene creata, nessun metodo chiamato.
//
// Hardware: Waveshare E-Paper ESP32 Driver Board V3, che scambia SCK e MOSI
// rispetto al default HSPI. Lo switch n.1 della board NON sceglie il tipo di
// pannello: sceglie la resistenza di sense del booster (A = 3R, B = 0,47R), e
// il wiki mette in A i 13.3", il riferimento più vicino per silicio e
// geometria. Se i colori pieni escono deboli o pieni di ghosting, la posizione
// dello switch va provata prima di dare la colpa alla waveform.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_CONTROLLER_H
#define PANEL_DIAGNOSTIC_CONTROLLER_H

#include <Arduino.h>
#include <SPI.h>

#define SOLUM_PANEL_097C
#include <GxEPD2_SOLUM.h>

#include "Config.h"
#include "Report.h"
#include "Graphics.h"

// ---------------------------------------------------------------------------
// Pin, identici a Layout_097c.h del firmware.
//
// PIN_MISO è un dummy: sul FPC a 24 pin del pannello la linea dati di ritorno
// non esiste (schematico della board: il pin 12 è solo SDI), quindi nessuna
// lettura di registro è attendibile. Le tre letture che la suite fa servono
// solo a dimostrarlo.
//
// Tre pin del controller che il connettore della board gestisce e che invece
// vanno guardati su qualunque coda cablata a mano: BS1 sceglie l'interfaccia
// (L = 4 fili, H = 3 fili a 9 bit) e flottante può far ignorare tutto il
// traffico; M/S# va a VDDIO su un pannello a chip singolo; CL è il clock che
// un master in cascade emette. Vedi docs/fonti_esterne.md §4.
// ---------------------------------------------------------------------------
static const int PIN_CS   = 15;
static const int PIN_DC   = 27;
static const int PIN_RST  = 26;
static const int PIN_BUSY = 25;
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;

/** Sul SSD1677 il BUSY è attivo alto, come il busy_level = HIGH del driver. */
static const int BUSY_ACTIVE = HIGH;

SPIClass hspi(HSPI);
static SPISettings spiSettings(10000000, MSBFIRST, SPI_MODE0);   // riassegnata da cfg.spiHz
/** Clock ridotto per i tentativi di lettura: in lettura il controller è molto
 *  più lento che in scrittura. */
static SPISettings spiReadSettings(2500000, MSBFIRST, SPI_MODE0);

// Contatori cumulativi del bus, alimentati dalle primitive: base del riepilogo.
static uint32_t totalSpiBytes   = 0;
static uint32_t totalSpiMicros  = 0;
static uint32_t totalBusyMillis = 0;
static uint32_t totalCommands   = 0;
static uint32_t totalParamBytes = 0;

// Costi per byte misurati dal benchmark, usati per le stime.
static double bulkUsPerByte = 0.0;   // blocco più grande, limite del bus
static double rowUsPerByte  = 0.0;   // blocchi da 120, la riga immagine del driver

// Stato del controller che le primitive devono ricordare.
static bool     lutCustomLoaded = false;   // in RAM c'è una waveform dell'MCU
static bool     analogOn        = false;   // clock e analog accesi
static uint16_t ramWinX = 0, ramWinY = 0, ramWinW = SRC, ramWinH = GATE;
static uint8_t  ramEntryMode = 0xFF;       // 0xFF = non ancora impostato

// ---------------------------------------------------------------------------
// Primitive del bus. Stesso ordine di operazioni di GxEPD2_EPD::_writeCommand e
// _writeData, D/C riportato alto in coda compreso, così il costo misurato è
// quello che paga il driver.
// ---------------------------------------------------------------------------
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

static void writeData(uint8_t d)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  hspi.transfer(d);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  ++totalParamBytes;
}

/** Comando più n parametri, la forma con cui le tabelle di sequenze scrivono. */
static void writeCommandData(uint8_t c, const uint8_t* data, uint8_t n)
{
  writeCommand(c);
  for (uint8_t i = 0; i < n; ++i) writeData(data[i]);
}

/**
 * Riempie la finestra RAM corrente con un byte costante, a blocchi da 256: è il
 * percorso di _writeScreenBuffer del driver, stessa dimensione di chunk.
 * Ritorna i microsecondi del solo transfer.
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
    const uint32_t chunk = count > sizeof(buf) ? (uint32_t)sizeof(buf) : count;
    hspi.writeBytes(buf, chunk);
    count -= chunk;
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes  += total;
  totalSpiMicros += dt;
  return dt;
}

/** Attende la discesa del BUSY. Ritorna i ms attesi, -1 al timeout. */
static int32_t waitBusy(uint32_t timeoutMs)
{
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
  {
    if ((millis() - t0) > timeoutMs)
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
 * Porta il BUSY a riposo prima di una misura che lo usa come testimone: un
 * BUSY rimasto alto da un comando precedente verrebbe letto come "il comando
 * appena inviato è stato accettato", e si misurerebbe la coda dell'operazione
 * precedente. Ritorna false quando il chiamante NON deve misurare.
 */
static bool ensureBusyLow(const char* where)
{
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE) return true;
  const int32_t ms = waitBusy(3000);
  if (ms < 0)
  {
    logLine("BUSY alto a 3000 ms prima di [%s]: niente da misurare", where);
    return false;
  }
  logDetail("BUSY era alto prima di [%s]: attesi %ld ms", where, (long)ms);
  return true;
}

/** Costo per byte di riferimento: il migliore misurato, o il limite del clock. */
static double usPerByteReference()
{
  if (bulkUsPerByte > 0.0) return bulkUsPerByte;
  return 8.0 * 1000000.0 / (double)cfg.spiHz;
}

/**
 * Impulso di reset. lowMs è il livello basso, e non è un dettaglio: il firmware
 * chiama init(..., 2, false), quindi GxEPD2_EPD::_reset tiene RST alto 10 ms,
 * basso 2 ms e alto altri 10; gli upstream passano 10 e il firmware di fabbrica
 * OEPL sale a 20 con ritentativi. È uno dei campi di InitSequence.
 */
static void resetPanel(uint8_t lowMs)
{
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  digitalWrite(PIN_RST, LOW);
  delay(lowMs);
  digitalWrite(PIN_RST, HIGH);
  delay(lowMs > 10 ? lowMs : 10);
}

/**
 * Finestra RAM e cursore, nei tre versi di scansione che le sequenze usano.
 *
 * 0x11 A[1:0] non ribalta i bit dentro il byte: cambia DOVE atterrano i byte,
 * quindi finestra e cursore vanno coerenti col verso, ed è il motivo per cui
 * stanno nella stessa funzione. Le combinazioni riprodotte sono le tre che si
 * incontrano nei driver reali:
 *   0x03  X++ Y++   driver custom e la maggior parte degli upstream
 *   0x02  X-- Y++   init di fabbrica SOLUM, con finestra X da 959 a 0
 *   0x01  X++ Y--   demo Good Display GDEM102Z91, con finestra Y rovesciata
 * L'entry mode viene riscritto solo quando cambia, così il traffico sul bus
 * resta quello del driver, che lo manda una volta sola in init.
 */
static void setRamWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint8_t entryMode = 0x03)
{
  ramWinX = x; ramWinY = y; ramWinW = w; ramWinH = h;
  if (entryMode != ramEntryMode)
  {
    writeCommand(0x11);
    writeData(entryMode);
    ramEntryMode = entryMode;
  }
  const uint16_t xLow  = x;
  const uint16_t xHigh = (uint16_t)(x + w - 1);
  const uint16_t yLow  = y;
  const uint16_t yHigh = (uint16_t)(y + h - 1);
  // Con X decrescente la finestra parte dall'estremo alto, e così il cursore.
  const bool xDown = (entryMode & 0x01) == 0x00;
  const bool yDown = (entryMode & 0x02) == 0x00;
  const uint16_t xStart = xDown ? xHigh : xLow;
  const uint16_t xEnd   = xDown ? xLow  : xHigh;
  const uint16_t yStart = yDown ? yHigh : yLow;
  const uint16_t yEnd   = yDown ? yLow  : yHigh;

  writeCommand(0x44);
  writeData(xStart % 256); writeData(xStart / 256);
  writeData(xEnd   % 256); writeData(xEnd   / 256);
  writeCommand(0x45);
  writeData(yStart % 256); writeData(yStart / 256);
  writeData(yEnd   % 256); writeData(yEnd   / 256);
  writeCommand(0x4E);
  writeData(xStart % 256); writeData(xStart / 256);
  writeCommand(0x4F);
  writeData(yStart % 256); writeData(yStart / 256);
}

// ---------------------------------------------------------------------------
// Init.
// ---------------------------------------------------------------------------

/**
 * Init del driver custom, riprodotta comando per comando: rispecchia
 * GxEPD2_SOLUM_097c_960x672::_InitDisplay(). È il punto di partenza di ogni
 * sonda, e insieme la prima cosa che le misure devono poter smentire.
 *
 * Il BUSY dopo lo SWRESET viene osservato dentro la finestra di 200 ms che il
 * driver spende comunque a delay: la durata reale del reset interno diventa un
 * dato invece di una stima, e sul bus non cambia niente.
 */
static void initPanel()
{
  const uint32_t tReset = micros();
  resetPanel(2);
  delay(10);
  const uint32_t usReset = micros() - tReset;

  writeCommand(0x12);   // SWRESET
  const uint32_t tSw = millis();
  uint32_t riseMs = 0;
  bool rose = false;
  while ((millis() - tSw) < 50)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { rose = true; riseMs = millis() - tSw; break; }
  }
  int32_t swBusy = -1;
  if (rose) swBusy = waitBusy(1000);
  const uint32_t swElapsed = millis() - tSw;
  if (swElapsed < 200) delay(200 - swElapsed);

  writeCommand(0x0C);   // soft start, Level 2
  writeData(0xAE); writeData(0xC7); writeData(0xC3); writeData(0xC0); writeData(0x80);
  writeCommand(0x01);   // MUX 671 -> 672 gate line
  writeData(0x9F); writeData(0x02); writeData(0x00);
  writeCommand(0x3C);   // border waveform: LUT1, bianco
  writeData(0x01);
  writeCommand(0x18);   // sensore di temperatura interno
  writeData(0x80);
  ramEntryMode = 0xFF;  // l'entry mode lo scrive setRamWindow qui sotto
  setRamWindow(0, 0, SRC, GATE, 0x03);

  lutCustomLoaded = false;   // il SWRESET ha rimesso le LUT dell'OTP
  analogOn = false;
  if (rose)
    logDetail("init: reset %lu us, SWRESET BUSY su dopo %lu ms, alto %ld ms"
              " (il driver attende 200 ms fissi)",
              (unsigned long)usReset, (unsigned long)riseMs, (long)swBusy);
  else
    logDetail("init: reset %lu us, SWRESET BUSY mai salito entro 50 ms",
              (unsigned long)usReset);
}

/** Reset più init del driver custom: l'apertura di ogni sonda. */
static void enterRaw()
{
  initPanel();
}

// ---------------------------------------------------------------------------
// Riempimento dei piani.
// ---------------------------------------------------------------------------

/**
 * Riempie un piano immagine col generatore di pattern del controller: 0x47 per
 * il piano B/N (0x24), 0x46 per l'accent (0x26). Il parametro codifica
 * A[7] = valore del primo step, A[6:4] = 111 step height 680, A[2:0] = 111
 * step width 960, cioè un unico step su tutta la RAM nativa.
 *
 * Rispecchia _fillPlaneByPattern() del driver, e con 0x46 vale anche per le due
 * primitive del frame precedente: (0x46, 0xFF) è writeScreenBufferPrevious(),
 * (0x46, 0x00) è _cleanColorIfPrevious().
 * Ritorna i ms del riempimento, -1 se il comando non è praticabile.
 */
static int32_t fillByPattern(uint8_t patternCommand, uint8_t value)
{
  const uint8_t param = value ? 0xF7 : 0x77;
  ensureBusyLow("pattern");
  setRamWindow(0, 0, SRC, GATE, ramEntryMode == 0xFF ? 0x03 : ramEntryMode);
  writeCommand(patternCommand);
  writeData(param);
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - t0) < 200) delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    logLine("0x%02X param 0x%02X: BUSY mai salito, pattern non supportato",
            patternCommand, param);
    return -1;
  }
  const int32_t ms = waitBusy(2000);
  if (ms < 0)
  {
    logLine("0x%02X param 0x%02X: timeout", patternCommand, param);
    return -1;
  }
  logDetail("pattern 0x%02X = 0x%02X in %ld ms (push SPI equivalente %.0f ms)",
            patternCommand, param, (long)ms,
            usPerByteReference() * PLANE_BYTES / 1000.0);
  return ms;
}

/**
 * Righe di testo precomposte più la riga di fondo. Statiche e non sullo stack:
 * sono 21 x 120 byte, e servono a tenere la composizione FUORI dalla regione
 * cronometrata.
 */
static uint8_t textRows[TEXT_MAX_LINES * FONT_H][ROW_BYTES];
static uint8_t textBgRow[ROW_BYTES];

/**
 * Riempie la finestra RAM corrente riga per riga sovraimprimendo la frase della
 * passata: fondo a bg, pixel del testo a fg. È il percorso di _writeImage del
 * driver, blocchi da ROW_BYTES, quindi misura l'altro dei due modi in cui il
 * driver spinge i dati. Ritorna i microsecondi del solo transfer.
 */
static uint32_t writeRowsWithText(uint8_t bg, uint16_t h, uint8_t fg, const TextLayout& lay)
{
  memset(textBgRow, bg, sizeof(textBgRow));
  for (uint8_t l = 0; l < lay.lines; ++l)
  {
    for (uint8_t fr = 0; fr < FONT_H; ++fr)
    {
      uint8_t* row = textRows[l * FONT_H + fr];
      memset(row, bg, ROW_BYTES);
      for (uint8_t k = 0; k < lay.len[l]; ++k)
      {
        const uint8_t bits = glyphFor(lay.start[l][k])[fr];
        for (uint8_t c = 0; c < FONT_W; ++c)
          if (bits & (0x10 >> c))
            setSpan(row, (uint16_t)(TEXT_MARGIN_X + ((uint16_t)k * FONT_PITCH + c) * lay.scale),
                    lay.scale, fg);
      }
    }
  }

  const int16_t lineH = (int16_t)FONT_H * lay.scale;
  const int16_t pitch = lineH + (int16_t)TEXT_LINE_GAP * lay.scale;

  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  const uint32_t t0 = micros();
  for (uint16_t r = 0; r < h; ++r)
  {
    const uint8_t* src = textBgRow;
    const int16_t dy = (int16_t)r - lay.top;
    if (dy >= 0)
    {
      const int16_t line   = dy / pitch;
      const int16_t within = dy % pitch;
      if (line < (int16_t)lay.lines && within < lineH)
        src = textRows[line * FONT_H + within / lay.scale];
    }
    hspi.writeBytes(src, ROW_BYTES);
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes  += (uint32_t)ROW_BYTES * h;
  totalSpiMicros += dt;
  return dt;
}

/**
 * Fascia a larghezza piena su un piano, con valore costante e frase opzionale.
 * La frase è la stessa stringa che finisce nel log: chi legge il pannello
 * ritrova la riga della passata, e viceversa. Le due chiamate per banda, una
 * per piano, devono passare lo stesso testo, perchè sono i due piani insieme a
 * determinare il colore dei pixel della frase.
 */
static void fillBand(uint8_t plane, uint16_t y, uint16_t h, uint8_t value,
                     const char* text = nullptr, uint8_t fg = 0x00)
{
  const uint32_t bytes = (uint32_t)ROW_BYTES * h;
  const TextLayout lay = layoutText(text, h);
  const bool withText = lay.scale > 0;
  setRamWindow(0, y, SRC, h, ramEntryMode == 0xFF ? 0x03 : ramEntryMode);
  writeCommand(plane);
  const uint32_t us = withText ? writeRowsWithText(value, h, fg, lay)
                               : writeConst(value, bytes);
  logDetail("0x%02X y=%u..%u val=%02X  %lu B  %lu us  %.2f us/B",
            plane, (unsigned)y, (unsigned)(y + h - 1), value,
            (unsigned long)bytes, (unsigned long)us, (double)us / (double)bytes);
}

/**
 * Rettangolo di un piano con valore costante, finestra ristretta anche lungo X.
 * x e w devono essere multipli di 8: sull'asse source la finestra lavora per
 * byte. Ritorna i microsecondi del solo transfer.
 */
static uint32_t fillPlaneRect(uint8_t plane, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h, uint8_t value)
{
  setRamWindow(x, y, w, h, ramEntryMode == 0xFF ? 0x03 : ramEntryMode);
  writeCommand(plane);
  return writeConst(value, (uint32_t)(w / 8) * h);
}

/**
 * Scrive una bitmap del chiamante dentro un rettangolo di un piano: rispecchia
 * _writeImage() del driver, righe da w/8 byte. invert serve al piano accent
 * scritto da una bitmap in convenzione "bit = 1 non è quel colore", che è
 * quello che fa writeImageRed(); il frame precedente del partial invece si
 * scrive SENZA invert, perchè lì 0x26 porta il piano B/N.
 */
static void writePlaneRect(uint8_t plane, const uint8_t* bitmap,
                           uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool invert)
{
  const uint16_t wb = w / 8;
  setRamWindow(x, y, w, h, ramEntryMode == 0xFF ? 0x03 : ramEntryMode);
  writeCommand(plane);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  const uint32_t t0 = micros();
  uint8_t rowBuf[ROW_BYTES];
  for (uint16_t r = 0; r < h; ++r)
  {
    const uint8_t* src = bitmap + (uint32_t)r * wb;
    if (invert)
    {
      for (uint16_t i = 0; i < wb; ++i) rowBuf[i] = (uint8_t)~src[i];
      hspi.writeBytes(rowBuf, wb);
    }
    else hspi.writeBytes((uint8_t*)src, wb);
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes  += (uint32_t)wb * h;
  totalSpiMicros += dt;
}

// ---------------------------------------------------------------------------
// Riquadro del numero di schermata.
// ---------------------------------------------------------------------------
static const uint8_t BADGE_MARGIN  = 8;    // distanza dal bordo della finestra
static const uint8_t BADGE_SCALE_MAX = 6;  // cifre 30x42 px
static const uint8_t BADGE_SCALE_MIN = 2;  // cifre 10x14 px, il minimo leggibile
static const uint8_t BADGE_MAX_H     = FONT_H * BADGE_SCALE_MAX + 2 * (BOX_BORDER + 2);
static const uint8_t BADGE_MAX_BYTES = 24;

static uint8_t badgeRows[BADGE_MAX_H][BADGE_MAX_BYTES];

/** Spinge il riquadro su un piano. Non alimenta i contatori del bus: è
 *  strumentazione del test, non lavoro che il driver pagherebbe. */
static void pushBadgePlane(uint8_t plane, uint16_t bx, uint16_t by,
                           uint8_t bw, uint8_t bh, bool digits, bool invert)
{
  static const uint8_t zeros[BADGE_MAX_BYTES] = { 0 };
  const uint8_t nbyte = (uint8_t)(bw / 8);
  uint8_t rowBuf[BADGE_MAX_BYTES];
  setRamWindow(bx, by, bw, bh, 0x03);
  writeCommand(plane);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t r = 0; r < bh; ++r)
  {
    const uint8_t* src = digits ? badgeRows[r] : zeros;
    if (invert)
    {
      for (uint8_t i = 0; i < nbyte; ++i) rowBuf[i] = (uint8_t)~src[i];
      hspi.writeBytes(rowBuf, nbyte);
    }
    else hspi.writeBytes((uint8_t*)src, nbyte);
  }
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Disegna il numero della schermata in alto a destra della finestra RAM
 * corrente: cifre nere su fondo bianco con cornice, accent spento sotto tutto
 * il riquadro, così si legge sopra qualunque colore la passata dipinga. Stando
 * dentro la finestra, il refresh lo ridipinge anche quando è ristretta, ed è
 * per questo che il numero compare in alto a destra dell'AREA ridipinta e non
 * sempre nello stesso posto.
 *
 * Il riquadro si scrive sempre con entry mode 0x03 e viene pre-invertito quando
 * la sequenza in prova mette 0x21 in BW inverse: così resta leggibile qualunque
 * cosa faccia il resto della schermata. Finestra ed entry mode del chiamante
 * vengono salvati e rimessi: è la finestra presente alla master activation a
 * definire l'area che il refresh percorre.
 */
static void drawBadge(uint16_t number, bool invertBW = false)
{
  char text[4];
  const int len = snprintf(text, sizeof(text), "%u", (unsigned)(number % 1000));

  uint8_t scale = 0, bw = 0, bh = 0;
  for (uint8_t sc = BADGE_SCALE_MAX; sc >= BADGE_SCALE_MIN; --sc)
  {
    const uint16_t textW = (uint16_t)(len * FONT_PITCH - 1) * sc;
    const uint16_t wide  = (uint16_t)((textW + 2 * (BOX_BORDER + 2) + 7) & ~7);
    const uint16_t high  = (uint16_t)(FONT_H * sc + 2 * (BOX_BORDER + 2));
    if (wide + BADGE_MARGIN > ramWinW) continue;
    if (high > ramWinH) continue;
    if (wide / 8 > BADGE_MAX_BYTES || high > BADGE_MAX_H) continue;
    scale = sc; bw = (uint8_t)wide; bh = (uint8_t)high;
    break;
  }
  if (scale == 0)
  {
    logDetail("finestra %ux%u troppo piccola per il riquadro %u",
              (unsigned)ramWinW, (unsigned)ramWinH, (unsigned)number);
    return;
  }

  const Bitmap1bpp box = { &badgeRows[0][0], BADGE_MAX_BYTES, (int16_t)bw, (int16_t)bh };
  for (uint8_t r = 0; r < bh; ++r) memset(badgeRows[r], 0xFF, BADGE_MAX_BYTES);
  drawNumberBox(box, 0, 0, (int16_t)bw, (int16_t)bh, number);

  uint16_t bx = (uint16_t)((ramWinX + ramWinW - bw - BADGE_MARGIN) & ~7);
  if (bx < ramWinX) bx = ramWinX;
  const uint16_t slack = (uint16_t)(ramWinH - bh);
  const uint16_t by = (uint16_t)(ramWinY + (slack > 8 ? 8 : slack));

  const uint16_t sx = ramWinX, sy = ramWinY, sw = ramWinW, sh = ramWinH;
  const uint8_t  sm = ramEntryMode;
  pushBadgePlane(0x24, bx, by, bw, bh, true,  invertBW);
  pushBadgePlane(0x26, bx, by, bw, bh, false, false);
  setRamWindow(sx, sy, sw, sh, sm);
  screenOnGlass = number;
}

// ---------------------------------------------------------------------------
// Refresh.
// ---------------------------------------------------------------------------

/**
 * Attende la fine del refresh e rileva eventuali passate successive: un
 * pannello multi-fase riabbassa e rialza il BUSY, e saperlo cambia il timeout
 * che il driver deve tenere.
 *
 * Ritorna i ms dalla master activation all'ULTIMA discesa del BUSY. Gli 800 ms
 * della finestra con cui si guarda se il BUSY risale NON ci sono dentro: sono
 * tempo di misura e non di refresh. Le pause fra una fase e l'altra invece sì,
 * perchè quelle sono refresh. -1 al timeout.
 */
static int32_t waitRefresh(uint32_t timeoutMs)
{
  const uint32_t t0 = millis();
  int phase = 0;
  uint32_t tFall = t0;
  bool timeout = false;
  while (true)
  {
    ++phase;
    const uint32_t tPhase = millis();
    while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      if ((millis() - t0) > timeoutMs) { timeout = true; break; }
      delay(1);
    }
    if (timeout) break;
    tFall = millis();
    const uint32_t tIdle = millis();
    bool again = false;
    while ((millis() - tIdle) < 800)
    {
      if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { again = true; break; }
      delay(1);
    }
    if (!again) break;
    logDetail("fase chiusa a %lu ms, il BUSY è risalito",
              (unsigned long)(tFall - tPhase));
  }
  if (timeout)
  {
    totalBusyMillis += (millis() - t0);
    return -1;
  }
  const uint32_t dt = tFall - t0;
  totalBusyMillis += dt;
  if (phase > 1) logDetail("refresh in %d fasi", phase);
  return (int32_t)dt;
}

/**
 * Esegue una passata di refresh e la misura. caption è la stessa stringa che la
 * passata ha scritto sul vetro, ed è l'etichetta con cui la misura compare nel
 * registro: di ogni passata esiste un solo nome.
 *
 * Se una schermata è aperta, il suo numero viene disegnato in RAM prima della
 * master activation, o non si vedrebbe.
 * Ritorna i ms del BUSY, -1 se la passata non si è conclusa, REFRESH_NOT_RUN se
 * non è stata nemmeno avviata perchè il controller era occupato.
 */
static int32_t runRefresh(uint8_t updateSequence, const char* caption,
                          uint32_t timeoutMs = 0, bool invertBadge = false)
{
  if (timeoutMs == 0) timeoutMs = cfg.timeoutMs;
  if (!ensureBusyLow(caption))
  {
    measure(caption, updateSequence, REFRESH_NOT_RUN);
    return REFRESH_NOT_RUN;
  }
  if (screenOpen) drawBadge(screenOpen, invertBadge);

  writeCommand(0x22);
  writeData(updateSequence);
  writeCommand(0x20);   // master activation

  const uint32_t tAct = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - tAct) < 1000) delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    logLine("BUSY mai salito: il controller non ha preso 0x22 = 0x%02X", updateSequence);
    measure(caption, updateSequence, -1);
    return -1;
  }
  logDetail("BUSY su dopo %lu ms", (unsigned long)(millis() - tAct));
  const int32_t ms = waitRefresh(timeoutMs);
  if (ms < 0) logLine("TIMEOUT a %lu ms, refresh non concluso", (unsigned long)timeoutMs);
  measure(caption, updateSequence, ms);
  // Le sequenze con i bit 1 e 0 spengono analog e clock alla fine; le altre no.
  if ((updateSequence & 0x03) == 0x03) analogOn = false;
  else if (updateSequence & 0x40)      analogOn = true;
  return ms;
}

// ---------------------------------------------------------------------------
// Alimentazione, nelle tre forme che i driver usano.
// ---------------------------------------------------------------------------

/** Power on esplicito: rispecchia _PowerOn() del driver, 0x22 = 0xC0. */
static int32_t partialPowerOn()
{
  if (analogOn) return 0;
  writeCommand(0x22);
  writeData(0xC0);
  writeCommand(0x20);
  const int32_t ms = waitBusy(2000);
  analogOn = true;
  return ms;
}

/**
 * Power off: rispecchia _PowerOff() del driver con 0xC3. Il parametro esiste
 * perchè gli altri driver SSD1677 di GxEPD2 usano 0x83 (1160_T91, GDEM133T91,
 * GDEQ0426T82) e il community SDK Xteink usa 0x03, e quale dei tre il pannello
 * gradisca è una misura, non una scelta di stile.
 */
static int32_t powerOffPanel(uint8_t sequence = 0xC3)
{
  writeCommand(0x22);
  writeData(sequence);
  writeCommand(0x20);
  const int32_t ms = waitBusy(2000);
  analogOn = false;
  return ms;
}

/** HV Ready Detection: 0x14 alza il BUSY per la durata della detection e la
 *  chiude quando le alte tensioni sono pronte, quindi la durata È l'esito anche
 *  senza la linea di lettura. Richiede clock e analog accesi. */
static int32_t hvReady()
{
  writeCommand(0x14);
  writeData(0x00);
  return waitBusy(3000);
}

/** VCI Detection: come sopra, ma il datasheet non promette una conclusione
 *  anticipata, quindi qui conta solo che il blocco analogico reagisca. */
static int32_t vciDetect()
{
  writeCommand(0x15);
  writeData(0x04);   // POR: soglia 2,3 V
  return waitBusy(3000);
}

// ---------------------------------------------------------------------------
// Waveform.
//
// Un waveform setting del SSD1677 è di 110 byte e 0x32 ne scrive 105:
//   0..49    VS, dieci byte per LUT0..LUT4, quattro fasi da 2 bit
//   50..99   dieci gruppi da { TP[nA..nD], RP[n] }
//   100..104 frame rate, dieci nibble
//   105      VGH                                           0x03
//   106..108 VSH1, VSH2, VSL                                0x04
//   109      VCOM                                           0x2C
// Chi scrive solo 0x32 eredita le tensioni dell'ultimo load dall'OTP, ed è la
// differenza fra un nero pieno e un nero pallido: le due strade restano
// separate di proposito, perchè la differenza fra loro è una misura.
// ---------------------------------------------------------------------------
static const uint16_t WAVEFORM_BYTES = 110;
static_assert(WAVEFORM_BYTES == GxEPD2_SOLUM_097c_960x672::LUT_PARTIAL_BYTES,
              "il layout del waveform setting deve restare quello del driver");

static uint8_t waveform[WAVEFORM_BYTES];

/** Un byte di VS con tutte e quattro le fasi allo stesso livello. */
static inline uint8_t vsAllPhases(uint8_t level)
{
  return (uint8_t)((level << 6) | (level << 4) | (level << 2) | level);
}

/**
 * Programma un gruppo: le tensioni di LUT1 e LUT2 e la lunghezza delle sue
 * quattro fasi. I frame si dividono in parti uguali e il resto va sulla fase D.
 * TP è a 8 bit, quindi un gruppo esprime al massimo 4 x 255 frame: oltre, la
 * richiesta viene tagliata e waveformFrames() riporta il valore vero.
 */
static void setWaveformGroup(uint8_t g, uint8_t vsLut1, uint8_t vsLut2, uint16_t frames)
{
  if (frames == 0 || g > 9) return;
  waveform[10 + g] = vsAllPhases(vsLut1);   // LUT1, nero -> bianco
  waveform[20 + g] = vsAllPhases(vsLut2);   // LUT2, bianco -> nero
  uint16_t per  = frames / 4;
  uint16_t last = frames - per * 3;
  if (per > 255)  per = 255;
  if (last > 255) last = 255;
  uint8_t* tp = &waveform[50 + 5 * g];
  tp[0] = (uint8_t)per; tp[1] = (uint8_t)per; tp[2] = (uint8_t)per; tp[3] = (uint8_t)last;
  tp[4] = 0;                                // RP: gruppo non ripetuto
}

/** Frame totali che la waveform corrente fa scorrere, per prevedere la durata. */
static uint16_t waveformFrames()
{
  uint16_t n = 0;
  for (uint8_t g = 0; g < 10; ++g)
  {
    const uint8_t* tp = &waveform[50 + 5 * g];
    const uint16_t group = (uint16_t)tp[0] + tp[1] + tp[2] + tp[3];
    n += group * ((uint16_t)tp[4] + 1);
  }
  return n;
}

/**
 * Guardia di sicurezza sul film, e non è prudenza generica: su questo pannello
 * VSH2 è la tensione del pigmento ROSSO (misurato dal probe dei livelli di
 * sorgente), VSH1 sopra il POR di 15 V è l'unica leva che può danneggiare il
 * film in modo permanente, e il VCOM è tarato di fabbrica. allowVsh2 lo alza
 * solo il probe dei livelli, che esiste apposta per separare le due tensioni.
 * Ritorna false quando la waveform non va mandata al controller.
 *
 * QUELLO CHE LA GUARDIA NON GARANTISCE: rifiutare il code point 11 evita di
 * COMANDARE il rosso, non di disturbarlo. La lut_full del 370, che quel code
 * point non lo contiene, ha comunque sbiadito la fascia rossa in modo
 * cumulativo fra due passate. Chi prova una waveform nuova deve mettere in
 * conto un accent più debole, e recuperarlo con un refresh pieno.
 */
static bool waveformGuard(bool allowVsh2 = false)
{
  if (!allowVsh2)
  {
    for (uint8_t i = 0; i < 50; ++i)
      for (uint8_t shift = 0; shift < 8; shift += 2)
        if (((waveform[i] >> shift) & 0x03) == VS_VSH2)
        {
          logLine("waveform RIFIUTATA: contiene VSH2, che su questo film è il ROSSO");
          return false;
        }
  }
  if (waveform[106] > VSH1_15V)
  {
    logLine("waveform RIFIUTATA: VSH1 0x%02X oltre il POR 0x41", waveform[106]);
    return false;
  }
  if (waveform[105] != VGH_POR)
  {
    logLine("waveform RIFIUTATA: VGH 0x%02X diverso dal POR", waveform[105]);
    return false;
  }
  if (waveform[109] > VCOM_17V)
  {
    logLine("waveform RIFIUTATA: VCOM 0x%02X oltre -1,7 V", waveform[109]);
    return false;
  }
  return true;
}

/**
 * Copia in waveform[] la LUT del partial del driver custom, che è l'unica cosa
 * che questa suite prende dalla libreria. Con dual la LUT1 diventa il duale
 * esatto di LUT2 (due byte di differenza, durata invariata): serve alla fase
 * del bianco della taratura.
 */
static void copyDriverWaveform(bool dual)
{
  memcpy_P(waveform, GxEPD2_SOLUM_097c_lut_partial, WAVEFORM_BYTES);
  if (dual)
  {
    waveform[10] = vsAllPhases(VS_VSH1);   // gruppo 0: reset verso il nero
    waveform[11] = vsAllPhases(VS_VSL);    // gruppo 1: drive verso il bianco
  }
}

/**
 * Compone in waveform[] la LUT descritta dalla spec. LUT0, LUT3 e LUT4 restano
 * a zero: sono la confinatura del partial, cioè i pixel il cui bit non cambia
 * fra le due RAM, e non si toccano.
 *
 * Con cycles > 1 le alternanze occupano i gruppi 0..2k-1, reset nei pari e
 * drive nei dispari, e i frame si dividono fra i cicli: a frame totali costanti
 * la durata non cambia, e serve a distinguere "serve più carica" da "il
 * pigmento va attivato".
 *
 * Le sorgenti non composte portano i byte 0..104 dalla loro tabella e prendono
 * le TENSIONI dalla spec: così lo stesso sweep di VSH1 si può fare su qualunque
 * LUT, che è ciò che rende inutile una ricompilazione per un tentativo nuovo.
 */
static bool buildWaveform(const WaveformSpec& spec, const uint8_t* table105 = nullptr)
{
  switch (spec.source)
  {
    case WS_DRIVER:      copyDriverWaveform(false); break;
    case WS_DRIVER_DUAL: copyDriverWaveform(true);  break;
    case WS_FULL_370:
      if (!table105) return false;
      memcpy_P(waveform, table105, 105);
      memset(waveform + 105, 0, 5);
      break;
    case WS_COMPOSER:
    default:
    {
      memset(waveform, 0x00, sizeof(waveform));
      uint8_t cycles = spec.cycles;
      if (cycles < 1) cycles = 1;
      if (cycles > 5) cycles = 5;
      for (uint8_t c = 0; c < cycles; ++c)
      {
        // Reset: polarità opposta al drive, per sbloccare il pigmento.
        setWaveformGroup((uint8_t)(2 * c),     VS_VSH1, VS_VSL,  spec.resetFrames / cycles);
        // Drive: LUT1 verso il bianco a VSL, LUT2 verso il nero a VSH1.
        setWaveformGroup((uint8_t)(2 * c + 1), VS_VSL,  VS_VSH1, spec.driveFrames / cycles);
      }
      for (uint8_t i = 100; i < 105; ++i)
        waveform[i] = (uint8_t)((spec.frCode << 4) | spec.frCode);
      break;
    }
  }
  waveform[105] = spec.vgh;
  waveform[106] = spec.vsh1;
  waveform[107] = spec.vsh2;
  waveform[108] = spec.vsl;
  waveform[109] = spec.writeVcom ? spec.vcom : 0x00;
  return true;
}

/**
 * Manda al controller la waveform in waveform[]: border, i 105 byte di 0x32 e
 * le tensioni richieste. Rispecchia _Init_Part() del driver quando si chiama
 * con sourceVoltages = true e gli altri due a false, che è la sua sequenza
 * esatta: 0x3C = 0xC0, 0x32, 0x04. VGH e VCOM il driver non li manda di
 * proposito, e le due varianti servono a misurare se ha ragione.
 */
static bool applyWaveform(uint8_t border, bool sourceVoltages,
                          bool gateVoltage, bool vcomVoltage, bool allowVsh2 = false)
{
  if (!waveformGuard(allowVsh2)) return false;
  writeCommand(0x3C);
  writeData(border);
  writeCommand(0x32);
  for (uint16_t i = 0; i < 105; ++i) writeData(waveform[i]);
  if (gateVoltage)   { writeCommand(0x03); writeData(waveform[105]); }
  if (sourceVoltages)
  {
    writeCommand(0x04);
    writeData(waveform[106]); writeData(waveform[107]); writeData(waveform[108]);
  }
  if (vcomVoltage)   { writeCommand(0x2C); writeData(waveform[109]); }
  lutCustomLoaded = true;
  logDetail("waveform caricata: %u frame, border 0x%02X, VSH1 0x%02X",
            (unsigned)waveformFrames(), border, waveform[106]);
  return true;
}

/** La sequenza esatta di _Init_Part(): border HiZ, 0x32 e le sole tensioni di
 *  sorgente. La waveform deve già stare in waveform[]. */
static bool partialInit()
{
  return applyWaveform(0xC0, true, false, false);
}

/**
 * Cambia waveform a sessione aperta: rispecchia setPartialLut() del driver.
 * Il power off non è opzionale — senza, le tensioni nuove arrivano ad analog
 * già acceso e la passata girerebbe con quelle di prima.
 */
static bool setWaveform(const WaveformSpec& spec, const uint8_t* table105 = nullptr)
{
  if (analogOn) powerOffPanel(0xC3);
  if (!buildWaveform(spec, table105)) return false;
  // Border HiZ e nessun VCOM sono esattamente il contratto di _Init_Part():
  // in quel caso passa dalla primitiva che lo rispecchia, così la sequenza del
  // driver vive in un posto solo e non può divergere da quella generica.
  if (spec.border == 0xC0 && !spec.writeVcom) return partialInit();
  return applyWaveform(spec.border, true, false, spec.writeVcom);
}

/** Passata di partial: rispecchia _Update_Part(), 0x22 = 0xCC, Mode 2 col bit 4
 *  spento perchè l'OTP non sovrascriva la LUT custom. */
static int32_t partialUpdate(const char* caption)
{
  partialPowerOn();
  setRamWindow(0, 0, SRC, GATE, 0x03);
  return runRefresh(0xCC, caption, 5000);
}

/**
 * Il ciclo completo su una bitmap B/N: rispecchia drawImagePartial(). Scrive il
 * frame nuovo in 0x24, fa la passata e ricopia la stessa bitmap in 0x26, così
 * il frame precedente resta allineato al vetro e il partial successivo non ha
 * bisogno di preparazione.
 */
static int32_t drawPartial(const uint8_t* bitmap, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, const char* caption)
{
  writePlaneRect(0x24, bitmap, x, y, w, h, false);
  const int32_t ms = partialUpdate(caption);
  writePlaneRect(0x26, bitmap, x, y, w, h, false);
  return ms;
}

/**
 * Refresh pieno: rispecchia _Update_Full(). Se in RAM c'è una waveform
 * dell'MCU rimette il border di produzione, mentre la LUT e le tensioni le
 * ricarica dall'OTP il bit 4 di 0xF7.
 */
static int32_t fullRefresh(const char* caption, uint32_t timeoutMs = 0)
{
  if (lutCustomLoaded)
  {
    writeCommand(0x3C);
    writeData(0x01);
    lutCustomLoaded = false;
  }
  setRamWindow(0, 0, SRC, GATE, ramEntryMode == 0xFF ? 0x03 : ramEntryMode);
  return runRefresh(0xF7, caption, timeoutMs);
}

// ---------------------------------------------------------------------------
// Runner di una sequenza di init: un driver di partenza in una riga di tabella.
//
// Esegue i campi di InitSequence nell'ordine in cui i driver reali li mandano.
// Lo usano la sonda delle sequenze di partenza, la variante editabile dal menu
// e la passata libera: da qui il fatto che provare l'init di un altro driver
// non costa una ricompilazione.
// ---------------------------------------------------------------------------
static void runInitSequence(const InitSequence& s)
{
  // I registri indipendenti (finestra, 0x3C, 0x18, 0xB1) qui hanno UN ordine
  // solo, mentre i sorgenti riprodotti li mettono in punti diversi: nessuno di
  // loro dipende dagli altri, e l'unico vincolo vero — la finestra prima di
  // scrivere la RAM — è rispettato. La riproduzione è quindi fedele nei valori
  // e nei comandi inviati, non nell'ordine relativo di quei quattro.
  resetPanel(s.resetLowMs);
  delay(10);

  writeCommand(0x12);   // SWRESET
  if (s.swresetOnBusy)
  {
    const uint32_t t0 = millis();
    while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - t0) < 50) delay(1);
    waitBusy(1000);
    delay(10);
  }
  else delay(s.swresetMs);

  if (s.tempFirst) { writeCommand(0x18); writeData(0x80); }

  if (s.patternInInit)
  {
    writeCommand(0x46); writeData(0xF7); delay(15);
    writeCommand(0x47); writeData(0xF7); delay(15);
  }

  writeCommand(0x0C);
  writeData(0xAE); writeData(0xC7); writeData(0xC3); writeData(0xC0); writeData(s.softStart5);

  // Il campo è il valore del registro, non il conteggio: 671 programma 672 gate
  // line. Scriverlo così tiene la riga di tabella identica al sorgente da cui
  // viene, che è l'unico modo di riprodurre una sequenza senza reinterpretarla.
  writeCommand(0x01);
  writeData((uint8_t)(s.muxRegister & 0xFF));
  writeData((uint8_t)((s.muxRegister >> 8) & 0x03));
  writeData(s.muxB);

  writeCommand(0x3C); writeData(0x01);
  if (!s.tempFirst) { writeCommand(0x18); writeData(0x80); }

  ramEntryMode = 0xFF;
  setRamWindow(0, 0, SRC, (uint16_t)(s.muxRegister + 1), s.entryMode);

  if (s.loadB1)
  {
    writeCommand(0x22); writeData(0xB1);
    writeCommand(0x20);
    const int32_t ms = waitBusy(5000);
    logDetail("0x22 = 0xB1 (carico temperatura e waveform): %ld ms", (long)ms);
  }
  if (s.armF7) { writeCommand(0x22); writeData(0xF7); }   // come da fabbrica, senza 0x20
  if (s.ctrl21When == 1) writeCommandData(0x21, s.ctrl21, 2);

  lutCustomLoaded = false;
  analogOn = false;
}

// ---------------------------------------------------------------------------
// Registri in lettura. Sul connettore della board la linea dati di ritorno non
// è cablata, quindi servono a dimostrarlo, non a leggere: 0x2F ha POR 0x01 con
// chip ID 01 ed è l'unico registro con un valore atteso noto.
// ---------------------------------------------------------------------------
static void readRegister(uint8_t cmd, uint8_t* out, uint8_t n)
{
  writeCommand(cmd);
  hspi.beginTransaction(spiReadSettings);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t i = 0; i < n; ++i) out[i] = hspi.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/** Ritorna true se il percorso di lettura è credibile. */
static bool readStatus()
{
  uint8_t st = 0;
  readRegister(0x2F, &st, 1);
  const bool credible = (st != 0x00 && st != 0xFF && (st & 0x03) == 0x01);
  logLine("0x2F status 0x%02X: %s", st,
          credible ? "chip ID atteso, la linea di lettura è cablata"
                   : "chip ID non atteso, letture da ignorare");
  return credible;
}

static void readUserId()
{
  uint8_t id[10];
  memset(id, 0, sizeof(id));
  readRegister(0x2E, id, sizeof(id));
  logLine("0x2E user ID %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
          id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9]);
}

static void readTemperature(const char* when)
{
  uint8_t raw[2] = { 0, 0 };
  readRegister(0x1B, raw, 2);
  const uint16_t reg = ((uint16_t)raw[0] << 8) | raw[1];
  int16_t value = (int16_t)(reg >> 4);
  if (value & 0x0800) value -= 4096;
  if (reg == 0x0000 || reg == 0xFFFF)
    logLine("0x1B temperatura %s: raw 0x%04X, non attendibile", when, reg);
  else
    logLine("0x1B temperatura %s: %.1f gradi", when, (double)value / 16.0);
}

/**
 * Costo del bus, separando l'overhead fisso per transazione dal costo per byte.
 * I due blocchi che contano per il driver sono 120 byte, la riga di
 * _writeImage, e 256, il chunk di _writeScreenBuffer; 4096 dice dove la curva
 * si appiattisce. Lavora sul piano accent dentro una finestra di una sola gate
 * row, e il piano viene comunque riscritto subito dopo.
 */
static void benchmarkBus()
{
  static const uint16_t chunkSizes[] = { 120, 256, 4096 };
  static const uint32_t BENCH_BYTES = 8192;
  static uint8_t buf[4096];
  memset(buf, 0x00, sizeof(buf));

  const uint32_t cmdSnapshot   = totalCommands;
  const uint32_t paramSnapshot = totalParamBytes;
  const double theoretical = (double)cfg.spiHz / 8.0 / 1000000.0;

  for (uint8_t i = 0; i < sizeof(chunkSizes) / sizeof(chunkSizes[0]); ++i)
  {
    const uint16_t chunk = chunkSizes[i];
    const uint32_t calls = BENCH_BYTES / chunk;
    const uint32_t written = calls * chunk;
    setRamWindow(0, 0, SRC, 1, 0x03);
    writeCommand(0x26);
    digitalWrite(PIN_DC, HIGH);
    hspi.beginTransaction(spiSettings);
    digitalWrite(PIN_CS, LOW);
    const uint32_t t0 = micros();
    for (uint32_t n = 0; n < calls; ++n) hspi.writeBytes(buf, chunk);
    const uint32_t dt = micros() - t0;
    digitalWrite(PIN_CS, HIGH);
    hspi.endTransaction();

    const double upb = (double)dt / (double)written;
    logLine("bus blocco %4u B: %.3f us/B, %.0f%% del limite del clock",
            (unsigned)chunk, upb, (double)written / (double)dt / theoretical * 100.0);
    if (chunk == 120) rowUsPerByte = upb;
    if (upb > 0.0 && (bulkUsPerByte == 0.0 || upb < bulkUsPerByte)) bulkUsPerByte = upb;
  }

  // Via di writeCommand e writeData, quella che il driver evita nei hot path.
  const uint32_t SINGLE = 512;
  setRamWindow(0, 0, SRC, 1, 0x03);
  writeCommand(0x26);
  const uint32_t tData = micros();
  for (uint32_t n = 0; n < SINGLE; ++n) writeData(0x00);
  const uint32_t usData = micros() - tData;
  // 0x7F è NOP: comando vuoto, l'unica ripetizione che non altera lo stato.
  const uint32_t tCmd = micros();
  for (uint32_t n = 0; n < SINGLE; ++n) writeCommand(0x7F);
  const uint32_t usCmd = micros() - tCmd;
  logLine("bus writeData %.2f us/B, writeCommand %.2f us, piano da %lu B in %.0f ms",
          (double)usData / SINGLE, (double)usCmd / SINGLE,
          (unsigned long)PLANE_BYTES,
          (rowUsPerByte > 0.0 ? rowUsPerByte : usPerByteReference()) * PLANE_BYTES / 1000.0);

  totalCommands   = cmdSnapshot;
  totalParamBytes = paramSnapshot;
}

/** Apre il bus e i pin. Chiamata una sola volta da setup(). */
static void controllerBegin()
{
  pinMode(PIN_CS, OUTPUT);   digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);   digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_BUSY, INPUT);
  spiSettings = SPISettings(cfg.spiHz, MSBFIRST, SPI_MODE0);
  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
}

#endif // PANEL_DIAGNOSTIC_CONTROLLER_H
