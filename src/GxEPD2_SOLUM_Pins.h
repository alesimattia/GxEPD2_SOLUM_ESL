// =============================================================================
// Pinout uniforme dei driver SOLUM di questa libreria.
//
// Esiste per un motivo solo: i pannelli SOLUM non hanno tutti lo stesso numero
// di segnali. Il 9.7" è single-controller (CS, DC, RST, BUSY), il 12.2" ha due
// controller e quindi due CS e due BUSY. Se ogni driver esponesse soltanto il
// proprio costruttore ad arità propria, uno sketch che cambia pannello
// dovrebbe riscrivere la riga di costruzione del display, e non basterebbe più
// cambiare il #define di selezione.
//
// Ogni driver della libreria accetta questa struct e legge i campi che gli
// servono, ignorando gli altri. Un campo a -1 significa "assente".
// =============================================================================

#ifndef _GxEPD2_SOLUM_Pins_H_
#define _GxEPD2_SOLUM_Pins_H_

#include <Arduino.h>

/**
 * Pinout di un pannello SOLUM. I primi quattro campi li usano tutti i driver,
 * gli altri solo quelli che ne hanno bisogno:
 *
 *   - cs2 / busy2: secondo controller. Li usa il 12.2"; sul 9.7" restano -1.
 *   - sck / miso / mosi: pin del bus. Servono ai driver che aprono il bus da
 *     sè (il 12.2"); i driver che si appoggiano a selectSPI() li ignorano,
 *     perchè lì il bus lo apre lo sketch. Lasciarli a -1 significa "bus di
 *     default della board".
 *
 * L'inizializzazione per lista rispetta l'ordine di dichiarazione dei campi:
 *
 *   GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }                       // 9.7"
 *   GxEPD2_SOLUM_Pins{ 15, 27, 26, 25, 33, 35, 13, 12, 14 }   // 12.2" dual
 *
 * Richiede C++14 o superiore per l'inizializzazione per lista con default
 * member initializer: il core ESP32 compila a gnu++17, quindi va.
 */
struct GxEPD2_SOLUM_Pins
{
  int16_t cs    = -1;  // CS del controller principale
  int16_t dc    = -1;
  int16_t rst   = -1;  // -1 = nessun reset hardware
  int16_t busy  = -1;  // -1 = nessun BUSY, il driver usa i tempi nominali
  int16_t cs2   = -1;  // CS del secondo controller, -1 se il pannello ne ha uno
  int16_t busy2 = -1;  // BUSY del secondo controller
  int16_t sck   = -1;  // pin del bus SPI, per i driver che lo aprono da sè
  int16_t miso  = -1;
  int16_t mosi  = -1;
};

#endif
