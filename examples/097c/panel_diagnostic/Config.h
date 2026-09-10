// =============================================================================
// Config.h — parametri della suite, persistenti in NVS, e input seriale bloccante.
//
// UNA SOLA COMPILAZIONE. Tutto ciò che in un ciclo di reverse engineering
// verrebbe voglia di ritoccare e riflashare è un campo di questa struct: le
// sonde non leggono mai un #define. Due blob, `seq` e `lut`, tengono l'ultima
// sequenza di init e l'ultima waveform provate, così un tentativo riuscito
// sopravvive al reboot ed è ripetibile alla lettera.
//
// I limiti dei campi non sono prudenza: vengono dal datasheet SSD1677 e dalle
// specifiche SOLUM in docs/, e stanno qui perchè un valore fuori range non
// misura niente o danneggia il film. Vedi askNumber() nei sottomenu.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_CONFIG_H
#define PANEL_DIAGNOSTIC_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Geometria nativa del pannello: 960 source sull'asse RAM X, 672 gate sull'asse
// RAM Y. Sono le uniche costanti che non si toccano a runtime, perchè sono il
// vetro e non una scelta.
// ---------------------------------------------------------------------------
static const uint16_t SRC        = 960;
static const uint16_t GATE       = 672;
static const uint16_t ROW_BYTES  = SRC / 8;                        // 120
static const uint32_t PLANE_BYTES = (uint32_t)ROW_BYTES * GATE;    // 80.640
static const uint16_t BAND_H     = GATE / 4;                       // 168

// Punti degli sweep configurabili.
static const uint8_t TEMP_PASSES = 4;
static const uint8_t MUX_PASSES  = 3;

// ---------------------------------------------------------------------------
// Limiti, dal datasheet e da docs/. Applicati dai sottomenu e da waveformGuard().
// ---------------------------------------------------------------------------
static const uint16_t MUX_MIN     = 300;      // 0x01: range dichiarato 300..680
static const uint16_t MUX_MAX     = 680;
static const uint32_t SPI_HZ_MIN  = 100000;
static const uint32_t SPI_HZ_MAX  = 20000000; // feature page: max 20 MHz in scrittura
static const int16_t  TEMP_MIN    = -128;     // 0x1A: 12 bit c.a.2, gradi per sedici
static const int16_t  TEMP_MAX    = 127;

/** Range di esercizio del pannello, concorde in tre fonti di docs/:
 *  Newton-PRO_Data-sheet §3.1 e le due Specifications danno 0..40 °C, e il
 *  §Precautions dichiara ghosting sotto i 15 °C. Serve alla sonda dei banchi
 *  per distinguere una passata dentro il range da una di controllo. */
static const int16_t PANEL_TEMP_MIN = 0;
static const int16_t PANEL_TEMP_MAX = 40;

// ---------------------------------------------------------------------------
// Tensioni: code point del SSD1677 e tetti di sicurezza.
//
// 0x04 A = VSH1 con A[7] = 0: 0x23 = 9 V, +0,2 V per passo. Il POR è 0x41 =
// 15 V, ed è il punto a cui questo film ha già girato nella sonda dei livelli:
// oltre non si sale, perchè alzare VSH1 è l'unica leva che può danneggiare il
// pigmento in modo permanente e la waveform del produttore non è nota.
// 0x2C VCOM parte da 0x08 = -0,2 V con -0,1 V ogni quattro passi: 0x44 = -1,7 V
// è il valore che Good Display accoppia a questa LUT sull'SSD1677
// (GxEPD2_370_TC1::_InitDisplay), ed è il tetto.
// ---------------------------------------------------------------------------
static const uint8_t VSH1_9V   = 0x23;
static const uint8_t VSH1_15V  = 0x41;   // POR, e il massimo consentito
static const uint8_t VSH2_POR  = 0xA8;   // 5 V
static const uint8_t VSL_POR   = 0x32;   // -15 V
static const uint8_t VGH_POR   = 0x00;   // 20 V
static const uint8_t VCOM_17V  = 0x44;   // -1,7 V, il massimo consentito
static const uint8_t VCOM_OTP  = 0x00;   // "non scrivere 0x2C": resta quello dell'OTP

// Livelli di VS, Table 6-6: 00 VSS, 01 VSH1, 10 VSL, 11 VSH2.
// VS_VSH2 è il ROSSO su questo film: nessuna waveform lo usa, tranne la sonda
// dei livelli di sorgente, che esiste apposta per separarlo da VSH1.
static const uint8_t VS_VSS  = 0;
static const uint8_t VS_VSH1 = 1;
static const uint8_t VS_VSL  = 2;
static const uint8_t VS_VSH2 = 3;

// Codici di frame rate, Table 6-7 del SSD1683 (il SSD1677 Rev 1.0 non la
// riporta, ma la misura torna al millisecondo): 0x2 = 50 Hz è quello della
// waveform del driver.
static const uint8_t FR_25HZ  = 0x1;
static const uint8_t FR_50HZ  = 0x2;
static const uint8_t FR_75HZ  = 0x3;
static const uint8_t FR_100HZ = 0x4;
static const uint8_t FR_125HZ = 0x5;

// Forma della waveform del partial del driver, per riprodurla dal compositore.
static const uint16_t RESET_FRAMES = 10;
static const uint16_t DRIVE_FRAMES = 18;

// ---------------------------------------------------------------------------
// Sequenza di init: una riga di questa struct è un intero driver di partenza.
//
// Il runner runInitSequence() la esegue campo per campo, quindi provare l'init
// di un altro driver upstream non costa una ricompilazione ma una riga di
// tabella, e la variante "custom-live" prende i campi da cfg.seq, editabili dal
// menu. I valori di ogni variante e la loro fonte stanno in ProbesUpstream.h.
// ---------------------------------------------------------------------------
struct InitSequence
{
  const char* name;              // sul vetro e nel log
  uint8_t     resetLowMs;        // durata del livello basso di RST
  bool        swresetOnBusy;     // true: attende il BUSY; false: delay fisso
  uint8_t     swresetMs;         // il delay fisso, se swresetOnBusy è false
  bool        tempFirst;         // 0x18 prima di 0x0C (idioma GDEQ0426T82)
  bool        patternInInit;     // 0x46 = 0xF7 e 0x47 = 0xF7 con 15 ms (fabbrica)
  uint8_t     softStart5;        // quinto byte di 0x0C: 0x40 Level 1, 0x80 Level 2
  uint16_t    muxRegister;       // valore del registro 0x01: le gate line sono +1,
                                 // ed è la forma in cui i sorgenti upstream lo scrivono
                                 // (0x9F 0x02 = 671 -> 672 linee, 0xA7 0x02 = 679 -> 680)
  uint8_t     muxB;              // terzo byte di 0x01: B[2:0] GD/SM/TB
  uint8_t     entryMode;         // 0x11: 0x03 X++ Y++, 0x02 X--, 0x01 Y--
  bool        loadB1;            // 0x22 = 0xB1 + 0x20 in init (carica temp e LUT)
  bool        armF7;             // 0x22 = 0xF7 senza 0x20, come il firmware di fabbrica
  uint8_t     ctrl21[2];         // parametri di 0x21
  uint8_t     ctrl21When;        // 0 non inviarlo, 1 in init, 2 prima del refresh
  bool        powerOnFirst;      // 0x22 = 0xC0 + 0x20 prima del refresh
  uint8_t     refresh;           // parametro di 0x22 del refresh
  int16_t     powerOff;          // -1 nessuno, altrimenti parametro di 0x22
};

/** Sorgente della waveform di una passata: da dove escono i 110 byte. */
enum WaveformSource : uint8_t
{
  WS_DRIVER,        // la costante del driver custom, invariata
  WS_DRIVER_DUAL,   // la stessa, con LUT1 resa duale di LUT2
  WS_COMPOSER,      // composta da buildWaveform() con i campi qui sotto
  WS_FULL_370,      // lut_full di GxEPD2_370_TC1: refresh pieno B/N dall'MCU
};

/**
 * Waveform di una passata. Il compositore la costruisce a runtime, quindi
 * cambiare tensioni, frame o frame rate non costa una ricompilazione. I campi
 * di tensione valgono anche per le sorgenti non composte, perchè 0x32 scrive
 * solo i byte 0..104 e le tensioni viaggiano su 0x03 / 0x04 / 0x2C.
 */
struct WaveformSpec
{
  uint8_t  source;         // WaveformSource
  uint8_t  vsh1, vsh2, vsl, vgh, vcom;
  uint16_t resetFrames, driveFrames;
  uint8_t  cycles;         // alternanze reset/drive, a frame totali costanti
  uint8_t  frCode;         // FR_*
  uint8_t  border;         // 0x3C: 0xC0 HiZ per il partial, 0x01 LUT1 per il pieno
  bool     writeVcom;      // false: 0x2C non viene inviato e il VCOM resta dell'OTP
};

// ---------------------------------------------------------------------------
// Configurazione completa della sessione.
// ---------------------------------------------------------------------------
struct Config
{
  bool         exhaustive;          // esegue anche le passate condizionali
  bool         verbose;             // righe di dettaglio sul seriale
  int16_t      temp[TEMP_PASSES];   // gradi forzati via 0x18 / 0x1A
  uint16_t     mux[MUX_PASSES];     // gate line programmate in 0x01
  uint32_t     spiHz;
  uint32_t     timeoutMs;           // timeout di default di un refresh
  InitSequence seq;                 // variante "custom-live" della sonda i
  WaveformSpec lut;                 // waveform della passata libera m
};

/** Init di partenza della variante editabile: quella del driver custom. */
static const InitSequence SEQ_DEFAULT =
{
  "custom-live", 2, true, 0, false, false, 0x80, 671, 0x00, 0x03,
  false, false, { 0x00, 0x00 }, 0, false, 0xF7, -1
};
static_assert(MUX_MIN <= 672 && 672 <= MUX_MAX, "il MUX del pannello deve stare nel range");

/** Waveform di partenza: quella del driver custom, tensioni comprese. */
static const WaveformSpec LUT_DEFAULT =
{
  WS_DRIVER, VSH1_15V, VSH2_POR, VSL_POR, VGH_POR, VCOM_OTP,
  RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, 0xC0, false
};

static const Config CFG_DEFAULT =
{
  false, false,
  { 0, 20, 40, 70 },
  { 680, 672, 336 },
  10000000, 40000,
  SEQ_DEFAULT, LUT_DEFAULT
};

static Config cfg = CFG_DEFAULT;

// Versione dei blob in NVS: un profilo scritto da una build con campi diversi
// va scartato, non riletto come se fosse compatibile.
static const uint8_t CFG_BLOB_VERSION = 1;

static Preferences prefs;

// ---------------------------------------------------------------------------
// Input seriale. Sempre bloccante: la suite non avanza mai da sola, perchè ogni
// schermata va guardata prima che la successiva la sovrascriva.
// ---------------------------------------------------------------------------

/** Scarta quello che è rimasto nel buffer di ricezione. */
static void flushInput()
{
  while (Serial.available()) Serial.read();
}

/** Aspetta un tasto e lo ritorna. Non ha timeout: è la regola della suite. */
static char waitAnyKey()
{
  while (!Serial.available()) delay(20);
  const char c = (char)Serial.read();
  flushInput();
  return c;
}

/**
 * Legge una riga, bloccando finchè non arriva un a capo. Ritorna i caratteri
 * letti, 0 per una riga vuota, che vale "tieni il valore corrente".
 */
static uint8_t readLine(char* buf, uint8_t max)
{
  uint8_t n = 0;
  while (true)
  {
    if (!Serial.available()) { delay(10); continue; }
    const int c = Serial.read();
    if (c == '\r' || c == '\n')
    {
      // Il LF che chiude un CRLF arriva subito dopo: va consumato qui, o
      // chiuderebbe a vuoto la domanda successiva.
      if (c == '\r')
      {
        const uint32_t t0 = millis();
        while ((millis() - t0) < 20 && !Serial.available()) delay(1);
        if (Serial.available() && Serial.peek() == '\n') Serial.read();
      }
      break;
    }
    if (n < max - 1) buf[n++] = (char)c;
  }
  buf[n] = '\0';
  return n;
}

/** Domanda numerica con limiti: fuori range tiene il valore corrente. */
static long askNumber(const char* prompt, long lo, long hi, long current)
{
  flushInput();
  Serial.printf("  %s [%ld..%ld, invio = %ld]: ", prompt, lo, hi, current);
  char buf[16];
  if (readLine(buf, sizeof(buf)) == 0) { Serial.printf("%ld\n", current); return current; }
  char* end = nullptr;
  const long v = strtol(buf, &end, 0);
  if (end == buf || v < lo || v > hi)
  {
    Serial.printf("%ld (fuori range o non numerico)\n", current);
    return current;
  }
  Serial.printf("%ld\n", v);
  return v;
}

/** Domanda su un byte, letto in esadecimale. */
static uint8_t askHex(const char* prompt, uint8_t current)
{
  flushInput();
  Serial.printf("  %s [hex, invio = 0x%02X]: ", prompt, current);
  char buf[8];
  if (readLine(buf, sizeof(buf)) == 0) { Serial.printf("0x%02X\n", current); return current; }
  char* end = nullptr;
  const long v = strtol(buf, &end, 16);
  if (end == buf || v < 0 || v > 0xFF)
  {
    Serial.printf("0x%02X (non valido)\n", current);
    return current;
  }
  Serial.printf("0x%02X\n", (unsigned)v);
  return (uint8_t)v;
}

static bool askYesNo(const char* prompt, bool current)
{
  flushInput();
  Serial.printf("  %s [s/n, invio = %s]: ", prompt, current ? "s" : "n");
  char buf[8];
  if (readLine(buf, sizeof(buf)) == 0) { Serial.println(current ? "s" : "n"); return current; }
  const bool v = (buf[0] == 's' || buf[0] == 'S' || buf[0] == 'y' || buf[0] == 'Y');
  Serial.println(v ? "s" : "n");
  return v;
}

// ---------------------------------------------------------------------------
// Persistenza. I due blob portano un byte di versione in testa.
// ---------------------------------------------------------------------------

/** Legge il profilo salvato; i campi assenti restano ai valori di fabbrica. */
static void loadConfig()
{
  prefs.begin("pd097", true);
  cfg.exhaustive = prefs.getBool  ("exh",   cfg.exhaustive);
  cfg.verbose    = prefs.getBool  ("verb",  cfg.verbose);
  cfg.spiHz      = prefs.getULong ("spihz", cfg.spiHz);
  cfg.timeoutMs  = prefs.getULong ("tmout", cfg.timeoutMs);
  prefs.getBytes("temp", cfg.temp, sizeof(cfg.temp));
  prefs.getBytes("mux3", cfg.mux,  sizeof(cfg.mux));

  uint8_t blob[1 + sizeof(InitSequence)];
  if (prefs.getBytes("seq", blob, sizeof(blob)) == sizeof(blob) && blob[0] == CFG_BLOB_VERSION)
    memcpy(&cfg.seq, blob + 1, sizeof(InitSequence));
  uint8_t lutBlob[1 + sizeof(WaveformSpec)];
  if (prefs.getBytes("lut", lutBlob, sizeof(lutBlob)) == sizeof(lutBlob)
      && lutBlob[0] == CFG_BLOB_VERSION)
    memcpy(&cfg.lut, lutBlob + 1, sizeof(WaveformSpec));
  prefs.end();

  // Il nome è un puntatore a letterale: dopo il ripristino da NVS punterebbe
  // a un indirizzo di un'altra build, quindi si riscrive sempre.
  cfg.seq.name = SEQ_DEFAULT.name;
}

static void saveConfig()
{
  prefs.begin("pd097", false);
  prefs.putBool  ("exh",   cfg.exhaustive);
  prefs.putBool  ("verb",  cfg.verbose);
  prefs.putULong ("spihz", cfg.spiHz);
  prefs.putULong ("tmout", cfg.timeoutMs);
  prefs.putBytes ("temp",  cfg.temp, sizeof(cfg.temp));
  prefs.putBytes ("mux3",  cfg.mux,  sizeof(cfg.mux));

  uint8_t blob[1 + sizeof(InitSequence)];
  blob[0] = CFG_BLOB_VERSION;
  memcpy(blob + 1, &cfg.seq, sizeof(InitSequence));
  prefs.putBytes("seq", blob, sizeof(blob));
  uint8_t lutBlob[1 + sizeof(WaveformSpec)];
  lutBlob[0] = CFG_BLOB_VERSION;
  memcpy(lutBlob + 1, &cfg.lut, sizeof(WaveformSpec));
  prefs.putBytes("lut", lutBlob, sizeof(lutBlob));
  prefs.end();
}

// ---------------------------------------------------------------------------
// Sottomenu. Ognuno stampa lo stato corrente e chiede solo i campi che lo
// riguardano; l'uscita salva.
// ---------------------------------------------------------------------------

/** Parametri di misura: gli sweep e i tempi. */
static void menuParameters()
{
  Serial.println(F("\n-- parametri di misura --"));
  for (uint8_t t = 0; t < TEMP_PASSES; ++t)
  {
    char p[40];
    snprintf(p, sizeof(p), "temperatura %u, gradi", t + 1);
    cfg.temp[t] = (int16_t)askNumber(p, TEMP_MIN, TEMP_MAX, cfg.temp[t]);
  }
  for (uint8_t m = 0; m < MUX_PASSES; ++m)
  {
    char p[32];
    snprintf(p, sizeof(p), "MUX %u, gate line", m + 1);
    cfg.mux[m] = (uint16_t)askNumber(p, MUX_MIN, MUX_MAX, cfg.mux[m]);
  }
  cfg.spiHz     = (uint32_t)askNumber("clock SPI, Hz", SPI_HZ_MIN, SPI_HZ_MAX, cfg.spiHz);
  cfg.timeoutMs = (uint32_t)askNumber("timeout di un refresh, ms", 1000, 300000, cfg.timeoutMs);
  saveConfig();
}

/** Sequenza di init editabile, cioè la variante custom-live della sonda i. */
static void menuInitSequence()
{
  InitSequence& s = cfg.seq;
  Serial.println(F("\n-- sequenza di init editabile (variante custom-live) --"));
  s.resetLowMs    = (uint8_t)askNumber("RST basso, ms", 1, 100, s.resetLowMs);
  s.swresetOnBusy = askYesNo("SWRESET atteso sul BUSY", s.swresetOnBusy);
  if (!s.swresetOnBusy)
    s.swresetMs   = (uint8_t)askNumber("delay fisso dopo SWRESET, ms", 1, 200, s.swresetMs);
  s.tempFirst     = askYesNo("0x18 prima di 0x0C", s.tempFirst);
  s.patternInInit = askYesNo("pattern 0x46/0x47 in init", s.patternInInit);
  s.softStart5    = askHex("0x0C quinto byte (40 Level 1, 80 Level 2)", s.softStart5);
  s.muxRegister   = (uint16_t)askNumber("0x01 MUX, valore del registro (gate line = +1)",
                                        MUX_MIN - 1, MUX_MAX - 1, s.muxRegister);
  s.muxB          = askHex("0x01 terzo byte (GD/SM/TB)", s.muxB);
  s.entryMode     = askHex("0x11 entry mode (03 X++Y++, 02 X--, 01 Y--)", s.entryMode);
  s.loadB1        = askYesNo("0x22 = 0xB1 in init", s.loadB1);
  s.armF7         = askYesNo("0x22 = 0xF7 armato senza 0x20", s.armF7);
  s.ctrl21When    = (uint8_t)askNumber("0x21: 0 mai, 1 in init, 2 prima del refresh",
                                       0, 2, s.ctrl21When);
  if (s.ctrl21When)
  {
    s.ctrl21[0] = askHex("0x21 primo byte", s.ctrl21[0]);
    s.ctrl21[1] = askHex("0x21 secondo byte", s.ctrl21[1]);
  }
  s.powerOnFirst  = askYesNo("power on 0x22 = 0xC0 prima del refresh", s.powerOnFirst);
  s.refresh       = askHex("0x22 del refresh", s.refresh);
  s.powerOff      = (int16_t)askNumber("0x22 di power off (-1 nessuno, 195 = C3, 131 = 83)",
                                       -1, 255, s.powerOff);
  saveConfig();
}

/** Waveform editabile, cioè quella che usa la passata libera. */
static void menuWaveform()
{
  WaveformSpec& w = cfg.lut;
  Serial.println(F("\n-- waveform editabile (passata libera) --"));
  w.source      = (uint8_t)askNumber("sorgente: 0 driver, 1 duale, 2 compositore, 3 lut_full 370",
                                     0, 3, w.source);
  w.vsh1        = askHex("VSH1 (23 = 9 V, 41 = 15 V POR e massimo)", w.vsh1);
  if (w.vsh1 > VSH1_15V)
  {
    Serial.println(F("  VSH1 oltre il POR: riportato a 0x41"));
    w.vsh1 = VSH1_15V;
  }
  w.vsh2        = askHex("VSH2 (A8 = 5 V POR)", w.vsh2);
  w.vsl         = askHex("VSL (32 = -15 V POR)", w.vsl);
  w.vgh         = askHex("VGH (00 = 20 V POR)", w.vgh);
  w.writeVcom   = askYesNo("scrivere 0x2C (no = VCOM dell'OTP)", w.writeVcom);
  if (w.writeVcom)
  {
    w.vcom = askHex("VCOM (24 = -0,9 V, 34 = -1,3 V, 44 = -1,7 V massimo)", w.vcom);
    if (w.vcom > VCOM_17V)
    {
      Serial.println(F("  VCOM oltre -1,7 V: riportato a 0x44"));
      w.vcom = VCOM_17V;
    }
  }
  w.resetFrames = (uint16_t)askNumber("frame di reset", 0, 1000, w.resetFrames);
  w.driveFrames = (uint16_t)askNumber("frame di drive", 0, 1000, w.driveFrames);
  w.cycles      = (uint8_t)askNumber("cicli reset/drive", 1, 5, w.cycles);
  w.frCode      = (uint8_t)askNumber("frame rate: 1 = 25 Hz, 2 = 50, 3 = 75, 4 = 100, 5 = 125",
                                     1, 5, w.frCode);
  w.border      = askHex("0x3C border (C0 HiZ, 01 LUT1, 80 VCOM)", w.border);
  saveConfig();
}

/** Riporta tutto ai valori di fabbrica, blob compresi. */
static void resetConfig()
{
  prefs.begin("pd097", false);
  prefs.clear();
  prefs.end();
  cfg = CFG_DEFAULT;
  saveConfig();
}

#endif // PANEL_DIAGNOSTIC_CONFIG_H
