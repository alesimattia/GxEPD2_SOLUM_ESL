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
// giustificare. Il .ino conosce solo runDriverPass(), che prende il modello di
// indirizzamento come argomento.
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
 * specchiato si riconosce a occhio: dritto ha 8 righe nere in alto, subito sotto
 * una fascia nera con la tacca bianca spostata a destra, la scacchiera, e 16
 * righe nere in basso.
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
 * Istanza corrente del driver e il pinout con cui è stata costruita. Il driver
 * non si può riassegnare, quindi cambiare il pinout vuol dire distruggere
 * l'istanza e costruirne un'altra.
 */
static GxEPD2_SOLUM_DRIVER_CLASS* driverInstance = nullptr;
static int16_t driverCs2   = -2;
static int16_t driverBusy2 = -2;

/**
 * Il driver si costruisce quando serve e si ricostruisce quando cambia il pinout
 * del secondo controller: cs2 e busy2 devono restare a -1 senza secondo
 * connettore, e in cascade dove lo slave sta sul CS del master, altrimenti il
 * driver aspetterebbe un BUSY su un pin scollegato e andrebbe in timeout. Non è
 * una rinuncia ma la configurazione giusta, e metterci dei pin che non arrivano
 * a niente costa 190 ms di push a vuoto per frame.
 *
 * Il pinout è la struct uniforme della libreria, con l'ordine dei campi cs, dc,
 * rst, busy, cs2, busy2; il bus non ne fa parte, lo apre preparaBanco(). Il
 * gancio del gate si reinstalla a ogni costruzione, perchè appartiene
 * all'istanza e non allo sketch; un'istanza nuova
 * riparte anche dalle specchiature di default.
 */
static GxEPD2_SOLUM_DRIVER_CLASS& driver(bool withSecondCs)
{
  const int16_t cs2   = withSecondCs ? (int16_t)PIN_CS_OTHER   : (int16_t)-1;
  const int16_t busy2 = withSecondCs ? (int16_t)PIN_BUSY_OTHER : (int16_t)-1;
  if (!driverInstance || cs2 != driverCs2 || busy2 != driverBusy2)
  {
    delete driverInstance;
    GxEPD2_SOLUM_Pins pins{ PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, cs2, busy2 };
    driverInstance = new GxEPD2_SOLUM_DRIVER_CLASS(pins);
    driverInstance->setScreenAdvanceHook(&gateDaDriver);
    driverCs2   = cs2;
    driverBusy2 = busy2;
  }
  return *driverInstance;
}

/**
 * Una passata della fase driver: un frame solo, e un refresh solo. Ritorna il
 * numero del frame, così il chiamante può leggere la risposta data al gate.
 *
 * La fase probe misura il SILICIO senza passare da nessuno strato software,
 * questa misura il DRIVER: se il probe stampa e questa no, il difetto è nel
 * driver e non nel pannello. Il frame prova tutto insieme:
 *   - writeScreenBuffer con fondo bianco e accent spento, cioè la polarità dei
 *     due piani come la scrive il driver;
 *   - cinque tile ai quattro angoli e uno a cavallo della giunzione a
 *     y = PART_HEIGHT: il dispatch per righe e la geometria dello split. Il
 *     tile ha bordo pieno e un angolo marcato, quindi uno specchiato si vede;
 *   - una barra accent lungo il bordo basso della banda master (righe
 *     384..767, il lato del vetro dove esce la coda lunga), l'unico elemento che
 *     passa dal piano 0x26 del driver invece che dalla SPI diretta.
 *
 * Il clock è il default del driver, non quello della diagnostica: qui si
 * verifica il driver com'è configurato. `mode` è il modello di indirizzamento, e
 * `domanda` quella che il gate pone sul frame: D_DIPINTO dal menu, D_META nella
 * procedura guidata, dove la risposta decide il passo successivo.
 */
static uint8_t runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::AddressingMode mode,
                             Domanda domanda = D_DIPINTO)
{
  const bool cascade = (mode == GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE);
  // in cascade lo slave sta sul CS del master: il secondo connettore non si pilota
  const bool withSecondCs = cfg.secondFfc && !cascade;
  static char etichetta[48];
  snprintf(etichetta, sizeof(etichetta), "fase driver: ADDRESSING_%s",
           cascade ? "CASCADE" : "DUAL_CS");
  const Frame f = frameCon(etichetta, domanda);
  const uint8_t numero = registraFrame(f);

  Serial.println(F("\n=================================================="));
  logLine("FASE DRIVER - GxEPD2_SOLUM_122c_960x768");
  Serial.printf ("  geometria          %ux%u, bande da %ux%u (master righe %u..%u, slave 0..%u)\n",
                 GxEPD2_SOLUM_DRIVER_CLASS::WIDTH, GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT,
                 GxEPD2_SOLUM_DRIVER_CLASS::PART_WIDTH,
                 GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT,
                 (unsigned)GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT,
                 (unsigned)(GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT - 1),
                 (unsigned)(GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT - 1));
  if (withSecondCs)
    Serial.printf ("  controller         due: CS=%d BUSY=%d e CS=%d BUSY=%d\n",
                   PIN_CS, PIN_BUSY, PIN_CS_OTHER, PIN_BUSY_OTHER);
  else if (cascade)
    Serial.printf ("  controller         due in cascade, un solo CS=%d BUSY=%d\n",
                   PIN_CS, PIN_BUSY);
  else
    Serial.printf ("  controller         uno: CS=%d BUSY=%d, banda slave non pilotata\n",
                   PIN_CS, PIN_BUSY);
  if (cascade)
    Serial.printf ("  indirizzamento     CASCADE: un solo CS (%d), slave a opcode|0x%02X,\n"
                   "                     master in cascade con 0x21 = 00 10\n",
                   PIN_CS, (unsigned)GxEPD2_SOLUM_DRIVER_CLASS::CASCADE_CMD_OFFSET);
  else
    logLine("indirizzamento     DUAL CS: un opcode solo, un CS per controller");
  Serial.printf ("  clock              %lu MHz, il default del driver\n",
                 (unsigned long)(cfg.spiDriver / 1000000UL));

  /**
   * selectSPI e setAddressingMode vanno prima di init(). Il bus è quello che
   * preparaBanco() ha aperto sui pin della board, prerequisito di ogni passata;
   * init() decide quali CS configurare, e in cascade il secondo chip select
   * non esiste e non va pilotato. init() chiude con un reset hardware, che basta
   * a svegliare il pannello dal deep sleep in cui la fase probe lo ha lasciato.
   */
  GxEPD2_SOLUM_DRIVER_CLASS& drv = driver(withSecondCs);
  drv.selectSPI(hspi, SPISettings(cfg.spiDriver, MSBFIRST, SPI_MODE0));
  drv.setAddressingMode(mode);
  drv.init(115200);

  const int16_t H = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT;
  const int16_t W = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::WIDTH;
  const int16_t SPLIT = (int16_t)GxEPD2_SOLUM_DRIVER_CLASS::PART_HEIGHT;

  Serial.println(F("\nframe del driver: fondo + 5 tile + barra accent, un refresh solo"));
  if (!withSecondCs && !cascade)
  {
    logLine("con un controller solo compaiono i due tile degli angoli BASSI e la");
    logLine("metà inferiore di quello sulla giunzione: gli altri tre cadono nella");
    logLine("banda slave (righe 0..383), che non è pilotata. Non è un difetto del dispatch");
  }
  else if (cascade)
  {
    logLine("in cascade compaiono tutti e cinque i tile se lo slave esegue gli opcode");
    logLine("offset; se compaiono solo quelli bassi lo slave non è stato indirizzato");
  }

  drv.writeScreenBuffer(0xFF, 0x00);          // fondo bianco, accent spento
  drv.writeImage(TILE, 0, 0, 64, 64, false, false, true);            // angolo alto sx
  drv.writeImage(TILE, W - 64, 0, 64, 64, false, false, true);       // angolo alto dx
  drv.writeImage(TILE, 0, H - 64, 64, 64, false, false, true);       // angolo basso sx
  drv.writeImage(TILE, W - 64, H - 64, 64, 64, false, false, true);  // angolo basso dx
  drv.writeImage(TILE, W / 2 - 32, SPLIT - 32, 64, 64, false, false, true); // giunzione
  /**
   * Barra accent lungo il bordo basso della banda master (H - 96 con 32 px di
   * margine dal vetro): è l'unico elemento del frame che passa dal piano 0x26 del
   * driver, e senza di lei la fase driver non proverebbe mai il canale accent.
   * writeImageRed vuole la convenzione bit = 1 dove il pixel NON appartiene al
   * canale, quindi la barra è un blocco di zeri: il driver applica l'inversione.
   */
  static const uint8_t ACCENT_BAR[64 * 8] PROGMEM = { 0 };
  for (int16_t x = 0; x < W; x += 64)
    drv.writeImageRed(ACCENT_BAR, x, H - 96, 64, 64, true);

  /**
   * Il riquadro col numero passa dal driver come tutto il resto del frame, ed è
   * dentro la banda master (PART_HEIGHT + 8): è la sola banda pilotata con un
   * controller solo, quindi il numero si legge sempre. La convenzione di
   * writeImage è bit = 1 bianco, quindi si compone con le costanti nude e non
   * con bwByteFor(), che descrive il piano RAM.
   */
  uint8_t bw = 0, bh = 0;
  if (componiBadge(numero, (uint16_t)W, (uint16_t)SPLIT, 0xFF, 0x00, &bw, &bh))
    drv.writeImage(badgeBuf, (int16_t)(W - bw - 8), (int16_t)(SPLIT + 8), bw, bh);

  /**
   * Il gate lo fa il driver, dal gancio installato alla costruzione: il refresh
   * parte da dentro refresh(), quindi da qui non ci sarebbe modo di fermarsi fra
   * RAM scritta e vetro ridipinto. Vedi gateDaDriver().
   */
  const uint32_t t0 = millis();
  drv.refresh(false);
  driverTilesMs = (int32_t)(millis() - t0);
  attivaFramePrenotato();
  durataFrame(driverTilesMs);
  numeroRiquadro = numeroSulVetro;
  logLine("refresh in %ld ms", (long)driverTilesMs);

  drv.hibernate();
  logLine("driver in hibernate");

  look("quali tile compaiono? il tile dritto ha la tacca bianca in alto e a destra, 8 righe nere sopra e 16 sotto; la barra accent e' rossa sul bordo basso della banda del master?");
  return numero;
}

/**
 * Spazza i modelli di indirizzamento che il cablaggio rende possibili.
 *
 * Con il cablaggio dichiarato dalla procedura guidata gira un modello solo: la
 * cascade con il ponte cascade, i due CS con il ponte a due CS. Senza cablaggio
 * dichiarato e con un solo connettore c'è solo ADDRESSING_DUAL_CS, e non perchè
 * sia quello giusto: in cascade la ScreenPart slave scriverebbe sul CS del master
 * con gli opcode offset, ma lo slave del pannello non è sul bus, quindi le due
 * passate darebbero lo stesso identico frame e la seconda sarebbe venti secondi e
 * una pausa spesi per niente. Con il secondo connettore cablato si provano
 * entrambi, ed è il confronto che dice quale dei due modelli il driver deve
 * tenere.
 */
static void runDriverPhase()
{
  if (cfg.wiring == WIRING_CASCADE)
  {
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE);
    return;
  }
  if (cfg.wiring == WIRING_DUAL_CS)
  {
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_DUAL_CS);
    return;
  }
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
    logLine("La sonda elettrica del bit 7 misura solo come il controller in J1 tratta");
    logLine("gli opcode offset, non lo slave: la prova dello slave è la sonda b a ponte fatto.");
  }
}

/**
 * Passata della fase driver per la procedura guidata del .ino: il gate pone la
 * domanda su quale banda ha dipinto, la cui risposta decide i passi successivi.
 * Prende un bool e non il tipo del modello di indirizzamento perchè il .ino non
 * nomina la classe del driver. Ritorna il numero del frame.
 */
static uint8_t guidedDriverPass(bool cascade)
{
  return runDriverPass(cascade ? GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE
                               : GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_DUAL_CS,
                       D_META);
}

#endif // DUAL_PANEL_FINDER_PROBES_DRIVER_H
