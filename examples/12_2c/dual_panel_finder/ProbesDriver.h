// =============================================================================
// ProbesDriver.h — la fase che misura il DRIVER, non il silicio.
//
// La fase probe parla al pannello a SPI diretta e misura il silicio; questa
// costruisce GxEPD2_SOLUM_DRIVER_CLASS e misura il driver: se il probe stampa e
// questa no, il difetto è nel driver e non nel pannello.
//
// PERCHÈ IL TIPO DEL DRIVER STA QUI E NON NELLO SKETCH. Il preprocessore di
// Arduino inserisce i prototipi generati prima del primo include del .ino,
// quindi una funzione del .ino che ritorna quel tipo non compilerebbe. Tenendo
// driver() e il gancio dentro questo header il .ino non nomina mai la classe, e
// il vincolo cade insieme alle dichiarazioni anticipate che serviva a
// giustificare.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_PROBES_DRIVER_H
#define DUAL_PANEL_FINDER_PROBES_DRIVER_H

#include <Arduino.h>
#include "Config.h"
#include "Report.h"
#include "Controller.h"

// L'ombrello include il driver giusto e ne espone il nome come
// GxEPD2_SOLUM_DRIVER_CLASS: lo sketch non nomina mai la classe concreta.
#define SOLUM_PANEL_122C
#include <GxEPD2_SOLUM.h>

// =====================================================================
// FASE DRIVER: verifica del driver custom, non del silicio
// =====================================================================
/**
 * Tile 64x64 a scacchi, in PROGMEM: 8 byte per riga, 512 byte in tutto. Serve a
 * esercitare il dispatch del driver con una bitmap vera invece che con un
 * riempimento uniforme, senza allocare i 92160 byte di un frame intero.
 *
 * Il bordo del tile è pieno e un angolo è marcato, così un tile ruotato o
 * specchiato si riconosce a occhio.
 */
static const uint8_t TILE[64 * 8] PROGMEM = {
#define TILE_ROW_FULL   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
#define TILE_ROW_EDGE   0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00
#define TILE_ROW_CHECK  0x00,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0x00
#define TILE_ROW_MARK   0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00
  // 8 righe piene: bordo superiore
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  // 8 righe con l'angolo marcato in alto a sinistra
  TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK,
  TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK, TILE_ROW_MARK,
  // 32 righe a scacchi
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK, TILE_ROW_CHECK,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,  TILE_ROW_EDGE,
  // 16 righe piene: bordo inferiore
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
  TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL, TILE_ROW_FULL,
};

/**
 * Pinout del driver nella struct uniforme della libreria: l'ordine dei campi è
 * cs, dc, rst, busy, cs2, busy2, sck, miso, mosi.
 *
 * Senza il secondo connettore il secondo controller resta a -1, cioè assente: il
 * driver salta le scritture verso la sua banda e non guarda quel BUSY. Non è una
 * rinuncia ma la configurazione giusta, e metterci dei pin che non arrivano a
 * niente costa 190 ms di push a vuoto per frame e mette in AND un BUSY
 * flottante in ogni attesa.
 */
/**
 * Il driver si costruisce alla prima passata e non prima, perchè i suoi pin
 * dipendono dalla configurazione: senza secondo connettore cs2 e busy2 devono
 * restare a -1, altrimenti il driver aspetterebbe un BUSY su un GPIO35
 * flottante e andrebbe in timeout.
 */
static GxEPD2_SOLUM_DRIVER_CLASS& driver()
{
  static GxEPD2_SOLUM_Pins pins{ PIN_CS, PIN_DC, PIN_RST, PIN_BUSY,
                                 (int16_t)(cfg.secondFfc ? PIN_CS_OTHER : -1),
                                 (int16_t)(cfg.secondFfc ? PIN_BUSY_OTHER : -1),
                                 PIN_SCK, PIN_MISO, PIN_MOSI };
  static GxEPD2_SOLUM_DRIVER_CLASS d(pins);
  return d;
}

/**
 * Una passata della fase driver: un frame solo, e un refresh solo.
 *
 * La fase probe misura il SILICIO senza passare da nessuno strato software,
 * questa misura il DRIVER: se il probe stampa e questa no, il difetto è nel
 * driver e non nel pannello. Il frame prova tutto insieme:
 *   - writeScreenBuffer con fondo bianco e accent spento, cioè la polarità dei
 *     due piani come la scrive il driver;
 *   - cinque tile ai quattro angoli e uno a cavallo della giunzione a
 *     y = PART_HEIGHT: il dispatch per righe e la geometria dello split. Il
 *     tile ha bordo pieno e un angolo marcato, quindi uno specchiato si vede;
 *   - una barra accent lungo il bordo basso della banda master, l'unico
 *     elemento che passa dal piano 0x26 del driver invece che dalla SPI
 *     diretta.
 *
 * Il clock è il default del driver, non quello della diagnostica: qui si
 * verifica il driver com'è configurato. `mode` è il modello di indirizzamento,
 * e la fase gira una volta per ognuno che il cablaggio rende possibile.
 */
static void runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::AddressingMode mode)
{
  const bool cascade = (mode == GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE);
  static char etichetta[48];
  snprintf(etichetta, sizeof(etichetta), "fase driver: ADDRESSING_%s",
           cascade ? "CASCADE" : "DUAL_CS");
  const Frame f = frameCon(etichetta, D_DIPINTO);
  const uint8_t numero = registraFrame(f);

  Serial.println(F("\n=================================================="));
  logLine("FASE DRIVER - GxEPD2_SOLUM_122c_960x768");
  Serial.printf ("  geometria          %ux%u, bande da %ux%u\n",
                 GxEPD2_SOLUM_DRIVER_CLASS::WIDTH, GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT,
                 GxEPD2_SOLUM_DRIVER_CLASS::PART_WIDTH,
                 GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT);
  if (cfg.secondFfc)
    Serial.printf ("  controller         due: CS=%d BUSY=%d e CS=%d BUSY=%d\n",
                   PIN_CS, PIN_BUSY, PIN_CS_OTHER, PIN_BUSY_OTHER);
  else
    Serial.printf ("  controller         uno: CS=%d BUSY=%d, banda slave non pilotata\n",
                   PIN_CS, PIN_BUSY);
  if (cascade)
    Serial.printf ("  indirizzamento     CASCADE: un solo CS (%d), slave a opcode|0x%02X,\n"
                   "                     master in cascade con 0x21 = 08 10\n",
                   PIN_CS, (unsigned)GxEPD2_SOLUM_DRIVER_CLASS::CASCADE_CMD_OFFSET);
  else
    logLine("indirizzamento     DUAL CS: un opcode solo, un CS per controller");
  Serial.printf ("  clock              %lu MHz, il default del driver\n",
                 (unsigned long)(cfg.spiDriver / 1000000UL));

  /**
   * selectSPI e setAddressingMode vanno prima di init(): è init() ad aprire il
   * bus e a decidere quali pin configurare, e in cascade il secondo chip select
   * non esiste e non va pilotato. init() chiude con un reset hardware, che basta
   * a svegliare il pannello dal deep sleep in cui la fase probe lo ha lasciato.
   */
  driver().selectSPI(hspi, SPISettings(cfg.spiDriver, MSBFIRST, SPI_MODE0));
  driver().setAddressingMode(mode);
  driver().init(115200);

  const int16_t H = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT;
  const int16_t W = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::WIDTH;
  const int16_t SPLIT = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT;

  Serial.println(F("\nframe del driver: fondo + 5 tile + barra accent, un refresh solo"));
  if (!cfg.secondFfc)
  {
    logLine("con un controller solo compaiono i due tile degli angoli alti e la");
    logLine("metà superiore di quello sulla giunzione: gli altri tre cadono nella");
    logLine("banda slave, che non è pilotata. Non è un difetto del dispatch");
  }

  driver().writeScreenBuffer(0xFF, 0x00);          // fondo bianco, accent spento
  driver().writeImage(TILE, 0, 0, 64, 64, false, false, true);            // angolo alto sx
  driver().writeImage(TILE, W - 64, 0, 64, 64, false, false, true);       // angolo alto dx
  driver().writeImage(TILE, 0, H - 64, 64, 64, false, false, true);       // angolo basso sx
  driver().writeImage(TILE, W - 64, H - 64, 64, 64, false, false, true);  // angolo basso dx
  driver().writeImage(TILE, W / 2 - 32, SPLIT - 32, 64, 64, false, false, true); // giunzione
  /**
   * Barra accent lungo il bordo basso della banda master: è l'unico elemento del
   * frame che passa dal piano 0x26 del driver, e senza di lei la fase driver non
   * proverebbe mai il canale accent. writeImageRed vuole la convenzione
   * bit = 1 dove il pixel NON appartiene al canale, quindi la barra è un blocco
   * di zeri: il driver applica l'inversione.
   */
  static const uint8_t ACCENT_BAR[64 * 8] PROGMEM = { 0 };
  for (int16_t x = 0; x < W; x += 64)
    driver().writeImageRed(ACCENT_BAR, x, SPLIT - 96, 64, 64, true);

  /**
   * Il riquadro col numero passa dal driver come tutto il resto del frame: la
   * convenzione di writeImage è bit = 1 bianco, quindi si compone con le
   * costanti nude e non con bwByteFor(), che descrive il piano RAM.
   */
  uint8_t bw = 0, bh = 0;
  if (componiBadge(numero, (uint16_t)W, (uint16_t)SPLIT, 0xFF, 0x00, &bw, &bh))
    driver().writeImage(badgeBuf, (int16_t)(W - bw - 8), 8, bw, bh);

  /**
   * Il gate lo fa il driver, dal gancio installato in setup(): il refresh parte
   * da dentro refresh(), quindi da qui non ci sarebbe modo di fermarsi fra RAM
   * scritta e vetro ridipinto. Vedi gateDaDriver().
   */
  const uint32_t t0 = millis();
  driver().refresh(false);
  driverTilesMs = (int32_t)(millis() - t0);
  attivaFramePrenotato();
  durataFrame(driverTilesMs);
  numeroRiquadro = numeroSulVetro;
  logLine("refresh in %ld ms", (long)driverTilesMs);

  driver().hibernate();
  logLine("driver in hibernate");

  look("i cinque tile sono al posto giusto e non specchiati? la barra accent e' rossa sul bordo basso della banda del master?");
}

/**
 * Spazza i modelli di indirizzamento che il cablaggio rende possibili.
 *
 * Con un solo connettore c'è solo ADDRESSING_DUAL_CS, e non perchè sia quello
 * giusto: in cascade la ScreenPart slave scriverebbe sul CS del master con gli
 * opcode offset, ma lo slave del pannello non è sul bus, quindi le due passate
 * darebbero lo stesso identico frame e la seconda sarebbe venti secondi e una
 * pausa spesi per niente. Con il secondo connettore cablato invece si provano
 * entrambi, ed è il confronto che dice quale dei due modelli il driver deve
 * tenere.
 */
static void runDriverPhase()
{
  if (cfg.secondFfc)
  {
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_DUAL_CS);
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE);
  }
  else
  {
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_DUAL_CS);
    Serial.println(F("\nADDRESSING_CASCADE non provato: con un solo connettore lo slave non"));
    logLine("è sul bus, quindi la passata darebbe lo stesso frame di DUAL_CS.");
    logLine("Quello che si poteva sapere sul suo indirizzamento lo ha misurato la");
    logLine("sonda elettrica del bit 7 dell'opcode.");
  }
}
/**
 * Gancio di avanzamento passato al driver: nella fase driver il refresh parte
 * da dentro la libreria, quindi è da qui che il gate deve scattare. Fa
 * esattamente quello che runRefresh() fa per i frame a SPI nativa, e per la
 * stessa ragione: la schermata che sta sul vetro va guardata prima che il frame
 * nuovo la cancelli. Non tocca SPI, come il contratto del gancio pretende.
 */
static void gateDaDriver(void* /*ctx*/, const char* motivo)
{
  Serial.printf("\n  il driver sta per lanciare un %s\n", motivo ? motivo : "refresh");
  chiudiFrame(etichettaPrenotata());
}

/**
 * Installa il gancio di avanzamento sul driver. Va chiamata una volta sola da
 * setup(): il driver si costruisce alla prima chiamata di driver(), e da lì in
 * poi ogni suo refresh passa dal gate.
 */
static void driverBegin()
{
  driver().setScreenAdvanceHook(&gateDaDriver);
}

#endif // DUAL_PANEL_FINDER_PROBES_DRIVER_H
