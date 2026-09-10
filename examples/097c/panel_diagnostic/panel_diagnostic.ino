// =============================================================================
// panel_diagnostic — suite di reverse engineering del SOLUM ESL 9.7"
// (672x960 nativi, pilotato landscape 960x672, controller SSD1677).
//
// A COSA SERVE. Il suo prodotto è un LOG che, insieme alle schermate viste sul
// vetro, basta a correggere il driver custom src/GxEPD2_SOLUM_097c_960x672.h.
// Due domande sono aperte e le chiude questa suite:
//   - da quale dei nove driver SSD1677 di GxEPD2 il driver dovrebbe derivare:
//     oggi viene da GDEM133Z91 e la scelta non è mai stata verificata;
//   - se il suo partial refresh funziona davvero, e con quali sequenze.
// Il driver può fare override dei virtual di GxEPD2_EPD e ridefinire i propri
// metodi interni: il blocco DRV del riepilogo dice, voce per voce, quale
// metodo rivedere e con quale valore.
//
// COME PARLA AL PANNELLO. Sempre a SPI diretta, mai attraverso GxEPD2: il
// driver è l'oggetto della misura, non lo strumento, e passare da lui
// farebbe ereditare a ogni misura le sue assunzioni. Del driver si
// riproducono le SEQUENZE, comando per comando, e Controller.h dice quale
// metodo ciascuna primitiva rispecchia. L'unica cosa che arriva dalla libreria
// è la costante della waveform del partial, perchè la taratura deve
// confrontare i byte che andrebbero in produzione.
//
// UNA SOLA COMPILAZIONE. Nessun #define cambia il comportamento: sonde,
// profondità, verbosità, parametri di misura, sequenza di init e waveform si
// scelgono dal menu e restano in NVS. La passata libera esegue una
// combinazione arbitraria, quindi provare un'idea nuova non costa un riflash.
//
// COME SI LEGGE IL LOG. Una riga per fatto, tag in colonna fissa, e fra
// parentesi quadre il numero del riquadro in alto a destra dell'area appena
// ridipinta:
//
//   == 1  4 bande Mode 1 (1 refresh) ==
//   [12] MEAS  LE 4 BANDE MODE 1            22=F7   24015 ms
//   [12] guarda: 1 bianca, 2 nera, 3 e 4 rosse: la linea y=504 si vede? [o/n]
//   [12] SEEN  o
//
// MEAS è una misura, SEEN la risposta dell'operatore, VERDICT una conclusione
// che una durata decide da sè, DRV una voce per il driver. Il tasto v accende
// le righe di dettaglio, spento di default. Le motivazioni non stanno sul
// seriale ma nei commenti dei file e nel README.
//
// COSTO. Un refresh pieno su questo pannello dura ~24 s: il numero fra
// parentesi nel menu è quello che una sonda ne fa. Nessuna sonda avanza da
// sola, e il pannello va guardato prima che la schermata dopo lo sovrascriva.
//
// Hardware: Waveshare E-Paper ESP32 Driver Board V3 (CS 15, DC 27, RST 26,
// BUSY 25, SCK 13, MOSI 14, MISO 12 fittizio). Lo switch n.1 sceglie la
// resistenza di sense del booster: se i colori escono deboli, va provato
// prima di dare la colpa alla waveform.
//
// Author: Mattia Alesi
// =============================================================================

#include "Config.h"
#include "Report.h"
#include "Graphics.h"
#include "Controller.h"
#include "ProbesBase.h"
#include "ProbesUpstream.h"
#include "ProbesWaveform.h"

// ---------------------------------------------------------------------------
// Le sonde. Menu, comando "tutte" e riepilogo derivano da questa tabella:
// aggiungerne una costa una riga e una funzione.
// ---------------------------------------------------------------------------
struct Probe
{
  char        key;
  const char* name;
  uint8_t     refreshes;   // refresh pieni da ~24 s, nel caso non esaustivo
  void      (*run)();
};

static constexpr Probe PROBES[] =
{
  { 'b', "bus e controller",        0, probeBus },
  { '1', "4 bande Mode 1",          1, probeBandsMode1 },
  { '2', "4 bande Mode 2",          1, probeBandsMode2 },
  { '3', "livelli di sorgente",     2, probeSourceLevels },
  { '4', "sequenze di 0x22",        3, probeUpdateSequences },
  { '5', "finestra RAM e trappola", 3, probeWindowTrap },
  { '6', "banchi per temperatura",  4, probeTemperatureBanks },
  { '7', "MUX ridotto",             3, probeMux },
  { 'i', "sequenze di partenza",    7, probeInitSequences },
  { 'w', "waveform piena 370",      3, probeFullWaveform370 },
  { '8', "partial LUT senza 0x04",  2, probePartialLutRaw },
  { 'p', "catena di partial",       3, probePartialChain },
  { 't', "taratura waveform",       4, probeTuning },
  { '0', "deep sleep",              4, probeDeepSleep },
  { 'm', "passata libera",          1, probeFreePass },
};
static const uint8_t PROBE_COUNT = sizeof(PROBES) / sizeof(PROBES[0]);

/** Tasti unici: due sonde sullo stesso tasto renderebbero la seconda
 *  irraggiungibile, e non si vedrebbe leggendo la tabella. */
static constexpr bool probeKeysUnique(const Probe* p, uint8_t n)
{
  for (uint8_t i = 0; i < n; ++i)
    for (uint8_t j = (uint8_t)(i + 1); j < n; ++j)
      if (p[i].key == p[j].key) return false;
  return true;
}
static_assert(probeKeysUnique(PROBES, PROBE_COUNT), "due sonde hanno lo stesso tasto");

// ---------------------------------------------------------------------------
// Riepilogo per il driver.
//
// Ogni riga è DRV <voce> <valore> <- <sonda> [<schermata>], e punta al metodo
// da rivedere in GxEPD2_SOLUM_097c_960x672.h. Il README spiega voce per voce
// come si traduce in codice.
// ---------------------------------------------------------------------------

/** Prima misura la cui descrizione comincia col prefisso, nullptr se manca. */
static const Measurement* findMeasurement(const char* prefix)
{
  const uint8_t n = (uint8_t)strlen(prefix);
  for (uint8_t i = 0; i < measurementCount; ++i)
    if (strncmp(measurements[i].caption, prefix, n) == 0) return &measurements[i];
  return nullptr;
}

/** Stampa una voce del blocco, con la sonda e la schermata da cui viene. */
static void drvLine(const char* item, const char* value, const Measurement* m)
{
  Serial.printf("DRV  %-26s %-28s", item, value);
  if (m) Serial.printf(" <- sonda %c [%u]", m->probeKey, (unsigned)m->screen);
  Serial.println();
}

static void printDriverSheet()
{
  Serial.println(F("\n-- per il driver --"));
  char v[40];

  const Measurement* full = findMeasurement("LE 4 BANDE MODE 1");
  if (full && full->ms > 0)
  {
    snprintf(v, sizeof(v), "%ld ms", (long)full->ms);
    drvLine("_Update_Full 0xF7", v, full);
  }
  // Il banco più lento misurato è quello che _busy_timeout deve coprire.
  int32_t slowest = 0;
  const Measurement* slowestM = nullptr;
  for (uint8_t i = 0; i < measurementCount; ++i)
    if (measurements[i].ms > slowest) { slowest = measurements[i].ms; slowestM = &measurements[i]; }
  if (slowestM)
  {
    snprintf(v, sizeof(v), "%ld ms, timeout > %ld ms", (long)slowest, (long)(slowest * 2));
    drvLine("_busy_timeout", v, slowestM);
  }

  const Measurement* on  = findMeasurement("POWER ON");
  const Measurement* off = findMeasurement("POWER OFF 0x22=C3");
  if (on)  { snprintf(v, sizeof(v), "%ld ms", (long)on->ms);  drvLine("power_on_time", v, on); }
  if (off) { snprintf(v, sizeof(v), "%ld ms", (long)off->ms); drvLine("power_off_time", v, off); }

  const Measurement* part = findMeasurement("PARTIAL 1");
  if (part) { snprintf(v, sizeof(v), "%ld ms senza 0x04", (long)part->ms);
              drvLine("_Init_Part senza 0x04", v, part); }
  const Measurement* chain = findMeasurement("CATENA FASCIA A NERA");
  if (chain) { snprintf(v, sizeof(v), "%ld ms con 0x04", (long)chain->ms);
               drvLine("_Update_Part 0xCC", v, chain); }
  const Measurement* clean = findMeasurement("USCITA SENZA PULIRE");
  if (clean) drvLine("_cleanColorIfPrevious", clean->seen == 'o' ? "serve: senza esce rosso"
                                                                 : "da rivedere", clean);
  const Measurement* fast = findMeasurement("WAVEFORM 370 AREA A NERO");
  if (fast && fast->ms > 0)
  {
    snprintf(v, sizeof(v), "%ld ms, waveform dall'MCU", (long)fast->ms);
    drvLine("_Init_Full alla 370", v, fast);
  }

  // Fra le sequenze di init, quelle che l'operatore ha promosso.
  Serial.println(F("DRV  _InitDisplay: varianti promosse dall'occhio"));
  for (uint8_t i = 0; i < measurementCount; ++i)
    for (uint8_t k = 0; k < INIT_SEQUENCE_COUNT; ++k)
      if (strcmp(measurements[i].caption, INIT_SEQUENCES[k].name) == 0)
        Serial.printf("DRV    %-16s %6ld ms  visto %c  [%u]\n",
                      INIT_SEQUENCES[k].name, (long)measurements[i].ms,
                      measurements[i].seen, (unsigned)measurements[i].screen);

  if (sleepParamFound)
  {
    snprintf(v, sizeof(v), "0x10 = 0x%02X", sleepParamFound);
    drvLine("hibernate", v, nullptr);
  }

  Serial.printf("DRV  cfg.seq  %s reset %u ms, 0x0C[4] %02X, MUX %u/%02X, 0x11 %02X,"
                " 0x21 %02X%02X@%u, 0x22 %02X, off %d\n",
                cfg.seq.name, cfg.seq.resetLowMs, cfg.seq.softStart5, cfg.seq.muxRegister,
                cfg.seq.muxB, cfg.seq.entryMode, cfg.seq.ctrl21[0], cfg.seq.ctrl21[1],
                cfg.seq.ctrl21When, cfg.seq.refresh, cfg.seq.powerOff);
  Serial.printf("DRV  cfg.lut  sorgente %u, VSH1 %02X VSH2 %02X VSL %02X VGH %02X"
                " VCOM %02X%s, %u+%u frame x%u, FR %u, border %02X\n",
                cfg.lut.source, cfg.lut.vsh1, cfg.lut.vsh2, cfg.lut.vsl, cfg.lut.vgh,
                cfg.lut.vcom, cfg.lut.writeVcom ? "" : " (non inviato)",
                cfg.lut.resetFrames, cfg.lut.driveFrames, cfg.lut.cycles,
                cfg.lut.frCode, cfg.lut.border);
}

/** Totali del bus: quanto di un aggiornamento è SPI e quanto è attesa. */
static void printBusTotals()
{
  Serial.println(F("\n-- bus --"));
  if (totalSpiBytes > 0 && totalSpiMicros > 0)
    Serial.printf("  %lu byte in %lu us -> %.2f us/byte\n",
                  (unsigned long)totalSpiBytes, (unsigned long)totalSpiMicros,
                  (double)totalSpiMicros / (double)totalSpiBytes);
  Serial.printf("  %lu comandi, %lu parametri, %lu ms di attesa BUSY, heap %lu\n",
                (unsigned long)totalCommands, (unsigned long)totalParamBytes,
                (unsigned long)totalBusyMillis, (unsigned long)ESP.getFreeHeap());
}

static void printSummary()
{
  printMeasurements();
  printScreenIndex();
  printBusTotals();
  printDriverSheet();
}

// ---------------------------------------------------------------------------
// Menu.
// ---------------------------------------------------------------------------
static void printMenu()
{
  Serial.println(F("\n-- sonde, fra parentesi i refresh da 24 s --"));
  for (uint8_t i = 0; i < PROBE_COUNT; ++i)
  {
    Serial.printf(" %c %-24s (%u)", PROBES[i].key, PROBES[i].name, PROBES[i].refreshes);
    if ((i % 3) == 2 || i == PROBE_COUNT - 1) Serial.println();
  }
  Serial.printf("-- sessione:  a tutte   s riepilogo   c parametri   e init   l waveform"
                "   x esaustivo=%s   v dettagli=%s   h menu\n",
                cfg.exhaustive ? "si" : "no", cfg.verbose ? "si" : "no");
}

/** Esegue una sonda: intestazione, esecuzione, etichetta nel registro. */
static void runProbe(const Probe& p)
{
  activeProbeKey = p.key;
  logProbeHeader(p.key, p.name, p.refreshes);
  p.run();
  activeProbeKey = '-';
}

/** Tutte le sonde in sequenza, tranne la passata libera, che è interattiva. */
static void runAllProbes()
{
  for (uint8_t i = 0; i < PROBE_COUNT; ++i)
    if (PROBES[i].key != 'm') runProbe(PROBES[i]);
  printSummary();
}

static void handleKey(char c)
{
  for (uint8_t i = 0; i < PROBE_COUNT; ++i)
    if (PROBES[i].key == c) { runProbe(PROBES[i]); printMenu(); return; }

  switch (c)
  {
    case 'a': runAllProbes(); break;
    case 's': printSummary(); break;
    case 'c': menuParameters(); break;
    case 'e': menuInitSequence(); break;
    case 'l': menuWaveform(); break;
    case 'x': cfg.exhaustive = !cfg.exhaustive; saveConfig(); break;
    case 'v': cfg.verbose = !cfg.verbose; saveConfig(); break;
    case 'r': resetConfig(); Serial.println(F("  profilo riportato ai valori di fabbrica")); break;
    case 'h': break;
    default:  Serial.printf("\ntasto '%c' sconosciuto\n", c); break;
  }
  printMenu();
}

void setup()
{
  Serial.begin(115200);
  delay(1500);
  loadConfig();
  controllerBegin();

  Serial.println(F("\n=== panel_diagnostic: SOLUM ESL 9.7 pollici, SSD1677, SPI diretta ==="));
  Serial.printf("pannello %u x %u, %lu byte per piano, clock %lu Hz, timeout %lu ms\n",
                (unsigned)SRC, (unsigned)GATE, (unsigned long)PLANE_BYTES,
                (unsigned long)cfg.spiHz, (unsigned long)cfg.timeoutMs);
  Serial.println(F("il numero fra parentesi quadre e' il riquadro in alto a destra"
                   " dell'area ridipinta"));
  printMenu();
}

void loop()
{
  if (!Serial.available()) { delay(20); return; }
  const char c = (char)Serial.read();
  if (c == '\r' || c == '\n' || c == ' ') return;
  flushInput();
  handleKey(c);
}
