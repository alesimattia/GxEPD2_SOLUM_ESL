// =============================================================================
// Graphics.h — font, riquadro del numero, composizione delle righe.
//
// QUI NON SI TOCCA SPI. Tutto quello che sta in questo file compone byte in un
// buffer; a spingerli sul bus pensa Controller.h. Il confine serve a poter
// comporre un pattern fuori dalla regione cronometrata di una misura: se la
// composizione stesse dentro il push, ogni durata misurata comprenderebbe il
// tempo di calcolo dell'MCU invece del solo costo del bus.
//
// DUE GRANULARITÀ, e la differenza è sostanziale. paintSpan() riempie BYTE
// INTERI, quindi tutti gli elementi dei pattern sono allineati a 8 px e non
// serve mascheramento: è la ragione per cui la scala dei glifi dei righelli è
// un multiplo di 8. badgeSpan() invece lavora a livello di BIT, e serve a due
// cose che non possono essere allineate: il riquadro del numero, che deve
// entrare anche nelle finestre basse della sonda d'area, e i pattern
// asimmetrici DENTRO un byte della sonda dell'entry mode — quelli sono l'unico
// modo di distinguere una specchiatura di byte da una di bit.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_GRAPHICS_H
#define DUAL_PANEL_FINDER_GRAPHICS_H

#include <Arduino.h>
#include "Config.h"
#include "Report.h"

/**
 * Font 5x7 per le etichette dei righelli e per la lettera della coda. Una riga
 * per elemento, bit 4 = colonna più a sinistra. Scala e offset X sono sempre
 * multipli di 8, così ogni colonna del font copre byte RAM interi e la
 * composizione delle righe non richiede mascheramento a livello di bit.
 */
static const uint8_t GLYPH_W = 5;
static const uint8_t GLYPH_H = 7;

static const uint8_t GLYPHS[12][GLYPH_H] =
{
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
  { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },   // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   // 5
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   // 6
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   // 7
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   // 9
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   // C
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   // L
};

static const uint8_t GLYPH_C = 10;
static const uint8_t GLYPH_L = 11;
// --- composizione del pattern ---------------------------------------

/**
 * Dipinge un intervallo orizzontale di byte interi dentro la riga. x0 e w
 * devono essere multipli di 8: tutti gli elementi del pattern sono allineati al
 * byte, quindi qui non serve mascheramento.
 */
static void paintSpan(uint8_t* row, int32_t x0, int32_t w, uint8_t value)
{
  if (w <= 0) return;
  int32_t from = x0 / 8;
  int32_t to   = (x0 + w) / 8;
  if (from < 0) from = 0;
  if (to > ROW_BYTES) to = ROW_BYTES;
  for (int32_t i = from; i < to; ++i)
    row[i] = value;
}

/**
 * Dipinge la riga y di un glifo scalato, se quella riga lo attraversa. scale
 * multiplo di 8: ogni colonna del font diventa un numero intero di byte.
 */
static void paintGlyph(uint8_t* row, int16_t y, uint8_t idx,
                       int32_t gx, int32_t gy, int32_t scale, uint8_t value)
{
  const int32_t h = (int32_t)GLYPH_H * scale;
  if (y < gy || y >= gy + h) return;
  const uint8_t bits = GLYPHS[idx][(y - gy) / scale];
  for (uint8_t c = 0; c < GLYPH_W; ++c)
  {
    if (bits & (0x10 >> c))
      paintSpan(row, gx + (int32_t)c * scale, scale, value);
  }
}

/**
 * Dipinge un numero decimale come sequenza di glifi. Ritorna la larghezza in
 * px occupata, così il chiamante sa dove finisce l'etichetta.
 */
static int32_t paintNumber(uint8_t* row, int16_t y, uint16_t n,
                           int32_t gx, int32_t gy, int32_t scale, uint8_t value)
{
  uint8_t digits[5];
  uint8_t nd = 0;
  do {
    digits[nd++] = n % 10;
    n /= 10;
  } while (n > 0 && nd < sizeof(digits));
  const int32_t advance = (int32_t)(GLYPH_W + 1) * scale;
  for (uint8_t i = 0; i < nd; ++i)
    paintGlyph(row, y, digits[nd - 1 - i], gx + i * advance, gy, scale, value);
  return nd * advance;
}

// Geometria del pattern, tutta allineata al byte
static const int32_t CORNER      = 96;   // lato dei blocchi d'angolo
static const int32_t LABEL_SCALE = 8;    // glifi dei righelli: 40x56 px
static const int32_t BIG_SCALE   = 16;   // lettera della coda: 80x112 px
static const int32_t YRULE_TICK  = 64;   // lunghezza del tick del righello Y
static const int32_t YLABEL_X    = 104;  // oltre il blocco d'angolo
static const int32_t XLABEL_Y    = 8;
// --- il riquadro sul vetro -------------------------------------------------

static const uint8_t BADGE_BORDO     = 2;   // spessore della cornice, in px
static const uint8_t BADGE_ARIA      = 3;   // aria fra cornice e cifre
static const uint8_t BADGE_MARGINE   = 8;   // distanza dal bordo della finestra
static const uint8_t BADGE_SCALA_MAX = 6;   // cifre 30x42 px
static const uint8_t BADGE_SCALA_MIN = 2;   // cifre 10x14 px, il minimo leggibile

// Ingombro massimo: tre cifre al corpo massimo, arrotondate al byte
static const uint8_t BADGE_MAX_H     = GLYPH_H * BADGE_SCALA_MAX
                                       + 2 * (BADGE_BORDO + BADGE_ARIA);
static const uint8_t BADGE_MAX_BYTES = 16;

/**
 * Righe del riquadro, contigue e con stride pari alla sua larghezza in byte:
 * così lo stesso buffer serve sia al push SPI della fase probe sia a
 * writeImage() della fase driver, che pretende righe contigue.
 */
static uint8_t badgeBuf[BADGE_MAX_H * BADGE_MAX_BYTES];
static uint8_t badgeStride = 0;

/** Prima riga del riquadro nel buffer. */
static inline uint8_t* badgeRiga(uint8_t r)
{
  return badgeBuf + (uint16_t)r * badgeStride;
}

/**
 * Accende n pixel consecutivi a partire da x. Il riquadro è l'unica cosa in
 * questo sketch che lavora a livello di BIT: paintSpan() riempie byte interi,
 * quindi il corpo minimo dei suoi glifi è 8 e un riquadro così non entrerebbe
 * nelle finestre basse della sonda d'area.
 */
static void badgeSpan(uint8_t* row, uint16_t x, uint8_t n, uint8_t fg)
{
  for (uint8_t i = 0; i < n; ++i)
  {
    const uint16_t px = x + i;
    if (px >= (uint16_t)BADGE_MAX_BYTES * 8) return;
    const uint8_t m = (uint8_t)(0x80 >> (px % 8));
    uint8_t& b = row[px / 8];
    b = (uint8_t)((b & ~m) | (fg & m));
  }
}

/**
 * Spinge il riquadro su un piano: le righe di badgeRows sul piano B/N, accent
 * spento sul piano 0x26. Passa da csAssert/csRelease come writePlane, così
 * onora csBoth e finisce su entrambi i controller quando i due CS sono uniti.
 */
/**
 * Compone in badgeBuf il riquadro col numero della schermata: cifre nere su
 * fondo bianco con cornice. Il corpo è il più grande che entra in maxW x maxH,
 * da 30x42 px a 10x14; sotto quello ritorna false.
 *
 * bianco e nero sono parametri perchè i due chiamanti hanno convenzioni
 * diverse: la fase probe passa bwByteFor(), così il riquadro resta leggibile
 * anche se BW_POLARITY risultasse inversa, mentre la fase driver passa le
 * costanti della convenzione di GxEPD2.
 */
static bool componiBadge(uint8_t numero, uint16_t maxW, uint16_t maxH,
                         uint8_t bianco, uint8_t nero, uint8_t* bwOut, uint8_t* bhOut)
{
  char testo[4];
  const int len = snprintf(testo, sizeof(testo), "%u", (unsigned)numero);

  uint8_t scala = 0;
  uint8_t bw = 0, bh = 0;
  for (uint8_t sc = BADGE_SCALA_MAX; sc >= BADGE_SCALA_MIN; --sc)
  {
    const uint16_t testoW = (uint16_t)(len * (GLYPH_W + 1) - 1) * sc;
    const uint16_t larga  = (uint16_t)((testoW + 2 * (BADGE_BORDO + BADGE_ARIA) + 7) & ~7);
    const uint16_t alta   = (uint16_t)(GLYPH_H * sc + 2 * (BADGE_BORDO + BADGE_ARIA));
    if (larga + BADGE_MARGINE > maxW) continue;
    if (alta > maxH) continue;
    scala = sc;
    bw = (uint8_t)larga;
    bh = (uint8_t)alta;
    break;
  }
  if (scala == 0)
    return false;

  badgeStride = (uint8_t)(bw / 8);

  // fondo bianco, cornice nera sui quattro lati
  for (uint8_t r = 0; r < bh; ++r)
  {
    uint8_t* row = badgeRiga(r);
    if (r < BADGE_BORDO || r >= bh - BADGE_BORDO)
      memset(row, nero, badgeStride);
    else
    {
      memset(row, bianco, badgeStride);
      badgeSpan(row, 0, BADGE_BORDO, nero);
      badgeSpan(row, (uint16_t)(bw - BADGE_BORDO), BADGE_BORDO, nero);
    }
  }

  // cifre centrate nel riquadro, stessi glifi 5x7 delle fasce
  const uint16_t testoW = (uint16_t)(len * (GLYPH_W + 1) - 1) * scala;
  const uint16_t x0 = (uint16_t)((bw - testoW) / 2);
  const uint8_t  y0 = (uint8_t)((bh - GLYPH_H * scala) / 2);
  for (int k = 0; k < len; ++k)
  {
    const uint8_t* g = GLYPHS[testo[k] - '0'];
    for (uint8_t fr = 0; fr < GLYPH_H; ++fr)
      for (uint8_t sy = 0; sy < scala; ++sy)
      {
        uint8_t* row = badgeRiga((uint8_t)(y0 + fr * scala + sy));
        for (uint8_t c = 0; c < GLYPH_W; ++c)
          if (g[fr] & (0x10 >> c))
            badgeSpan(row, (uint16_t)(x0 + ((uint16_t)k * (GLYPH_W + 1) + c) * scala),
                      scala, nero);
      }
  }

  *bwOut = bw;
  *bhOut = bh;
  return true;
}

#endif // DUAL_PANEL_FINDER_GRAPHICS_H
