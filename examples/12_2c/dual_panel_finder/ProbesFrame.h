// =============================================================================
// ProbesFrame.h — le sonde che dipingono un frame e lo fanno guardare
//
// Ogni sonda finisce in una riga che nomina il metodo di
// src/GxEPD2_SOLUM_122c_960x768.h da cambiare: il log e quello che si vede sul
// vetro devono bastare a correggere il driver, e una sonda che non produce una
// riga di quel tipo sta spendendo refresh senza rispondere a niente.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_PROBES_FRAME_H
#define DUAL_PANEL_FINDER_PROBES_FRAME_H

#include <Arduino.h>
#include "Config.h"
#include "Report.h"
#include "Graphics.h"
#include "Controller.h"
static int32_t showBandsAndBoxesFrame()
{
  Serial.println(F("\n--- frame: 4 bande dei piani + 4 box a finestra parziale ---"));
  /**
   * Le etichette delle bande nominano il valore del BIT, non il colore, e la
   * traduzione fra i due la dà probeBwPolarity(), che gira prima di questo
   * frame. Va detto qui perchè il frame gira dopo initPanel, e la candidata
   * CAND_SOLUM scrive 0x21 = 0x08 0x00: se quel valore inverte il piano BW su
   * questo silicio, le bande 1 e 2 escono scambiate rispetto alla Table 6-4,
   * e con loro la 3 e la 4.
   */
  logLine("polarità del piano BW: %s", polaritaLabel());
  if (cfg.bwPolarity == BW_UNKNOWN)
    logLine("(la sonda della polarità non l'ha determinata: le etichette BW = 1 e BW = 0 seguono la convenzione del datasheet)");
  const uint32_t t0 = millis();
  writePlane(0x24, cfg.gate, composeRowBandsBW);
  writePlane(0x26, cfg.gate, composeRowBandsRED);

  /**
   * I box vanno sopra le bande, non in un frame a parte: sono due domande
   * indipendenti — che colore rendono le quattro combinazioni dei piani, e se
   * l'addressing con x diverso da zero funziona — e su un pannello dove ogni
   * refresh costa venti secondi e una pausa di osservazione, tenerle separate
   * era un frame e uno sguardo buttati.
   *
   * Tre box neri equispaziati dentro la banda 1, che è bianca: se escono
   * allineati ed equidistanti la finestra parziale in X funziona, e con essa
   * ogni writeImagePart del driver. Un quarto box accent dentro la banda 2, che
   * è nera: prova la stessa finestra sul piano 0x26.
   */
  const uint16_t bandH = cfg.gate / 4;
  writeBoxConst(0x24, 0,   8, 64, 64, 0x00);
  writeBoxConst(0x24, 448, 8, 64, 64, 0x00);
  writeBoxConst(0x24, 896, 8, 64, 64, 0x00);
  writeBoxConst(0x26, 224, bandH + 16, 64, 64, 0xFF);
  logLine("bande + 4 box scritti in %lu ms", (unsigned long)(millis() - t0));

  /**
   * Finestra piena prima del refresh: writeBoxConst la lascia sull'ultimo box,
   * e con quella in vigore il refresh coprirebbe 64x64 invece della banda.
   */
  setRamWindow(0, 0, SRC, cfg.gate);
  const Frame f = frameCon("4 bande dei piani più i box a finestra parziale", D_QUARTO);
  return runRefresh(0xF7, "una ventina di secondi", 0, &f);
}
// --- refresh parziale d'area -----------------------------------------

// Durate della sonda, -1 se la passata non si è conclusa o non è stata fatta
static int32_t areaMsFirst = -1;   // prima passata 0xFC, finestra alta
static int32_t areaMsThin  = -1;   // passata 0xFC su 24 righe
static int32_t areaMsMode1 = -1;   // passata 0xF4, Mode 1 su finestra

/**
 * Mappa verticale della sonda del partial d'area, espressa in frazioni della
 * banda, così restano disgiunte qualunque sia il conteggio gate reale. A sonda
 * finita si leggono tutte insieme sullo stesso schermo.
 *
 * Il riquadro è l'unica finestra ristretta anche lungo X. x e w multipli di 8:
 * sull'asse source la RAM è organizzata a byte.
 */
static uint16_t AREA_BAND   = cfg.gate;
static const uint16_t AREA_P1_Y   = 0;                    // passata 1, poi la 4
static uint16_t AREA_P1_H   = AREA_BAND / 6;
static uint16_t AREA_TRAP_Y = AREA_P1_H + 16;       // fascia di trappola
static uint16_t AREA_TRAP_H = AREA_BAND / 12;
static uint16_t AREA_THIN_Y = AREA_BAND / 3;        // finestra sottile
static const uint16_t AREA_THIN_H = 24;
static uint16_t AREA_M1_Y   = AREA_BAND / 3 + 40;   // passata Mode 1
static uint16_t AREA_M1_H   = AREA_BAND / 12;
static uint16_t AREA_P2_Y   = AREA_BAND / 2 + 32;   // passata 2
static uint16_t AREA_P2_H   = AREA_BAND / 6;
static const uint16_t AREA_BOX_X  = 448;                  // riquadro, ristretto in X
static const uint16_t AREA_BOX_W  = 128;
static uint16_t AREA_BOX_Y  = AREA_BAND * 3 / 4 + 32;
static uint16_t AREA_BOX_H  = AREA_BAND / 8;

// Banda minima perchè le fasce della mappa restino disgiunte
static const uint16_t AREA_BAND_MIN = 320;

// Valore che significa "fascia uniforme, senza numero"
static const uint16_t AREA_NO_DIGIT = STRIPE_SENZA_NUMERO;


/**
 * Esito di una passata della sonda d'area, tenuto per il riepilogo: il
 * confronto fra passate serve più del valore singolo.
 */
struct AreaPass
{
  const char* label;
  uint16_t x, y, w, h;
  uint8_t  sequence;   // parametro di 0x22 usato per aggiornare
  uint32_t pushMs;     // scrittura della finestra su 0x24
  int32_t  ms;         // BUSY del refresh, -1 se non conclusa
};

static AreaPass areaPasses[6];
static uint8_t  areaPassCount = 0;

/**
 * Una passata della sonda d'area: scrive la finestra su 0x24, aggiorna con la
 * sequenza indicata e poi riallinea la stessa finestra su 0x26, perchè la
 * passata successiva trovi come frame precedente quello che il pannello sta
 * davvero mostrando. È lo stesso schema di writeImagePartToPrevious del driver
 * monocromatico GxEPD2_1330_GDEM133T91, che gira sullo stesso silicio.
 *
 * La finestra viene reimpostata subito prima della master activation: sono i
 * registri 0x44/0x45 presenti in quel momento a definire l'area che il refresh
 * percorre, ed è esattamente il punto in prova.
 *
 * Con digit sotto 10 la fascia porta la cifra e deve essere a larghezza piena;
 * con AREA_NO_DIGIT esce uniforme e può essere ristretta anche in X. Ritorna i
 * ms del refresh, -1 se non si è concluso.
 */
static int32_t areaPass(const char* label, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint8_t value, uint16_t numero_ignorato, uint8_t fg,
                        uint8_t updateSequence, uint32_t timeout_ms)
{
  const bool withDigit = (numero_ignorato != AREA_NO_DIGIT) && (w == SRC);

  logLine("-- %s: finestra x=%u..%u y=%u..%u, %u righe, %lu byte",
          label, (unsigned)x, (unsigned)(x + w - 1),
          (unsigned)y, (unsigned)(y + h - 1), (unsigned)h,
          (unsigned long)((uint32_t)(w / 8) * h));

  /**
   * Ogni passata d'area è un frame proprio: sono l'una il controllo dell'altra,
   * e vederne solo l'ultima non dice niente. Il numero si PRENOTA qui, prima di
   * dipingere, perchè la fascia lo porta scritto dentro: senza questo sul vetro
   * ci sarebbero due numeri diversi e nel log uno solo.
   */
  const Frame f = frameCon(label, D_DIPINTO);
  const uint8_t numero = prenotaNumero(f);

  const uint32_t t0 = millis();
  if (withDigit)
    writeStripeWithDigit(0x24, y, h, value, numero, fg);
  else
    writeBoxConst(0x24, x, y, w, h, value);
  const uint32_t pushMs = millis() - t0;
  logLine("0x24 scritto in %lu ms", (unsigned long)pushMs);

  setRamWindow(x, y, w, h);
  const char* attesa = (updateSequence == 0xF4)
                       ? "Mode 1 su finestra: fra meno di un secondo e una ventina, si misura"
                       : "atteso sotto il secondo se finestra e banco differenziale valgono";
  const int32_t ms = runRefresh(updateSequence, attesa, timeout_ms, &f);

  // frame precedente allineato a quello che si vede adesso, nella stessa finestra
  if (withDigit)
    writeStripeWithDigit(0x26, y, h, value, numero, fg);
  else
    writeBoxConst(0x26, x, y, w, h, value);

  if (areaPassCount < (uint8_t)(sizeof(areaPasses) / sizeof(areaPasses[0])))
  {
    AreaPass& p = areaPasses[areaPassCount++];
    p.label = label;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.sequence = updateSequence;
    p.pushMs = pushMs;
    p.ms = ms;
  }
  return ms;
}

/**
 * Riepilogo della sonda d'area: la tabella delle passate e le letture che
 * contano, cioè se la durata scala con l'altezza della finestra e quanto
 * costerebbe un aggiornamento B/N fatto per finestre su questa banda.
 */
static void reportAreaPasses()
{
  Serial.println(F("\nesito della sonda del partial d'area:"));
  if (areaPassCount == 0)
  {
    Serial.println(F("  nessuna passata eseguita"));
    return;
  }

  Serial.println(F("  finestra                    righe  0x22   push   refresh"));
  for (uint8_t k = 0; k < areaPassCount; ++k)
  {
    const AreaPass& p = areaPasses[k];
    Serial.printf("  x=%3u..%3u y=%3u..%3u    %5u  0x%02X  %4lu ms  ",
                  (unsigned)p.x, (unsigned)(p.x + p.w - 1),
                  (unsigned)p.y, (unsigned)(p.y + p.h - 1), (unsigned)p.h,
                  p.sequence, (unsigned long)p.pushMs);
    if (p.ms < 0)
      Serial.println(F("non conclusa"));
    else
      logLine("%ld ms", (long)p.ms);
  }

  /**
   * Scala con l'altezza. Se la durata è proporzionale alle gate line, al
   * driver conviene passare la finestra minima che contiene il disegno
   * cambiato; se è costante, il costo è tutto della waveform e restringere la
   * finestra fa risparmiare solo push SPI.
   */
  if (areaMsFirst > 0 && areaMsThin > 0)
  {
    Serial.printf("  %u righe %ld ms contro %u righe %ld ms: tempi 1:%.2f, righe 1:%.2f\n",
                  (unsigned)AREA_P1_H, (long)areaMsFirst,
                  (unsigned)AREA_THIN_H, (long)areaMsThin,
                  (double)areaMsFirst / (double)areaMsThin,
                  (double)AREA_P1_H / (double)AREA_THIN_H);
    if ((double)areaMsFirst / (double)areaMsThin > 1.5)
      Serial.println(F("  la durata scala con le gate line coinvolte"));
    else
      Serial.println(F("  la durata non dipende dall'altezza: il costo è della waveform"));
  }

  if (areaMsFirst > 0 && refreshMs > 0)
    Serial.printf("  passata d'area %ld ms contro %ld ms del refresh pieno: %.1fx\n",
                  (long)areaMsFirst, (long)refreshMs,
                  (double)refreshMs / (double)areaMsFirst);

  /** Il valore si consiglia solo se la passata d'area è davvero più corta di un
   *  frame intero. Se costa uguale non c'è niente da tarare, e proporre il
   *  doppio del misurato — che starebbe anche sopra il busy_timeout del driver —
   *  sarebbe un consiglio sbagliato. */
  const bool partialConviene = (areaMsFirst > 0) && (refreshMs > 0) &&
                               (areaMsFirst * 2 < refreshMs);
  if (partialConviene)
    Serial.printf("  partial_refresh_time da mettere nel driver: %ld ms con margine\n",
                  (long)(areaMsFirst * 2 + 200));
  else if (areaMsFirst > 0)
    verdict("la passata d'area costa come un frame intero: partial_refresh_time resta pari al pieno, hasFastPartialUpdate falso");

  if (areaMsMode1 > 0)
  {
    Serial.printf("  Mode 1 su finestra (0x22 = 0xF4) %ld ms su %u righe",
                  (long)areaMsMode1, (unsigned)AREA_M1_H);
    if (refreshMs > 0)
      Serial.printf(", contro %ld ms del pieno", (long)refreshMs);
    Serial.println();
    if (refreshMs > 0 && areaMsMode1 * 2 < refreshMs)
      logLine("Mode 1 si accorcia con la finestra: un refresh d'area costa meno di uno pieno e non consuma 0x26");
    else if (refreshMs > 0)
      Serial.println(F("  Mode 1 dura come il pieno: la finestra non accorcia la waveform"));
  }

  logLine("le passate 0xFC valgono per frame in bianco e nero: lì 0x26 fa da frame precedente, quindi in quella modalità l'accent non esiste");
  if (partialConviene)
    logLine("su due controller una finestra dentro una banda impegna un solo chip: l'altro non va svegliato");
}

/**
 * Sonda del partial d'area: se restringendo la finestra RAM il controller
 * aggiorna davvero solo quella porzione della banda, e a che prezzo in tempo.
 *
 * showPartialWindowFrame() verifica l'addressing in SCRITTURA, cioè dove
 * finiscono i byte; qui il REFRESH, cioè quali gate line vengono scandite alla
 * master activation. Sul fratello monocromatico dello stesso silicio,
 * GxEPD2_1330_GDEM133T91, refresh(x,y,w,h) fa _setPartialRamArea più 0x22 =
 * 0xFC, oppure 0xF4 se il banco differenziale non c'è; il driver 122c manda
 * sempre _Update_Full a finestra piena, quindi qui non c'è niente di già
 * verificato sul campo.
 *
 * Il contenuto finale non distingue una finestra rispettata da una ignorata: la
 * RAM accumula le scritture e le due ipotesi danno la stessa immagine. Serve
 * una discordanza voluta, ed è la fascia di trappola, nera in 0x24 e mai
 * compresa in una finestra di refresh.
 *
 * Le passate sono disgiunte lungo Y, così a sonda finita si leggono insieme:
 *   fascia 1   passata 1, nera col numero del suo frame; in esaustivo la
 *              passata 4 la riporta a bianca col numero del proprio: dice se
 *              una catena di partial sulla stessa area regge
 *   fascia 2   trappola, scritta in RAM e mai refreshata
 *   fascia 3   finestra sottile di 24 righe: dice se la durata scala con
 *              l'altezza o è tutta della waveform
 *   fascia 4   passata Mode 1 (0x22 = 0xF4), la strada che resta se 0xFC non va
 *   fascia 5   in esaustivo, nera col proprio numero e bordo 0x3C = 0x80
 *   riquadro   ristretto anche in X, x = 448..575
 *
 * Il gate fra la passata 3 e quella Mode 1 non è decorativo: la fascia di
 * trappola va guardata PRIMA che la Mode 1 parta, perchè se comparisse dopo non
 * si saprebbe più quale delle due passate l'ha fatta comparire.
 *
 * Prezzo accettato in partenza: nelle passate 0xFC la 0x26 fa da frame
 * precedente e non da accent, quindi quella misura vale per un pannello bianco
 * e nero.
 */
static void probePartialProbe()
{
  /**
   * Con una banda troppo bassa le fasce della mappa si sovrapporrebbero e il
   * risultato non sarebbe leggibile: meglio non misurare che misurare male.
   */
  if (AREA_BAND < AREA_BAND_MIN)
  {
    Serial.printf("banda di %u righe sotto il minimo di %u: sonda saltata\n",
                  (unsigned)AREA_BAND, (unsigned)AREA_BAND_MIN);
    return;
  }

  // baseline bianca col pattern hardware: fondo su cui il nero si legge subito
  Serial.println(F("\nbaseline: banda bianca col pattern, poi refresh pieno Mode 1"));
  patternFill(0x47, 0xF7, "B/N   ");   // 0x24 tutto bianco
  patternFill(0x46, 0x77, "accent");   // 0x26 spento: il refresh pieno lo legge come accent
  if (runRefresh() < 0)
  {
    Serial.println(F("baseline non riuscita: sonda del partial d'area abbandonata"));
    return;
  }

  /**
   * Ora che la banda è bianca, 0x26 va portata a bianco: da questo punto non è
   * più l'accent ma il frame precedente, e deve contenere quello che si vede.
   * Il pattern hardware lo fa senza spingere 46 KB sul bus.
   */
  patternFill(0x46, 0xF7, "prec. ");

  logLine("trappola: fascia nera in 0x24 a y=%u..%u, fuori da ogni finestra di refresh",
                (unsigned)AREA_TRAP_Y, (unsigned)(AREA_TRAP_Y + AREA_TRAP_H - 1));
  writeBoxConst(0x24, 0, AREA_TRAP_Y, SRC, AREA_TRAP_H, 0x00);

  // bordo come lo lascia l'init, cioè come lo tiene il driver oggi
  writeCommand(0x3C);
  writeData(0x01);
  areaMsFirst = areaPass("passata 1: fascia alta a nero, bordo 0x3C = 0x01",
                         0, AREA_P1_Y, SRC, AREA_P1_H, 0x00, 1, 0xFF, 0xFC, 30000);

  /**
   * Se la prima passata non si è conclusa, le altre quattro con 0xFC sarebbero
   * solo altrettanti timeout: si va diritti a Mode 1, che è la strada che resta.
   */
  if (areaMsFirst > 0)
  {
    /**
     * DUE PASSATE NEL CASO BASE, e non cinque. Che la finestra RAM confini
     * l'area ridipinta ma NON accorci la durata è già misurato sul 9.7", stesso
     * silicio — 168, 48 e 24 righe danno tutte 24,65 s — e il finder ha già
     * raccolto lo stesso esito qui: 18167, 18170 e 18158 ms contro i 18308 di un
     * frame intero. Restano fuori dal conto minimo la passata con la fascia di
     * trappola, che è quella sopra, e il riquadro ristretto anche in X, che è la
     * sola domanda ancora aperta sulla finestra: se lungo X non vale, ogni
     * writeImagePart del driver va allargata a tutta la riga.
     *
     * Le altre girano in esaustivo, che è il default del menu: il conteggio di
     * SONDE dichiara il minimo, e refreshPrevisti() aggiunge le condizionali.
     */
    areaPass("passata 2: riquadro ristretto anche in X",
             AREA_BOX_X, AREA_BOX_Y, AREA_BOX_W, AREA_BOX_H, 0x00,
             AREA_NO_DIGIT, 0x00, 0xFC, 30000);

    if (cfg.esaustivo)
    {
      /**
       * Bordo su VCOM invece che sulla LUT: su questa famiglia di controller è
       * il valore che tiene ferma la cornice durante un partial. Le due passate
       * differiscono solo per questo, quindi il confronto è pulito.
       */
      writeCommand(0x3C);
      writeData(0x80);
      areaPass("passata 3: fascia centrale a nero, bordo 0x3C = 0x80",
               0, AREA_P2_Y, SRC, AREA_P2_H, 0x00, 2, 0xFF, 0xFC, 30000);
    }

    /**
     * Le due passate che seguono si fanno solo se il partial guadagna tempo. La
     * 4 chiede se una catena di partial sulla stessa area regge, la 5 se la
     * durata scala con l'altezza della finestra: se la passata 1 è durata come
     * un refresh pieno, la catena non interessa a nessuno e il tempo non scala
     * per definizione, quindi sono quaranta secondi che non dicono niente.
     */
    const bool partialGuadagna = (refreshMs <= 0) || (areaMsFirst * 2 < refreshMs);
    if (!partialGuadagna && !cfg.esaustivo)
    {
      Serial.println(F("\n-- passate 4 e 5 saltate: la passata 1 è durata come un refresh"));
      Serial.println(F("   pieno, quindi non c'è nessun tempo da far scalare e nessuna"));
      Serial.println(F("   catena di partial che valga la pena provare"));
    }

    // seconda scrittura sulla stessa area della passata 1: la catena regge?
    if (partialGuadagna || cfg.esaustivo)
      areaPass("passata 4: la fascia alta torna bianca",
               0, AREA_P1_Y, SRC, AREA_P1_H, 0xFF, 3, 0x00, 0xFC, 30000);

    if (partialGuadagna || cfg.esaustivo)
      areaMsThin = areaPass("passata 5: finestra sottile di 24 righe",
                          0, AREA_THIN_Y, SRC, AREA_THIN_H, 0x00,
                          AREA_NO_DIGIT, 0x00, 0xFC, 30000);
  }
  else
    verdict("la prima passata 0xFC non si è conclusa: le altre quattro sarebbero altrettanti timeout, si passa a Mode 1 su finestra");

  look("la fascia di trappola e' ancora bianca? guardala ADESSO, la passata Mode 1 potrebbe farla comparire");

  /**
   * Mode 1 su finestra. 0xF4 è 0xF7 senza il power down finale, ed è quello che
   * il driver monocromatico manda quando hasFastPartialUpdate è false: waveform
   * piena, ma sempre delimitata dalla finestra. Vale la misura in ogni caso, e
   * a differenza di 0xFC non consuma 0x26, quindi resterebbe compatibile con
   * l'accent.
   */
  writeCommand(0x3C);
  writeData(0x01);
  if (cfg.esaustivo || areaMsFirst <= 0)
    areaMsMode1 = areaPass("passata Mode 1 su finestra (0x22 = 0xF4)",
                         0, AREA_M1_Y, SRC, AREA_M1_H, 0x00,
                         AREA_NO_DIGIT, 0x00, 0xF4, cfg.timeoutMs);

  // 0xFC e 0xF4 lasciano clock e analogico accesi: si spengono come fa _PowerOff
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  waitBusy(5000);

  reportAreaPasses();

  look("la fascia di trappola e' BIANCA (finestra rispettata) o NERA (il refresh percorre tutta la banda)? e il riquadro ristretto in X ha i bordi verticali netti?");
}
// --- fasce delle sonde con LUT custom --------------------------------

/** Quattro fasce, una per variante della sonda del partial con LUT custom. */
static uint16_t LUTP_H = 384 / 4;
/** Split fra le due metà del probe dei livelli di sorgente. */
static uint16_t LEVELS_SPLIT = 384 / 2;

// --- interruttori del differenziale sul banco dell'OTP ---------------

/** Esiti delle passate della sonda differenziale approfondita. */
static const uint8_t DIFF_PASSES_MAX = 6;
static const char*   diffLabel[DIFF_PASSES_MAX]    = { nullptr };
static uint8_t       diffSequence[DIFF_PASSES_MAX] = { 0 };
static int32_t       diffMs[DIFF_PASSES_MAX]       = { -1, -1, -1, -1, -1, -1 };
static uint8_t       diffCount = 0;

/** Registra una passata nella tabella di riepilogo. */
static void recordDiffPass(const char* label, uint8_t sequence, int32_t ms)
{
  if (diffCount >= DIFF_PASSES_MAX)
    return;
  diffLabel[diffCount]    = label;
  diffSequence[diffCount] = sequence;
  diffMs[diffCount]       = ms;
  ++diffCount;
}

/**
 * Sonda differenziale approfondita: gli interruttori che la sonda d'area non
 * tocca, tutti sul banco di waveform dell'OTP. Sono le quattro domande che sul
 * 9.7" hanno permesso di dire che il differenziale non esiste come fatto
 * misurato invece che come prudenza.
 *
 *   1. il controller CONFRONTA i due piani? Lo stesso 0xFC coi piani identici e
 *      poi coi piani opposti: se la durata non cambia, il contenuto non entra
 *      nel conto e nessun differenziale esiste. È il test minimo e chiude la
 *      questione da solo;
 *   2. quanto costa la RICARICA della LUT? 0xCF e 0xC7 sono Mode 2 e Mode 1 col
 *      bit 5 e il bit 4 spenti, cioè display senza ricaricare LUT e
 *      temperatura;
 *   3. quanto costa il solo CARICO? 0x99 carica la LUT di Mode 2 e non dipinge:
 *      è il tetto al guadagno che le due sopra possono cercare;
 *   4. la RAM rosso entra nel conto? 0x21 = 0x40 0x00 la bypassa come zero: se
 *      la durata cambia, 0x26 pesa sul refresh.
 *
 * Ogni passata lascia una cifra diversa sul vetro, così la cifra che resta è
 * l'ultima che ha davvero pilotato il pannello.
 */
static void probeDifferentialDeeper()
{
  const uint16_t fasciaH = (uint16_t)(cfg.gate / 4);

  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  forceRamOptionsNormal();

  /**
   * 1a. Differenza zero: i due piani con lo stesso contenuto. Se il controller
   * confronta 0x24 e 0x26 per decidere quali pixel muovere, qui non ha niente
   * da muovere e la passata dovrebbe essere quasi istantanea.
   */
  Serial.println(F("\n-- 1a. differenza zero: 0x24 e 0x26 con lo stesso contenuto"));
  patternFill(0x47, bwPatternFor(true), "B/N   ");
  patternFill(0x46, bwPatternFor(true), "prec. ");
  setRamWindow(0, 0, SRC, cfg.gate);
  /**
   * Il riquadro va scritto identico nei due piani: qui la misura È la differenza
   * fra 0x24 e 0x26, e un riquadro nel solo BW la porterebbe da zero al numero
   * di pixel del riquadro. Scritto uguale, la differenza resta zero e il frame
   * ha comunque il suo numero sul vetro.
   */
  const Frame fZero = { "differenziale 1a: differenza zero", BADGE_PIANI_UGUALI, D_NESSUNA, 0x00 };
  int32_t ms = runRefresh(0xFC, "molto corta se il motore confronta i piani", 0, &fZero);
  recordDiffPass("differenza zero", 0xFC, ms);
  const int32_t msZero = ms;

  /**
   * 1b. Differenza massima: si inverte solo 0x24, quindi ogni pixel differisce.
   * È il riferimento contro cui leggere la passata precedente.
   */
  /**
   * UNA PASSATA NEL CASO BASE. Che il controller non confronti i due piani è
   * già misurato sul 9.7", stesso silicio e con margine: 0xFC, 0xFF, 0xCF e
   * 0xC7 danno tutti 24,6-24,8 s, con 2 ms di scarto fra piani identici e piani
   * opposti. Il riferimento a differenza massima serve solo a chi vuole
   * rifare quel confronto da zero, e gira in esaustivo — che è il default del
   * menu, quindi non si perde: cambia solo cosa il conteggio di SONDE dichiara
   * come minimo.
   */
  int32_t msMax = -1;
  if (cfg.esaustivo)
  {
    logLine("-- 1b. differenza massima: si inverte solo 0x24");
    patternFill(0x47, bwPatternFor(false), "B/N   ");
    setRamWindow(0, 0, SRC, cfg.gate);
    // Qui il riquadro normale va bene: i due piani sono già complementari
    // ovunque, e scriverlo complementare non cambia il fatto che ogni pixel
    // differisca.
    const Frame fMax = frame("differenziale 1b: differenza massima");
    ms = runRefresh(0xFC, "riferimento per il confronto con la 1a", 0, &fMax);
    recordDiffPass("differenza massima", 0xFC, ms);
    msMax = ms;
  }

  if (msZero > 0 && msMax > 0)
  {
    Serial.printf("\n  differenza zero %ld ms contro differenza massima %ld ms: scarto %ld ms\n",
                  (long)msZero, (long)msMax, (long)(msMax - msZero));
    if (labs((long)(msMax - msZero)) < 2000)
      verdict("la durata NON dipende dal contenuto dei piani: il controller non li confronta, hasFastPartialUpdate = false e' un fatto");
    else
      logLine("scarto grande: il controller confronta i due piani, quindi un meccanismo differenziale c'è e va perseguito");
  }

  /**
   * 2. Display senza ricarica. Il bit 5 di 0x22 è load temperature e il bit 4
   * load LUT: 0xCF e 0xC7 li hanno spenti, quindi dipingono con la LUT che è
   * già in RAM. Se la durata cala, il costo era il carico e non la waveform.
   * Ogni passata porta la sua cifra, così si sa quale ha dipinto.
   */
  /**
   * 2. Solo carico della LUT di Mode 2, senza dipingere: 0x99 ha il bit 4 e il
   * bit 3 ma non il bit 2. Costa un secondo, e serve da misura del TETTO al
   * guadagno che le due passate dopo possono cercare: quelle dipingono senza
   * ricaricare la LUT, quindi al massimo risparmiano quello che il carico
   * costa. Se il carico è breve non c'è niente da risparmiare, e quelle due
   * passate si saltano.
   */
  Serial.println(F("\n-- 2. 0x99: carica la LUT di Mode 2 e non dipinge"));
  // Non dipinge, quindi non è un frame: nessun numero e niente da guardare.
  ms = runRefresh(0x99, "breve: non dipinge, carica", 10000);
  recordDiffPass("carico LUT Mode 2", 0x99, ms);
  const int32_t msCarico = ms;

  /**
   * 3. Display senza ricarica, e solo se il carico pesa. Il bit 5 di 0x22 è
   * load temperature e il bit 4 load LUT: 0xCF e 0xC7 li hanno spenti, quindi
   * dipingono con la LUT già in RAM. Il loro guadagno massimo è msCarico:
   * sotto i tre secondi non vale due refresh da venti.
   */
  const bool displaySenzaRicarica = (cfg.esaustivo || msCarico > 3000);

  if (displaySenzaRicarica)
  {
    logLine("-- 3. solo display, senza ricarica di LUT e temperatura");
    patternFill(0x47, bwPatternFor(true), "B/N   ");
    const Frame f3 = frameCon("differenziale 3: 0xCF, solo display Mode 2", D_DIPINTO);
    writeStripeWithDigit(0x24, 0, fasciaH, bwByteFor(true), prenotaNumero(f3),
                         bwByteFor(false));
    setRamWindow(0, 0, SRC, cfg.gate);
    ms = runRefresh(0xCF, "sotto il secondo se il costo era la ricarica", 0, &f3);
    recordDiffPass("solo display Mode 2", 0xCF, ms);

    logLine("-- 3a. controllo Mode 1 nelle stesse condizioni");
    const Frame f3a = frameCon("differenziale 3a: 0xC7, solo display Mode 1", D_DIPINTO);
    writeStripeWithDigit(0x24, fasciaH, fasciaH, bwByteFor(true), prenotaNumero(f3a),
                         bwByteFor(false));
    setRamWindow(0, 0, SRC, cfg.gate);
    ms = runRefresh(0xC7, "controllo di Mode 1 nelle stesse condizioni", 0, &f3a);
    recordDiffPass("solo display Mode 1", 0xC7, ms);
  }
  else
  {
    logLine("-- 3. 0xCF e 0xC7 saltate: il carico della LUT costa %ld ms, quindi non ricaricarla non può far risparmiare più di quello",
                  (long)msCarico);
  }

  /**
   * 4. RAM rosso bypassata, e solo se serve. 0x21 A[7:4] = 0100 tratta il
   * contenuto di 0x26 come zero: dice se quel piano entra nel conto del
   * refresh. Ma se la differenza zero e la differenza massima hanno dato la
   * stessa durata, che il contenuto dei piani non entri nel conto è già
   * dimostrato, e questa passata sarebbe una conferma di una misura.
   */
  const bool pianiContano = (msZero > 0 && msMax > 0
                             && labs((long)(msMax - msZero)) >= 2000);

  if (pianiContano || cfg.esaustivo)
  {
    logLine("-- 4. 0x21 = 0x40 0x00, RAM rosso bypassata come zero");
    writeCommand(0x21);
    writeData(0x40);
    writeData(0x00);
    const Frame f4 = frameCon("differenziale 4: 0x26 bypassata via 0x21", D_DIPINTO);
    writeStripeWithDigit(0x24, (uint16_t)(fasciaH * 2), fasciaH,
                         bwByteFor(true), prenotaNumero(f4), bwByteFor(false));
    setRamWindow(0, 0, SRC, cfg.gate);
    ms = runRefresh(0xFC, "diverso dalla 1b se 0x26 entra nel conto", 0, &f4);
    recordDiffPass("0x26 bypassata via 0x21", 0xFC, ms);
    forceRamOptionsNormal();
  }
  else
  {
    logLine("-- 4. bypass di 0x26 saltato: differenza zero e massima coincidono, il contenuto dei piani non entra nel conto");
  }

  Serial.println(F("\nesito della sonda differenziale approfondita:"));
  Serial.println(F("  passata                       0x22    BUSY"));
  for (uint8_t k = 0; k < diffCount; ++k)
  {
    if (diffMs[k] < 0)
      logLine("%-28s 0x%02X   non conclusa", diffLabel[k], diffSequence[k]);
    else
      Serial.printf("  %-28s 0x%02X   %6ld ms\n", diffLabel[k], diffSequence[k],
                    (long)diffMs[k]);
  }
  if (refreshMs > 0)
    logLine("riferimento: refresh pieno 0xF7 %ld ms", (long)refreshMs);
  /**
   * Il blocco di osservazione vale solo se almeno una passata che dipinge una
   * cifra è stata eseguita: le due di differenza zero e massima non ne lasciano
   * nessuna, e senza cifre non c'è niente da attribuire.
   */
  if (displaySenzaRicarica || pianiContano || cfg.esaustivo)
  {
    look("quale numero e' rimasto sul vetro? e' l'ultima passata che ha davvero dipinto");
  }

  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
}
/**
 * Determina la polarità del piano BW e se 0x21 = 0x08 la inverte davvero.
 *
 * Due frame consecutivi con la RAM IDENTICA e solo 0x21 diverso: è l'unico modo
 * di isolare quel registro, e rende la risposta indipendente dal datasheet, che
 * qui non è affidabile — la Rev 1.0 definisce 0x21 con un parametro solo mentre
 * l'init di fabbrica SOLUM gliene scrive due, e la decodifica di A[3:0] = 1000
 * come "BW inverse" viene da quella revisione.
 *
 * Frame 1, 0x21 al POR, cioè la condizione in cui gira il driver: fascia 1 con
 * 0x24 = 0xFF, fascia 2 con 0x24 = 0x00. La più chiara dice quale bit rende
 * bianco, e il riferimento è nello stesso frame.
 * Frame 2, 0x21 = 0x08 0x00 e RAM non riscritta: se i colori si scambiano, 0x08
 * inverte il piano BW; se restano dov'erano, quel valore non inverte niente su
 * questo silicio e l'init di fabbrica non correggeva nessuna polarità.
 *
 * Due refresh e due pause: è la sonda più economica del test, e senza il suo
 * esito ogni riga della scheda che nomini un colore è un'ipotesi.
 */
static void probeBwPolarity()
{
  logLine("configurazione attuale del test: %s", polaritaLabel());
  Serial.println(F("il datasheet su questo punto non basta: la Rev 1.0 definisce 0x21 con"));
  Serial.println(F("un solo parametro, l'init di fabbrica gliene scrive due, e la"));
  Serial.println(F("decodifica di A[3:0] = 1000 come BW inverse viene da quella revisione."));
  Serial.println(F("Qui la RAM resta identica fra i due frame: l'unica variabile è 0x21."));

  const uint16_t fasciaH = (uint16_t)(cfg.gate / 3);
  if (fasciaH < GLYPH_H * LABEL_SCALE + 8)
  {
    Serial.printf("fasce di %u righe: troppo basse per la cifra, sonda saltata\n",
                  (unsigned)fasciaH);
    return;
  }

  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  forceRamOptionsNormal();

  /**
   * Accent spento e le due fasce scritte una volta sola. I byte sono espressi
   * come costanti, 0xFF e 0x00, e non passano dagli helper della polarità: è
   * proprio la loro resa che questa sonda deve misurare, quindi assumerla qui
   * sarebbe circolare.
   */
  patternFill(0x46, 0x77, "accent");
  patternFill(0x47, 0xF7, "B/N   ");
  /**
   * Qui le due cifre sulle fasce sono ETICHETTE DI POSIZIONE, non numeri di
   * frame: le due fasce stanno nello stesso frame, e il confronto è fra loro.
   * È l'unica sonda in cui il vetro porta due numerazioni — il riquadro in alto
   * a destra col numero del frame, e 1 e 2 sulle fasce — quindi il log lo
   * dichiara invece di lasciarlo indovinare.
   */
  writeStripeWithDigit(0x24, 0, fasciaH, 0xFF, 1, 0x00);
  writeStripeWithDigit(0x24, fasciaH, fasciaH, 0x00, 2, 0xFF);
  logLine("le cifre 1 e 2 sulle fasce sono posizioni, non numeri di frame: il");
  logLine("numero della schermata sta nel riquadro in alto a destra");
  /**
   * Cosa aspettarsi, e da dove viene. Sul 9.7", stesso silicio, 0x21 nella
   * forma a due byte FUNZIONA, misurato sul vetro in due passate: {08 00} manda
   * lo schermo in negativo — fondo nero e fascia del nome bianca con testo nero
   * — e {40 00} fa sparire l'accent bypassando la RED RAM. Concorda con le tre
   * implementazioni indipendenti che scrivono due byte su questo controller:
   * init di fabbrica SOLUM, dualssd.cpp di OEPL e papyrix-reader.
   * Qui le due fasce dovrebbero quindi SCAMBIARSI. Se non lo fanno non è una
   * misura sporca nè una conferma: è una differenza fra i due controller, ed è
   * un risultato da registrare come tale.
   */
  logDetail("sul 9.7\" 0x21 = {08 00} INVERTE il piano BW, misurato sul vetro:");
  logDetail("qui le fasce dovrebbero scambiarsi, e se non lo fanno e' un dato");

  /**
   * Finestra piena: writeStripeWithDigit la lascia sulla propria fascia, e le
   * due fasce devono essere ridipinte entrambe, altrimenti il confronto fra
   * loro non esiste. Il frame 2 non riscrive niente, quindi eredita questa.
   */
  setRamWindow(0, 0, SRC, cfg.gate);

  const Frame fPol1 = frame("polarità BW, frame 1: 0x21 al POR");
  const int32_t msPor = runRefresh(0xF7, "frame 1, 0x21 al POR", 0, &fPol1);

  look("quale delle due fasce e' BIANCA? la prima ha 0x24 pieno di 0xFF, la seconda di 0x00");

  /**
   * La risposta si chiede qui e si applica subito: è quello che toglie la
   * ricompilazione a metà bring-up. L'ultima voce vale "non l'ho guardata" e
   * lascia la polarità come era.
   */
  static const char* const VOCI_FRAME1[] = { "la PRIMA, in alto", "la SECONDA",
                                             "non lo so / non si vede" };
  const int rispostaBianca = chiediScelta("  quale fascia è BIANCA?", VOCI_FRAME1, 3, 2);


  /**
   * Solo 0x21 cambia. Nessun init, nessuna riscrittura dei piani: se i colori
   * si muovono, il merito è di quel registro e di nient'altro.
   */
  writeCommand(0x21);
  writeData(0x08);
  writeData(0x00);
  const Frame fPol2 = frame("polarità BW, frame 2: 0x21 = 0x08 0x00");
  const int32_t msInv = runRefresh(0xF7, "frame 2, 0x21 = 0x08 0x00, RAM invariata",
                                   0, &fPol2);

  look("le due fasce si sono SCAMBIATE di colore rispetto al frame precedente?");
  logLine("durate: frame 1 %ld ms, frame 2 %ld ms; devono essere uguali,",
                    (long)msPor, (long)msInv);
  logLine("0x21 non tocca la waveform e uno scarto grande è misura sporca");

  const bool scambiate = chiediSN("  le due fasce si sono SCAMBIATE?", false);

  /**
   * Il frame 1 gira con 0x21 al POR, che è la condizione del driver: è quello a
   * dare la polarità. La risposta al frame 2 non la cambia, dice se 0x21 = 0x08
   * inverte, ed è un'informazione sull'init di fabbrica.
   */
  if (rispostaBianca == 0 || rispostaBianca == 1)
  {
    cfg.bwPolarity = (rispostaBianca == 0) ? BW_DATASHEET : BW_INVERSE;
    registraEsito(C_POLARITA, (int8_t)rispostaBianca, numeroDiRiga(), false);
    logLine("polarità determinata: %s", polaritaLabel());
    logLine("vale da adesso per ogni sonda di questa sessione: niente da");
    logLine("mettere nel sorgente e niente da ricompilare. Alla sessione");
    logLine("prossima si ridichiara dalla voce caratteristiche note del menu");
  }
  else
    logLine("polarità non determinata: resta da misurare, e le etichette");

  logLine("0x21 = 0x08 %s il piano BW su questo silicio",
                    scambiate ? "INVERTE" : "non inverte");


  forceRamOptionsNormal();
}

/**
 * Frame di identificazione della candidata corrente: cornice, righelli
 * numerati, blocchi d'angolo, scaletta diagonale e il numero della candidata al
 * centro, scritto ai due clock.
 *
 * È il solo frame che si ripete per ogni candidata, ed è quello che risponde
 * alle domande che il BUSY non può toccare: se l'immagine esce, con quale
 * verso, quante gate line sono davvero pilotate, e se il bus tiene i 20 MHz.
 */
static int32_t showIdentityFrame(const Frame* f)
{
  Serial.printf("\n--- frame: identificazione, candidata %u (%u MHz sopra, %u MHz sotto) ---\n",
                g_cand + 1,
                (unsigned)(cfg.spiBase / 1000000UL), (unsigned)(cfg.spiFast / 1000000UL));
  const uint32_t t0 = millis();
  writePlaneDualClock(0x24, composeRowBW);
  writePlaneDualClock(0x26, composeRowRED);
  logLine("due piani scritti in %lu ms", (unsigned long)(millis() - t0));
  return runRefresh(0xF7, "una ventina di secondi", 0, f);
}
/**
 * Frame di identificazione, uno per candidata di init viva. Ogni candidata è un
 * frame proprio: sono la stessa RAM sotto init diversi, e vederne solo l'ultimo
 * non direbbe quale init fa rispondere il pannello.
 *
 * Le specchiature si chiedono una volta sola alla fine, e non per candidata:
 * sono una proprietà del cablaggio della coda, non dell'init.
 */
static void probeIdentityFrames()
{
  /** Righe del blocco di osservazione, uguali per tutte e tre le candidate. */

  /** Etichette dei frame: portano il numero della candidata, quindi durano. */
  static char etichetta[CAND_COUNT][48];

  for (uint8_t c = 0; c < CAND_COUNT; ++c)
  {
    if (!candProbe[c].vivo)
    {
      logLine("candidata %u (%s) saltata: elettricamente muta, un frame e uno sguardo sarebbero buttati", c + 1, CAND_LABEL[c]);
      continue;
    }
    initPanel(c, (c == CAND_MINIMAL) ? MUX_NOT_WRITTEN : cfg.gate);
    snprintf(etichetta[c], sizeof(etichetta[c]),
             "identificazione, candidata %u: %s", c + 1, CAND_LABEL[c]);
    const Frame f = frameCon(etichetta[c], D_META);
    identityMs[c] = showIdentityFrame(&f);

    look("il pattern e' comparso? righelli leggibili, angoli al posto giusto, nessuna corruzione nella meta' scritta al clock alto");
    logLine("candidata %u di init: %s", c + 1, CAND_LABEL[c]);
  }

  /**
   * Le specchiature: una domanda sola, dopo l'ultimo frame, e sul vetro c'è
   * ancora il pattern su cui si legge il verso.
   *
   * Il gate dell'ultimo frame va chiuso qui e non al refresh successivo, perchè
   * la domanda sulle specchiature ha senso solo se una candidata ha DIPINTO, e
   * l'unica cosa che lo dice è la risposta a D_META: la durata del refresh no,
   * perchè il BUSY scende comunque anche quando sul vetro non compare niente.
   * Senza questa chiusura, con una sola candidata viva D_META resterebbe senza
   * risposta e la domanda sul verso non verrebbe mai posta.
   */
  chiudiFrame(nullptr);
  if (esiti[C_META].risposta >= 0 && esiti[C_META].risposta <= 2)
  {
    const char* voci[8];
    for (uint8_t i = 0; i < QUANTE_DI[C_MIRROR]; ++i)
      voci[i] = VOCI_DI[C_MIRROR][i].risposta;
    const int scelta = chiediScelta("  il verso della banda: che specchiature vedi?",
                                    voci, QUANTE_DI[C_MIRROR], 0);
    registraEsito(C_MIRROR, (int8_t)scelta, numeroDiRiga(), false);
  }
}

/**
 * Frame delle quattro combinazioni dei piani più i box a finestra parziale. Due
 * domande in un refresh solo, ed è deliberato: su un pannello dove un refresh
 * costa ~19 s, tenerle separate sarebbe un frame e uno sguardo in più.
 */
static void probeBandsAndBoxes()
{
  initPanel(CAND_DRIVER, cfg.gate);
  bandsRefreshMs = showBandsAndBoxesFrame();

  look("che colore rende ogni banda? e i tre box neri della banda 1 sono allineati, equidistanti e netti?");
}

/**
 * Riepilogo della fase probe: le misure accanto alla costante del driver che
 * ognuna determina. Si ristampa dal menu, perchè confronta fra loro passate che
 * stanno in sonde diverse e vale anche a sonde eseguite una alla volta.

// =====================================================================
// LE TRE SONDE DEL PANNELLO E DEI REGISTRI
// =====================================================================
 */

/**
 * Righello a piena altezza: un numero ogni RIGHELLO_PASSO righe, su fondo
 * bianco. La cifra più bassa che si legge sul vetro dice fino a dove il
 * controller ha davvero scandito, che è l'unica lettura utile — la durata non
 * serve, perchè NON scala col MUX (misurato sul 9.7": 24010 / 24031 / 24033 ms
 * a MUX 671 / 335 / 167).
 */
static const int32_t RIGHELLO_PASSO = 64;

static void composeRowRighello(int16_t y, uint8_t* row, uint16_t gate)
{
  memset(row, bwByteFor(true), ROW_BYTES);
  if (y < 4 || y >= (int16_t)gate - 4)
  {
    memset(row, bwByteFor(false), ROW_BYTES);
    return;
  }
  const int32_t banda = y / RIGHELLO_PASSO;
  const int32_t gy    = banda * RIGHELLO_PASSO + 4;
  paintNumber(row, y, (uint16_t)(banda * RIGHELLO_PASSO), 32, gy,
              LABEL_SCALE, bwByteFor(false));
  // tacca a ogni passo, così si contano anche le bande senza numero leggibile
  if ((y % RIGHELLO_PASSO) == 0)
    paintSpan(row, 0, YRULE_TICK, bwByteFor(false));
}

/**
 * IDENTITÀ DEL DIE. Tre limiti indipendenti del datasheet SSD1677 dicono che un
 * controller solo non può coprire 768 righe:
 *   - §8.4, 0x45: "00h <= YSA[9:0], YEA[9:0] <= 2A7h", cioè la finestra RAM in
 *     Y si ferma alla riga 679;
 *   - §8.1, 0x01: "Multiplex ratio (MUX ratio) from 300 MUX to 680 MUX";
 *   - p.5 e Table 5-5: RAM 960x680 bit per piano, uscite G[679:0].
 *
 * Ma il progetto ha due indizi che il die NON sia una Rev 1.0: l'init di
 * fabbrica SOLUM scrive 0x21 con due byte, e la Rev 1.0 ne dichiara uno solo;
 * e i pin della cascade esistono. Questa sonda chiede quindi al silicio se è
 * davvero quello che il datasheet descrive, spazzando il MUX fino FUORI
 * specifica. Non si tocca nessuna tensione, e chiedere più linee di quante
 * esistano è un overflow del contatore di scansione: il 9.7" ha già girato a
 * MUX 167 e 335, cioè sotto il pavimento dichiarato, senza danni.
 *
 * Chiude anche una circolarità: il rettangolo 960x384 osservato al bring-up è
 * stato ottenuto CON IL MUX PROGRAMMATO A 383, quindi conferma che il MUX
 * funziona, non quante gate line il controller piloti.
 */
static void probeIdentitaDie()
{
  const uint16_t salvaGate = cfg.gate;

  for (uint8_t k = 0; k < MUX_PASSES; ++k)
  {
    const uint16_t mux = cfg.mux[k];
    if (mux == MUX_NOT_WRITTEN) continue;
    if (!cfg.esaustivo && mux <= MUX_SPEC_MAX && k > 0) continue;

    static char didascalie[MUX_PASSES][40];
    snprintf(didascalie[k], sizeof(didascalie[k]), "RIGHELLO A MUX %u", (unsigned)mux);

    cfg.gate = mux;
    initPanel(CAND_DRIVER, mux);
    logLine("MUX %u%s, finestra Y 0..%u", (unsigned)mux,
            (mux > MUX_SPEC_MAX) ? " (FUORI specifica)" : "", (unsigned)(mux - 1));
    writePlane(0x24, mux, composeRowRighello);
    writePlane(0x26, mux, composeRowRED);

    const Frame f = frame(didascalie[k]);
    runRefresh(0xF7, nullptr, 0, &f);
    look("fino a quale numero il righello e' arrivato? e' quello il conteggio "
         "gate reale di questo controller");
  }

  cfg.gate = salvaGate;
  if (cfg.mux[MUX_PASSES - 2] > MUX_SPEC_MAX)
    verdict("se il righello passa la riga %u il die non e' una Rev 1.0, e "
            "l'architettura va rifatta al contrario", (unsigned)YEA_SPEC_MAX);
  initPanel(CAND_DRIVER, cfg.gate);
}

/**
 * TB = 1: reverse scan dei gate IN HARDWARE. È la sonda col rapporto fra valore
 * e costo più alto della suite.
 *
 * Sul SSD1677 il datasheet dà 0x01 B[0] TB come **Reserved**, e la scansione va
 * solo da G0 a G679. Sul SSD1683 lo stesso bit vale "scan from G299 to G0". Se
 * questo die si comporta da 1683 — e sul secondo byte di 0x21 lo fa già —
 * allora la specchiatura della banda capovolta si risolve in un registro, e il
 * reverse di righe, byte e bit nel data path del driver diventa cancellabile:
 * è il pezzo più fragile di GxEPD2_SOLUM_122c_960x768.h.
 *
 * Un refresh solo: il pattern è quello di identificazione, che è asimmetrico su
 * entrambi gli assi, e la domanda è se esce ribaltato in verticale.
 */
static void probeTBReverse()
{
  const bool salva = cfg.tbReverse;
  cfg.tbReverse = true;

  initPanel(CAND_DRIVER, cfg.gate);
  logLine("0x01 B[0] TB = 1: Reserved sul SSD1677, reverse scan sul SSD1683");
  writePlane(0x24, cfg.gate, composeRowBW);
  writePlane(0x26, cfg.gate, composeRowRED);

  const Frame f = frame("TB = 1, REVERSE SCAN GATE");
  const int32_t ms = runRefresh(0xF7, nullptr, 0, &f);
  look("il pattern e' RIBALTATO in verticale rispetto al frame di "
       "identificazione? righello Y che scende invece di salire, angoli scambiati");

  cfg.tbReverse = salva;
  if (ms > 0)
    verdict("se ribaltato: TB e' implementato, _reverseBits e il reverse di "
            "righe e byte si cancellano dal driver");
  else
    verdict("il frame non si e' concluso: TB = 1 potrebbe aver bloccato la scansione");
  initPanel(CAND_DRIVER, cfg.gate);
}

/**
 * Specchiatura via ENTRY MODE, il ripiego se TB non è implementato, e comunque
 * il modo di sapere cosa fa il contatore di indirizzo.
 *
 * QUI IL DATASHEET NON RISOLVE LA DOMANDA, ed è per questo che la sonda vale.
 * §8.2 dice che il contatore è "incrementato/decrementato di 1" e §8.3 che la
 * finestra X va "per unità di indirizzo" fino a 3BFh = 959, cioè in pixel — ma
 * non dice NIENTE su cosa accada agli otto bit di un byte scritto. Upstream il
 * GDEY0579Z93 legge i byte in ordine naturale e non rovescia nè byte nè bit, e
 * la sua metà capovolta esce giusta; la memoria del progetto asserisce invece
 * che il decremento non specchia. Solo il pannello può dirlo.
 *
 * Perchè servano quattro bande in UN refresh: l'entry mode è un registro
 * globale, ma finestra e cursore si riscrivono a ogni banda, quindi le quattro
 * combinazioni di ID[1:0] stanno tutte nello stesso frame.
 *
 * Il pattern ha struttura SOTTO il byte — tre pixel accesi su otto, in
 * posizione nota — perchè senza quello la sonda non distingue una specchiatura
 * di byte da una di bit, che è esattamente la domanda.
 */
static void probeEntryMode()
{
  const uint8_t salva = cfg.entryMode;
  static const uint8_t MODI[4] = { 0x00, 0x01, 0x02, 0x03 };

  initPanel(CAND_DRIVER, cfg.gate);
  const uint16_t fasciaH = (uint16_t)(cfg.gate / 4);

  // fondo bianco su tutta la banda, così le quattro fasce si leggono da sole
  patternFill(0x47, bwPatternFor(true), "FONDO BIANCO");
  patternFill(0x46, 0x00,               "ACCENT SPENTO");

  uint8_t row[ROW_BYTES];
  for (uint8_t k = 0; k < 4; ++k)
  {
    const uint16_t y = (uint16_t)(k * fasciaH);
    setRamWindowEntry(0, y, SRC, fasciaH, MODI[k]);
    writeCommand(0x24);

    hspi.beginTransaction(spiSettings);
    digitalWrite(PIN_DC, HIGH);
    csAssert();
    for (uint16_t r = 0; r < fasciaH; ++r)
    {
      memset(row, bwByteFor(true), ROW_BYTES);
      // marcatore a granularita' di BYTE: dice se i byte sono stati specchiati
      paintNumber(row, (int16_t)r, (uint16_t)(k + 1), 32,
                  (int32_t)(fasciaH - GLYPH_H * LABEL_SCALE) / 2,
                  LABEL_SCALE, bwByteFor(false));
      // marcatore SOTTO il byte: tre pixel su otto, a x = 800, in una riga su
      // due. Dice se anche l'ordine dei bit dentro il byte e' stato rovesciato.
      if ((r % 2) == 0)
        badgeSpan(row, 800, 3, bwByteFor(false));
      hspi.writeBytes(row, ROW_BYTES);
    }
    csRelease();
    hspi.endTransaction();
    logDetail("fascia %u a y=%u, entry mode 0x%02X", k + 1, y, MODI[k]);
  }

  setRamWindow(0, 0, SRC, cfg.gate);
  const Frame f = frame("QUATTRO ENTRY MODE, UNA FASCIA CIASCUNO");
  runRefresh(0xF7, nullptr, 0, &f);
  look("in quali fasce il numero e' DRITTO e nel posto giusto? e il gruppo di "
       "tre pixel a x=800 sta a sinistra o a destra dentro il suo byte?");
  logLine("fasce dall'alto: 1 = 0x11 0x00 (X- Y-), 2 = 0x01 (X+ Y-), "
          "3 = 0x02 (X- Y+), 4 = 0x03 (X+ Y+, il POR)");
  verdict("numero specchiato ma tre pixel nello stesso posto = specchiatura di "
          "BYTE: al driver serve ancora il reverse dei bit");

  cfg.entryMode = salva;
  initPanel(CAND_DRIVER, cfg.gate);
}

#endif // DUAL_PANEL_FINDER_PROBES_FRAME_H
