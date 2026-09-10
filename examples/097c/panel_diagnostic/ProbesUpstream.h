// =============================================================================
// ProbesUpstream.h — le sequenze di partenza, riprodotte a SPI diretta.
//
// PERCHE' ESISTONO. Il driver custom nasce da GxEPD2_1330c_GDEM133Z91, e quella
// scelta non è mai stata verificata sul vetro. In GxEPD2 1.6.9 i driver SSD1677
// sono NOVE, e differiscono in punti che contano: il quinto byte del soft
// start, l'ordine fra 0x18 e 0x0C, il MUX, l'entry mode, se caricano la
// waveform in init con 0xB1, se scrivono 0x21 prima di ogni refresh, quale
// sequenza di power off usano. Nessuna di quelle differenze si può giudicare
// leggendo il codice: si mette sul vetro una variante per volta e si guarda.
//
//   epd/GxEPD2_1160_T91          960x640 B/N, da cui viene la LUT del partial
//   epd/GxEPD2_370_TC1           280x480 B/N, l'unico che scrive TUTTA la waveform
//   epd3c/GxEPD2_750c_Z90        880x528 BWR
//   gdem/GxEPD2_1020_GDEM102T91  960x640 B/N
//   gdem/GxEPD2_1330_GDEM133T91  960x680 B/N, il gemello mono del donor
//   gdem/GxEPD2_397_GDEM0397T81  800x480 B/N, fra i più recenti
//   gdem3c/GxEPD2_1330c_GDEM133Z91  960x680 BWR, la base attuale
//   gdeq/GxEPD2_426_GDEQ0426T82  800x480 B/N, il riferimento SSD1677 moderno
//   gdey3c/GxEPD2_1160c_GDEY116Z91  960x640 BWR, init nuda
//
// Dei tre a tre colori NESSUNO ha il partial, e i sei monocromatici lo hanno
// tutti con la RAM 0x26 come frame precedente: è la ragione per cui il partial
// del driver custom viene dal 1160_T91 e non dalla sua base.
//
// L'unica variante non riprodotta è quella di GDEY116Z91, che manda solo
// SWRESET e 0x3C lasciando tutto il front-end analogico al POR: togliere il
// soft start è la sola istruzione che può stressare il booster, e senza una
// waveform del produttore non c'è modo di sapere quanto margine ci sia.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef PANEL_DIAGNOSTIC_PROBES_UPSTREAM_H
#define PANEL_DIAGNOSTIC_PROBES_UPSTREAM_H

#include "Controller.h"

// ---------------------------------------------------------------------------
// Le sequenze, una riga per driver di partenza. I valori sono quelli letterali
// dei sorgenti citati, MUX compreso: il campo è il VALORE DEL REGISTRO 0x01, e
// le gate line sono una in più. Sta a 671 (= 672 linee, quante il pannello ne
// ha) su tutte tranne quella del donor, che tiene i suoi 679 come upstream: il
// conteggio delle gate è proprietà del pannello e non del driver, e cambiarlo
// insieme al resto confonderebbe due variabili in una misura sola. La variante custom-live non è in tabella: la
// aggiunge il runner leggendo cfg.seq, editabile dal menu.
// ---------------------------------------------------------------------------
static const InitSequence INIT_SEQUENCES[] =
{
  // driver custom: _InitDisplay() di GxEPD2_SOLUM_097c_960x672.h
  { "custom",        2, true,   0, false, false, 0x80, 671, 0x00, 0x03,
    false, false, { 0x00, 0x00 }, 0, false, 0xF7, -1 },

  // firmware di fabbrica SOLUM, docs/openepaperlink/.../unissd.cpp ramo 0x19.
  // epdReset() ritenta con impulsi crescenti finchè il BUSY non scende: il
  // primo giro è a 10 ms, ed è quello riprodotto qui.
  { "SOLUM fabbrica", 10, true, 0, false, true,  0x80, 671, 0x00, 0x02,
    false, true,  { 0x08, 0x00 }, 1, false, 0xF7, -1 },

  // gdem3c/GxEPD2_1330c_GDEM133Z91.cpp:296-329, la base attuale del driver
  { "GDEM133Z91",   10, false, 10, false, false, 0x80, 679, 0x00, 0x03,
    false, false, { 0x00, 0x00 }, 0, false, 0xF7, 0xC3 },

  // epd/GxEPD2_1160_T91.cpp:356-420, da cui viene la LUT del partial
  { "GDEH116T91",   10, false, 10, false, false, 0x40, 671, 0x00, 0x03,
    true,  false, { 0x00, 0x00 }, 0, true,  0xF4, 0x83 },

  // epd3c/GxEPD2_750c_Z90.cpp:374-417, l'altro SSD1677 a tre colori
  { "GDEH075Z90",   10, true,   0, false, false, 0x40, 671, 0x00, 0x03,
    true,  false, { 0x00, 0x00 }, 0, false, 0xC7, 0xC3 },

  // gdeq/GxEPD2_426_GDEQ0426T82.cpp:360-417 e gdem/GxEPD2_397_GDEM0397T81.cpp:
  // il riferimento SSD1677 moderno, con 0x18 prima di 0x0C, SM nel MUX e 0x21
  // scritto prima di ogni refresh
  { "GDEQ0426T82",  10, false, 10, true,  false, 0x80, 671, 0x02, 0x03,
    false, false, { 0x40, 0x00 }, 2, false, 0xF7, 0x83 },

  // docs/097c/gooddisplay_GDEM102Z91_arduino/Display_EPD_W21.cpp:15-70, il demo
  // ufficiale del gemello commerciale 960x640 BWR
  { "GDEM102Z91",   10, true,   0, false, false, 0x80, 671, 0x00, 0x01,
    false, false, { 0x00, 0x00 }, 0, false, 0xF7, -1 },
};
static const uint8_t INIT_SEQUENCE_COUNT = sizeof(INIT_SEQUENCES) / sizeof(INIT_SEQUENCES[0]);
static_assert(INIT_SEQUENCE_COUNT == 7, "le sequenze documentate sono sette");

/**
 * Dipinge la schermata di una variante e la misura.
 *
 * Sul vetro finiscono quattro cose, e ognuna risponde a una domanda diversa:
 * la fascia nera in alto porta il NOME della variante, quindi si sa sempre cosa
 * si sta guardando; la fascia rossa dice se quell'init rende l'accent; la riga
 * nera sulle ultime otto gate dice se il MUX le scandisce davvero; il riquadro
 * col numero lega la schermata al log.
 *
 * Le letture attese, che il log dichiara prima: con entry mode 0x02 l'immagine
 * esce specchiata lungo X, con 0x21 in BW inverse esce in negativo, con entry
 * mode 0x01 esce a testa in giù. Il firmware di fabbrica compensa le prime due
 * in software, quindi vederle è la conferma che la sequenza è stata riprodotta
 * fedelmente, non un difetto.
 */
static void runInitVariant(const InitSequence& s)
{
  runInitSequence(s);
  const bool inverted = (s.ctrl21When != 0) && (s.ctrl21[0] & 0x08);

  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  fillBand(0x24, 0, BAND_H, 0x00, s.name, 0xFF);                 // nome, bianco su nero
  fillBand(0x26, (uint16_t)(BAND_H * 2), (uint16_t)(BAND_H / 2), 0xFF);   // fascia rossa
  fillPlaneRect(0x24, 0, (uint16_t)(GATE - 8), SRC, 8, 0x00);    // ultime otto gate
  setRamWindow(0, 0, SRC, (uint16_t)(s.muxRegister + 1), s.entryMode);

  if (s.powerOnFirst) partialPowerOn();
  if (s.ctrl21When == 2) writeCommandData(0x21, s.ctrl21, 2);
  runRefresh(s.refresh, s.name, 40000, inverted);
  if (s.powerOff >= 0) powerOffPanel((uint8_t)s.powerOff);
}

/**
 * i — le sequenze di partenza a confronto, una schermata per variante.
 *
 * È la sonda che decide da quale driver di GxEPD2 il driver custom dovrebbe
 * derivare: l'operatore risponde o/n a "nero pieno, rosso saturo, bordi netti
 * come la custom?", e la risposta finisce nel registro accanto alla durata.
 */
static void probeInitSequences()
{
  for (uint8_t i = 0; i < INIT_SEQUENCE_COUNT; ++i)
  {
    openScreen(INIT_SEQUENCES[i].name);
    runInitVariant(INIT_SEQUENCES[i]);
    look("nero pieno, rosso saturo, riga in fondo, testo dritto? [o/n]");
    waitKey();
    closeScreen();
  }
  if (cfg.exhaustive)
  {
    openScreen("custom-live (dal menu)");
    runInitVariant(cfg.seq);
    look("nero pieno, rosso saturo, riga in fondo, testo dritto? [o/n]");
    waitKey();
    closeScreen();
  }
  enterRaw();
  powerOffPanel(0xC3);
}

// ---------------------------------------------------------------------------
// w — la waveform piena scritta dall'MCU.
//
// GxEPD2_370_TC1 è l'unico driver SSD1677 di GxEPD2 che non si affida all'OTP:
// scrive lui tensioni, VCOM, il registro delle opzioni di display e una
// waveform COMPLETA a quattro LUT via 0x32, e col refresh 0xCF fa un frame in
// meno di un secondo dove l'OTP di questo pannello ne impiega ventiquattro.
//
// Qui la sua sequenza viene riprodotta con la geometria del SOLUM. La domanda
// è doppia: la waveform gira, e a che prezzo per il ROSSO. Con quattro LUT
// popolate ogni pixel viene pilotato, accent compreso, quindi la fascia rossa
// del riferimento deve sparire — e se sparisce, un refresh pieno B/N veloce è
// una strada per il driver, ma vale solo per i frame senza accent.
// ---------------------------------------------------------------------------

/**
 * lut_full di GxEPD2_370_TC1, copiata verbatim da
 * GxEPD2/src/epd/GxEPD2_370_TC1.cpp:406-419 (GPL-3.0, come questa libreria).
 * Sono i byte 0..104: le tensioni le scrive la sequenza di init.
 *
 * Decodificata col layout di §6.7: quattro LUT popolate (LUT0..LUT3), gruppo 0
 * da 15 frame e gruppo 1 da 23, nessuno ripetuto, frame rate 0x22 = 50 Hz.
 * Trentotto frame a 20 ms fanno 760 ms, più la rampa di clock e analog: la
 * durata attesa è intorno agli 843 ms.
 *
 * ATTENZIONE, e la passata stessa lo ha dimostrato: nessuna delle sue fasi
 * porta una sorgente a VSH2, e ciò nonostante questa waveform SBIADISCE LA
 * FASCIA ROSSA, di più alla seconda passata. Non selezionare il code point 11
 * evita di COMANDARE il rosso, non di disturbarlo: su un BWR nero e rosso hanno
 * la stessa carica positiva e un campo li muove insieme. Provarla non è
 * gratis, il danno all'accent si accumula, e a rimetterlo è un refresh pieno
 * dell'OTP.
 */
static const uint8_t LUT_FULL_370_TC1[105] PROGMEM =
{
  0x2A, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0
  0x05, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1
  0x2A, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2
  0x05, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x00, 0x02, 0x03, 0x0A, 0x00, 0x02, 0x06, 0x0A, 0x05, 0x00, // TP/RP gruppi 0-1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22                                // frame rate, 50 Hz
};
static_assert(sizeof(LUT_FULL_370_TC1) == 105,
              "0x32 scrive 105 byte: le cinque tensioni stanno su 0x03, 0x04 e 0x2C");

/**
 * Init di GxEPD2_370_TC1::_InitDisplay(), righe 356-404, con MUX e finestra del
 * SOLUM. Scrive quello che il driver custom lascia all'OTP: tensione di gate,
 * tensioni di sorgente, VCOM e il registro 0x37, che dichiara per ogni stadio
 * della waveform se è Mode 1 o Mode 2 e accende il RAM ping-pong.
 */
static void initLike370()
{
  resetPanel(10);
  delay(10);
  writeCommand(0x12);   // SWRESET
  delay(10);
  const uint16_t mux = 671;
  writeCommand(0x01);
  writeData((uint8_t)(mux & 0xFF)); writeData((uint8_t)((mux >> 8) & 0x03)); writeData(0x00);
  writeCommand(0x03); writeData(VGH_POR);                       // gate voltage
  writeCommand(0x04);
  writeData(VSH1_15V); writeData(VSH2_POR); writeData(VSL_POR); // source voltage
  writeCommand(0x0C);
  writeData(0xAE); writeData(0xC7); writeData(0xC3); writeData(0xC0); writeData(0xC0);
  writeCommand(0x18); writeData(0x80);
  writeCommand(0x2C); writeData(VCOM_17V);                      // -1,7 V
  writeCommand(0x37);
  writeData(0x00);
  writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF);
  writeData(0x4F);                                              // ping-pong per Mode 2
  writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF);
  ramEntryMode = 0xFF;
  setRamWindow(0, 0, SRC, GATE, 0x03);
  lutCustomLoaded = false;
  analogOn = false;
}

static void probeFullWaveform370()
{
  // Riferimento con la waveform dell'OTP: bianco, una fascia nera e una rossa.
  // Serve a due cose, la scala del nero e la sorte del rosso.
  enterRaw();
  openScreen("riferimento prima della waveform 370");
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  fillBand(0x24, 0, BAND_H, 0x00, "RIFERIMENTO OTP PRIMA DELLA 370", 0xFF);
  fillBand(0x26, (uint16_t)(BAND_H * 2), BAND_H, 0xFF);
  setRamWindow(0, 0, SRC, GATE, 0x03);
  runRefresh(0xF7, "RIFERIMENTO OTP PRIMA DELLA 370");
  look("fascia nera in alto e fascia rossa al centro, entrambe sature? [o/n]");
  waitKey();
  closeScreen();

  // La sequenza del 370, con la sua waveform completa.
  initLike370();
  WaveformSpec spec = cfg.lut;
  spec.source = WS_FULL_370;
  spec.vgh = VGH_POR; spec.vsh1 = VSH1_15V; spec.vsh2 = VSH2_POR; spec.vsl = VSL_POR;
  spec.writeVcom = true; spec.vcom = VCOM_17V;
  if (!buildWaveform(spec, LUT_FULL_370_TC1)) { powerOffPanel(0xC3); return; }
  if (!applyWaveform(0x01, true, true, true)) { powerOffPanel(0xC3); return; }
  logLine("waveform 370: %u frame, attesi ~%lu ms",
          (unsigned)waveformFrames(), (unsigned long)(waveformFrames() * 20UL + 83UL));

  openScreen("waveform 370, area a nero");
  fillBand(0x24, 0, GATE, 0xFF, "WAVEFORM 370 AREA A NERO 0X22=CF", 0x00);
  setRamWindow(0, 0, SRC, GATE, 0x03);
  partialPowerOn();
  runRefresh(0xCF, "WAVEFORM 370 AREA A NERO 0X22=CF", 10000);
  look("la scritta e' comparsa sotto il secondo, e il rosso e' sparito? [o/n]");
  waitKey();
  closeScreen();

  openScreen("waveform 370, area a bianco");
  fillBand(0x24, 0, GATE, 0xFF, "WAVEFORM 370 AREA A BIANCO 0X22=CF", 0xFF);
  setRamWindow(0, 0, SRC, GATE, 0x03);
  runRefresh(0xCF, "WAVEFORM 370 AREA A BIANCO 0X22=CF", 10000);
  const int32_t full = fullRefreshReference();
  const int32_t fast = lastMeasurement(0xCF);
  if (full > 0 && fast > 0)
    verdict("370 %ld ms contro %ld ms dell'OTP: %.0fx", (long)fast, (long)full,
            (double)full / (double)fast);
  look("il bianco e' pulito, senza fantasmi della scritta? [o/n]");
  waitKey();
  closeScreen();

  enterRaw();
  powerOffPanel(0xC3);
}

#endif // PANEL_DIAGNOSTIC_PROBES_UPSTREAM_H
