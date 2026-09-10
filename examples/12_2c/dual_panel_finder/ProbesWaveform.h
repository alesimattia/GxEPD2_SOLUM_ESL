// =============================================================================
// ProbesWaveform.h — waveform, LUT, temperatura, sonno e secondo controller
//
// Ogni sonda finisce in una riga che nomina il metodo di
// src/GxEPD2_SOLUM_122c_960x768.h da cambiare: il log e quello che si vede sul
// vetro devono bastare a correggere il driver, e una sonda che non produce una
// riga di quel tipo sta spendendo refresh senza rispondere a niente.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_PROBES_WAVEFORM_H
#define DUAL_PANEL_FINDER_PROBES_WAVEFORM_H

#include <Arduino.h>
#include "Config.h"
#include "Report.h"
#include "Graphics.h"
#include "Controller.h"
#include "ProbesFrame.h"

/**
 * Sonda del partial con LUT caricata dall'MCU, l'unica strada che le altre non
 * potevano provare: le passate della sonda d'area usano 0xFC e 0xF4, che hanno
 * il bit 4 di 0x22 attivo e ricaricano la waveform dall'OTP. Misurano sempre
 * quella, e una durata pari al refresh pieno dice che in OTP ce n'è una sola,
 * non che il partial sia impossibile.
 *
 * La sequenza è quella del GDEH116T91, stesso command set con gli stessi 960
 * source, che così fa 700 ms:
 *
 *   0x3C = 0xC0        border HiZ
 *   0x32 + 105 byte    waveform breve, scritta dall'MCU
 *   0x26 <- frame precedente, 0x24 <- frame nuovo
 *   0x22 = 0xCC        bit 4 SPENTO: non ricarica l'OTP sopra la LUT custom
 *   0x20
 *
 * Due varianti, le DIAGONALI della matrice LUT per Display Mode, ognuna sulla
 * propria fascia, ognuna col numero del proprio frame:
 *
 *   fascia 1  LUT del 1160, 0x22 = 0xCC (Mode 2)
 *   fascia 2  LUT riassegnata alla Table 6-4, 0x22 = 0xC4 (Mode 1)
 *
 * Mode 1 contro Mode 2 conta perchè cambia il significato delle due RAM: in
 * Mode 2 sono (precedente, nuovo), in Mode 1 la Table 6-4 le legge come
 * (accent, BW). Le due LUT sono costruite per le due letture, e quale dipinge
 * dice quale delle due il silicio applica. Le celle incrociate sono mute per
 * costruzione — in Mode 2 la fascia nera cade su LUT2, a zero nella
 * riassegnata, e in Mode 1 su LUT0, a zero in quella del 1160 — quindi non si
 * provano: sarebbero due refresh da venti secondi che non possono dipingere.
 *
 * Esito, in ordine di importanza: durata sotto i 3 s CON la sua cifra sul vetro
 * = partial trovato; sotto i 3 s SENZA niente = la waveform non pilota, va
 * aggiustata ma la strada è quella; durata piena = il controller ha ricaricato
 * l'OTP e la LUT custom è stata ignorata.
 *
 * Ogni variante riparte da reset più init, perchè il SWRESET rimette le LUT
 * dell'OTP e nessuna deve trovare i registri sporchi dalla precedente.
 */
static void probePartialLut()
{
  static uint16_t numeroFascia[VARIANTI_LUT] = { 0 };

  if (LUTP_H < GLYPH_H * LABEL_SCALE + 8)
  {
    Serial.printf("fasce di %u righe: troppo basse per la cifra, sonda saltata\n",
                  (unsigned)LUTP_H);
    return;
  }

  /**
   * Nessuna baseline, ed è un refresh risparmiato senza perdere niente: se una
   * variante dipinge, ridipinge lei tutto lo schermo secondo la RAM, che il
   * pattern porta a bianco più le fasce; se non dipinge, il vetro resta come
   * l'ha lasciato la sonda precedente, ed è esattamente l'esito da leggere.
   */
  struct Variante
  {
    const uint8_t* lut;
    const char*    lutName;
    uint8_t        sequence;
    uint16_t       y;
    uint8_t        digit;
  };
  /**
   * Le due diagonali della matrice, e solo quelle. Le celle incrociate — LUT
   * del 1160 in Mode 1 e LUT riassegnata in Mode 2 — sono MUTE PER
   * COSTRUZIONE: in Mode 1 la fascia nera cade su LUT0, che nella LUT del 1160
   * è a zero, e in Mode 2 cade su LUT2, che nella riassegnata è a zero. Non
   * potendo dipingere, servivano da controllo del modello di lettura delle due
   * RAM; ma quel controllo lo dà già quale delle due diagonali dipinge, e a
   * costo zero. Due refresh da venti secondi in meno.
   */
  const Variante varianti[VARIANTI_LUT] =
  {
    { LUT_PARTIAL_1160, "LUT 1160 (Mode 2)",      0xCC, (uint16_t)(LUTP_H * 0), 1 },
    { LUT_PARTIAL_T64,  "LUT Table 6-4 (Mode 1)", 0xC4, (uint16_t)(LUTP_H * 1), 2 },
  };

  /**
   * Etichette dei frame, una per variante: areaPass() le tiene in areaPasses[]
   * per il riepilogo, quindi devono sopravvivere al giro di ciclo e non possono
   * stare sullo stack.
   */
  static char etichettaLut[VARIANTI_LUT][48];

  for (int v = 0; v < VARIANTI_LUT; ++v)
  {
    const Variante& t = varianti[v];
    Serial.printf("\n-- variante %d: %s, 0x22 = 0x%02X, cifra %u\n",
                  v + 1, t.lutName, t.sequence, (unsigned)t.digit);

    /**
     * Reset più init a ogni variante: il SWRESET rimette in RAM le LUT
     * dell'OTP, quindi ognuna parte dallo stesso stato e la LUT custom
     * dell'iterazione precedente non sopravvive per sbaglio. Il pattern
     * ricarica i due piani, che il reset ha azzerato.
     */
    resetPanel();
    initPanel(CAND_DRIVER, cfg.gate);
    forceRamOptionsNormal();
    patternFill(0x47, bwPatternFor(true), "B/N   ");

    /**
     * 0x26 secondo il Display Mode della passata, e non è un dettaglio.
     *
     * In Mode 2 quella RAM è il frame precedente e va scritta con la polarità
     * del piano BW, cioè a bianco. In Mode 1 invece è ancora l'accent, e
     * lasciarla accesa manderebbe ogni pixel su LUT2 o LUT3 per la Table 6-4:
     * nella LUT riassegnata quelle due sono a zero, quindi le varianti Mode 1
     * non dipingerebbero niente e la misura sarebbe muta per costruzione.
     */
    const bool mode2 = (t.sequence & 0x08) != 0;
    patternFill(0x46, mode2 ? bwPatternFor(true) : 0x77,
                mode2 ? "prec. " : "accent");

    /**
     * Le fasce delle varianti già passate vengono riscritte in RAM: il refresh
     * ridipinge secondo la RAM, quindi la sola fascia corrente cancellerebbe
     * le precedenti, e la scheda chiede di confrontarle fra loro. Va fatto
     * prima di areaPass, che scrive la propria e poi lancia il refresh.
     */
    for (uint8_t d = 1; d < t.digit; ++d)
      writeStripeWithDigit(0x24, (uint16_t)(LUTP_H * (d - 1)), LUTP_H,
                           bwByteFor(false), numeroFascia[d - 1], bwByteFor(true));

    /**
     * Le due strade per le tensioni, una per variante quando l'esaustivo è
     * acceso: se rendono lo stesso nero, quella con 0x04 è utilizzabile dal
     * driver, che a metà vita non può fare uno SWRESET.
     */
    if (cfg.esaustivo)
    {
      cfg.lutSwreset  = (v == 0);
      cfg.lutTensioni = (v != 0);
      logLine("tensioni: %s", cfg.lutSwreset ? "SWRESET, tornano ai POR da se'"
                                             : "0x04 coi POR dopo 0x32");
    }
    loadWaveformLut(t.lut, cfg.lutBorder);
    powerOnExplicit();

    snprintf(etichettaLut[v], sizeof(etichettaLut[v]),
             "LUT custom %d di %d: %s", v + 1, VARIANTI_LUT, t.lutName);
    lutPartialMs[v] = areaPass(etichettaLut[v], 0, t.y, SRC, LUTP_H,
                               bwByteFor(false), 0, bwByteFor(true),
                               t.sequence, 30000);
    /**
     * Il numero che areaPass ha prenotato e scritto sulla fascia: serve alle
     * passate successive, che ridipingono le fasce già fatte e devono
     * rimetterci lo STESSO numero, non la cifra della passata.
     */
    numeroFascia[v] = numeroSulVetro;
  }

  Serial.println(F("\nesito della sonda del partial con LUT custom:"));
  Serial.println(F("  variante                     0x22    BUSY    cifra"));
  for (int v = 0; v < VARIANTI_LUT; ++v)
  {
    const Variante& t = varianti[v];
    if (lutPartialMs[v] < 0)
      Serial.printf("  %-24s   0x%02X   non conclusa   %u\n",
                    t.lutName, t.sequence, (unsigned)t.digit);
    else
      Serial.printf("  %-24s   0x%02X   %6ld ms   %u\n",
                    t.lutName, t.sequence, (long)lutPartialMs[v], (unsigned)t.digit);
  }
  if (refreshMs > 0)
    logLine("riferimento: refresh pieno 0xF7 %ld ms", (long)refreshMs);

  int veloci = 0;
  for (int v = 0; v < VARIANTI_LUT; ++v)
    if (lutPartialMs[v] >= 0 && lutPartialMs[v] < 3000)
      ++veloci;
  look("quale fascia e' diventata nera? con 0x3C = 0xC0 la cornice non deve lampeggiare");
  logLine("passate sotto i 3 s: %d", veloci);

  // I registri restano con una LUT custom in RAM: un reset rimette l'OTP.
  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  Serial.println(F("  reset hardware finale: le LUT tornano quelle dell'OTP"));
}

/**
 * Probe del quarto colore per livello di sorgente, la misura che il frame a
 * bande non può fare. Su questo pannello conta: il codice modello EL122H6W4A ha
 * campo colore 4, cioè BWRY nominale, contro un vetro che dice "BWR normal".
 * Le bande mostrano cosa rende ogni coppia di bit sotto la waveform dell'OTP,
 * ma se lì LUT3 è aliasata su LUT2 — quello che la Table 6-4 descrive per un
 * film a tre colori — le bande 3 e 4 escono identiche anche su un film a
 * quattro pigmenti.
 *
 * Qui LUT2 va a VSH1 e LUT3 a VSH2, stessi tempi, e le due metà della banda
 * portano la coppia di bit che le seleziona: metà alta (accent=1, BW=1) su
 * LUT3, metà bassa (accent=1, BW=0) su LUT2.
 *
 * Mode 1 (0x22 = 0xC4) e non Mode 2, perchè è Mode 1 che legge le due RAM
 * secondo la Table 6-4; in Mode 2 sarebbero (precedente, nuovo) e la coppia di
 * bit non selezionerebbe più la LUT che interessa. Bit 4 spento, o l'OTP
 * sovrascrive la LUT appena caricata. Le tensioni restano quelle di fabbrica:
 * VSH1 e VSH2 stanno ai byte 106 e 107, fuori dalla portata di 0x32.
 */
static void probeFourthColorLevels()
{
  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  forceRamOptionsNormal();

  /**
   * Il riquadro lo dipinge il fondo bianco, non la passata dei livelli: in
   * LUT_LEVELS le LUT0 e LUT1 sono a zero, quindi lì un riquadro bianco e nero
   * non verrebbe pilotato. Disegnato adesso, con la waveform dell'OTP, resta sul
   * vetro perchè la passata dei livelli non tocca quei pixel.
   */
  patternFill(0x47, 0xF7, "B/N   ");
  patternFill(0x46, 0x77, "accent");
  const Frame fFondo = frame("livelli: fondo bianco, con la waveform dell'OTP");
  if (runRefresh(0xF7, "una ventina di secondi", 0, &fFondo) < 0)
  {
    Serial.println(F("fondo bianco non riuscito: probe dei livelli abbandonato"));
    return;
  }

  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  forceRamOptionsNormal();
  loadWaveformLut(LUT_LEVELS, 0x01);

  /**
   * Accent acceso su tutta la banda, e il piano BW che distingue le due metà:
   * la metà alta con BW=1 seleziona LUT3, quella bassa con BW=0 seleziona LUT2.
   * Con LUT0 e LUT1 a zero nessun pixel bianco o nero viene pilotato, quindi
   * qualunque cosa si veda viene dalle due LUT dell'accent.
   */
  patternFill(0x46, 0xF7, "accent");                          // accent = 1 ovunque
  patternFill(0x47, 0xF7, "B/N   ");                          // BW = 1 ovunque
  writeBoxConst(0x24, 0, LEVELS_SPLIT, SRC,
                (uint16_t)(cfg.gate - LEVELS_SPLIT), 0x00);  // metà bassa: BW = 0

  powerOnExplicit();
  setRamWindow(0, 0, SRC, cfg.gate);
  const Frame fLivelli = frameSenzaBadge("livelli: LUT2 a VSH1 contro LUT3 a VSH2", D_QUARTO);
  levelsMs = runRefresh(0xC4, "Mode 1 senza ricarica di LUT, qualche secondo", 0, &fLivelli);
  if (levelsMs >= 0)
    Serial.printf("  BUSY %ld ms (200 frame attesi: la LUT non ripete il gruppo)\n",
                  (long)levelsMs);

  look("le due meta' hanno colore diverso? alta = accent 1 BW 1 = LUT3 = VSH2, bassa = accent 1 BW 0 = LUT2 = VSH1");
  logLine("split a y=%u", (unsigned)LEVELS_SPLIT);

  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  Serial.println(F("  reset hardware finale: le LUT tornano quelle dell'OTP"));
}
// --- banchi di waveform per temperatura ------------------------------

/**
 * Range di esercizio dichiarato dal produttore, concorde in tre fonti di
 * docs/: Newton-PRO_Data-sheet §3.1 "Operating Temperature — BWRY: 0°C ~ 40°C"
 * e le due Specifications. Fuori di qui nessun TR dell'OTP copre la richiesta,
 * ed è la condizione che rende la passata di controllo un rifiuto atteso.
 */
static const int16_t TEMP_RANGE_MIN = 0;
static const int16_t TEMP_RANGE_MAX = 40;

/** Vero se la temperatura chiesta cade fuori dal range di esercizio. */
static bool tempFuoriRange(int16_t degC)
{
  return degC < TEMP_RANGE_MIN || degC > TEMP_RANGE_MAX;
}

/**
 * Pavimento del timeout per le passate di questa sonda, l'unica che ne chiede
 * uno più lungo. Un banco per temperatura può essere più corto di quello
 * ambiente, che è la ragione della sonda, ma può anche essere più LUNGO: al
 * freddo il pigmento migra più lentamente e le waveform dei range bassi sono
 * più lunghe. Coi 40 s del default la passata fredda scade prima di
 * concludersi, e la misura va persa proprio dove il banco è diverso dagli
 * altri.
 */
static const uint32_t TEMP_TIMEOUT_MS = 120000;

/**
 * Timeout effettivo della sonda: il pavimento sopra, o il timeout scelto dal
 * menu se è più alto. Alzare cfg.timeoutMs non deve accorciare proprio la
 * passata che ne ha più bisogno.
 */
static uint32_t tempTimeoutMs()
{
  return cfg.timeoutMs > TEMP_TIMEOUT_MS ? cfg.timeoutMs : TEMP_TIMEOUT_MS;
}

/**
 * Cifra della fascia di ogni passata: le prime tre ne dipingono una, l'ultima
 * è la passata di controllo e non ha cifra propria — riscrive a bianco quella
 * della terza, vedi probeTemperatureBanks(). Tre punti dentro il range bastano
 * alla domanda "l'OTP ha più di un banco?": se gli estremi e il centro danno la
 * stessa durata, un quarto in mezzo non cambierebbe la conclusione.
 */
static const uint8_t TEMP_SWEEP_DIGIT[TEMP_PASSES] = { 1, 2, 3, 0 };
static int32_t tempSweepMs[TEMP_PASSES]            = { -1, -1, -1, -1 };

/**
 * Forza la temperatura con cui il controller sceglie la waveform.
 *
 * 0x18 = 0x48 mette il sensore su esterno: da quel momento il controller non
 * campiona il proprio sensore ma legge il registro scritto con 0x1A. Formato
 * del paragrafo 6.8.3: 12 bit in complemento a due, valore = gradi per sedici,
 * quindi 25 gradi = 0x190 e 50 gradi = 0x320. Sul bus i 12 bit vanno come li
 * restituisce 0x1B in lettura, A[11:4] nel primo byte e A[3:0] nei bit alti
 * del secondo.
 */
static void setForcedTemperature(int16_t degC)
{
  const int16_t raw = (int16_t)(degC * 16);
  writeCommand(0x18);
  writeData(0x48);
  writeCommand(0x1A);
  writeData((uint8_t)((raw >> 4) & 0xFF));
  writeData((uint8_t)((raw << 4) & 0xF0));
}

/** Rimette il sensore interno, che è quello che il driver usa. */
static void restoreInternalTemperature()
{
  writeCommand(0x18);
  writeData(0x80);
}

/**
 * Sonda dei banchi di waveform per temperatura.
 *
 * Il §6.9 del datasheet, "Waveform LUT Searching Mechanism", dà l'OTP per
 * capace di 34 set, WS0..WS33, uno per range di temperatura TR0..TR33, che il
 * controller sceglie leggendo la temperatura e caricando il set dell'ULTIMO
 * range che corrisponde. Ogni passata di questo test ha girato col sensore
 * interno a temperatura ambiente, quindi ha esercitato un set su 34.
 *
 * Perchè conta per il partial: qui non si inventa una waveform, si chiede al
 * silicio una di quelle di fabbrica, tarate su questo film. Sui pannelli a
 * inchiostro le waveform calde sono più corte, perchè il pigmento migra prima:
 * se una lo è, il partial esiste senza rischio e senza LUT non qualificate.
 *
 * Le prime tre passate stampano una fascia numerata; l'ultima è la passata di
 * controllo e va messa fuori dal range dichiarato 0..40 gradi: il datasheet
 * avverte che senza un range corrispondente "display will not be updated",
 * quindi un BUSY brevissimo là è un rifiuto e non una waveform veloce. Quella
 * passata riscrive a bianco la fascia dell'ultima riuscita, così il
 * discriminante è la sparizione del numero della terza fascia. Con un valore
 * dentro 0..40 il
 * rifiuto non è atteso e il controllo non discrimina: la sonda lo dice.
 *
 * Rischio nessuno: sono waveform di fabbrica, e il reset riporta il sensore a
 * quello interno.
 */
static void probeTemperatureBanks()
{
  static uint16_t numeroFascia[TEMP_PASSES] = { 0 };
  Serial.printf ("waveform si allunga, e per questo il timeout qui è %lu ms.\n",
                 (unsigned long)tempTimeoutMs());

  const uint16_t fasciaH = (uint16_t)(cfg.gate / 4);
  if (fasciaH < GLYPH_H * LABEL_SCALE + 8)
  {
    Serial.printf("fasce di %u righe: troppo basse per la cifra, sonda saltata\n",
                  (unsigned)fasciaH);
    return;
  }

  /**
   * Le fasce si accumulano sul vetro, ma ogni passata è un frame proprio: sono
   * quattro temperature diverse e vederne solo l'ultima non dice quale banco ha
   * risposto. Le etichette stanno in un array statico perchè portano il valore.
   */
  static char etichettaTemp[TEMP_PASSES][56];

  /**
   * Nessuna baseline: ogni passata porta la RAM a bianco più le proprie fasce,
   * quindi se dipinge stabilisce lei il fondo, e se non dipinge il vetro resta
   * quello di prima, che è l'esito. Un refresh in meno.
   */
  for (int t = 0; t < TEMP_PASSES; ++t)
  {
    const int16_t degC  = cfg.temp[t];
    const uint8_t digit = TEMP_SWEEP_DIGIT[t];
    const int16_t raw   = (int16_t)(degC * 16);
    snprintf(etichettaTemp[t], sizeof(etichettaTemp[t]),
             "banco per temperatura: %d gradi%s", (int)degC,
             tempFuoriRange(degC) ? ", FUORI RANGE" : "");

    Serial.printf("\n-- %d gradi (0x1A = 0x%03X)%s\n", (int)degC, (unsigned)(raw & 0xFFF),
                  digit ? ""
                        : (tempFuoriRange(degC)
                             ? ", fuori range: qui un BUSY corto è un rifiuto"
                             : ", passata di controllo, ma dentro 0..40: il rifiuto"
                               " non è atteso"));

    resetPanel();
    initPanel(CAND_DRIVER, cfg.gate);
    forceRamOptionsNormal();
    setForcedTemperature(degC);
    patternFill(0x47, bwPatternFor(true), "B/N   ");
    patternFill(0x46, 0x77, "accent");

    if (digit)
    {
      /**
       * Le fasce si accumulano: 0xF7 è un refresh pieno e ridipinge tutto
       * secondo la RAM, quindi la sola fascia corrente cancellerebbe le
       * precedenti. Con l'accumulo una passata rifiutata lascia sul vetro le
       * fasce dell'ultima riuscita, ed è il confronto che serve.
       */
      numeroFascia[digit - 1] = prenotaNumero(frameCon(etichettaTemp[t], D_DIPINTO));
      for (uint8_t d = 1; d <= digit; ++d)
        writeStripeWithDigit(0x24, (uint16_t)(fasciaH * (d - 1)), fasciaH,
                             bwByteFor(false), numeroFascia[d - 1], bwByteFor(true));
    }
    else
    {
      /**
       * Le fasce 1 e 2 come sono, e la 3 riportata a bianco: se la passata
       * viene eseguita il numero della terza fascia sparisce dal vetro, se
       * viene rifiutata
       * resta. Il discriminante è quello, e non serve una fascia in più.
       */
      for (uint8_t d = 1; d <= 2; ++d)
        writeStripeWithDigit(0x24, (uint16_t)(fasciaH * (d - 1)), fasciaH,
                             bwByteFor(false), numeroFascia[d - 1], bwByteFor(true));
      prenotaNumero(frameCon(etichettaTemp[t], D_DIPINTO));
      writeBoxConst(0x24, 0, (uint16_t)(fasciaH * 2), SRC, fasciaH, bwByteFor(true));
    }

    // finestra piena: la variabile di questa sonda è la waveform, non l'area
    setRamWindow(0, 0, SRC, cfg.gate);

    /**
     * 0xF7 e non una sequenza ridotta: i bit 5 e 4 sono "load temperature" e
     * "load LUT", e servono entrambi, perchè è proprio la ricarica che deve
     * andare a cercare il set del range forzato.
     */
    const Frame f = frameCon(etichettaTemp[t], D_DIPINTO);
    tempSweepMs[t] = runRefresh(0xF7, "0xF7 con temperatura forzata",
                                tempTimeoutMs(), &f);

    /**
     * Timeout non vuol dire misura persa: il refresh è ancora in corso, e il
     * reset della passata successiva a metà transizione lascerebbe il pigmento
     * in uno stato non noto e quella passata senza baseline. Si attende la
     * discesa e si stampa la durata vera, che è il dato interessante proprio
     * perchè esce dal quadro delle altre.
     */
    if (tempSweepMs[t] < 0 && digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      const uint32_t scaduto = tempTimeoutMs();
      const int32_t  coda    = waitBusy(scaduto);
      if (coda >= 0)
        logLine("BUSY sceso altri %ld ms dopo il timeout: il banco di questo range dura %ld ms, cioè più di quello a temperatura ambiente",
                      (long)coda, (long)((int32_t)scaduto + coda));
      else
        logLine("BUSY ancora attivo dopo altri %lu ms: il pannello resta a metà transizione", (unsigned long)scaduto);
    }
    restoreInternalTemperature();
  }

  Serial.println(F("\nesito della sonda dei banchi per temperatura:"));
  Serial.println(F("  temperatura   0x1A    BUSY       cifra"));
  for (int t = 0; t < TEMP_PASSES; ++t)
  {
    const int16_t raw = (int16_t)(cfg.temp[t] * 16);
    if (tempSweepMs[t] < 0)
      Serial.printf("  %4d gradi   0x%03X   non conclusa   %u\n",
                    (int)cfg.temp[t], (unsigned)(raw & 0xFFF),
                    (unsigned)TEMP_SWEEP_DIGIT[t]);
    else
      Serial.printf("  %4d gradi   0x%03X   %6ld ms   %u\n",
                    (int)cfg.temp[t], (unsigned)(raw & 0xFFF),
                    (long)tempSweepMs[t], (unsigned)TEMP_SWEEP_DIGIT[t]);
  }
  if (refreshMs > 0)
    Serial.printf("  riferimento: refresh pieno a temperatura ambiente %ld ms\n",
                  (long)refreshMs);

  int32_t minMs = -1, maxMs = -1;
  for (int t = 0; t < TEMP_PASSES - 1; ++t)   // le passate che dipingono la propria fascia
  {
    if (tempSweepMs[t] < 0) continue;
    if (minMs < 0 || tempSweepMs[t] < minMs) minMs = tempSweepMs[t];
    if (maxMs < 0 || tempSweepMs[t] > maxMs) maxMs = tempSweepMs[t];
  }
  if (minMs > 0 && maxMs > 0)
  {
    Serial.printf("  scarto fra la più corta e la più lunga: %ld ms\n",
                  (long)(maxMs - minMs));
    if ((maxMs - minMs) > 2000)
      logLine("l'OTP ha più di un set, e uno è più corto: la fascia di quella passata lo dice, vedi il blocco qui sotto");
    else
      logLine("scarto trascurabile: un solo set di waveform per tutto il range utile, e la temperatura non è una leva su questo pannello");
  }

  look("quali fasce hanno dipinto? e il numero della passata di controllo, fuori range, e' ancora sul vetro?");
}
// --- RAM ping-pong: l'interruttore che il datasheet lega al differenziale ---

/**
 * Sonda del RAM ping-pong via 0x37, F[6].
 *
 * 0x37 sono dieci byte che di norma vengono dall'OTP: B[7:0]..F[3:0] portano il
 * bit di Display Mode per ognuno dei 36 stadi della waveform, F[6] abilita il
 * RAM ping-pong per Mode 2, e G..J sono module ID e versione della waveform. È
 * l'unico interruttore che il datasheet lega esplicitamente al differenziale,
 * con la nota che il ping-pong "is not support for Display Mode 1".
 *
 * TENTATIVO ALLA CIECA, e va letto come tale: senza read-back non si sa cosa
 * l'OTP abbia scritto in quei dieci byte, quindi scriverli significa
 * sostituirli con un'ipotesi. Solo un esito POSITIVO vale qualcosa; un esito
 * negativo non distingue "il ping-pong non serve" da "abbiamo scritto i bit
 * sbagliati". Per questo la sonda chiude con un reset hardware, che rimette i
 * valori dell'OTP.
 *
 * La fascia porta il numero del suo frame su fondo bianco: se dopo la passata
 * resta la cifra della sonda precedente, questa non ha dipinto.
 */
static void probePingPong()
{
  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
  forceRamOptionsNormal();

  /**
   * Nessuna baseline: la passata scrive una fascia BIANCA su fondo nero, e se
   * dipinge è lei a stabilire entrambi. 0x26 fa da frame precedente e riceve
   * il fondo nero, cioè quello che il frame nuovo sostituisce.
   */
  patternFill(0x47, bwPatternFor(false), "B/N   ");
  patternFill(0x46, bwPatternFor(false), "prec. ");

  /**
   * 0x37, dieci byte, e il layout conta perchè la versione precedente di questa
   * sonda lo aveva sbagliato — era il motivo per cui il README la definiva "un
   * tentativo alla cieca". Dal datasheet:
   *
   *   A[7:0]  scritto a ZERO, e' riservato
   *   B[7:0]  Display Mode per WS[7:0]      0 = Mode 1, 1 = Mode 2
   *   C[7:0]  Display Mode per WS[15:8]
   *   D[7:0]  Display Mode per WS[23:16]
   *   E[7:0]  Display Mode per WS[31:24]
   *   F[3:0]  Display Mode per WS[35:32]
   *   F[6]    RAM ping-pong: 1 abilita
   *   G..J    module ID / waveform version
   *
   * E la nota che chiude la questione: "RAM ping-pong function is not support
   * for Display Mode 1". Quindi il ping-pong va acceso INSIEME al Mode 2 su
   * tutti e trentasei gli stadi, F[3:0] compresi, e la sequenza di update deve
   * essere di Mode 2 — 0xFC lo è, perchè ha il bit 3 alto come 0xCF e 0xFF.
   * Accendere F[6] lasciando qualche stadio in Mode 1 chiede al silicio una
   * combinazione che il datasheet dichiara non supportata, e un esito negativo
   * lì non prova niente.
   */
  writeCommand(0x37);
  writeData(0x00);                                    // A: riservato
  writeData(0xFF); writeData(0xFF);                   // B, C: WS[15:0]  Mode 2
  writeData(0xFF); writeData(0xFF);                   // D, E: WS[31:16] Mode 2
  writeData(0x4F);                                    // F: ping-pong + WS[35:32] Mode 2
  writeData(0x00); writeData(0x00); writeData(0x00); writeData(0x00);

  const uint16_t fasciaH = (uint16_t)(cfg.gate / 4);
  const Frame fPp = frameCon("RAM ping-pong via 0x37, F[6] = 1", D_DIPINTO);
  writeStripeWithDigit(0x24, 0, fasciaH, bwByteFor(true), prenotaNumero(fPp),
                       bwByteFor(false));

  // finestra piena, come nelle altre sonde che misurano una waveform
  setRamWindow(0, 0, SRC, cfg.gate);
  const int32_t ms = runRefresh(0xFC,
                                "sotto il secondo solo se l'ipotesi sui bit è giusta",
                                40000, &fPp);
  if (ms >= 0)
    logLine("BUSY %ld ms", (long)ms);
  if (refreshMs > 0)
    logLine("riferimento: refresh pieno %ld ms", (long)refreshMs);
  if (ms > 0 && refreshMs > 0 && ms < refreshMs / 4)
    verdict("il ping-pong accorcia il refresh: il controller alterna i due piani come current e previous");
  else
    verdict("nessun guadagno col ping-pong acceso in Mode 2 su tutti gli stadi: il meccanismo non c'e' su questo silicio");

  look("la fascia in cima e' diventata bianca? conta insieme al colore la durata: breve senza dipingere e' un refresh ingoiato");

  logLine("reset hardware: rimetto i valori OTP dopo il tentativo su 0x37");
  resetPanel();
  initPanel(CAND_DRIVER, cfg.gate);
}
// --- deep sleep: quale parametro accetta il modulo -------------------

/**
 * Prova decisiva di sordità: si manda al controller un'operazione che da
 * sveglio si vedrebbe di sicuro sul BUSY — riempimento della RAM col pattern
 * più la master activation di un refresh — e si guarda se il BUSY fa quello
 * che farebbe se il comando fosse stato eseguito.
 *
 * Serve perchè il livello del BUSY da solo non prova niente: il datasheet lo
 * dà alto durante il deep sleep, ma su questa board un pin non cablato sta alto
 * lo stesso, ed è proprio il caso che il test deve saper distinguere sulla coda
 * che non risponde. Il criterio dipende quindi da come il BUSY sta mentre il
 * controller dorme:
 *
 *   BUSY alto  -> da sveglio il refresh finirebbe e il BUSY scenderebbe. Se
 *                 non scende entro la finestra, il comando non è stato eseguito.
 *   BUSY basso -> da sveglio il refresh alzerebbe il BUSY entro un secondo. Se
 *                 non si alza, il comando non è stato eseguito.
 *
 * Se il controller esegue, la banda diventa nera e si vede: è comunque un
 * esito, non un danno. Ritorna true se ha ignorato tutto, cioè se dorme.
 */
static bool deepSleepIgnoresCommands(uint32_t window_ms)
{
  const bool busyAlto = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  Serial.printf("   prova di sordità: BUSY %s, si manda pattern nero + refresh\n",
                busyAlto ? "alto" : "basso");

  writeCommand(0x47);   // pattern sul piano B/N: da sveglio riempirebbe la RAM
  writeData(0x77);      // tutto nero
  writeCommand(0x22);
  writeData(0xF7);
  writeCommand(0x20);   // master activation

  if (busyAlto)
  {
    const int32_t ms = waitBusy(window_ms);
    if (ms < 0)
    {
      Serial.printf("   BUSY mai sceso in %lu ms: il comando non è stato eseguito, dorme\n",
                    (unsigned long)window_ms);
      return true;
    }
    Serial.printf("   BUSY sceso dopo %ld ms: il refresh è stato eseguito, NON dorme\n",
                  (long)ms);
    return false;
  }

  const uint32_t t0 = millis();
  while ((millis() - t0) < 2000)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      // ha preso il comando: si attende la fine per non lasciare il pannello a metà
      const int32_t ms = waitBusy(window_ms);
      logLine("BUSY salito e refresh durato %ld ms: NON dorme", (long)ms);
      return false;
    }
    delay(1);
  }
  logLine("BUSY mai salito in 2000 ms: la master activation è stata ignorata, dorme");
  return true;
}

/**
 * Sonda del deep sleep: non "quale parametro alza il BUSY", che è solo un
 * livello su un pin, ma se il controller si addormenta davvero e se poi si
 * risveglia e torna a stampare. Un deep sleep da cui non si torna non è un
 * risparmio, è una banda morta fino al power cycle.
 *
 * Il datasheet SSD1677 definisce per 0x10 solo A[1:0] = 00 (normale) e 11, cioè
 * 0x03, e dà il BUSY alto in deep sleep; 0x11 ha A[1:0] = 01, che nella tabella
 * non c'è ed è il valore del driver stock 1160c. Quale dei due il modulo
 * accetti decide il byte che hibernate() deve mandare.
 *
 * In ordine: prova i due parametri, ognuno da reset più init perchè dal deep
 * sleep non si esce altrimenti e senza ripartire da sveglio la seconda misura
 * leggerebbe la coda della prima; sul parametro scelto manda da addormentato un
 * refresh intero e guarda il BUSY più a lungo di quanto un refresh duri; lascia
 * una finestra ferma per il multimetro, che è l'unica prova del risparmio;
 * cronometra il risveglio e stampa una banda nera, che o arriva o no; e infine
 * riaddormenta, così il test finisce nello stato in cui hibernate() lascerebbe
 * il pannello.
 *
 * QUESTA SONDA VEDE UN SOLO CONTROLLER: il CS dell'altra coda resta alto, quindi
 * l'altro non riceve mai 0x10 e resta sveglio. Nel driver hibernate() deve
 * mandarlo a entrambi; il reset invece è in parallelo sulle due code.
 */
static void probeDeepSleep()
{
  Serial.println(F("\ndeep sleep: si addormenta, e soprattutto si sveglia?"));

  /**
   * Il gate va chiuso qui a mano: la prova della sordità manda 0x22 e 0x20 a
   * SPI diretta, senza passare da runRefresh(), quindi se il modulo non
   * ignorasse i comandi ridipingerebbe il vetro senza che nessuno abbia
   * guardato la schermata precedente.
   */
  chiudiFrame(nullptr);

  // finestra di attesa più lunga di un refresh pieno, che è il metro della prova
  const uint32_t finestra = (uint32_t)(refreshMs > 0 ? refreshMs : 20000) + 8000;

  static const uint8_t PARAM[2] = { 0x03, 0x11 };
  static const char* PARAM_DESC[2] =
  {
    "A[1:0]=11, l'unico valore che il datasheet definisce",
    "A[1:0]=01, fuori tabella sul 1677: e' il Mode 1 del SSD1683"
  };
  bool candidato[2] = { false, false };

  for (uint8_t k = 0; k < 2; ++k)
  {
    Serial.printf("\n-- 0x10 = 0x%02X  (%s)\n", PARAM[k], PARAM_DESC[k]);
    // reset hardware più init: si riparte svegli e da uno stato noto
    initPanel(CAND_DRIVER, cfg.gate);
    writeCommand(0x10);
    writeData(PARAM[k]);
    delay(50);

    const bool busyAlto = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
    if (PARAM[k] == 0x03)
      sleepBusy03 = busyAlto;
    else
      sleepBusy11 = busyAlto;
    Serial.printf("   BUSY dopo il comando: %s\n",
                  busyAlto ? "ALTO, come il datasheet descrive il deep sleep"
                           : "basso, che il datasheet non prevede per il deep sleep");

    if (busyAlto)
    {
      candidato[k] = true;
      logLine("livello compatibile col deep sleep, ma un pin non cablato fa lo stesso: la conferma arriva dalla prova lunga più sotto");
      continue;
    }

    /**
     * Col BUSY basso la prova costa poco: da sveglio il pattern hardware lo
     * alza entro qualche decina di ms, e se non lo alza i comandi non arrivano.
     *
     * Vale però solo dove il pattern è supportato, e la prova di vita fatta a
     * inizio test lo dice: senza quel comando un BUSY fermo non distingue
     * "dorme" da "il comando non esiste", e prenderlo per sonno sarebbe un
     * falso positivo. In quel caso si rimanda alla prova decisiva, che usa la
     * master activation e quella un controller vivo la esegue sempre.
     */
    if (patternMs47 < 0)
    {
      candidato[k] = true;
      verdict("il pattern hardware non è supportato su questo pannello: la prova rapida non decide, risponde la prova lunga più sotto");
      continue;
    }
    writeCommand(0x47);
    writeData(0x77);
    const uint32_t t0 = millis();
    bool reagisce = false;
    while ((millis() - t0) < 300)
    {
      if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { reagisce = true; break; }
      delay(1);
    }
    if (reagisce)
    {
      const int32_t ms = waitBusy(5000);
      verdict("il pattern ha alzato il BUSY per %ld ms: il controller esegue ancora, NON dorme", (long)ms);
    }
    else
    {
      candidato[k] = true;
      verdict("il pattern non ha alzato il BUSY: i comandi non arrivano più, dorme");
    }
  }

  // si preferisce 0x03, che è il valore del datasheet, se entrambi sono plausibili
  int scelto = -1;
  for (uint8_t k = 0; k < 2; ++k)
  {
    if (candidato[k]) { scelto = k; break; }
  }
  if (scelto < 0)
  {
    logLine("nessuno dei due parametri addormenta il controller: hibernate() sarebbe un rischio senza guadagno, misura la corrente");
    sleepParamOk = 0x00;
    return;
  }
  sleepParamOk = PARAM[scelto];
  logLine("parametro scelto per il resto della sonda: 0x10 = 0x%02X", sleepParamOk);
  /**
   * Il code point A[1:0] = 01 NON esiste sul SSD1677: il datasheet dà per 0x10
   * solo 00 (Normal) e 11 (Enter Deep Sleep). Sul SSD1683 invece è il Deep
   * Sleep Mode 1, quello che "Retain RAM data but cannot access the RAM". Se il
   * controller si addormenta con quel valore, questo die NON è una Rev 1.0 —
   * ed è lo stesso indizio che dà il secondo byte di 0x21, che l'init di
   * fabbrica SOLUM manda e che la Rev 1.0 non definisce.
   *
   * Ha una conseguenza diretta sul driver: se esiste il Mode 1, la RAM immagine
   * sopravvive al sonno e il riarmo di _initial_write al risveglio è inutile;
   * col solo Mode 2 è invece necessario, perchè la tabella elettrica dà
   * "Cannot retain RAM data".
   */
  if ((sleepParamOk & 0x03) == 0x01)
    verdict("0x10 A[1:0]=01 accettato, come sul 9.7\": il die si comporta da SSD1683 e col Mode 1 la RAM sopravvive");
  else
    verdict("solo A[1:0]=11: il deep sleep non ritiene la RAM, _initial_write va riarmato al risveglio");

  // prova decisiva sul parametro scelto, ripartendo da sveglio
  Serial.println(F("\n-- prova decisiva: da addormentato, un refresh intero viene eseguito?"));
  initPanel(CAND_DRIVER, cfg.gate);
  writeCommand(0x10);
  writeData(sleepParamOk);
  delay(50);
  sleepIgnoresCmd = deepSleepIgnoresCommands(finestra);

  if (!sleepIgnoresCmd)
  {
    verdict("il controller esegue anche dopo 0x10: quello sul BUSY non e' deep sleep");
    return;
  }

  /**
   * Finestra ferma per il multimetro. È l'unico modo di sapere se il deep sleep
   * serve a qualcosa: il firmware può dire che il controller ignora i comandi,
   * non quanto assorbe. Si misura sul 3V3 che alimenta il pannello, e va tenuto
   * presente che l'altro controller qui è sveglio, quindi il valore letto è il
   * consumo di uno che dorme più uno che sta fermo.
   */
  look("l'immagine e' intatta? e MISURA ORA LA CORRENTE sul 3V3 del pannello: e' l'unica prova del risparmio, e il firmware non puo' farla");
  logLine("il controller resta addormentato per %lu s",
                    (unsigned long)(cfg.sleepMs / 1000));
  for (uint32_t left = cfg.sleepMs / 1000; left >= 5; left -= 5)
  {
    logDetail("%lu s", (unsigned long)left);
    delay(5000);
  }

  // risveglio cronometrato: è il costo che il firmware paga a ogni sonno
  Serial.println(F("\n-- risveglio: reset hardware più init"));
  const uint32_t tWake = millis();
  initPanel(CAND_DRIVER, cfg.gate);
  wakeInitMs = (int32_t)(millis() - tWake);
  logLine("reset più init in %ld ms", (long)wakeInitMs);

  /**
   * Refresh di prova a banda nera. Il nero è distinguibile da qualunque cosa ci
   * fosse prima, quindi se arriva il ciclo dormi / svegliati / stampa funziona
   * per intero, che è la sola cosa che il firmware deve sapere.
   */
  patternFill(0x47, 0x77, "B/N   ");   // piano B/N tutto nero
  patternFill(0x46, 0x77, "accent");   // accent spento
  const Frame fSleep = frameCon("deep sleep: refresh di prova dopo il risveglio", D_DIPINTO);
  wakeRefreshMs = runRefresh(0xF7, "refresh di prova dopo il risveglio, una ventina di secondi",
                             0, &fSleep);

  look("la banda e' diventata NERA? se no, dal deep sleep non si torna senza un power cycle");

  // stato finale come lo lascerebbe hibernate()
  writeCommand(0x10);
  writeData(sleepParamOk);
  delay(50);
  Serial.printf("\ncontroller riaddormentato con 0x10 = 0x%02X, BUSY %s\n",
                sleepParamOk,
                digitalRead(PIN_BUSY) == BUSY_ACTIVE ? "alto" : "basso");
}

/**
 * Prova a pilotare il secondo controller senza un secondo chip select,
 * sommando CMD_OFFSET a ogni opcode indirizzato. È la trascrizione
 * dell'idioma di docs/openepaperlink/nrf52811_tag_fw/dualssd.cpp, dove un
 * pannello a due controller SSD sta su un solo CS e il secondo si distingue
 * solo per il bit 7 dell'opcode.
 *
 * Cosa aspettarsi, e come leggerlo:
 *   - si accende la banda che questa coda NON serve  -> ipotesi confermata: il
 *     driver va rifatto su un solo CS con gli opcode offset, e la coda muta non
 *     era rotta;
 *   - si accende la stessa banda di sempre           -> il controller di questa
 *     coda risponde a entrambe le forme, quindi l'offset non seleziona niente;
 *   - non si accende niente                          -> nessuna conferma, e
 *     restano in piedi le tre ipotesi di cablaggio.
 *
 * Il BUSY non è un testimone affidabile qui: appartiene al controller che
 * risponde ai comandi normali, e quello non viene attivato. Per questo il
 * timeout di runRefresh non è un errore ma un esito previsto, e l'attesa
 * prosegue a tempo.
 */
/**
 * Offset degli opcode diretti allo slave: è il valore del firmware di fabbrica
 * SOLUM, e la sonda elettrica del bit 7 ha già misurato se questo controller lo
 * interpreta o lo esegue.
 */
static const uint8_t CMD_OFFSET = 0x80;

static void probeSlaveByOpcodeOffset()
{
  /**
   * Il bit 7 dell'opcode è il meccanismo con cui una coppia in CASCADE
   * distingue i due chip sullo stesso chip select. Su questo pannello la
   * cascade è esclusa — SSD1683 §6.12 la dà per "800 (sources) x 300 (gates)",
   * cioè raddoppia le sorgenti e lascia i gate, e 680 < 768 — quindi un esito
   * NEGATIVO qui è atteso e conferma il quadro, invece di lasciare la domanda
   * aperta. Un esito positivo lo smentirebbe, e sarebbe la notizia.
   *
   * Vale la pena spendere il refresh perchè l'alternativa è che il PANNELLO
   * ponticelli internamente i rail fra le due COF: in quel caso il secondo chip
   * sarebbe alimentato e indirizzabile, e i 960x768 si raggiungerebbero da un
   * connettore solo. È la sola strada che lo renderebbe possibile.
   */
  logLine("bit 7 = meccanismo della cascade, che su questo pannello e' esclusa:");
  logLine("un esito negativo e' atteso; uno positivo sarebbe la notizia");

  // comandi comuni in broadcast: senza offset, come fa dualssd.cpp
  const uint8_t comune = 0x00;

  // i due chip select bassi insieme riproducono il cablaggio di fabbrica
  csBoth = true;

  /**
   * Reset e SWRESET senza offset. Attenzione al significato: con i due
   * controller su un solo chip select "senza offset" vuol dire "al master", non
   * "a entrambi". Il broadcast vero esiste solo finchè i due CS sono separati e
   * si tengono bassi insieme, cioè con SLAVE_OPCODE_BOTH_CS a 1.
   */
  cmdOffset = 0x00;
  resetPanel();
  writeCommand(0x12);
  delay(200);

  /**
   * Configurazione del MASTER. Non è un di più: è lui che genera clock e alte
   * tensioni per entrambi, ed è la sua master activation che chiude la sequenza,
   * quindi la sua banda viene scandita comunque. Lasciarlo ai default POR
   * significherebbe fargli scandire 680 gate line invece delle 384 misurate.
   */
  writeSoftStart();
  writeMux(cfg.gate);                // le 384 gate line misurate
  writeCommand(0x11);                     // entry mode
  writeData(0x03);
  writeCommand(0x3C);                     // border waveform
  writeData(0x01);
  writeCommand(0x18);                     // sensore di temperatura interno
  writeData(0x80);
  /**
   * Cascade selection: senza questo bit il master non emette CL e lo slave resta
   * senza clock, quindi non esegue niente per quanto bene lo si indirizzi.
   * A = 0x00 lascia entrambi i piani in Normal, B[4] = 1 accende la cascade.
   *
   * Va sempre al MASTER, quindi senza offset anche quando i comandi comuni sono
   * offsettati: è una proprietà del chip che genera il clock, non dello slave.
   */
  cmdOffset = 0x00;
  writeCommand(0x21);
  writeData(0x00);
  writeData(0x10);
  cmdOffset = comune;

  /**
   * Configurazione dello SLAVE, con l'offset. I due riferimenti — dualssd.cpp di
   * OEPL e GxEPD2_579c_GDEY0579Z93 di GxEPD2 — allo slave mandano soltanto entry
   * mode, finestra RAM e dati: in cascade l'analogico è disabilitato, quindi
   * soft start e sensore di temperatura non avrebbero destinatario.
   *
   * Il MUX invece glielo mandiamo, ed è una differenza voluta: in quei due
   * pannelli lo split è sulle sorgenti e le gate line sono comuni, mentre qui
   * ogni controller ha le sue 384 gate. Se non gliele si programma resta al POR
   * di 680. Se il bring-up mostrasse che lo slave non deve avere un MUX
   * proprio, è la prima riga da togliere.
   */
  cmdOffset = CMD_OFFSET;
  const uint16_t muxSlave = cfg.gate - 1;
  writeCommand(0x01);                     // driver output control -> 0x81
  writeData(uint8_t(muxSlave & 0xFF));
  writeData(uint8_t(muxSlave >> 8));
  writeData(0x00);
  writeCommand(0x11);                     // entry mode -> 0x91
  writeData(0x03);

  const uint32_t msBW  = writePlane(0x24, cfg.gate, composeRowBW);
  const uint32_t msRED = writePlane(0x26, cfg.gate, composeRowRED);
  Serial.printf("piani scritti con 0x%02X / 0x%02X: %lu + %lu ms\n",
                (unsigned)(0x24 | CMD_OFFSET), (unsigned)(0x26 | CMD_OFFSET),
                (unsigned long)msBW, (unsigned long)msRED);

  cmdOffset = comune;
  /**
   * Il riquadro va scritto a opcode|0x80, cioè allo SLAVE: scritto con gli
   * opcode nudi finirebbe sul master e ne sporcherebbe la banda proprio
   * nell'esperimento che deve dire quale controller ha eseguito. La master
   * activation invece resta al master, ed è il motivo per cui l'offset del
   * riquadro è un campo del frame e non lo stato globale.
   */
  const Frame fSlave = { "secondo controller a opcode|0x80", BADGE_NORMALE,
                         D_SLAVE, CMD_OFFSET, false };
  const int32_t ms = runRefresh(0xF7,
                                "una ventina di secondi, se il secondo controller ha preso i comandi",
                                0, &fSlave);
  cmdOffset = 0x00;
  csBoth = false;
  if (cfg.secondFfc)
    digitalWrite(PIN_CS_OTHER, HIGH);

  if (ms < 0)
  {
    logLine("BUSY fermo: atteso se il refresh e' partito sull'altro controller, il");
    logLine("cui BUSY non e' su questa coda. Attendo 25 s a tempo.");
    /**
     * Il BUSY della coda sotto test appartiene al controller che risponde ai
     * comandi normali: se il refresh è partito sull'altro, qui non si vede
     * niente e l'unico modo di attenderlo è a tempo.
     */
    delay(25000);
  }

  look("il pattern e' comparso, e su quale meta' del pannello? il riquadro e' scritto a opcode|0x80 come il resto, quindi se compare ha eseguito il secondo chip");

  slaveOpcodeMs = ms;
}

// =====================================================================
// IL BITMASK DI 0x22
// =====================================================================
/**
 * 0x22 non è un enum di sequenze: è un BITMASK DI STADI, e il datasheet ne dà
 * la tabella completa ("Display Update Sequence Option: Enable the stage for
 * Master Activation"). Gli stadi documentati:
 *
 *   0x80  enable clock
 *   0x01  disable clock
 *   0xC0  enable clock -> enable analog
 *   0x03  enable analog -> disable clock
 *   0x91  enable clock -> load LUT con DISPLAY Mode 1 -> disable clock
 *   0x99  come sopra in Mode 2
 *   0xB1  enable clock -> load temperatura -> load LUT Mode 1 -> disable clock
 *   0xB9  come sopra in Mode 2
 *   0xC7  clock + analog -> DISPLAY Mode 1 -> disable analog -> disable OSC
 *   0xCF  come sopra in Mode 2
 *   0xF7  clock + analog + load temperatura + load LUT + DISPLAY Mode 1 + ...
 *   0xFF  come sopra in Mode 2
 *
 * I valori 0xCC, 0xFC e 0xF4 che le due suite usano NON sono code point
 * documentati: sono costruzioni su quel bitmask. Avendo la tabella, lo sweep
 * diventa sistematico invece che a tentativi — si spegne UNO stadio per volta a
 * partire da 0xF7 e si guarda cosa cade — ed è il modo di trovare la sequenza
 * più corta che ancora dipinge. Sul 9.7" è quello che ha prodotto il partial.
 */
static void probeSeq22()
{
  struct Passo
  {
    uint8_t     param;
    const char* nome;
    bool        condizionale;   // solo in esaustivo
  };
  static const Passo PASSI[] =
  {
    { 0xC7, "0xC7 senza load di temperatura e LUT", false },
    { 0xCF, "0xCF come sopra in Display Mode 2",    false },
    { 0xB1, "0xB1 solo load, senza display",        true  },
    { 0x91, "0x91 load LUT senza temperatura",      true  },
  };
  static const uint8_t N = sizeof(PASSI) / sizeof(PASSI[0]);

  const int32_t pieno = (refreshMs > 0) ? refreshMs : ultimaMisura(0xF7);
  if (pieno > 0)
    logLine("riferimento: un 0xF7 pieno dura %ld ms", (long)pieno);
  else
    logLine("nessun riferimento di durata: lancia prima una sonda che faccia un 0xF7");

  const uint16_t fasciaH = (uint16_t)(cfg.gate / 4);
  if (fasciaH < GLYPH_H * LABEL_SCALE + 8)
  {
    logLine("fasce di %u righe: troppo basse per il numero, sonda saltata",
            (unsigned)fasciaH);
    return;
  }

  initPanel(CAND_DRIVER, cfg.gate);
  patternFill(0x47, bwPatternFor(true), "FONDO BIANCO");
  patternFill(0x46, 0x00,               "ACCENT SPENTO");

  static char didascalie[4][48];
  for (uint8_t k = 0; k < N; ++k)
  {
    if (PASSI[k].condizionale && !cfg.esaustivo)
    {
      logLine("%s: saltato, si esegue in esaustivo", PASSI[k].nome);
      continue;
    }
    snprintf(didascalie[k], sizeof(didascalie[k]), "%s", PASSI[k].nome);

    const uint16_t y = (uint16_t)(k * fasciaH);
    const Frame f = frame(didascalie[k]);
    /**
     * Il numero si prenota PRIMA di dipingere, perchè va anche sulla fascia:
     * senza questo la cifra sulla fascia e il numero nel riquadro sarebbero due
     * numeri diversi, e nel log ce ne sarebbe uno solo.
     */
    const uint8_t n = prenotaNumero(f);
    writeStripeWithDigit(0x24, y, fasciaH, bwByteFor(false), n, bwByteFor(true));

    setRamWindow(0, 0, SRC, cfg.gate);
    const int32_t ms = runRefresh(PASSI[k].param, nullptr, 0, &f);

    // la fascia appena dipinta diventa il frame precedente, per la passata dopo
    writeStripeWithDigit(0x26, y, fasciaH, 0x00, STRIPE_SENZA_NUMERO, 0x00);

    if (ms > 0 && pieno > 0 && ms * 2 < pieno)
      verdict("0x%02X dipinge in %ld ms contro %ld: quello stadio costa, e "
              "togliergli il load accorcia il refresh", PASSI[k].param, (long)ms, (long)pieno);
    else if (ms > 0 && pieno > 0)
      verdict("0x%02X dura %ld ms su %ld: quello stadio non pesa", PASSI[k].param,
              (long)ms, (long)pieno);
  }

  look("quali fasce hanno dipinto? una fascia nera con la durata molto sotto il "
       "refresh pieno e' una sequenza piu' corta che funziona");
  verdict("la sequenza piu' corta che dipinge e' quella che _Update_Full deve mandare");
}

#endif // DUAL_PANEL_FINDER_PROBES_WAVEFORM_H
