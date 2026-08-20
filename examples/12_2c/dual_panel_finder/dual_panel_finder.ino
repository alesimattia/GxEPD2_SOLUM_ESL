// =============================================================================
// dual_panel_finder.ino — sonda di identificazione del SOLUM Newton-Core 12.2"
//
// Sketch standalone: NON usa il driver custom di questa libreria, usa il driver
// stock GxEPD2_1160c_GDEY116Z91 (controller SSD16xx) per pilotare uno dei due
// controller del pannello alla volta.
//
// Cosa deve stabilire: se il Newton-Core 12.2" risponda a SSD16xx e non a
// UC8179, che è l'assumption non validata su cui poggia
// src/GxEPD2_SOLUM_122c_960x768.h. È la misura che decide su quale base va
// scritto quel driver, quindi va eseguita prima di toccarlo.
//
// PROCEDURA
//   1. Compila con TEST_TARGET = TEST_MASTER, flasha, osserva il pannello e
//      annota i tempi sul serial (115200). La metà del pannello pilotata dal
//      controller del FFC #1 deve mostrare il pattern con la lettera M.
//   2. Compila con TEST_TARGET = TEST_SLAVE, flasha, ripeti. La metà
//      complementare (FFC #2) deve mostrare lo stesso pattern con la S.
//
// COME SI LEGGE IL PATTERN
//   Il pattern è asimmetrico su entrambi gli assi, così un esito parziale
//   resta interpretabile:
//     - cuneo nero pieno nell'angolo in alto a sinistra: ancora dell'origine.
//       Se compare a destra, la scan X è invertita rispetto all'atteso;
//     - barra rossa piena in basso a destra: chiude l'asse opposto. Se il
//       cuneo si vede e la barra no, il controller sta scandendo meno righe
//       di quante gliene sono state programmate;
//     - righello in alto con tick ogni 64 px ed etichette ogni 128 px: dice
//       quale banda di colonne è pilotata da questo controller, cioè come i
//       due FFC si dividono il pannello;
//     - lettera M o S alta 112 px al centro: identifica a distanza quale dei
//       due controller ha reagito;
//     - diagonale nera da angolo ad angolo: rende visibile uno shift di
//       stride, che spezza la linea in gradini.
//   Nessun pixel del pattern è precalcolato: viene disegnato con le primitive
//   Adafruit_GFX, quindi lo sketch non porta bitmap versionate.
//
// HARDWARE
//   - Board: Waveshare E-Paper ESP32 Driver Board (HSPI: SCK=13, MISO=12,
//     MOSI=14). FFC #1 nel connettore interno della board.
//   - FFC #2: breakout 24-pin esterno cablato come da docs/122c/connessioni.html,
//     con CS_S=GPIO33 e BUSY_S=GPIO35.
//
// NOTE TECNICHE
//   - GxEPD2_1160c::HEIGHT = 640, il SOLUM è alto 768: lo sketch scrive solo
//     le prime 640 righe. Le ultime 128 restano allo stato precedente, che per
//     un test di vita è accettabile.
//   - SPI clock 4 MHz, non 20 MHz: margine sicuro per il bring-up.
//   - reset_duration = 2 ms.
// =============================================================================

#include <SPI.h>
#include <GxEPD2_3C.h>

// === Compile-time switch: master | slave ============================
#define TEST_MASTER   1
#define TEST_SLAVE    2
#define TEST_TARGET   TEST_MASTER   // <<< cambia tra MASTER e SLAVE e riflasha

// === Pin condivisi (Waveshare E-Paper ESP32 Driver Board, HSPI) =====
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;
static const int EPD_DC   = 27;
static const int EPD_RST  = 26;

#if TEST_TARGET == TEST_MASTER
  static const int  EPD_CS         = 15;   // FFC #1 (connettore interno della board)
  static const int  EPD_BUSY       = 25;
  static const char TARGET_LABEL[] = "MASTER (FFC#1, CS=15, BUSY=25)";
  static const char TARGET_GLYPH[] = "M";
#elif TEST_TARGET == TEST_SLAVE
  static const int  EPD_CS         = 33;   // FFC #2 (breakout esterno)
  static const int  EPD_BUSY       = 35;
  static const char TARGET_LABEL[] = "SLAVE  (FFC#2, CS=33, BUSY=35)";
  static const char TARGET_GLYPH[] = "S";
#else
  #error "TEST_TARGET deve essere TEST_MASTER o TEST_SLAVE"
#endif

SPIClass hspi(HSPI);

GxEPD2_3C<GxEPD2_1160c_GDEY116Z91, GxEPD2_1160c_GDEY116Z91::HEIGHT / 2>
    display(GxEPD2_1160c_GDEY116Z91(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static const int16_t PATTERN_W = GxEPD2_1160c_GDEY116Z91::WIDTH;   // 960
static const int16_t PATTERN_H = GxEPD2_1160c_GDEY116Z91::HEIGHT;  // 640

/**
 * Disegna il pattern di riconoscimento dentro il loop paged. Ogni elemento è
 * ancorato a un angolo diverso: un esito parziale dice comunque quale porzione
 * del pannello ha reagito e con quale orientamento.
 */
void drawPattern()
{
  // Cornice: delimita l'area effettivamente scandita dal controller.
  for (int16_t i = 0; i < 4; ++i)
    display.drawRect(i, i, PATTERN_W - 2 * i, PATTERN_H - 2 * i, GxEPD_BLACK);

  // Cuneo nero in alto a sinistra: ancora dell'origine (0,0).
  display.fillTriangle(8, 8, 208, 8, 8, 208, GxEPD_BLACK);

  // Barra rossa in basso a destra: angolo opposto, e prova del secondo piano.
  display.fillRect(PATTERN_W - 268, PATTERN_H - 108, 260, 100, GxEPD_RED);

  // Diagonale da angolo ad angolo: uno shift di stride la spezza in gradini.
  display.drawLine(0, 0, PATTERN_W - 1, PATTERN_H - 1, GxEPD_BLACK);

  // Righello delle colonne: tick ogni 64 px, etichetta ogni 128 px.
  display.setTextColor(GxEPD_BLACK);
  for (int16_t x = 0; x < PATTERN_W; x += 64)
  {
    const bool labelled = ((x % 128) == 0);
    display.drawFastVLine(x, 8, labelled ? 40 : 22, GxEPD_BLACK);
    if (labelled && x > 0)
    {
      display.setTextSize(2);
      display.setCursor(x + 4, 54);
      display.print(x);
    }
  }

  // Lettera del controller sotto test, alta ~112 px: leggibile da lontano.
  display.setTextSize(16);
  display.setCursor(PATTERN_W / 2 - 44, PATTERN_H / 2 - 56);
  display.print(TARGET_GLYPH);

  // Etichetta testuale del target, centrata sotto la lettera.
  display.setTextSize(3);
  display.setCursor(PATTERN_W / 2 - 260, PATTERN_H / 2 + 96);
  display.print(TARGET_LABEL);
}

void drawImage()
{
  display.setRotation(0);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawPattern();
  } while (display.nextPage());
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.print  (F(" dual_panel_finder — target: "));
  Serial.println(TARGET_LABEL);
  Serial.println(F(" stock driver: GxEPD2_1160c_GDEY116Z91 (SSD16xx)"));
  Serial.println(F("=================================================="));

  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, EPD_CS);
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  /**
   * init(serial_diag_bitrate=115200, initial=true, reset_duration=2,
   *      pulldown_rst_mode=false): chiama Serial.begin internamente, abilita
   * i log diagnostici (tempi BUSY, "Busy Timeout!") e usa un pulse di reset
   * corto.
   */
  Serial.println(F("init..."));
  unsigned long t0 = millis();
  display.init(115200, true, 2, false);
  Serial.print(F("init done in "));
  Serial.print(millis() - t0);
  Serial.println(F(" ms"));

  Serial.println(F("draw pattern (full window, primitive GFX)..."));
  t0 = millis();
  drawImage();
  Serial.print(F("draw + refresh in "));
  Serial.print(millis() - t0);
  Serial.println(F(" ms"));

  display.hibernate();
  Serial.println(F("hibernate done — osserva il pannello ora"));
}

void loop()
{
}
