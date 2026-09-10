// =============================================================================
// Report.h — numerazione delle schermate, registro delle misure, log a una riga.
//
// IL LOG È IL PRODOTTO DI QUESTA SUITE: insieme a quello che si vede sul vetro
// deve bastare a correggere src/GxEPD2_SOLUM_097c_960x672.h. Da qui due scelte.
//
// UNA RIGA PER FATTO. Ogni riga ha un tag in colonna fissa, così il log si
// legge a colpo d'occhio e si filtra con grep:
//
//   == 1  bande Mode 1 (1 refresh) ==
//   [12] MEAS  LE 4 BANDE MODE 1            22=F7   24015 ms
//   [12] guarda: 1 bianca, 2 nera, 3 e 4 rosse; linea y=504 visibile?  [o/n]
//   [12] SEEN  o
//   [12] VERDICT durata da refresh pieno
//
// Le motivazioni non stanno sul seriale ma nei commenti e nel README: un run
// completo produce una trentina di refresh, e un paragrafo per sonda rende il
// log illeggibile proprio dove servono i numeri. Il toggle `v` accende le righe
// di dettaglio (byte, microsecondi, salita del BUSY), spento di default.
//
// MISURA E OSSERVAZIONE NELLA STESSA RIGA. waitKey() non aspetta e basta: il
// tasto che l'operatore preme viene registrato accanto alla misura come esito
// visivo, quindi il log si legge da solo senza incrociare appunti presi a mano.
//
// Il numero fra parentesi quadre è quello del riquadro in alto a destra
// dell'area appena ridipinta: è l'unico modo di sapere quale blocco di log
// commenta quello che si sta guardando.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_REPORT_H
#define PANEL_DIAGNOSTIC_REPORT_H

#include <Arduino.h>
#include <stdarg.h>
#include "Config.h"

// ---------------------------------------------------------------------------
// Numerazione delle schermate: un contatore unico per sessione.
//
// Non è l'indice di una enum fissa: le sonde si lanciano in qualsiasi ordine e
// quante volte si vuole, quindi il numero lo assegna chi apre la schermata e
// nella sequenza non restano buchi.
// ---------------------------------------------------------------------------
struct ScreenEntry
{
  uint16_t    number;
  const char* name;
};

static const uint8_t SCREEN_INDEX_MAX = 48;
static ScreenEntry screenIndex[SCREEN_INDEX_MAX];
static uint8_t     screenIndexCount = 0;

static uint16_t screenCounter   = 0;   // ultimo numero assegnato
static uint16_t screenOpen      = 0;   // schermata aperta, 0 = nessuna
static uint16_t screenOnGlass   = 0;   // ultimo riquadro disegnato

/**
 * Apre una schermata: assegna il numero successivo, lo registra nell'indice e
 * lo ritorna. Fino a closeScreen() ogni refresh disegna quel numero sul vetro e
 * ogni riga di log lo porta in testa.
 */
static uint16_t openScreen(const char* name)
{
  ++screenCounter;
  screenOpen = screenCounter;
  if (screenIndexCount < SCREEN_INDEX_MAX)
  {
    screenIndex[screenIndexCount].number = screenCounter;
    screenIndex[screenIndexCount].name   = name;
    ++screenIndexCount;
  }
  return screenCounter;
}

static void closeScreen() { screenOpen = 0; }

/**
 * Sospende e ripristina il riquadro senza chiudere la schermata. Serve alle
 * passate che confrontano i due piani bit per bit: un riquadro li renderebbe
 * diversi e falserebbe proprio la misura che quelle passate esistono per fare.
 */
static uint16_t suspendBadge() { const uint16_t s = screenOpen; screenOpen = 0; return s; }
static void resumeBadge(uint16_t s) { screenOpen = s; }

/**
 * Numero in testa alle righe: la schermata aperta se c'è, altrimenti l'ultimo
 * riquadro disegnato, cioè quello che in questo momento sta sul vetro.
 */
static uint16_t currentScreen()
{
  return screenOpen ? screenOpen : screenOnGlass;
}

// ---------------------------------------------------------------------------
// Registro delle misure: sostituisce le variabili globali di risultato.
//
// Una sonda che dipende da un'altra (il refresh pieno di riferimento, il
// differenziale vivo) non legge una variabile ma interroga il registro, quindi
// le sonde restano indipendenti e si possono lanciare in qualunque ordine.
// ---------------------------------------------------------------------------
struct Measurement
{
  char        probeKey;  // tasto della sonda che l'ha prodotta
  const char* caption;   // la stessa stringa scritta sul vetro
  uint8_t     seq22;     // parametro di 0x22, 0 se la misura non è un refresh
  int32_t     ms;        // BUSY in ms, negativo se non conclusa
  uint16_t    screen;    // riquadro sul vetro, 0 se nessuno
  char        seen;      // esito visivo registrato da waitKey()
};

static const uint8_t MEASUREMENT_MAX = 96;
static Measurement measurements[MEASUREMENT_MAX];
static uint8_t     measurementCount = 0;   // satura: le prime 64 sono le sole tenute

// Sonda in esecuzione, per etichettare le misure senza passarlo a ogni
// chiamata. È il TASTO e non un puntatore a stringa: una stringa composta a
// runtime vivrebbe in un buffer riusato dalla sonda successiva, e tutte le
// righe del registro finirebbero etichettate con l'ultima eseguita.
static char activeProbeKey = '-';

/**
 * Passate ripetute: le alternanze di una catena e i cicli nero/bianco della
 * taratura fanno decine di refresh identici, e registrarli tutti riempirebbe il
 * registro con righe che nessuno legge. Chi le esegue alza questo flag e stampa
 * una riga sola col totale; l'ultima passata di ogni gruppo va misurata normale.
 */
static bool quietMeasure = false;

/** Refresh non avviato, perchè il controller era occupato. */
static const int32_t REFRESH_NOT_RUN = -2;

// ---------------------------------------------------------------------------
// Righe di log.
// ---------------------------------------------------------------------------

/** Intestazione di una sonda: la riga che apre un blocco. */
static void logProbeHeader(char key, const char* name, uint8_t refreshes)
{
  Serial.println();
  Serial.printf("== %c  %s (%u refresh) ==\n", key, name, (unsigned)refreshes);
}

/** Riga normale, col numero della schermata in testa. */
static void logLine(const char* fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%u] %s\n", (unsigned)currentScreen(), buf);
}

/** Riga di dettaglio: stampata solo con il toggle `v` acceso. */
static void logDetail(const char* fmt, ...)
{
  if (!cfg.verbose) return;
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%u]  .   %s\n", (unsigned)currentScreen(), buf);
}

/**
 * Riga di misura e registrazione. caption è LA STESSA STRINGA che la passata
 * ha scritto sul vetro: chi legge il pannello la ritrova nel log e viceversa.
 * seq22 a 0 per una misura che non è un refresh (power on, carico LUT, pattern).
 */
static void measure(const char* caption, uint8_t seq22, int32_t ms)
{
  if (quietMeasure) return;
  if (seq22)
    Serial.printf("[%u] MEAS  %-34s 22=%02X %7ld ms\n",
                  (unsigned)currentScreen(), caption, seq22, (long)ms);
  else
    Serial.printf("[%u] MEAS  %-34s        %7ld ms\n",
                  (unsigned)currentScreen(), caption, (long)ms);

  if (measurementCount < MEASUREMENT_MAX)
  {
    Measurement& m = measurements[measurementCount++];
    m.probeKey = activeProbeKey;
    m.caption = caption;
    m.seq22   = seq22;
    m.ms      = ms;
    m.screen  = currentScreen();
    m.seen    = ' ';
  }
}

/** Riga di verdetto: una conclusione che una durata decide da sè. */
static void verdict(const char* fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%u] VERDICT %s\n", (unsigned)currentScreen(), buf);
}

/** Cosa guardare sul vetro: una riga, al massimo due per schermata. */
static void look(const char* text)
{
  Serial.printf("[%u] guarda: %s\n", (unsigned)currentScreen(), text);
}

/**
 * Attende un tasto e registra la risposta come esito visivo dell'ultima misura.
 * Non ha timeout: la suite non prosegue finchè l'operatore non ha guardato.
 * `o` e `n` sono la risposta attesa a una domanda binaria, una cifra serve a
 * "quale banda ha vinto", qualunque altro tasto vale "visto, senza giudizio".
 */
static char waitKey()
{
  flushInput();
  Serial.printf("[%u] tasto...\n", (unsigned)currentScreen());
  const char c = waitAnyKey();
  if (measurementCount > 0) measurements[measurementCount - 1].seen = c;
  Serial.printf("[%u] SEEN  %c\n", (unsigned)currentScreen(), c);
  return c;
}

// ---------------------------------------------------------------------------
// Interrogazione del registro: come una sonda legge l'esito di un'altra.
// ---------------------------------------------------------------------------

/** Ultima durata misurata con quel parametro di 0x22, -1 se non c'è. */
static int32_t lastMeasurement(uint8_t seq22)
{
  for (int8_t i = (int8_t)measurementCount - 1; i >= 0; --i)
    if (measurements[i].seq22 == seq22 && measurements[i].ms > 0)
      return measurements[i].ms;
  return -1;
}

/** Durata del refresh pieno di riferimento: il metro di tutte le altre. */
static int32_t fullRefreshReference()
{
  return lastMeasurement(0xF7);
}

/** Stampa il registro: è la prima metà del riepilogo. */
static void printMeasurements()
{
  Serial.println(F("\n-- misure --"));
  if (measurementCount == 0) { Serial.println(F("  nessuna")); return; }
  Serial.println(F("  sonda  0x22      ms  vis  descrizione"));
  for (uint8_t i = 0; i < measurementCount; ++i)
  {
    const Measurement& m = measurements[i];
    Serial.printf("  %c       ", m.probeKey);
    if (m.seq22) Serial.printf("%02X  ", m.seq22); else Serial.print(F("--  "));
    if (m.ms < 0) Serial.print(F("      -"));
    else          Serial.printf("%7ld", (long)m.ms);
    Serial.printf("   %c   %s", m.seen, m.caption);
    if (m.screen) Serial.printf("  [%u]", (unsigned)m.screen);
    Serial.println();
  }
}

/** Indice delle schermate prodotte: lega i numeri sul vetro ai nomi. */
static void printScreenIndex()
{
  Serial.println(F("\n-- schermate --"));
  for (uint8_t i = 0; i < screenIndexCount; ++i)
    Serial.printf("  [%u] %s\n", (unsigned)screenIndex[i].number, screenIndex[i].name);
  if (screenIndexCount == 0) Serial.println(F("  nessuna"));
}

#endif // PANEL_DIAGNOSTIC_REPORT_H
