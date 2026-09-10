// =============================================================================
// Graphics.h — font 5x7, impaginazione del testo e riquadro numerato.
//
// Due destinazioni di disegno, e nessun'altra:
//   - le RIGHE RAM larghe tutto il pannello (ROW_BYTES byte), che il controller
//     riceve una alla volta: le compone writeRowsWithText() in Controller.h con
//     setSpan(), perchè il corpo del font non è multiplo di 8 e una memset di
//     byte interi non basta;
//   - i BUFFER a 1 bit per pixel di dimensione arbitraria (Bitmap1bpp), da cui
//     escono il riquadro del numero e le fasce delle sonde del partial.
//
// Convenzione delle bitmap, la stessa del driver: bit = 1 è bianco, bit = 0 è
// nero, quindi "accendere" un pixel nero vuol dire azzerare il bit. Vale anche
// per il piano accent, dove l'invert lo applica chi scrive.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_GRAPHICS_H
#define PANEL_DIAGNOSTIC_GRAPHICS_H

#include <Arduino.h>
#include "Config.h"

// ---------------------------------------------------------------------------
// Font 5x7, sette byte per carattere, bit 4 = colonna più a sinistra.
// Copre i codici ASCII 0x20..0x5F: maiuscole, cifre, spazio e la punteggiatura
// usata nelle frasi scritte sul vetro. I codici non serviti restano a zero,
// cioè escono come spazio; le minuscole si mappano sulle maiuscole.
// ---------------------------------------------------------------------------
static const uint8_t FONT_W     = 5;   // colonne del glifo
static const uint8_t FONT_H     = 7;   // righe del glifo
static const uint8_t FONT_PITCH = 6;   // 5 colonne più una di spazio

static const uint8_t FONT5X7[96 - 32][FONT_H] =
{
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x20 spazio
  { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },   // !
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // "
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // #
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // $
  { 0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13 },   // %
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // &
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // '
  { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },   // (
  { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 },   // )
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // *
  { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 },   // +
  { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 },   // ,
  { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },   // -
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },   // .
  { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },   // /
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
  { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   // 5
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   // 6
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   // 7
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   // 9
  { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 },   // :
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // ;
  { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 },   // <
  { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 },   // =
  { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 },   // >
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 },   // ?
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // @
  { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   // A
  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },   // B
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   // C
  { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },   // D
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },   // E
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },   // F
  { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F },   // G
  { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   // H
  { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },   // I
  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },   // J
  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },   // K
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   // L
  { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 },   // M
  { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },   // N
  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   // O
  { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },   // P
  { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },   // Q
  { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },   // R
  { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },   // S
  { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },   // T
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   // U
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },   // V
  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },   // W
  { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },   // X
  { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 },   // Y
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },   // Z
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // [
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // backslash
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // ]
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // ^
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00 },   // _
};

/** Glifo di un carattere, minuscole mappate sulle maiuscole. Fuori tabella
 *  ritorna lo spazio, così una frase con un carattere non previsto resta
 *  leggibile invece di stampare rumore. */
static const uint8_t* glyphFor(char c)
{
  uint8_t u = (uint8_t)c;
  if (u >= 'a' && u <= 'z') u = (uint8_t)(u & ~0x20);
  if (u < 0x20 || u > 0x5F) u = 0x20;
  return FONT5X7[u - 0x20];
}

// ---------------------------------------------------------------------------
// Disegno dentro una riga RAM larga tutto il pannello.
// ---------------------------------------------------------------------------

/**
 * Accende n pixel consecutivi a partire da x nel valore fg, dentro una riga
 * RAM. Il bit più significativo di un byte è il pixel più a sinistra, e il
 * corpo del font non è multiplo di 8: serve la maschera.
 */
static void setSpan(uint8_t* row, uint16_t x, uint8_t n, uint8_t fg)
{
  for (uint8_t i = 0; i < n; ++i)
  {
    const uint16_t px = x + i;
    if (px >= SRC) return;
    const uint8_t m = (uint8_t)(0x80 >> (px % 8));
    uint8_t& b = row[px / 8];
    b = (uint8_t)((b & ~m) | (fg & m));
  }
}

// ---------------------------------------------------------------------------
// Impaginazione di una frase dentro una fascia.
// ---------------------------------------------------------------------------
static const uint16_t TEXT_MARGIN_X      = 24;   // margine laterale, per lato
static const uint16_t TEXT_MARGIN_BOTTOM = 16;   // distanza voluta dal fondo fascia
static const uint8_t  TEXT_LINE_GAP      = 2;    // interlinea, in unità di font
static const uint8_t  TEXT_MAX_LINES     = 3;    // oltre, la frase è troppo lunga
static const uint8_t  TEXT_SCALE_MAX     = 8;    // 40x56 px per carattere
static const uint8_t  TEXT_SCALE_MIN     = 2;    // 10x14 px, il minimo leggibile

/**
 * Disposizione del testo dentro una fascia: quale corpo, quante righe, dove
 * spezzarle e a che altezza parte il blocco. scale == 0 vuol dire che la frase
 * non ci sta nemmeno al corpo minimo, e la fascia va riempita uniforme.
 */
struct TextLayout
{
  uint8_t     scale = 0;
  uint8_t     lines = 0;
  const char* start[TEXT_MAX_LINES] = { nullptr, nullptr, nullptr };
  uint8_t     len[TEXT_MAX_LINES]   = { 0, 0, 0 };
  int16_t     top = 0;              // prima riga del blocco dentro la fascia
};

/**
 * Manda a capo la frase sugli spazi in righe da al massimo maxChars caratteri.
 * Ritorna false se servono più di TEXT_MAX_LINES righe, che è il segnale per
 * provare un corpo più piccolo. Una parola più lunga di maxChars viene spezzata
 * a metà parola: senza quel ramo il ciclo non terminerebbe.
 */
static bool wrapText(const char* text, uint8_t maxChars, TextLayout& out)
{
  out.lines = 0;
  const char* p = text;
  while (*p)
  {
    while (*p == ' ') ++p;         // niente spazi in testa a una riga
    if (!*p) break;
    if (out.lines >= TEXT_MAX_LINES) return false;

    uint8_t take = 0;
    uint8_t lastSpace = 0;
    while (p[take] && take < maxChars)
    {
      if (p[take] == ' ') lastSpace = take;
      ++take;
    }
    if (p[take] && lastSpace > 0) take = lastSpace;   // taglio sull'ultimo spazio

    out.start[out.lines] = p;
    out.len[out.lines]   = take;
    ++out.lines;
    p += take;
  }
  return out.lines > 0;
}

/**
 * Sceglie il corpo più grande con cui la frase sta dentro la fascia: prova le
 * scale dalla massima alla minima e si ferma alla prima che entra. Frasi corte
 * restano grandi, frasi lunghe scendono di corpo da sè, e le fasce basse (le
 * passate del MUX, la finestra sottile) prendono il corpo che ci sta.
 *
 * Il margine inferiore è adattivo: quello voluto se c'è spazio, metà dello
 * spazio che resta se la fascia è bassa. Con un margine fisso una fascia da 24
 * righe non porterebbe nessun testo, e sono proprio quelle le passate dove
 * serve sapere chi ha dipinto.
 */
static TextLayout layoutText(const char* text, uint16_t h)
{
  TextLayout lay;
  if (!text || !*text) return lay;
  for (uint8_t scale = TEXT_SCALE_MAX; scale >= TEXT_SCALE_MIN; --scale)
  {
    const uint16_t usable  = SRC - 2 * TEXT_MARGIN_X;
    const uint8_t  maxChar = (uint8_t)(usable / ((uint16_t)FONT_PITCH * scale));
    if (maxChar == 0) continue;
    TextLayout probe;
    if (!wrapText(text, maxChar, probe)) continue;
    const int16_t lineH  = (int16_t)FONT_H * scale;
    const int16_t gap    = (int16_t)TEXT_LINE_GAP * scale;
    const int16_t blockH = probe.lines * lineH + (probe.lines - 1) * gap;
    const int16_t spare  = (int16_t)h - blockH;
    if (spare < 0) continue;
    const int16_t margin = (spare / 2 < (int16_t)TEXT_MARGIN_BOTTOM)
                           ? spare / 2 : (int16_t)TEXT_MARGIN_BOTTOM;
    probe.scale = scale;
    probe.top   = spare - margin;
    return probe;
  }
  return lay;
}

// ---------------------------------------------------------------------------
// Disegno dentro un buffer a 1 bit per pixel.
// ---------------------------------------------------------------------------

/**
 * Destinazione di disegno: buffer a 1 bpp con le sue dimensioni. Serve perchè
 * le stesse primitive scrivono buffer di larghezze diverse — la fascia di
 * lavoro della taratura, il riquadro del numero, la banda del partial.
 */
struct Bitmap1bpp
{
  uint8_t* buf;
  int16_t  wb;      // byte per riga
  int16_t  w;
  int16_t  h;
};

static inline void setPixel(const Bitmap1bpp& dest, int16_t x, int16_t y, bool black)
{
  if (x < 0 || x >= dest.w || y < 0 || y >= dest.h) return;
  const uint32_t i = (uint32_t)y * dest.wb + (x >> 3);
  const uint8_t  m = (uint8_t)(0x80 >> (x & 7));
  if (black) dest.buf[i] &= (uint8_t)~m;
  else       dest.buf[i] |= m;
}

static void fillRect(const Bitmap1bpp& dest, int16_t x0, int16_t y0,
                     int16_t w, int16_t h, bool black)
{
  for (int16_t y = y0; y < y0 + h; ++y)
    for (int16_t x = x0; x < x0 + w; ++x)
      setPixel(dest, x, y, black);
}

static const int16_t BOX_BORDER = 3;   // spessore della cornice dei riquadri

/**
 * Riquadro con un numero di una, due o tre cifre: fondo bianco e cornice nera,
 * così si legge sia su un'area nera sia su una bianca. La scala della cifra si
 * ricava dal riquadro, quindi la stessa funzione serve il riquadro di banda
 * dentro un'area di lavoro e quello di schermata nell'angolo del pannello.
 *
 * Il contatore delle schermate è progressivo sulla sessione e con la passata
 * libera supera facilmente il 99: da qui le tre cifre.
 */
static void drawNumberBox(const Bitmap1bpp& dest, int16_t x, int16_t y,
                          int16_t w, int16_t h, uint16_t value)
{
  fillRect(dest, x, y, w, h, false);                          // fondo bianco
  fillRect(dest, x, y, w, BOX_BORDER, true);                  // cornice
  fillRect(dest, x, y + h - BOX_BORDER, w, BOX_BORDER, true);
  fillRect(dest, x, y, BOX_BORDER, h, true);
  fillRect(dest, x + w - BOX_BORDER, y, BOX_BORDER, h, true);

  const uint16_t v = value % 1000;
  const uint8_t digits = (v >= 100) ? 3 : ((v >= 10) ? 2 : 1);
  // Margine interno: la cornice più due pixel d'aria per lato. Le cifre stanno
  // su una griglia di celle: 5 celle per cifra più una di stacco fra due cifre.
  const int16_t margin  = 2 * (BOX_BORDER + 2);
  const int16_t columns = (int16_t)(digits * 6 - 1);
  int16_t scale = (w - margin) / columns;
  const int16_t scaleH = (h - margin) / 7;
  if (scaleH < scale) scale = scaleH;
  if (scale < 1) return;
  const int16_t gx = x + (w - columns * scale) / 2;
  const int16_t gy = y + (h - 7 * scale) / 2;
  for (uint8_t k = 0; k < digits; ++k)
  {
    uint8_t digit;
    if (digits == 3)      digit = (k == 0) ? (v / 100) : ((k == 1) ? ((v / 10) % 10) : (v % 10));
    else if (digits == 2) digit = (k == 0) ? (v / 10) : (v % 10);
    else                  digit = (uint8_t)v;
    const uint8_t* g = FONT5X7[('0' - 0x20) + digit];
    const int16_t ox = gx + (int16_t)k * 6 * scale;
    for (int16_t r = 0; r < 7; ++r)
      for (int16_t c = 0; c < 5; ++c)
        if (g[r] & (0x10 >> c))
          fillRect(dest, ox + c * scale, gy + r * scale, scale, scale, true);
  }
}

#endif // PANEL_DIAGNOSTIC_GRAPHICS_H
