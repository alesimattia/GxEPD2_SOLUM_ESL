// =============================================================================
// Verifica sul vetro del refresh differenziale del driver SOLUM 9.7".
//
// Prova con il codice VERO del driver, non con una copia a SPI diretta, le tre
// assunzioni che nessuna sonda di panel_diagnostic ha mai misurato:
//   1. l'orientamento scambiato delle RAM: dopo un refresh pieno il frame nuovo
//      va in 0x26 e le righe VS di LUT1 e LUT2 si mandano scambiate; dal frame
//      dopo i ruoli tornano normali. Si vede perchè a ogni passata metà dei
//      quadrati passa da bianco a nero e l'altra metà da nero a bianco;
//   2. il power off 0xC3 fra due passate (la catena delle sonde tiene l'analog
//      acceso): lo fa il template a fine loop, e qui hibernate() segue la stessa
//      strada del firmware;
//   3. un sonno di minuti in Sleep fra due passate, con la RAM che deve restare:
//      le attese sono 30 s per i primi frame e 5 minuti per gli ultimi.
//
// Sequenza: un refresh pieno (riferimento), sei frame che il driver deve
// eseguire in partial, un frame con il rosso spostato che deve essere RIFIUTATO
// e rifatto in pieno, e un ultimo partial che prova la ripartenza della catena.
// A ogni passo la seriale dice cosa il driver ha fatto e cosa guardare sul vetro.
//
// Cosa guardare, in ordine di importanza:
//   - nessun pixel rosso in più o in meno in nessun partial, e la fascia rossa
//     ferma: è la garanzia su cui regge tutto;
//   - i quattro casi di transizione nella riga di quadrati: bianco->nero,
//     nero->bianco, bianco fermo e nero fermo (i due quadrati di riferimento a
//     destra non devono mai cambiare);
//   - la zona "uguale fra N e N+1 ma diversa da N-1": un partial deve cambiarla
//     solo quando cambia davvero;
//   - l'alone attorno al quadrato rosso, con i rettangoli neri che si alternano a
//     6 e 9 px da lui: è la distanza dei blocchi meteo della dashboard;
//   - il grigio che resta dove il nero viene tolto, che cresce lungo la catena
//     e si azzera con il refresh pieno del passo 8.
//
// Compilazione da PC con arduino-cli, la libreria si passa solo per questa build:
//   arduino-cli --config-file C:/tmp/arduino/arduino-cli.yaml compile -b esp32:esp32:esp32
//       --library A:/epd/GxEPD2_SOLUM_ESL --build-path A:/tmp/arduino-build/differential_check <cartella>
//
// Cablaggio: Waveshare E-Paper ESP32 Driver Board (BUSY 25, RST 26, DC 27, CS 15,
// SPI su HSPI con SCK 13 e MOSI 14, scambiati rispetto al default).
//
// Author: Mattia Alesi
// =============================================================================

#define ENABLE_GxEPD2_GFX 1
#define SOLUM_PANEL_097C
#include <GxEPD2_3C.h>
#include <GxEPD2_SOLUM.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// Globale, non locale a setup(): selectSPI() conserva il puntatore a hspi.
SPIClass hspi(HSPI);

// Un ottavo dell'altezza, come il firmware: otto pagine da 84 righe.
static const int16_t PAGE_HEIGHT = GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT / 8;
GxEPD2_3C<GxEPD2_SOLUM_DRIVER_CLASS, PAGE_HEIGHT>
    display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }));

/** Passi della prova: frame, attesa PRIMA di disegnarlo e spostamento del rosso. */
struct Step
{
  uint8_t  frame;
  uint32_t waitBeforeMs;
  int16_t  redShift;
  const char* expected;
};

static const Step STEPS[] =
{
  { 0,      0, 0,  "PIENO (riferimento): nessun riferimento prima, quindi refresh pieno da ~24 s" },
  { 1,  30000, 0,  "PARTIAL, nuovo in 0x26, LUT SCAMBIATA (primo dopo un pieno), dopo 30 s di Sleep" },
  { 2,  30000, 0,  "PARTIAL, nuovo in 0x24, LUT originale, dopo 30 s" },
  { 3,  30000, 0,  "PARTIAL, nuovo in 0x26, LUT scambiata, dopo 30 s" },
  { 4, 300000, 0,  "PARTIAL, nuovo in 0x24, LUT originale, dopo 5 MINUTI di Sleep" },
  { 5, 300000, 0,  "PARTIAL, nuovo in 0x26, LUT scambiata, dopo 5 minuti" },
  { 6, 300000, 0,  "PARTIAL, nuovo in 0x24, LUT originale, dopo 5 minuti: catena a 6" },
  { 7,  30000, 60, "RIFIUTATO: la fascia rossa si sposta, il driver non tocca il pannello e il frame si rifà in PIENO" },
  { 8,  30000, 60, "PARTIAL dopo il pieno: la catena riparte da 1 e il grigio è azzerato" },
};
static const uint8_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

// Geometria della scena, in pixel dello schermo 960x672.
static const int16_t RED_BAND_X = 40, RED_BAND_Y = 90, RED_BAND_W = 360, RED_BAND_H = 50;
static const int16_t BLACK_BAND_X = 40, BLACK_BAND_Y = 160, BLACK_BAND_W = 360, BLACK_BAND_H = 30;
static const int16_t CHECK_X = 40, CHECK_Y = 240, CHECK_SIZE = 40, CHECK_STEP = 60, CHECK_COUNT = 8;
static const int16_t REF_BLACK_X = 560, REF_WHITE_X = 640, REF_Y = 240, REF_SIZE = 60;
static const int16_t ZONE_X = 40, ZONE_Y = 340, ZONE_W = 100, ZONE_H = 60;
static const int16_t HALO_RED_X = 790, HALO_RED_Y = 330, HALO_RED_W = 60, HALO_RED_H = 60;

/**
 * Disegna la scena del frame k nel buffer della pagina corrente. Tutto è
 * deterministico: due chiamate con gli stessi argomenti producono gli stessi
 * piani, quindi il CRC del rosso resta uguale finchè redShift non cambia.
 */
static void drawScene(uint8_t k, int16_t redShift)
{
  display.fillScreen(GxEPD_WHITE);

  // Titolo e contatore in nero: cambiano a ogni frame, sono il caso normale.
  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(40, 40);
  display.print("differential_check, frame ");
  display.print(k);
  display.setFont(&FreeSansBold18pt7b);
  display.setCursor(560, 180);
  display.print(k);

  // Fascia rossa ferma, con testo bianco: se si sposta il rosso è cambiato.
  display.fillRect(RED_BAND_X + redShift, RED_BAND_Y, RED_BAND_W, RED_BAND_H, GxEPD_RED);
  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(RED_BAND_X + redShift + 12, RED_BAND_Y + 32);
  display.print("ROSSO FERMO");

  // Fascia nera ferma: nero fermo.
  display.fillRect(BLACK_BAND_X, BLACK_BAND_Y, BLACK_BAND_W, BLACK_BAND_H, GxEPD_BLACK);

  // Riga di quadrati: ogni passata metà va bianco->nero e metà nero->bianco.
  for (int16_t i = 0; i < CHECK_COUNT; ++i)
  {
    const int16_t x = CHECK_X + i * CHECK_STEP;
    display.drawRect(x, CHECK_Y, CHECK_SIZE, CHECK_SIZE, GxEPD_BLACK);
    if (((k + i) & 1) == 0) display.fillRect(x, CHECK_Y, CHECK_SIZE, CHECK_SIZE, GxEPD_BLACK);
  }

  // Riferimenti che non devono mai cambiare: un quadrato nero e uno bianco con bordo.
  display.fillRect(REF_BLACK_X, REF_Y, REF_SIZE, REF_SIZE, GxEPD_BLACK);
  display.drawRect(REF_WHITE_X, REF_Y, REF_SIZE, REF_SIZE, GxEPD_BLACK);

  // Zona uguale fra N e N+1 e diversa da N-1: piena nei frame 2, 3, 6, 7.
  display.drawRect(ZONE_X, ZONE_Y, ZONE_W, ZONE_H, GxEPD_BLACK);
  if (((k / 2) & 1) == 1) display.fillRect(ZONE_X, ZONE_Y, ZONE_W, ZONE_H, GxEPD_BLACK);

  // Alone: quadrato rosso fermo e due rettangoli neri che si alternano a 9 px a
  // sinistra e 6 px a destra, come i blocchi meteo vicini all'icona rossa.
  display.fillRect(HALO_RED_X, HALO_RED_Y, HALO_RED_W, HALO_RED_H, GxEPD_RED);
  const int16_t leftX = HALO_RED_X - 9 - 40;
  const int16_t rightX = HALO_RED_X + HALO_RED_W + 6;
  display.drawRect(leftX, HALO_RED_Y, 40, HALO_RED_H, GxEPD_BLACK);
  display.drawRect(rightX, HALO_RED_Y, 40, HALO_RED_H, GxEPD_BLACK);
  if ((k & 1) == 0) display.fillRect(leftX, HALO_RED_Y, 40, HALO_RED_H, GxEPD_BLACK);
  else display.fillRect(rightX, HALO_RED_Y, 40, HALO_RED_H, GxEPD_BLACK);
}

/** Un loop paged completo della scena. */
static void renderPages(uint8_t k, int16_t redShift)
{
  display.firstPage();
  do
  {
    drawScene(k, redShift);
  } while (display.nextPage());
}

/**
 * Un frame con la stessa logica di Weather::renderFrame(): prima armato, e se il
 * driver rifiuta o non può armare un disegno pieno. Stampa cosa è successo.
 */
static void runFrame(const Step& step)
{
  const uint32_t t0 = millis();
  const bool armed = display.epd2.beginDifferentialFrame();
  bool fired = false;
  uint16_t mask = 0;
  if (armed)
  {
    renderPages(step.frame, step.redShift);
    fired = display.epd2.endDifferentialFrame();
    if (!fired) mask = display.epd2.differentialRefusedPages();
  }
  if (!fired) renderPages(step.frame, step.redShift);
  const uint32_t ms = millis() - t0;

  Serial.printf("  armato %s, partial %s, catena %u, pagine con rosso cambiato 0x%02X, durata %lu ms\n",
                armed ? "si" : "no", fired ? "si" : "no", (unsigned)display.epd2.differentialChain(),
                (unsigned)mask, (unsigned long)ms);
  if (fired && ms > 4000) Serial.println("  ATTENZIONE: un partial dura ~1,3 s, qui molto di piu: ha girato un pieno?");
  if (!fired && step.frame != 0 && step.redShift == 0)
    Serial.println("  ATTENZIONE: frame con rosso invariato ma NON partial: il riferimento e' andato perso");
}

/** Attesa con un punto ogni 10 secondi, per vedere che la prova non e' bloccata. */
static void waitWithDots(uint32_t ms)
{
  Serial.printf("  attesa %lu s (pannello in Sleep, nessun deep sleep) ", (unsigned long)(ms / 1000UL));
  uint32_t left = ms;
  while (left > 0)
  {
    const uint32_t slice = left > 10000UL ? 10000UL : left;
    delay(slice);
    left -= slice;
    Serial.print('.');
  }
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  hspi.begin(13, 12, 14, 15);                                  // SCK, MISO, MOSI, SS
  display.epd2.selectSPI(hspi, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 2, false);
  display.setRotation(0);                                      // 960w x 672h landscape
  display.setFullWindow();
  display.epd2.setDifferentialRefresh(true);

  Serial.println();
  Serial.println("=== differential_check: refresh differenziale B/N del driver SOLUM 9.7 ===");
  Serial.println("La prova dura circa 20 minuti. Guarda il vetro dopo ogni passo.");

  for (uint8_t i = 0; i < STEP_COUNT; ++i)
  {
    const Step& step = STEPS[i];
    Serial.printf("\nPASSO %u/%u, frame %u: %s\n", (unsigned)(i + 1), (unsigned)STEP_COUNT,
                  (unsigned)step.frame, step.expected);
    if (step.waitBeforeMs > 0) waitWithDots(step.waitBeforeMs);
    runFrame(step);
    // Come il firmware dopo ogni frame: con un riferimento valido resta in Sleep.
    display.hibernate();
  }

  Serial.println("\n=== FINE ===");
  Serial.println("Esito atteso: passi 2-7 partial con catena 1..6, passo 8 rifiutato e rifatto in pieno");
  Serial.println("(partial no, catena 0), passo 9 partial con catena 1. Sul vetro: rosso mai cambiato");
  Serial.println("nei partial, quadrati corretti nei due versi, riferimenti fermi, zona che cambia solo");
  Serial.println("quando cambia davvero, nessun alone vistoso attorno al quadrato rosso.");
}

void loop()
{
  delay(1000);
}
