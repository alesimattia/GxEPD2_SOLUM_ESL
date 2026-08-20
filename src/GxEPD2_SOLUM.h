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
//       display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25, 33, 35, 13, 12, 14 }));
//
// Nè il nome della classe nè l'arità del costruttore compaiono nello sketch:
// il pinout passa dalla struct uniforme GxEPD2_SOLUM_Pins, che ogni driver
// legge per la parte che gli serve.
//
// AGGIUNGERE UN DRIVER
//   1. nuovo header in src/, che include "GxEPDImage.h" e "GxEPD2_SOLUM_Pins.h"
//      e implementa l'API richiesta elencata in GxEPDImage.h;
//   2. un ramo #elif qui sotto, tre righe;
//   3. nient'altro: library.properties espone questo header, non i driver.
// =============================================================================

#ifndef _GxEPD2_SOLUM_H_
#define _GxEPD2_SOLUM_H_

#include "GxEPD2_SOLUM_Pins.h"

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
// proprio: su un pannello 960 px di larghezza il default ESP32 produce page da
// 273 righe (~65 KB di buffer), che è il compromesso di upstream ma non quello
// di un firmware che ha altro in RAM.
// ---------------------------------------------------------------------------
#if !defined(SOLUM_MAX_DISPLAY_BUFFER_SIZE)
  #if defined(ESP32)
    #define SOLUM_MAX_DISPLAY_BUFFER_SIZE 65536ul
  #else
    #define SOLUM_MAX_DISPLAY_BUFFER_SIZE 15000ul
  #endif
#endif

#define SOLUM_MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= (SOLUM_MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8) \
   ? EPD::HEIGHT \
   : (SOLUM_MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8))

#endif
