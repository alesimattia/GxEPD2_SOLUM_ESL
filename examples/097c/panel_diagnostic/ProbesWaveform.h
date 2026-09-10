// =============================================================================
// ProbesWaveform.h — il partial: la waveform scritta dall'MCU.
//
// Quattro sonde, in ordine di specificità crescente:
//   8  la waveform via 0x32 SENZA le tensioni, cioè la via del GDEH116T91
//   p  la catena di partial, con le sequenze esatte del driver custom
//   t  la taratura, sette varianti per schermata su bande a contatto
//   m  la passata libera, con i parametri scelti dal menu
//
// La differenza fra la 8 e la p non è cosmetica ed è la misura più importante
// di tutta la suite: un waveform setting è di 110 byte e 0x32 ne scrive 105.
// I cinque che restano sono le tensioni, e chi non le manda eredita quelle
// dell'ultimo load dall'OTP, cioè quelle della waveform di produzione tarata su
// ventiquattro secondi. Stessa LUT, stessa durata, resa diversa.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_PROBES_WAVEFORM_H
#define PANEL_DIAGNOSTIC_PROBES_WAVEFORM_H

#include "Controller.h"

// ---------------------------------------------------------------------------
// 8 — partial con LUT via 0x32, senza 0x04.
//
// Due varianti, che sono le due DIAGONALI della matrice LUT per Display Mode.
// Le celle incrociate non vengono provate perchè sono mute per costruzione: in
// Mode 2 la fascia nera cade su LUT2, a zero nella LUT riassegnata, e in Mode 1
// cade su LUT0, a zero in quella del driver. Quale delle due diagonali dipinge
// dice come il silicio legge le due RAM.
// ---------------------------------------------------------------------------

/**
 * La LUT del driver riassegnata alle LUT che la Table 6-4 usa su un pannello a
 * tre colori: LUT0 prende la waveform che nel partial sta in LUT2, cioè quella
 * che spinge nel verso opposto a LUT1, e LUT2/LUT3 restano a zero perchè un
 * frame aggiornato in partial è per forza senza accent. I valori sono gli
 * stessi, spostati di posto.
 */
static void buildTable64Waveform()
{
  copyDriverWaveform(false);
  uint8_t lut1[10], lut2[10];
  memcpy(lut1, waveform + 10, 10);
  memcpy(lut2, waveform + 20, 10);
  memcpy(waveform +  0, lut2, 10);   // LUT0, il nero
  memcpy(waveform + 10, lut1, 10);   // LUT1, il bianco
  memset(waveform + 20, 0x00, 10);   // LUT2, accent
  memset(waveform + 30, 0x00, 10);   // LUT3, accent
}

static void probePartialLutRaw()
{
  struct Variant { const char* caption; uint8_t seq22; bool mode2; bool reassigned; };
  static const Variant VARIANTS[2] =
  {
    { "PARTIAL 1 LUT DRIVER MODE 2 0X22=CC", 0xCC, true,  false },
    { "PARTIAL 2 LUT TABLE 6-4 MODE 1 0X22=C4", 0xC4, false, true  },
  };

  openScreen("partial con LUT via 0x32, senza 0x04");
  for (uint8_t v = 0; v < 2; ++v)
  {
    const Variant& t = VARIANTS[v];
    enterRaw();
    if (t.reassigned) buildTable64Waveform();
    else              copyDriverWaveform(false);
    // Nessun 0x04: è la sequenza del GDEH116T91, e la differenza con la catena
    // di partial, che invece le tensioni le manda, è la misura che serve.
    if (!applyWaveform(0xC0, false, false, false)) continue;

    // 0x26 secondo il Display Mode: in Mode 2 è il frame precedente e va a
    // bianco, in Mode 1 è ancora l'accent e a 0xFF accenderebbe il rosso
    // ovunque, mandando ogni pixel su LUT2 o LUT3, che lì sono a zero.
    fillByPattern(0x46, t.mode2 ? 0xFF : 0x00);
    fillByPattern(0x47, 0xFF);
    fillBand(0x24, (uint16_t)(BAND_H * v), BAND_H, 0x00, t.caption, 0xFF);
    setRamWindow(0, 0, SRC, GATE, 0x03);

    partialPowerOn();
    runRefresh(t.seq22, t.caption, 10000);
  }

  const int32_t full = fullRefreshReference();
  const int32_t part = lastMeasurement(0xCC);
  if (full > 0 && part > 0 && part < 3000)
    verdict("partial %ld ms contro %ld ms del pieno: %.0fx",
            (long)part, (long)full, (double)full / (double)part);
  look("quale delle due fasce e' diventata nera? [1/2/n] e il nero e' pieno?");
  waitKey();
  closeScreen();
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// p — la catena di partial, con le sequenze del driver custom.
//
// Qui non si cerca se il partial esiste: si verifica che le sequenze che il
// driver mette in _Init_Part(), _Update_Part() e drawImagePartial() reggano
// una catena, e che l'uscita dalla catena non lasci trappole.
//
// I due TESTIMONI sono il controllo: stanno fuori da ogni fascia di lavoro e
// nessun partial li indirizza, quindi se restano dove sono la confinatura per
// LUT funziona. Il testimone ROSSO risponde a una domanda in più: la waveform
// del partial non porta mai una sorgente a VSH2, che su questo film è il
// pigmento rosso, quindi il rosso già sul vetro non deve muoversi affatto.
// ---------------------------------------------------------------------------
static const int16_t CH_BAND_H   = 168;
static const int16_t CH_BAND_A_Y = 0;
static const int16_t CH_RED_Y    = 196;
static const int16_t CH_WIT_H    = 56;
static const int16_t CH_BAND_B_Y = 280;
static const int16_t CH_BLK_Y    = 476;

static uint8_t chainBand[(uint32_t)(SRC / 8) * CH_BAND_H];   // 20160 byte

static const Bitmap1bpp CHAIN_BMP = { chainBand, SRC / 8, SRC, CH_BAND_H };
static const int16_t CH_BOX_W = 64;
static const int16_t CH_BOX_H = 72;
static const int16_t CH_BOX_X = SRC - CH_BOX_W - 16;   // 880, multiplo di 8
static const int16_t CH_BOX_Y = 8;

/**
 * Riempie il buffer di fascia — 0 bianco, 1 nero, 2 righe verticali da 32 px —
 * e ci mette dentro il riquadro col numero di schermata.
 *
 * Il riquadro sta NELLA FASCIA e non nell'angolo del pannello, ed è la
 * differenza fra una lettura pulita e una falsata: disegnato fuori sarebbe un
 * gruppo di pixel pilotati fuori dall'area in prova, e la domanda "solo la
 * fascia B si è mossa?" non avrebbe più risposta. Per la stessa ragione le
 * passate della catena sospendono il riquadro di runRefresh.
 */
static void fillChainBand(uint8_t pattern, int16_t h, uint16_t number = 0)
{
  const int16_t wb = SRC / 8;
  for (int16_t r = 0; r < h; ++r)
    for (int16_t c = 0; c < wb; ++c)
    {
      uint8_t v;
      if (pattern == 0)      v = 0xFF;
      else if (pattern == 1) v = 0x00;
      else                   v = ((c / 4) & 1) ? 0xFF : 0x00;
      chainBand[(uint32_t)r * wb + c] = v;
    }
  if (number && h >= CH_BOX_Y + CH_BOX_H)
    drawNumberBox(CHAIN_BMP, CH_BOX_X, CH_BOX_Y, CH_BOX_W, CH_BOX_H, number);
}

/** Una passata della catena, col riquadro già dentro la fascia. */
static int32_t chainPass(uint8_t pattern, int16_t y, uint16_t number, const char* caption)
{
  fillChainBand(pattern, CH_BAND_H, number);
  const uint16_t held = suspendBadge();
  const int32_t ms = drawPartial(chainBand, 0, (uint16_t)y, SRC, CH_BAND_H, caption);
  resumeBadge(held);
  return ms;
}

/** Riferimento della catena: fondo bianco e i due testimoni, refresh pieno. */
static void chainReference(const char* caption)
{
  fillChainBand(1, CH_WIT_H);
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  writePlaneRect(0x26, chainBand, 0, CH_RED_Y, SRC, CH_WIT_H, true);    // testimone rosso
  writePlaneRect(0x24, chainBand, 0, CH_BLK_Y, SRC, CH_WIT_H, false);   // testimone nero
  setRamWindow(0, 0, SRC, GATE, 0x03);
  runRefresh(0xF7, caption);
}

static void probePartialChain()
{
  enterRaw();

  openScreen("catena di partial, riferimento");
  chainReference("CATENA RIFERIMENTO CON I DUE TESTIMONI");
  look("fondo bianco, fascia rossa a y=196, fascia nera a y=476: sature? [o/n]");
  waitKey();
  closeScreen();

  // Allineamento di 0x26, il passo che non si vede e senza cui non funziona
  // niente: dopo un refresh pieno quella RAM contiene l'accent, mentre il
  // partial la legge come frame precedente. Portarla uguale a 0x24 fa cadere i
  // pixel che non cambiano su LUT0 o LUT3, che nella waveform sono a zero.
  WaveformSpec spec = LUT_DEFAULT;
  if (!setWaveform(spec)) { powerOffPanel(0xC3); return; }
  fillByPattern(0x46, 0xFF);                                            // precedente bianco
  fillChainBand(1, CH_WIT_H);
  writePlaneRect(0x26, chainBand, 0, CH_BLK_Y, SRC, CH_WIT_H, false);   // tranne il nero
  logLine("0x26 allineato a 0x24: nessun refresh, il vetro non cambia");

  uint16_t n = openScreen("catena, fascia A a nero");
  chainPass(1, CH_BAND_A_Y, n, "CATENA FASCIA A NERA");
  look("fascia A nera in meno di un secondo, testimoni fermi, cornice ferma? [o/n]");
  waitKey();
  closeScreen();

  // Fra questa passata e la precedente 0x26 NON viene riallineata: se la fascia
  // torna bianca, drawImagePartial() mantiene da sè l'invariante e una catena
  // si può allungare quanto si vuole.
  n = openScreen("catena, fascia A di nuovo bianca");
  chainPass(0, CH_BAND_A_Y, n, "CATENA FASCIA A BIANCA");
  look("la fascia A e' tornata bianca senza preparazione? [o/n]");
  waitKey();
  closeScreen();

  n = openScreen("catena, fascia B a righe");
  chainPass(2, CH_BAND_B_Y, n, "CATENA FASCIA B A RIGHE");
  look("solo la fascia B e' cambiata, il resto e' fermo? [o/n]");
  waitKey();
  closeScreen();

  n = openScreen("catena, otto alternanze");
  uint32_t total = 0;
  quietMeasure = true;   // otto righe identiche non servono al registro
  for (uint8_t i = 0; i < 8; ++i)
  {
    if (i == 7) quietMeasure = false;   // l'ultima si misura come le altre
    const uint32_t t0 = millis();
    chainPass((i & 1) ? 0 : 1, CH_BAND_A_Y, n, "CATENA ALTERNANZA");
    total += millis() - t0;
  }
  logLine("otto passate in %lu ms, %lu ms per passata",
          (unsigned long)total, (unsigned long)(total / 8));
  look("testimoni e fondo come al riferimento, o sbiaditi? [o/n]");
  waitKey();
  closeScreen();

  if (cfg.exhaustive)
  {
    // La trappola che il driver chiude con _cleanColorIfPrevious(): alla fine
    // di una catena 0x26 contiene il frame precedente in polarità BW, cioè bit
    // a 1 dove il vetro è bianco, e un refresh pieno rilegge quella RAM come
    // ACCENT. Senza pulizia lo schermo deve diventare rosso.
    n = openScreen("catena, uscita SENZA pulizia di 0x26");
    fillChainBand(1, CH_BAND_H, n);
    writePlaneRect(0x24, chainBand, 0, CH_BAND_A_Y, SRC, CH_BAND_H, false);
    fullRefresh("USCITA SENZA PULIRE 0X26: ATTESO ROSSO");
    look("lo schermo e' diventato rosso? [o/n] o = la pulizia nel driver serve");
    waitKey();
    closeScreen();
  }

  // L'uscita corretta. chainReference() azzera 0x26 col pattern hardware prima
  // di riscrivere i testimoni, che è esattamente quello che il driver fa in
  // _cleanColorIfPrevious() all'inizio di ogni refresh pieno: il confronto con
  // la passata qui sopra dice se quella pulizia è necessaria o ridondante.
  openScreen("catena, uscita con pulizia di 0x26");
  chainReference("CATENA USCITA CON PULIZIA DI 0X26");
  look("il vetro e' netto come al riferimento? [o/n]");
  waitKey();
  closeScreen();

  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// t — taratura della waveform.
//
// Il pannello è diviso in sette bande orizzontali, ognuna in quattro colonne:
//
//     x    0.. 191   NERO DI RIFERIMENTO, dipinto dalla waveform dell'OTP
//     x  192.. 767   area di lavoro, la passata di partial della banda
//     x  768.. 863   BIANCO INTATTO
//     x  864.. 959   ROSSO DI RIFERIMENTO, e nella banda 1 il numero
//
// Il confronto si fa DENTRO la banda, contro il riferimento accanto: l'area di
// lavoro tocca il nero sul bordo sinistro, quindi la saturazione si giudica a
// occhio senza ricordare la schermata precedente. Le colonne di riferimento non
// vengono mai pilotate, perchè il loro bit è uguale nelle due RAM e cadono su
// LUT0 o LUT3, che sono a zero.
//
// L'ORDINE DELLA TABELLA E' L'ORDINE DI PITTURA, e non è quello delle bande: il
// VCOM scritto fuori banda resta attivo fino al prossimo load dall'OTP, quindi
// tutte le bande a VCOM di fabbrica vanno dipinte prima dello sweep.
// ---------------------------------------------------------------------------
static const uint8_t TUNE_BANDS   = 7;
static const int16_t TUNE_BAND_H  = 96;          // 7 x 96 = 672
static const int16_t TUNE_REF_W   = 192;
static const int16_t TUNE_WORK_X  = 192;
static const int16_t TUNE_WORK_W  = 576;
static const int16_t TUNE_WHITE_W = 96;
static const int16_t TUNE_RED_X   = 864;
static const int16_t TUNE_RED_W   = 96;
static const int16_t TUNE_WORK_WB = TUNE_WORK_W / 8;
static const int16_t TUNE_NUM_WB  = TUNE_RED_W / 8;

static_assert(TUNE_BANDS * TUNE_BAND_H == GATE, "le bande non tassellano il pannello");
static_assert(TUNE_REF_W + TUNE_WORK_W + TUNE_WHITE_W + TUNE_RED_W == SRC,
              "le colonne non coprono la larghezza");
static_assert(TUNE_WORK_X % 8 == 0 && TUNE_WORK_W % 8 == 0 && TUNE_RED_X % 8 == 0,
              "le X scritte in RAM devono essere multiple di 8");

static uint8_t tuneWork[(uint32_t)TUNE_WORK_WB * TUNE_BAND_H];   // 6912 byte
static uint8_t tuneNumber[(uint32_t)TUNE_NUM_WB * TUNE_BAND_H];  // 1152 byte

static const Bitmap1bpp TUNE_WORK_BMP   = { tuneWork,   TUNE_WORK_WB, TUNE_WORK_W, TUNE_BAND_H };
static const Bitmap1bpp TUNE_NUMBER_BMP = { tuneNumber, TUNE_NUM_WB,  TUNE_RED_W,  TUNE_BAND_H };

// Riquadro del numero di banda, sul lato destro dell'area di lavoro: il bordo
// sinistro resta libero per il confronto col nero di riferimento.
static const int16_t TUNE_BOX_W = 56;
static const int16_t TUNE_BOX_H = 64;
static const int16_t TUNE_BOX_X = TUNE_WORK_W - TUNE_BOX_W - 16;
static const int16_t TUNE_BOX_Y = (TUNE_BAND_H - TUNE_BOX_H) / 2;

static inline int16_t tuneBandY(uint8_t i) { return (int16_t)i * TUNE_BAND_H; }

/**
 * Una variante: cosa gira su una banda. vsh1 a 0 vuol dire "la tensione
 * vincente della fase 1", che il runner sostituisce; vgh e vcom a -1 vogliono
 * dire "non scrivere 0x03 / 0x2C", e un valore >= 0 viene scritto fuori banda
 * prima della passata, quindi resta attivo anche per le varianti successive.
 */
struct TuningVariant
{
  uint8_t     band;          // 1..TUNE_BANDS: dove viene dipinta
  const char* label;
  uint8_t     source;        // WaveformSource
  uint8_t     vsh1;
  uint16_t    resetFrames;
  uint16_t    driveFrames;
  uint8_t     cycles;
  uint8_t     frCode;
  int16_t     vgh;
  int16_t     vcom;
};

struct TuningPhase
{
  const char*          title;
  const TuningVariant* variants;
  uint8_t              count;
  uint8_t              pattern;    // 1 nero, 0 bianco; vale se cyclesBW == 0
  uint8_t              cyclesBW;   // > 0: cicli nero/bianco chiusi sul bianco
  const char*          reading;
};

// Le sette tensioni dello sweep, tutte al massimo il POR: 0x23 = 9 V e 0,2 V
// per passo. SWEEP_VSH1[band - 1] è la tensione della banda.
static const uint8_t SWEEP_VSH1[TUNE_BANDS] =
{
  0x23, 0x28, 0x2D, 0x32, 0x37, 0x3C, VSH1_15V
};

// fase 1: sweep di VSH1 a waveform corta, la stessa tabella per le due
// schermate. Cambia solo il pattern: prima al nero, poi al bianco.
static constexpr TuningVariant PHASE1[TUNE_BANDS] =
{
  { 1, "VSH1  9 V",       WS_COMPOSER, 0x23,     RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 2, "VSH1 10 V",       WS_COMPOSER, 0x28,     RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 3, "VSH1 11 V",       WS_COMPOSER, 0x2D,     RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 4, "VSH1 12 V",       WS_COMPOSER, 0x32,     RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 5, "VSH1 13 V",       WS_COMPOSER, 0x37,     RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 6, "VSH1 14 V",       WS_COMPOSER, 0x3C,     RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 7, "VSH1 15 V (POR)", WS_COMPOSER, VSH1_15V, RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
};

// fase 2: struttura, frame rate, frame, poi VGH e VCOM. La banda 1 è la LUT del
// driver, cioè il riferimento di quello che il firmware userebbe davvero.
static constexpr TuningVariant PHASE2[TUNE_BANDS] =
{
  { 1, "LUT del driver",        WS_DRIVER,   0, 0,            0,                1, FR_50HZ, -1, -1 },
  { 2, "compositore, uguale",   WS_COMPOSER, 0, RESET_FRAMES, DRIVE_FRAMES,     1, FR_50HZ, -1, -1 },
  { 3, "frame rate 25 Hz",      WS_COMPOSER, 0, RESET_FRAMES, DRIVE_FRAMES,     1, FR_25HZ, -1, -1 },
  { 4, "drive x2",              WS_COMPOSER, 0, RESET_FRAMES, DRIVE_FRAMES * 2, 1, FR_50HZ, -1, -1 },
  { 5, "drive x4",              WS_COMPOSER, 0, RESET_FRAMES, DRIVE_FRAMES * 4, 1, FR_50HZ, -1, -1 },
  { 6, "+ VGH 20 V (POR)",      WS_COMPOSER, 0, RESET_FRAMES, DRIVE_FRAMES,     1, FR_50HZ, VGH_POR, -1 },
  { 7, "+ VCOM -1,7 V",         WS_COMPOSER, 0, RESET_FRAMES, DRIVE_FRAMES,     1, FR_50HZ, -1, VCOM_17V },
};

// fase 3: il bianco. La banda 7 sta in quarta posizione perchè tutte le bande a
// VCOM di fabbrica vanno dipinte prima dello sweep del VCOM, che non è
// reversibile senza un refresh pieno.
static constexpr TuningVariant PHASE3[TUNE_BANDS] =
{
  { 1, "LUT del driver",        WS_DRIVER,      0,        0,            0,            1, FR_50HZ, -1, -1 },
  { 2, "LUT1 simmetrica",       WS_DRIVER_DUAL, 0,        0,            0,            1, FR_50HZ, -1, -1 },
  { 3, "compositore, cicli 1",  WS_COMPOSER,    VSH1_15V, RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, -1 },
  { 7, "compositore, cicli 3",  WS_COMPOSER,    VSH1_15V, RESET_FRAMES, DRIVE_FRAMES, 3, FR_50HZ, -1, -1 },
  { 4, "+ VCOM -0,9 V",         WS_COMPOSER,    VSH1_15V, RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, 0x24 },
  { 5, "+ VCOM -1,3 V",         WS_COMPOSER,    VSH1_15V, RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, 0x34 },
  { 6, "+ VCOM -1,7 V",         WS_COMPOSER,    VSH1_15V, RESET_FRAMES, DRIVE_FRAMES, 1, FR_50HZ, -1, VCOM_17V },
};

/**
 * Copertura di una tabella: ogni banda da 1 a TUNE_BANDS una volta sola. Serve
 * perchè l'ordine è quello di pittura e non quello delle bande, quindi un
 * doppione non si vedrebbe a occhio e sovrascriverebbe in silenzio la misura di
 * un'altra banda.
 */
static constexpr bool bandsCovered(const TuningVariant* v, uint8_t n)
{
  uint8_t mask = 0;
  for (uint8_t k = 0; k < n; ++k)
  {
    if (v[k].band < 1 || v[k].band > TUNE_BANDS) return false;
    const uint8_t bit = (uint8_t)(1u << (v[k].band - 1));
    if (mask & bit) return false;
    mask = (uint8_t)(mask | bit);
  }
  return mask == (uint8_t)((1u << TUNE_BANDS) - 1);
}
static_assert(bandsCovered(PHASE1, TUNE_BANDS), "PHASE1: bande duplicate o mancanti");
static_assert(bandsCovered(PHASE2, TUNE_BANDS), "PHASE2: bande duplicate o mancanti");
static_assert(bandsCovered(PHASE3, TUNE_BANDS), "PHASE3: bande duplicate o mancanti");

// Cicli nero/bianco per banda nella fase 3: un difetto appena percettibile in
// una passata sola si accumula, e ripetere il ciclo è quello che lo rende
// leggibile.
static const uint8_t TUNE_BW_CYCLES = 4;

// Banda vincente della fase 1, cioè la tensione con cui girano le altre fasi.
static uint8_t winningBand = TUNE_BANDS;

/** Area di lavoro tutta nera o tutta bianca, col riquadro del numero di banda. */
static void fillTuneWork(uint8_t pattern, uint8_t band)
{
  memset(tuneWork, pattern ? 0x00 : 0xFF, sizeof(tuneWork));
  if (band) drawNumberBox(TUNE_WORK_BMP, TUNE_BOX_X, TUNE_BOX_Y, TUNE_BOX_W, TUNE_BOX_H, band);
}

/**
 * Schermata di riferimento della taratura: colonna nera a sinistra, fondo
 * bianco, colonna rossa a destra nelle bande 2..7 e il riquadro del numero
 * nell'angolo, che qui entra nel piano nero e non costa una passata in più.
 * Il refresh pieno ricarica anche waveform e tensioni dall'OTP, quindi ogni
 * fase riparte da uno stato noto.
 */
static void tuneReference(uint16_t number)
{
  memset(tuneNumber, 0xFF, sizeof(tuneNumber));
  drawNumberBox(TUNE_NUMBER_BMP, 0, 0, TUNE_RED_W, TUNE_BAND_H, number);
  memset(tuneWork, 0x00, sizeof(tuneWork));   // tutto acceso: nero e rosso
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  for (uint8_t i = 0; i < TUNE_BANDS; ++i)
  {
    writePlaneRect(0x24, tuneWork, 0, tuneBandY(i), TUNE_REF_W, TUNE_BAND_H, false);
    // La banda 1 cede il suo slot rosso al riquadro del numero.
    if (i > 0)
      writePlaneRect(0x26, tuneWork, TUNE_RED_X, tuneBandY(i), TUNE_RED_W, TUNE_BAND_H, true);
  }
  writePlaneRect(0x24, tuneNumber, TUNE_RED_X, 0, TUNE_RED_W, TUNE_BAND_H, false);
  setRamWindow(0, 0, SRC, GATE, 0x03);
  const uint16_t held = suspendBadge();
  runRefresh(0xF7, "TARATURA RIFERIMENTO");
  resumeBadge(held);
}

/**
 * Porta 0x26 a coincidere con quello che sta sul vetro: bianco ovunque, tranne
 * la colonna nera e il riquadro. Senza, quelle zone verrebbero ridipinte a ogni
 * passata e diventerebbero più sature del nero dell'OTP, falsando il confronto.
 */
static void tuneAlignPrevious()
{
  fillByPattern(0x46, 0xFF);
  writePlaneRect(0x26, tuneNumber, TUNE_RED_X, 0, TUNE_RED_W, TUNE_BAND_H, false);
  memset(tuneWork, 0x00, sizeof(tuneWork));
  for (uint8_t i = 0; i < TUNE_BANDS; ++i)
    writePlaneRect(0x26, tuneWork, 0, tuneBandY(i), TUNE_REF_W, TUNE_BAND_H, false);
}

/** Una passata di partial sull'area di lavoro di una banda. */
static int32_t tunePass(uint8_t i, uint8_t pattern, uint8_t band, const char* label)
{
  fillTuneWork(pattern, band);
  return drawPartial(tuneWork, TUNE_WORK_X, tuneBandY(i), TUNE_WORK_W, TUNE_BAND_H, label);
}

/**
 * Carica nel controller la waveform di una variante.
 *
 * La tensione vincente della fase 1 sostituisce il campo vsh1 solo dove la
 * variante lo lascia a zero E la waveform è composta: le due varianti che
 * partono dalla LUT del driver devono girare con le tensioni del driver, o la
 * banda di riferimento non sarebbe più il riferimento di quello che il
 * firmware userebbe davvero.
 */
static bool loadVariant(const TuningVariant& v, uint8_t winningVsh1)
{
  const bool fromDriver = (v.source == WS_DRIVER || v.source == WS_DRIVER_DUAL);
  WaveformSpec spec = LUT_DEFAULT;
  spec.source      = v.source;
  spec.vsh1        = v.vsh1 ? v.vsh1 : (fromDriver ? LUT_DEFAULT.vsh1 : winningVsh1);
  spec.resetFrames = v.resetFrames;
  spec.driveFrames = v.driveFrames;
  spec.cycles      = v.cycles;
  spec.frCode      = v.frCode;
  spec.border      = 0xC0;
  spec.writeVcom   = false;
  return setWaveform(spec);
}

/** Esegue una fase: le varianti nell'ordine della tabella, una riga per banda. */
static void runTuningPhase(const TuningPhase& phase)
{
  const uint16_t number = openScreen(phase.title);
  // Il numero arriva sul vetro con una passata dedicata sulla LUT di DEFAULT
  // del driver: deve restare leggibile qualunque variante segua, anche quando
  // la banda gira a VSH1 9 V.
  memset(tuneNumber, 0xFF, sizeof(tuneNumber));
  drawNumberBox(TUNE_NUMBER_BMP, 0, 0, TUNE_RED_W, TUNE_BAND_H, number);
  WaveformSpec base = LUT_DEFAULT;
  setWaveform(base);
  const uint16_t saved = suspendBadge();
  drawPartial(tuneNumber, TUNE_RED_X, 0, TUNE_RED_W, TUNE_BAND_H, "NUMERO DI SCHERMATA");
  resumeBadge(saved);

  const uint8_t winningVsh1 = SWEEP_VSH1[winningBand - 1];
  for (uint8_t k = 0; k < phase.count; ++k)
  {
    const TuningVariant& v = phase.variants[k];
    if (!loadVariant(v, winningVsh1)) continue;
    // VGH e VCOM si scrivono fuori banda: nè 0x32 nè 0x04 li toccano, quindi
    // il valore sopravvive fino al prossimo load dall'OTP, cioè fino al
    // prossimo refresh pieno. È il motivo dell'ordine di pittura.
    if (v.vgh  >= 0) { writeCommand(0x03); writeData((uint8_t)v.vgh); }
    if (v.vcom >= 0) { writeCommand(0x2C); writeData((uint8_t)v.vcom); }

    const uint8_t i = (uint8_t)(v.band - 1);
    int32_t ms;
    const uint16_t hold = suspendBadge();
    if (phase.cyclesBW)
    {
      ms = 0;
      quietMeasure = true;   // il gruppo di cicli produce una riga sola
      for (uint8_t c = 0; c < phase.cyclesBW; ++c)
      {
        tunePass(i, 1, v.band, v.label);
        if (c == phase.cyclesBW - 1) quietMeasure = false;
        ms = tunePass(i, 0, v.band, v.label);
      }
    }
    else ms = tunePass(i, phase.pattern, v.band, v.label);
    resumeBadge(hold);

    // Il modello temporale: 20,0 ms per frame più 83 di rampa clock e analog.
    static const uint16_t PERIOD_TENTHS[6] = { 0, 400, 200, 133, 100, 80 };
    const uint16_t period = (v.frCode < 6) ? PERIOD_TENTHS[v.frCode] : 200;
    const uint16_t frames = waveformFrames();
    logLine("banda %u  %-24s %5ld ms  %3u fr  atteso %5lu",
            v.band, v.label, (long)ms, frames,
            (unsigned long)((uint32_t)frames * period / 10 + 83));
  }
  look(phase.reading);
  waitKey();
  closeScreen();
}

static const TuningPhase TUNE_PHASE_1_BLACK =
{
  "taratura fase 1, sweep VSH1 al nero", PHASE1, TUNE_BANDS, 1, 0,
  "quale banda ha il NERO PIENO contro il nero accanto? [1-7]"
};
static const TuningPhase TUNE_PHASE_1_WHITE =
{
  "taratura fase 1, le stesse bande al bianco", PHASE1, TUNE_BANDS, 0, 0,
  "quale banda torna BIANCA PULITA, senza fantasma della cifra? [1-7]"
};
static const TuningPhase TUNE_PHASE_2 =
{
  "taratura fase 2, struttura e tensioni", PHASE2, TUNE_BANDS, 1, 0,
  "1 driver | 2 stessi totali | 3 durata doppia | 4-5 piu frame | 6 VGH | 7 VCOM [o/n]"
};
static const TuningPhase TUNE_PHASE_3 =
{
  "taratura fase 3, il bianco", PHASE3, TUNE_BANDS, 0, TUNE_BW_CYCLES,
  "guarda i BORDI fra bande consecutive: quale bianco e' piu pulito? [1-7]"
};

/** Chiede la banda vincente della fase 1 e la registra. */
static void askWinningBand()
{
  winningBand = (uint8_t)askNumber("banda vincente della fase 1", 1, TUNE_BANDS, winningBand);
  logLine("fase 1: vince la banda %u, VSH1 0x%02X", winningBand, SWEEP_VSH1[winningBand - 1]);
}

static void probeTuning()
{
  Serial.println(F("\n  taratura: 1 sweep VSH1 | 2 struttura | 3 bianco | a tutte"));
  const char choice = waitAnyKey();
  if (choice != '1' && choice != '2' && choice != '3' && choice != 'a')
  {
    logLine("fase '%c' sconosciuta: nessuna passata eseguita", choice);
    return;
  }

  enterRaw();
  if (choice == '1' || choice == 'a')
  {
    tuneReference(openScreen("taratura, riferimento"));
    tuneAlignPrevious();
    closeScreen();
    runTuningPhase(TUNE_PHASE_1_BLACK);
    runTuningPhase(TUNE_PHASE_1_WHITE);
    askWinningBand();
  }
  if (choice == '2' || choice == 'a')
  {
    enterRaw();
    tuneReference(openScreen("taratura, riferimento"));
    tuneAlignPrevious();
    closeScreen();
    runTuningPhase(TUNE_PHASE_2);
  }
  if (choice == '3' || choice == 'a')
  {
    enterRaw();
    tuneReference(openScreen("taratura, riferimento"));
    tuneAlignPrevious();
    closeScreen();
    runTuningPhase(TUNE_PHASE_3);
  }
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// m — passata libera.
//
// È la sonda che rende inutile riflashare: init, waveform, sequenza di 0x22 e
// registri li sceglie l'operatore, e la passata finisce nel registro come tutte
// le altre. cfg.seq e cfg.lut si editano dal menu e sopravvivono al reboot,
// quindi un tentativo riuscito è ripetibile alla lettera.
// ---------------------------------------------------------------------------
static void probeFreePass()
{
  Serial.println(F("\n  passata libera: init e waveform vengono da cfg (menu e / l)"));
  const bool useCustomInit = askYesNo("usare la sequenza di init editabile", true);
  const bool useCustomLut  = askYesNo("caricare la waveform editabile via 0x32", false);
  const uint8_t seq22      = askHex("0x22 della passata", useCustomLut ? 0xCC : 0xF7);
  const bool sendCtrl21    = askYesNo("scrivere 0x21 prima della passata", false);
  uint8_t ctrl21[2] = { 0x00, 0x00 };
  if (sendCtrl21)
  {
    ctrl21[0] = askHex("0x21 primo byte", 0x40);
    ctrl21[1] = askHex("0x21 secondo byte", 0x00);
  }
  const bool hotBank = askYesNo("forzare il banco caldo con 0x1A = 0x5A", false);
  const long pattern = askNumber("area di lavoro: 0 bianca, 1 nera, 2 a righe", 0, 2, 1);

  if (useCustomInit) runInitSequence(cfg.seq);
  else               enterRaw();

  openScreen("passata libera");
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, useCustomLut ? 0xFF : 0x00);
  fillChainBand((uint8_t)pattern, CH_BAND_H);
  writePlaneRect(0x24, chainBand, 0, CH_BAND_A_Y, SRC, CH_BAND_H, false);   // riquadro dal refresh
  fillBand(0x24, (uint16_t)(CH_BAND_H + 8), BAND_H, 0xFF, "PASSATA LIBERA", 0x00);
  setRamWindow(0, 0, SRC, GATE, 0x03);

  if (useCustomLut && !setWaveform(cfg.lut)) { closeScreen(); powerOffPanel(0xC3); return; }
  if (hotBank) { writeCommand(0x1A); writeData(0x5A); writeData(0x00); }
  if (sendCtrl21) writeCommandData(0x21, ctrl21, 2);
  if (useCustomLut) partialPowerOn();

  runRefresh(seq22, "PASSATA LIBERA", 60000);
  logLine("init %s, waveform %s, 0x22 = 0x%02X",
          useCustomInit ? "cfg.seq" : "driver custom",
          useCustomLut ? "cfg.lut" : "OTP", seq22);
  look("com'e' uscita? [o/n]");
  waitKey();
  closeScreen();
  enterRaw();
  powerOffPanel(0xC3);
}

#endif // PANEL_DIAGNOSTIC_PROBES_WAVEFORM_H
