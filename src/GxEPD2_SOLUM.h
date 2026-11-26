// =============================================================================
// Header di selezione della libreria: sceglie UN driver SOLUM e ne espone il
// nome come macro, così lo sketch non nomina mai la classe concreta.
//
// È l'idioma di GxEPD2 upstream (GxEPD2_display_selection_new_style.h, dove
// GxEPD2_DRIVER_CLASS e MAX_HEIGHT() sono macro), portato dentro la libreria
// invece che dentro gli esempi: qui i driver sono pochi e la selezione può
// stare in un posto unico.
//
// USO
//
//   #define SOLUM_PANEL_122C          // unica riga da cambiare per pannello
//   #include <GxEPD2_3C.h>
//   #include <GxEPD2_SOLUM.h>
//
//   GxEPD2_3C<GxEPD2_SOLUM_DRIVER_CLASS, SOLUM_MAX_HEIGHT(GxEPD2_SOLUM_DRIVER_CLASS)>
//       display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25, 32, 22 }));
//
// Nè il nome della classe nè l'arità del costruttore compaiono nello sketch:
// il pinout passa dalla struct uniforme GxEPD2_SOLUM_Pins, che ogni driver
// legge per la parte che gli serve. Il bus SPI lo apre lo sketch sui pin della
// propria board, con GxEPD2_SOLUM::beginBus() e l'overload di init() che
// riceve bus e impostazioni (vedi in fondo al file).
//
// AGGIUNGERE UN DRIVER
//   1. nuovo header in src/, che include "GxEPDImage.h" e "GxEPD2_SOLUM_Pins.h"
//      e implementa l'API richiesta elencata in GxEPDImage.h;
//   2. un ramo #elif qui sotto, tre righe;
//   3. nient'altro: library.properties espone questo header, non i driver.
// =============================================================================

#ifndef _GxEPD2_SOLUM_H_
#define _GxEPD2_SOLUM_H_

#include <SPI.h>
#include <GxEPD2_3C.h>

#include "GxEPD2_SOLUM_Pins.h"
#include "GxEPDFrame.h"
#include "GxEPD2_SOLUM_Differential.h"

// ---------------------------------------------------------------------------
// Selezione del pannello.
// ---------------------------------------------------------------------------
#if defined(SOLUM_PANEL_097C) && defined(SOLUM_PANEL_122C)
  #error "GxEPD2_SOLUM: definire SOLO uno fra SOLUM_PANEL_097C e SOLUM_PANEL_122C"
#elif defined(SOLUM_PANEL_097C)
  #include "GxEPD2_SOLUM_097c_960x672.h"
  #define GxEPD2_SOLUM_DRIVER_CLASS GxEPD2_SOLUM_097c_960x672
#elif defined(SOLUM_PANEL_122C)
  #include "GxEPD2_SOLUM_122c_960x768.h"
  #define GxEPD2_SOLUM_DRIVER_CLASS GxEPD2_SOLUM_122c_960x768
#else
  #error "GxEPD2_SOLUM: definire SOLUM_PANEL_097C oppure SOLUM_PANEL_122C prima di questo include"
#endif

// ---------------------------------------------------------------------------
// Altezza della page del template GxEPD2_3C.
//
// Stessa formula di MAX_HEIGHT() upstream, specializzata: tutti i driver di
// questa libreria pilotano pannelli a due piani da 1 bpp, quindi il budget si
// divide per due (un buffer per piano) e poi per i byte di una riga. Il cap a
// EPD::HEIGHT evita page più alte del pannello.
//
// Il budget si può sovrascrivere definendo SOLUM_MAX_DISPLAY_BUFFER_SIZE prima
// dell'include, oppure ignorare del tutto passando al template un valore
// proprio: su un pannello 960 px di larghezza il default produce page da 273
// righe (~65 KB di buffer), che è il compromesso di upstream per ESP32, l'unica
// architettura della libreria, ma non quello di un firmware che ha altro in RAM.
// ---------------------------------------------------------------------------
#if !defined(SOLUM_MAX_DISPLAY_BUFFER_SIZE)
  #define SOLUM_MAX_DISPLAY_BUFFER_SIZE 65536ul
#endif

#define SOLUM_MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= (SOLUM_MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8) \
   ? EPD::HEIGHT \
   : (SOLUM_MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8))

// ---------------------------------------------------------------------------
// Altezza della page per un numero di pagine scelto, e display con il driver
// selezionato.
//
// SOLUM_PAGE_HEIGHT(EPD, PAGES) arrotonda per eccesso, così PAGES pagine
// coprono sempre il pannello. GxEPD2_SOLUM_Display<PH> è il GxEPD2_3C del
// driver selezionato, con un controllo a compile time: il refresh
// differenziale confronta al più GxEPD2_SOLUM_Differential::MAX_PAGES pagine, e
// con page più basse il partial non partirebbe mai, senza nessun errore.
// ---------------------------------------------------------------------------
#define SOLUM_PAGE_HEIGHT(EPD, PAGES) ((uint16_t)(((EPD::HEIGHT) + (PAGES) - 1) / (PAGES)))

template <class EPD, uint16_t PAGE_HEIGHT>
struct GxEPD2_SOLUM_DisplayType
{
  static_assert(!GxEPDFrame::SupportsDifferential<EPD>::value ||
                (uint32_t)PAGE_HEIGHT * GxEPD2_SOLUM_Differential::MAX_PAGES >= EPD::HEIGHT,
                "GxEPD2_SOLUM: con il refresh differenziale servono al più 16 pagine, alza l'altezza della page");
  using type = GxEPD2_3C<EPD, PAGE_HEIGHT>;
};

template <uint16_t PAGE_HEIGHT>
using GxEPD2_SOLUM_Display = typename GxEPD2_SOLUM_DisplayType<GxEPD2_SOLUM_DRIVER_CLASS, PAGE_HEIGHT>::type;

// ---------------------------------------------------------------------------
// Apertura del bus SPI.
//
// Nessun driver della libreria apre il bus: lo apre lo sketch sui pin della
// propria board, e le impostazioni arrivano al driver con l'overload nativo
// GxEPD2_3C::init(diag, initial, reset_ms, pulldown, spi, settings), che chiama
// selectSPI() prima di init(). Questo helper mette insieme le due cose:
//
//   display.init(115200, true, 2, false, hspi,
//                GxEPD2_SOLUM::beginBus(hspi, 13, 12, 14, 15, 10000000));
// ---------------------------------------------------------------------------
namespace GxEPD2_SOLUM
{
  /** Apre il bus sui pin dati e restituisce le impostazioni SPI dei pannelli
   *  della libreria (MSB first, modo 0) alla frequenza richiesta. */
  inline SPISettings beginBus(SPIClass& spi, int8_t sck, int8_t miso, int8_t mosi, int8_t cs,
                              uint32_t hz = 10000000UL)
  {
    spi.begin(sck, miso, mosi, cs);
    return SPISettings(hz, MSBFIRST, SPI_MODE0);
  }
}

#endif
