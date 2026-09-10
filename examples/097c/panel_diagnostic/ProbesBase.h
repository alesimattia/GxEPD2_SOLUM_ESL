// =============================================================================
// ProbesBase.h — le sonde del silicio: bus, colori, waveform dell'OTP, sonno.
//
// Ogni sonda risponde a UNA domanda, apre con enterRaw() e chiude col pannello
// spento, quindi si lancia in qualunque ordine e quante volte serve. Le
// dipendenze fra sonde passano dal registro delle misure, non da variabili
// globali: fullRefreshReference() dice quanto è durato l'ultimo refresh pieno,
// lastMeasurement(0xFC) se il differenziale ha concluso.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_PROBES_BASE_H
#define PANEL_DIAGNOSTIC_PROBES_BASE_H

#include "Controller.h"

// ---------------------------------------------------------------------------
// Frasi scritte sul vetro. Sono le stesse stringhe che finiscono nel log, così
// leggendo il pannello si ritrova la riga della passata e viceversa; il font
// copre solo maiuscole, cifre e poca punteggiatura.
// ---------------------------------------------------------------------------
static const char* const BAND_CAPTIONS[4] =
{
  "BANDA 1 BW=1 RED=0 LUT1 ATTESO BIANCO",
  "BANDA 2 BW=0 RED=0 LUT0 ATTESO NERO",
  "BANDA 3 BW=1 RED=1 LUT3 IL ROSSO DI OGGI",
  "BANDA 4 BW=0 RED=1 LUT2 MAI USATA DAL FIRMWARE",
};

/**
 * Scrive le quattro combinazioni dei due piani, una fascia per combinazione,
 * ognuna con la propria frase. Il valore dei due piani sui pixel del testo è
 * (BW=1, accent=0) su tutte le bande tranne la prima, che quella combinazione
 * la ha già come fondo: così la frase stacca dal fondo qualunque colore le due
 * combinazioni rendano.
 */
static void writeFourBands()
{
  static const uint8_t v24[4] = { 0xFF, 0x00, 0xFF, 0x00 };
  static const uint8_t v26[4] = { 0x00, 0x00, 0xFF, 0xFF };
  static const uint8_t t24[4] = { 0x00, 0xFF, 0xFF, 0xFF };
  for (uint8_t k = 0; k < 4; ++k)
  {
    const uint16_t y = (uint16_t)k * BAND_H;
    fillBand(0x24, y, BAND_H, v24[k], BAND_CAPTIONS[k], t24[k]);
    fillBand(0x26, y, BAND_H, v26[k], BAND_CAPTIONS[k], 0x00);
  }
  setRamWindow(0, 0, SRC, GATE, 0x03);
}

// ---------------------------------------------------------------------------
// b — bus e controller. Nessun refresh: solo i tempi che il driver codifica
// come costanti (power_on_time, power_off_time, _busy_timeout) e i comandi che
// i vari driver di partenza usano in modo diverso.
// ---------------------------------------------------------------------------
static void probeBus()
{
  enterRaw();
  benchmarkBus();

  measure("PATTERN 0x47 BIANCO", 0, fillByPattern(0x47, 0xFF));
  measure("PATTERN 0x46 SPENTO", 0, fillByPattern(0x46, 0x00));

  // Power on, poi le due misure che si possono fare solo ad analog acceso.
  measure("POWER ON 0x22=C0", 0xC0, partialPowerOn());
  measure("HV READY 0x14", 0, hvReady());
  measure("VCI DETECT 0x15", 0, vciDetect());
  measure("POWER OFF 0x22=C3", 0xC3, powerOffPanel(0xC3));

  // 0x83 è il power off di 1160_T91, GDEM133T91 e GDEQ0426T82; 0x03 quello del
  // community SDK Xteink. Quale il pannello gradisca è una misura.
  partialPowerOn();
  measure("POWER OFF 0x22=83", 0x83, powerOffPanel(0x83));
  partialPowerOn();
  measure("POWER OFF 0x22=03", 0x03, powerOffPanel(0x03));

  // 0xB1 carica temperatura e waveform dall'OTP senza dipingere: è il passo di
  // init di 1160_T91 e 750c_Z90, che il driver custom non fa.
  writeCommand(0x22); writeData(0xB1); writeCommand(0x20);
  measure("LOAD TEMP E LUT 0x22=B1", 0xB1, waitBusy(10000));
  writeCommand(0x22); writeData(0x99); writeCommand(0x20);
  measure("LOAD LUT MODE 2 0x22=99", 0x99, waitBusy(10000));

  // 0x13 non esiste nella Rev 1.0 del datasheet, ma il firmware di fabbrica lo
  // manda prima del deep sleep: se alza il BUSY, il silicio è più recente della
  // carta anche qui.
  writeCommand(0x13);
  const uint32_t t0 = millis();
  bool rose = false;
  while ((millis() - t0) < 200) { if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { rose = true; break; } delay(1); }
  measure("SOFT RESET 2 0x13", 0, rose ? waitBusy(2000) : -1);
  verdict("0x13 %s", rose ? "alza il BUSY: il comando esiste nel silicio"
                          : "non alza il BUSY: nessun effetto osservabile");

  enterRaw();
  readStatus();
  readUserId();
  readTemperature("a init finita");
}

// ---------------------------------------------------------------------------
// 1 — le quattro combinazioni dei due piani, Display Mode 1.
//
// È la misura che stabilisce quali colori il pannello rende, ed è anche il
// riferimento di durata di tutte le altre sonde: 0xF7 è la waveform di
// produzione, la stessa che il driver manda in _Update_Full().
// ---------------------------------------------------------------------------
static void probeBandsMode1()
{
  enterRaw();
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  writeFourBands();
  openScreen("4 bande, Display Mode 1");
  runRefresh(0xF7, "LE 4 BANDE MODE 1");
  readTemperature("dopo il refresh");
  look("1 bianca, 2 nera, 3 e 4 rosse: la linea y=504 si vede? [o/n]");
  waitKey();
  closeScreen();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 2 — le stesse quattro combinazioni in Display Mode 2.
//
// Stessa RAM, banco di waveform diverso preso dall'OTP. Mode 2 tratta la
// seconda RAM come frame precedente, quindi una differenza va interpretata
// prima di chiamarla colore.
// ---------------------------------------------------------------------------
static void probeBandsMode2()
{
  enterRaw();
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  writeFourBands();
  openScreen("4 bande, Display Mode 2");
  runRefresh(0xFF, "LE 4 BANDE MODE 2");
  const int32_t m1 = lastMeasurement(0xF7);
  const int32_t m2 = lastMeasurement(0xFF);
  if (m1 > 0 && m2 > 0)
    verdict("Mode 1 %ld ms, Mode 2 %ld ms: %s", (long)m1, (long)m2,
            (m2 > m1 + 500 || m2 < m1 - 500) ? "banchi diversi" : "stesso banco");
  look("i colori sono gli stessi della schermata Mode 1? [o/n]");
  waitKey();
  closeScreen();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 3 — il quarto colore per livello di sorgente.
//
// Le quattro combinazioni dei due piani sono già state viste tutte, e (1,0) e
// (1,1) escono entrambe rosse: sotto la waveform dell'OTP LUT2 e LUT3 rendono
// lo stesso colore, come la Table 6-4 dichiara. Ma quella tabella dice che le
// due LUT sono aliasate DALLA WAVEFORM, non che il film abbia tre pigmenti: se
// venissero pilotate a tensioni diverse, un quarto pigmento si separerebbe per
// soglia, che è esattamente come lavora un BWRY.
//
// È l'unica sonda autorizzata al code point VSH2, e la waveform la costruisce
// da sè invece di passare dal compositore: LUT2 a VSH1 e LUT3 a VSH2, stessi
// tempi, con nero e bianco fermi perchè l'unica differenza fra le due bande sia
// quale tensione tocca il film.
// ---------------------------------------------------------------------------
static void probeSourceLevels()
{
  enterRaw();
  openScreen("livelli di sorgente, quarto colore");

  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  runRefresh(0xF7, "FONDO BIANCO PRIMA DEI LIVELLI");

  enterRaw();
  memset(waveform, 0x00, sizeof(waveform));
  waveform[20] = vsAllPhases(VS_VSH1);    // LUT2, quattro fasi a VSH1
  waveform[30] = vsAllPhases(VS_VSH2);    // LUT3, quattro fasi a VSH2
  waveform[50] = 0x32; waveform[51] = 0x32; waveform[52] = 0x32; waveform[53] = 0x32;
  for (uint8_t i = 100; i < 105; ++i) waveform[i] = 0x22;   // 50 Hz
  waveform[105] = VGH_POR;
  waveform[106] = VSH1_15V; waveform[107] = VSH2_POR; waveform[108] = VSL_POR;
  // Le tensioni non vengono toccate: 0x32 scrive i byte 0..104 e questa sonda
  // non manda 0x04, quindi restano quelle che il SWRESET ha rimesso, cioè i
  // POR, e cambia solo QUALE delle due tensioni già presenti tocca ciascuna
  // LUT. È la stessa condizione in cui girava il probe originale.
  if (!applyWaveform(0x01, false, false, false, true)) { closeScreen(); return; }

  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  fillBand(0x26, BAND_H * 2, BAND_H * 2, 0xFF);   // accent acceso sulle bande 3 e 4
  fillBand(0x24, BAND_H * 3, BAND_H, 0x00);       // banda 4: BW=0 -> LUT2
  setRamWindow(0, 0, SRC, GATE, 0x03);
  partialPowerOn();
  runRefresh(0xC4, "LIVELLI LUT2 VSH1 CONTRO LUT3 VSH2", 10000);

  look("le due bande in basso hanno colore diverso? [o/n] o = quarto colore");
  waitKey();
  closeScreen();
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 4 — le sequenze di 0x22 dell'OTP.
//
// Una tabella di passate al posto di due sonde separate. La domanda centrale è
// se il controller CONFRONTA i due piani: se la durata di una passata con i
// piani identici è uguale a quella con i piani opposti, non li guarda, e
// nessun registro può fargli guardare quello che non guarda. Le passate che
// rifanno lo stesso confronto in altre condizioni sono quindi condizionali.
// ---------------------------------------------------------------------------
enum PassRam : uint8_t
{
  RAM_WHITE,   // 0x24 bianco con la frase, 0x26 come sta
  RAM_ZERO,    // i due piani identici bit per bit: nessun pixel da muovere
  RAM_MAX,     // 0x24 esatto inverso di 0x26: differenza totale
};

enum PassPre : uint8_t
{
  PRE_NONE,
  PRE_21_BYPASS,    // 0x21 = {40 00}: RED bypassata a 0, idioma GDEQ0426T82
  PRE_21_INVERSE,   // 0x21 = {08 00}: BW inverse, l'init di fabbrica SOLUM
  PRE_37_PINGPONG,  // 0x37 F[6]: RAM ping-pong, l'unico bit legato al Mode 2
  PRE_1A_HOT,       // 0x1A = 0x5A: il banco caldo di GDEQ0426T82
};

struct UpdatePass
{
  const char* caption;
  uint8_t     seq22;
  uint8_t     ram;
  uint8_t     pre;
  bool        exhaustiveOnly;
};

static const UpdatePass UPDATE_PASSES[] =
{
  { "RIFERIMENTO 0X22=F7",              0xF7, RAM_WHITE, PRE_NONE,        false },
  { "DIFFERENZA ZERO 0X22=FC",          0xFC, RAM_ZERO,  PRE_NONE,        false },
  { "DIFFERENZA MASSIMA 0X22=FC",       0xFC, RAM_MAX,   PRE_NONE,        false },
  { "MODE 2 COMPLETO 0X22=FF",          0xFF, RAM_WHITE, PRE_NONE,        true  },
  { "DISPLAY SENZA RICARICA 0X22=CF",   0xCF, RAM_WHITE, PRE_NONE,        true  },
  { "MODE 1 SENZA RICARICA 0X22=C7",    0xC7, RAM_WHITE, PRE_NONE,        true  },
  { "MODE 1 SENZA POWER DOWN 0X22=F4",  0xF4, RAM_WHITE, PRE_NONE,        true  },
  { "RED BYPASSATA 0X21=40 0X22=F7",    0xF7, RAM_WHITE, PRE_21_BYPASS,   true  },
  { "0X21 DI FABBRICA 08 0X22=FC",      0xFC, RAM_MAX,   PRE_21_INVERSE,  true  },
  { "RAM PING PONG 0X37 0X22=FC",       0xFC, RAM_MAX,   PRE_37_PINGPONG, true  },
  { "BANCO CALDO 0X1A=5A 0X22=D7",      0xD7, RAM_WHITE, PRE_1A_HOT,      true  },
};
static const uint8_t UPDATE_PASS_COUNT = sizeof(UPDATE_PASSES) / sizeof(UPDATE_PASSES[0]);

/** Prepara i due piani secondo il modo della passata. */
static void stagePassRam(const UpdatePass& p)
{
  switch (p.ram)
  {
    case RAM_ZERO:
      fillBand(0x24, 0, GATE, 0xFF, p.caption, 0x00);
      fillBand(0x26, 0, GATE, 0xFF, p.caption, 0x00);   // identica a 0x24
      break;
    case RAM_MAX:
      fillBand(0x26, 0, GATE, 0xFF, p.caption, 0x00);
      fillBand(0x24, 0, GATE, 0x00, p.caption, 0xFF);   // l'esatto inverso
      break;
    case RAM_WHITE:
    default:
      fillBand(0x24, 0, GATE, 0xFF, p.caption, 0x00);
      break;
  }
  setRamWindow(0, 0, SRC, GATE, 0x03);
}

/** Scrive il registro che la passata vuole provare, e lo rimette dopo. */
static void applyPassPre(uint8_t pre, bool restore)
{
  static const uint8_t CTRL21_NORMAL[2]  = { 0x00, 0x00 };
  static const uint8_t CTRL21_BYPASS[2]  = { 0x40, 0x00 };
  static const uint8_t CTRL21_INVERSE[2] = { 0x08, 0x00 };
  switch (pre)
  {
    case PRE_21_BYPASS:
      writeCommandData(0x21, restore ? CTRL21_NORMAL : CTRL21_BYPASS, 2);
      break;
    case PRE_21_INVERSE:
      writeCommandData(0x21, restore ? CTRL21_NORMAL : CTRL21_INVERSE, 2);
      break;
    case PRE_37_PINGPONG:
      if (restore) break;   // lo rimette solo un reset: 0x37 arriva dall'OTP
      // A..E a 0xFF mettono tutti gli stadi su Mode 2, F = 0x40 accende il
      // ping-pong, G..J restano a 0 perchè sono module ID e versione.
      writeCommand(0x37);
      writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF);
      writeData(0x40);
      writeData(0x00); writeData(0x00); writeData(0x00); writeData(0x00);
      break;
    case PRE_1A_HOT:
      // Solo il registro della temperatura, come GDEQ0426T82 e GDEM0397T81:
      // 0xD7 ha il bit 5 SPENTO, cioè non ricarica la temperatura dal sensore,
      // e la ricerca del banco usa il valore appena scritto. Toccare 0x18
      // aggiungerebbe una variabile che quei driver non hanno. Il valore torna
      // al POR col SWRESET della passata successiva, quindi non serve disfarlo.
      if (restore) break;
      writeCommand(0x1A); writeData(0x5A); writeData(0x00);
      break;
    default: break;
  }
}

static void probeUpdateSequences()
{
  openScreen("sequenze di 0x22");
  int32_t msZero = -1, msMax = -1;

  for (uint8_t i = 0; i < UPDATE_PASS_COUNT; ++i)
  {
    const UpdatePass& p = UPDATE_PASSES[i];
    if (p.exhaustiveOnly && !cfg.exhaustive) continue;

    enterRaw();
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);
    applyPassPre(p.pre, false);
    stagePassRam(p);

    // Con i piani a confronto il riquadro li renderebbe diversi, e falserebbe
    // proprio la misura per cui quelle passate esistono.
    const bool noBadge = (p.ram != RAM_WHITE);
    const uint16_t saved = noBadge ? suspendBadge() : screenOpen;
    const int32_t ms = runRefresh(p.seq22, p.caption, 40000);
    if (noBadge) resumeBadge(saved);

    applyPassPre(p.pre, true);
    if (p.pre == PRE_NONE && p.ram == RAM_ZERO) msZero = ms;
    if (p.pre == PRE_NONE && p.ram == RAM_MAX)  msMax  = ms;
  }

  if (msZero > 0 && msMax > 0)
  {
    const long delta = (long)msMax - (long)msZero;
    verdict("zero %ld ms, massima %ld ms, scarto %ld ms: il controller %s",
            (long)msZero, (long)msMax, delta,
            (delta > 2000 || delta < -2000) ? "CONFRONTA i due piani"
                                            : "NON confronta i due piani");
  }
  look("quale frase e' rimasta sul vetro? e' quella dell'ultima passata? [o/n]");
  waitKey();
  closeScreen();
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 5 — la finestra RAM limita l'area ridipinta?
//
// Il contenuto finale non basta a dirlo: la RAM accumula le scritture, quindi
// un refresh che ripassa tutto mostra la stessa immagine di uno che ripassa
// solo la finestra. Serve una discordanza voluta, ed è la fascia di TRAPPOLA:
// nera in 0x24 e mai compresa in nessuna finestra di refresh. Se resta bianca
// le finestre sono rispettate; se compare, il refresh percorre il pannello
// intero e nel driver refresh(x, y, w, h) non può essere un refresh d'area.
// ---------------------------------------------------------------------------
static const uint16_t TRAP_Y = 176, TRAP_H = 40;
static const uint16_t AREA1_Y = 0,   AREA1_H = BAND_H;
static const uint16_t THIN_Y = 224,  THIN_H = 24;
static const uint16_t MODE1_Y = 264, MODE1_H = 48;
static const uint16_t BOX_X = 256,   BOX_W = 256, BOX_Y = 504, BOX_H = BAND_H;

/** Una passata d'area: scrive la finestra, aggiorna e riallinea 0x26, come fa
 *  writeImagePartToPrevious del driver monocromatico. */
static int32_t areaPass(const char* caption, uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h, uint8_t value, uint8_t fg, uint8_t seq22)
{
  if (w == SRC)
  {
    const TextLayout lay = layoutText(caption, h);
    if (lay.scale > 0)
    {
      setRamWindow(x, y, w, h, 0x03);
      writeCommand(0x24);
      writeRowsWithText(value, h, fg, lay);
    }
    else fillPlaneRect(0x24, x, y, w, h, value);
  }
  else fillPlaneRect(0x24, x, y, w, h, value);

  setRamWindow(x, y, w, h, 0x03);
  const int32_t ms = runRefresh(seq22, caption, 40000);
  fillPlaneRect(0x26, x, y, w, h, value);
  return ms;
}

static void probeWindowTrap()
{
  enterRaw();
  openScreen("finestra RAM e fascia di trappola");

  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);   // accent spento: senza, ogni pixel sarebbe rosso
  if (runRefresh(0xF7, "BASELINE BIANCA DELL AREA") < 0) { closeScreen(); return; }

  fillPlaneRect(0x24, 0, TRAP_Y, SRC, TRAP_H, 0x00);   // la trappola, mai refreshata

  areaPass("AREA 1 FASCIA ALTA NERA 0X22=FC", 0, AREA1_Y, SRC, AREA1_H, 0x00, 0xFF, 0xFC);
  areaPass("AREA MODE 1 SU FINESTRA 0X22=F4", 0, MODE1_Y, SRC, MODE1_H, 0x00, 0xFF, 0xF4);

  if (cfg.exhaustive)
  {
    areaPass("AREA SOTTILE 24 RIGHE 0X22=FC", 0, THIN_Y, SRC, THIN_H, 0x00, 0xFF, 0xFC);
    areaPass("AREA RISTRETTA IN X 0X22=FC", BOX_X, BOX_Y, BOX_W, BOX_H, 0x00, 0x00, 0xFC);
    // Border su VCOM invece che su LUT1: è il valore che tiene ferma la cornice
    // durante un partial, e le due passate differiscono solo per questo.
    writeCommand(0x3C); writeData(0x80);
    areaPass("AREA 1 BIANCA BORDO 0X3C=80 0X22=FC", 0, AREA1_Y, SRC, AREA1_H, 0xFF, 0x00, 0xFC);
    writeCommand(0x3C); writeData(0x01);
  }

  const int32_t full = fullRefreshReference();
  const int32_t area = lastMeasurement(0xFC);
  if (full > 0 && area > 0)
    verdict("area %ld ms contro %ld ms del pieno: %s", (long)area, (long)full,
            (area * 2 < full) ? "la finestra accorcia" : "nessun guadagno dalla finestra");
  look("la trappola y=176..215 e' rimasta BIANCA? [o/n] n = il refresh ripassa tutto");
  waitKey();
  closeScreen();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 6 — i banchi di waveform per temperatura.
//
// Il §6.9 dà l'OTP per capace di 34 set WS0..WS33, uno per range TR0..TR33,
// scelti in base alla temperatura letta. Tutte le altre sonde girano col
// sensore interno a temperatura ambiente, quindi esercitano un solo set: qui
// se ne chiedono altri, e sono waveform di fabbrica già tarate su questo film.
//
// Una durata molto SOPRA quella ambiente conta quanto una sotto: dice che i
// banchi esistono e che verso il freddo la waveform si allunga, il che obbliga
// il driver a un timeout capace di coprire il banco più lungo. Da qui il
// pavimento di 120 s. SOLUM dichiara l'esercizio a 0..40 gradi e il ghosting
// sotto i 15: una passata fuori range è un controllo, non una misura.
// ---------------------------------------------------------------------------
static const uint32_t TEMP_TIMEOUT_MS = 120000;
static char tempCaptions[TEMP_PASSES][48];

/** Forza la temperatura che il controller usa per scegliere la waveform:
 *  0x18 = 0x48 mette il sensore su esterno, 0x1A porta i gradi per sedici in
 *  12 bit, A[11:4] nel primo byte e A[3:0] nei bit alti del secondo. */
static void setForcedTemperature(int16_t degC)
{
  const int16_t raw = (int16_t)(degC * 16);
  writeCommand(0x18); writeData(0x48);
  writeCommand(0x1A);
  writeData((uint8_t)((raw >> 4) & 0xFF));
  writeData((uint8_t)((raw << 4) & 0xF0));
}

static void probeTemperatureBanks()
{
  const uint32_t timeout = cfg.timeoutMs > TEMP_TIMEOUT_MS ? cfg.timeoutMs : TEMP_TIMEOUT_MS;
  openScreen("banchi di waveform per temperatura");

  for (uint8_t t = 0; t < TEMP_PASSES; ++t)
  {
    const int16_t degC = cfg.temp[t];
    const bool outOfRange = (degC < PANEL_TEMP_MIN || degC > PANEL_TEMP_MAX);
    snprintf(tempCaptions[t], sizeof(tempCaptions[t]), "TEMPERATURA %d GRADI%s 0X1A=%03X",
             (int)degC, outOfRange ? " FUORI RANGE" : "",
             (unsigned)((degC * 16) & 0xFFF));

    enterRaw();
    setForcedTemperature(degC);
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);
    // Le fasce si accumulano: 0xF7 ridipinge tutto secondo la RAM, quindi la
    // sola fascia corrente cancellerebbe le precedenti, e il confronto fra
    // passate è esattamente quello che serve.
    for (uint8_t d = 0; d <= t; ++d)
      fillBand(0x24, (uint16_t)(BAND_H * d), BAND_H, 0x00, tempCaptions[d], 0xFF);
    setRamWindow(0, 0, SRC, GATE, 0x03);

    const int32_t ms = runRefresh(0xF7, tempCaptions[t], timeout);
    if (ms < 0)
    {
      // Un timeout non è una misura persa: il refresh è ancora in corso, e la
      // durata vera è il dato interessante proprio perchè esce dal quadro.
      const int32_t tail = waitBusy(timeout);
      if (tail >= 0) verdict("il banco di questo range dura %ld ms", (long)(timeout + tail));
    }
    writeCommand(0x18); writeData(0x80);   // rimette il sensore interno
  }

  if (cfg.exhaustive)
  {
    // Banco caldo senza sensore esterno: 0x1A più 0xD7, cioè come GDEQ0426T82 e
    // GDEM0397T81 ottengono un refresh pieno più corto.
    enterRaw();
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);
    fillBand(0x24, 0, BAND_H, 0x00, "BANCO CALDO 0X1A=5A 0X22=D7", 0xFF);
    setRamWindow(0, 0, SRC, GATE, 0x03);
    writeCommand(0x1A); writeData(0x5A); writeData(0x00);
    runRefresh(0xD7, "BANCO CALDO 0X1A=5A 0X22=D7", timeout);
  }

  look("quali fasce sono nere, e quella fuori range e' stata rifiutata? [o/n]");
  waitKey();
  closeScreen();
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 7 — il MUX: il refresh scala con le gate line scandite?
//
// La finestra RAM dice solo dove finiscono i byte; 0x01 dice quante gate line
// il driver scandisce davvero. Se il tempo scala, esiste un partial per bande
// che si ottiene riducendo il MUX; se resta costante, il frame rate della
// waveform è fisso e nessuna leva geometrica accorcia il refresh.
// ---------------------------------------------------------------------------
static char muxCaptions[MUX_PASSES][32];

static void probeMux()
{
  int32_t passMs[MUX_PASSES];
  openScreen("MUX ridotto");
  for (uint8_t m = 0; m < MUX_PASSES; ++m)
  {
    const uint16_t gate = cfg.mux[m];
    snprintf(muxCaptions[m], sizeof(muxCaptions[m]), "MUX %u GATE LINE", (unsigned)gate);

    enterRaw();
    const uint16_t mux = (uint16_t)(gate - 1);
    writeCommand(0x01);
    writeData((uint8_t)(mux & 0xFF));
    writeData((uint8_t)((mux >> 8) & 0x03));
    writeData(0x00);
    setRamWindow(0, 0, SRC, gate, 0x03);
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);
    fillBand(0x24, 0, (uint16_t)(gate / 4), 0x00, muxCaptions[m], 0xFF);
    setRamWindow(0, 0, SRC, gate, 0x03);
    passMs[m] = runRefresh(0xF7, muxCaptions[m]);
  }

  const int32_t widest    = passMs[0];
  const int32_t narrowest = passMs[MUX_PASSES - 1];
  if (widest > 0 && narrowest > 0)
  {
    const double ratio = (double)widest / (double)narrowest;
    verdict("MUX %u contro %u: %.2fx, il refresh %s",
            (unsigned)cfg.mux[0], (unsigned)cfg.mux[MUX_PASSES - 1], ratio,
            (ratio > 2.0) ? "SCALA con le gate line" : "NON scala");
  }
  look("la fascia dell'ultima passata c'e'? [o/n]");
  waitKey();
  closeScreen();
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// 0 — deep sleep: si addormenta, si sveglia, e la RAM sopravvive?
//
// Le prime due domande decidono se hibernate() è utilizzabile; la terza decide
// una riga del driver. Oggi _InitDisplay() riarma _initial_write quando arriva
// da un hibernate, perchè la tabella elettrica del SSD1677 dà la RAM per non
// ritenuta; il SSD1683, che documenta due modi di deep sleep, dice invece che
// il modo 1 la RITIENE. Se questo silicio si comporta come l'SSD1683, quella
// riga costa 18 ms di pattern a ogni risveglio senza motivo.
//
// La prova: si stampa un riferimento, si dorme, si riparte con reset e init e
// si fa un refresh SENZA riscrivere la RAM. Se torna la stessa immagine, la
// RAM è rimasta.
// ---------------------------------------------------------------------------

/**
 * Prova di sordità: da addormentato si manda un'operazione che da sveglio si
 * vedrebbe di sicuro sul BUSY. Il livello del BUSY da solo non prova niente,
 * perchè un pin flottante sta alto come un controller che dorme.
 * Ritorna true se ha ignorato tutto.
 */
static bool sleepIgnoresCommands(uint32_t windowMs)
{
  const bool busyHigh = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  writeCommand(0x47); writeData(0x77);   // pattern nero: da sveglio riempirebbe la RAM
  writeCommand(0x22); writeData(0xF7);
  writeCommand(0x20);

  if (busyHigh) return waitBusy(windowMs) < 0;
  const uint32_t t0 = millis();
  while ((millis() - t0) < 2000)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { waitBusy(windowMs); return false; }
    delay(1);
  }
  return true;
}

static uint8_t sleepParamFound = 0x00;

/**
 * Frasi delle passate del sonno, una per parametro di 0x10 provato. Statiche e
 * non locali perchè il registro delle misure conserva il PUNTATORE alla frase:
 * un buffer locale morirebbe all'uscita dalla sonda, e uno solo condiviso fra
 * i parametri farebbe leggere a tutte le righe il testo dell'ultimo.
 */
static char sleepRefCaption[3][48];
static char sleepRamCaption[3][48];

static void probeDeepSleep()
{
  const int32_t full = fullRefreshReference();
  const uint32_t window = (uint32_t)(full > 0 ? full : 24000) + 8000;

  // 0x01 è il modo 1 del SSD1683 e il valore del demo Good Display; 0x03 è
  // l'unico che la Rev 1.0 del SSD1677 definisce, ed è quello che il driver
  // manda oggi; 0x11 lo usano GDEM133Z91 e 750c_Z90.
  static const uint8_t PARAMS[3]      = { 0x01, 0x03, 0x11 };
  const uint8_t paramCount = cfg.exhaustive ? 3 : 2;

  for (uint8_t k = 0; k < paramCount; ++k)
  {
    const uint8_t param = PARAMS[k];
    char* const caption = sleepRefCaption[k];
    snprintf(caption, sizeof(sleepRefCaption[k]), "RIFERIMENTO PRIMA DI 0X10=%02X", param);

    enterRaw();
    openScreen("deep sleep, riferimento");
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);
    fillBand(0x24, 0, BAND_H, 0x00, caption, 0xFF);
    setRamWindow(0, 0, SRC, GATE, 0x03);
    runRefresh(0xF7, caption);
    closeScreen();

    powerOffPanel(0xC3);
    writeCommand(0x10); writeData(param);
    delay(50);
    const bool busyHigh = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
    const bool deaf = sleepIgnoresCommands(window);
    logLine("0x10 = 0x%02X: BUSY %s, comandi %s", param,
            busyHigh ? "alto" : "basso", deaf ? "ignorati" : "ancora eseguiti");
    if (!deaf) continue;
    if (!sleepParamFound) sleepParamFound = param;

    look("l'immagine e' rimasta intatta mentre dorme? misura la corrente sul 3V3 [o/n]");
    waitKey();

    // Risveglio: reset più init, cronometrati, poi un refresh SENZA riscrivere
    // la RAM. Quello che compare dice se la RAM è sopravvissuta.
    const uint32_t tWake = millis();
    enterRaw();
    measure("RISVEGLIO RESET PIU INIT", 0, (int32_t)(millis() - tWake));
    openScreen("deep sleep, RAM dopo il risveglio");
    char* const probe = sleepRamCaption[k];
    snprintf(probe, sizeof(sleepRamCaption[k]), "RAM DOPO 0X10=%02X SENZA RISCRITTURA", param);
    // Il riquadro va sospeso: disegnarlo scriverebbe in RAM, e questa è
    // l'unica passata di tutta la suite che deve trovarla come il sonno l'ha
    // lasciata. Il numero resta nel log, non sul vetro.
    const uint16_t held = suspendBadge();
    runRefresh(0xF7, probe);
    resumeBadge(held);
    look("e' tornata l'immagine di prima? [o/n] o = la RAM e' ritenuta");
    const char seen = waitKey();
    verdict("0x10 = 0x%02X: RAM %s dopo il risveglio", param,
            (seen == 'o' || seen == 'O') ? "RITENUTA" : "persa");
    closeScreen();
  }

  // Sequenza di sonno del firmware di fabbrica: 0x13 prima di 0x10.
  if (sleepParamFound)
  {
    enterRaw();
    writeCommand(0x13);
    waitBusy(2000);
    writeCommand(0x10); writeData(sleepParamFound);
    delay(50);
    logLine("sonno OEPL 0x13 + 0x10 = 0x%02X: BUSY %s", sleepParamFound,
            digitalRead(PIN_BUSY) == BUSY_ACTIVE ? "alto" : "basso");
  }
  else logLine("nessun parametro di 0x10 addormenta il controller");

  enterRaw();
  powerOffPanel(0xC3);
}

#endif // PANEL_DIAGNOSTIC_PROBES_BASE_H
