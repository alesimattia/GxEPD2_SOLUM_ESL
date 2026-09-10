// =============================================================================
// dual_panel_finder — suite di reverse engineering del SOLUM 12.2"
// (768x960 nativi, pilotato landscape 960x768, due controller SSD16xx).
//
// A COSA SERVE. Il suo prodotto è un LOG che, insieme alle schermate viste sul
// vetro, basta a correggere src/GxEPD2_SOLUM_122c_960x768.h perchè piloti il
// pannello intero. Oggi il driver stampa correttamente su METÀ pannello con la
// coda lunga, e la metà dipinta è quella adiacente alla propria COF; con la
// coda corta, nello stesso connettore, non stampa niente.
//
// LA GEOMETRIA, dal datasheet SOLUM §3.1 letto su tre taglie:
//   9.7"   672 x 960 px    672 gate    una coda
//   11.6"  640 x 960 px    640 gate    una coda
//   12.2"  768 x 960 px    768 gate    DUE code
// Solo il 12.2" sfora i 680 gate dell'SSD1677, ed è l'unico con due COF. Le due
// code escono da bordi lunghi opposti alla stessa altezza, ognuna col proprio
// COF, quindi sono due driver IC: i 960 px stanno sull'asse delle source, e
// l'asse da 768 si divide in due bande di 384 gate, una per controller.
//
// LA CASCADE È ESCLUSA, e non da un'ipotesi: SSD1683 §6.12 dà la cascade per
// "800 (sources) x 300 (gates)", cioè raddoppia le SORGENTI e lascia i gate. Una
// coppia in cascade non copre 768 gate line per nessun cablaggio, e la banda
// larga 960 px che la coda lunga dipinge lo conferma — in cascade il master
// coprirebbe metà delle sorgenti, cioè 480 px. Quello che resta è due
// controller INDIPENDENTI, ciascuno col proprio oscillatore, che condividono i
// rail di pilotaggio: il datasheet li dà per alimentabili dall'esterno
// (Features p.5, Table 5-4), ed è la spiegazione del secondo connettore nudo.
//
// UNA SOLA COMPILAZIONE PER SESSIONE. Nessun #define cambia il comportamento.
// Dal menu si scelgono: la candidata di init (tasto i, sei varianti), la coda,
// il secondo FFC, l'esaustivo, la polarita' e i dettagli (tasti c f x w v); e
// dalla voce p le temperature, i quattro valori di MUX, le gate line per
// controller, i tre clock, il timeout, la finestra del multimetro, il livello
// del soft start, l'entry mode, TB, i tre campi di 0x21 con la sua posizione,
// durata e tentativi del reset, e il border sotto LUT custom.
//
// Le combinazioni che una singola sonda spazza da se' non stanno nel menu, e
// non e' una mancanza: i quattro entry mode, i valori di MUX, le due strade
// delle tensioni della LUT, gli stadi di 0x22 e i due modi del bus li sceglie
// la sonda, perche' il confronto fra loro E' la misura. Rilanciare una sonda o
// cambiare un parametro non costa ne' un reset ne' un riflash.
//
// COME SI LEGGE IL LOG. Una riga per fatto, tag in colonna fissa, e fra
// parentesi quadre il numero del riquadro in alto a destra dell'area appena
// ridipinta:
//
//   == 9  4 bande dei piani piu' i box (1 refresh) ==
//   [12] MEAS  4 BANDE E BOX                 22=F7   19012 ms
//   [12] guarda: che colore rende ogni banda?
//   [12] SEEN  1
//   [12] VERDICT tre colori: le primitive del terzo piano restano senza corpo
//   DRV  full_refresh_time      19012 ms      <- sonda 9 [12]
//
// MEAS è una misura, SEEN la risposta dell'operatore, VERDICT una conclusione
// che una misura decide da sè, DRV una riga da cambiare nel driver. Il tasto v
// accende le righe di dettaglio, spento di default. Le motivazioni non stanno
// sul seriale ma nei commenti dei file e nel README.
//
// IL GATE. Da una schermata alla successiva si passa SOLO premendo un tasto: un
// frame costa ~19 s e quello dopo lo cancella, quindi una schermata che avanza
// da sè è una misura buttata. Dove l'esito decide una riga del driver il gate
// fa una domanda, e la risposta vale come avanzamento.
//
// COSTO. Il numero fra parentesi nel menu è quanti refresh una sonda spende.
// Le sei sonde elettriche non ne spendono nessuno, ed è da quelle che conviene
// partire sulla coda che non risponde.
//
// Hardware: Waveshare E-Paper ESP32 Driver Board V3 (CS 15, DC 27, RST 26,
// BUSY 25, SCK 13, MOSI 14, MISO 12 fittizio; secondo connettore CS 32,
// BUSY 35). Lo switch n.1 sceglie la resistenza di sense del booster.
//
// Author: Mattia Alesi
// =============================================================================

#include "Config.h"
#include "Report.h"
#include "Graphics.h"
#include "Controller.h"
#include "ProbesElettriche.h"
#include "ProbesFrame.h"
#include "ProbesWaveform.h"
#include "ProbesDriver.h"

/**
 * Ricalcola le geometrie che dipendono dal conteggio gate. Le fasce delle sonde
 * sono frazioni della banda, e se il menu cambia cfg.gate — o se la sonda
 * dell'identità del die lo cambia per una passata — devono seguirlo: costanti
 * fissate una volta al boot resterebbero indietro, e le fasce si
 * sovrapporrebbero senza che niente lo dica.
 */
static void aggiornaGeometria()
{
  AREA_BAND   = cfg.gate;
  AREA_P1_H   = AREA_BAND / 6;
  AREA_TRAP_Y = AREA_P1_H + 16;
  AREA_TRAP_H = AREA_BAND / 12;
  AREA_THIN_Y = AREA_BAND / 3;
  AREA_M1_Y   = AREA_BAND / 3 + 40;
  AREA_M1_H   = AREA_BAND / 12;
  AREA_P2_Y   = AREA_BAND / 2 + 32;
  AREA_P2_H   = AREA_BAND / 6;
  AREA_BOX_Y  = AREA_BAND * 3 / 4 + 32;
  AREA_BOX_H  = AREA_BAND / 8;
  LUTP_H       = cfg.gate / 4;
  LEVELS_SPLIT = cfg.gate / 2;
}

// ---------------------------------------------------------------------------
// TABELLA DELLE SONDE
//
// L'ordine è quello canonico, e non è cosmetico. Prima le sei sonde che non
// spendono un refresh, perchè sulla coda che non risponde sono le sole che
// dicano qualcosa. Poi le due che riguardano il PANNELLO INTERO — se i 768 px
// fossero raggiungibili da una coda sola, tutto il resto cambierebbe — e subito
// dopo quelle che decidono la specchiatura, che è il pezzo più fragile del
// driver. Poi i frame, con le sonde che nomineranno un colore dopo quella che
// misura la polarità.
//
// I prerequisiti dicono di cosa ogni sonda ha bisogno, così una sonda lanciata
// da sola dal menu li può soddisfare invece di misurare a vuoto.
// ---------------------------------------------------------------------------
static constexpr Sonda SONDE[] =
{
  { '1', S_CANDIDATE, SCH_COUNT,     "candidate di init (elettrico)",       0, P_BANCO,                       sweepInitCandidates },
  { '2', S_BIT7,      SCH_COUNT,     "bit 7 dell'opcode (elettrico)",       0, P_BANCO,                       sweepOpcodeBit7 },
  { '3', S_CLOCK,     SCH_COUNT,     "costo del push ai tre clock",         0, P_BANCO | P_INIT,              sweepPushClocks },
  { '4', S_IMPRONTA,  SCH_COUNT,     "impronta della coda, e confronto",    0, P_BANCO,                       probeImprontaCoda },
  { '5', S_TREFILI,   SCH_COUNT,     "SPI a 3 fili: BS1 flottante",         0, P_BANCO,                       probeTreFili },
  { '6', S_RESET,     SCH_COUNT,     "reset a ritentativi",                 0, P_BANCO,                       probeResetRitentativi },
  { 'b', S_SLAVE,     SCH_SLAVE,     "secondo controller a opcode|0x80",    1, P_BANCO | P_INIT,              probeSlaveByOpcodeOffset },
  { '0', S_DIE,       SCH_COUNT,     "identita' del die: MUX fuori spec",   2, P_BANCO | P_INIT,              probeIdentitaDie },
  { 'j', S_TB,        SCH_COUNT,     "TB = 1, reverse scan gate",           1, P_BANCO | P_INIT,              probeTBReverse },
  { 'e', S_ENTRY,     SCH_COUNT,     "specchiatura via entry mode 0x11",    1, P_BANCO | P_INIT,              probeEntryMode },
  { '7', S_IDENT,     SCH_IDENT,     "frame di identificazione",            3, P_BANCO | P_CANDIDATE,         probeIdentityFrames },
  { '8', S_POLARITA,  SCH_POL,       "polarita' del piano BW e 0x21",       2, P_BANCO | P_INIT,              probeBwPolarity },
  { '9', S_BANDE,     SCH_BANDE,     "4 bande dei piani piu' i box",        1, P_BANCO | P_INIT | P_POLARITA, probeBandsAndBoxes },
  { 'a', S_AREA,      SCH_AREA,      "partial d'area",                      3, P_BANCO | P_INIT | P_POLARITA, probePartialProbe },
  { 'd', S_DIFF,      SCH_DIFF_DEEP, "differenziale",                       1, P_BANCO | P_INIT | P_POLARITA, probeDifferentialDeeper },
  { 'l', S_LUT,       SCH_LUT,       "LUT via 0x32 piu' le tensioni",       2, P_BANCO | P_INIT | P_POLARITA, probePartialLut },
  { 'y', S_LIVELLI,   SCH_LIVELLI,   "quarto colore, livelli di sorgente",  2, P_BANCO | P_INIT,              probeFourthColorLevels },
  { 'q', S_SEQ22,     SCH_COUNT,     "bitmask degli stadi di 0x22",         2, P_BANCO | P_INIT | P_POLARITA, probeSeq22 },
  { 'm', S_MUX,       SCH_COUNT,     "sweep del MUX (elettrico)",           0, P_BANCO,                       sweepMuxTiming },
  { 'g', S_TEMP,      SCH_TEMP,      "banchi di waveform per temperatura",  4, P_BANCO | P_INIT | P_POLARITA, probeTemperatureBanks },
  { 'o', S_PINGPONG,  SCH_PINGPONG,  "RAM ping-pong 0x37 in Mode 2",        1, P_BANCO | P_INIT | P_POLARITA, probePingPong },
  { 'u', S_SLEEP,     SCH_SLEEP,     "deep sleep",                          3, P_BANCO | P_INIT,              probeDeepSleep },
  { 'r', S_DRIVER,    SCH_DRIVER,    "fase driver",                         1, P_BANCO,                       runDriverPhase },
};
static const uint8_t SONDE_N = sizeof(SONDE) / sizeof(SONDE[0]);

/** Tasti unici: due sonde sullo stesso tasto renderebbero la seconda
 *  irraggiungibile, e non si vedrebbe leggendo la tabella. */
static constexpr bool tastiUnici(const Sonda* p, uint8_t n)
{
  for (uint8_t i = 0; i < n; ++i)
    for (uint8_t j = (uint8_t)(i + 1); j < n; ++j)
      if (p[i].tasto == p[j].tasto) return false;
  return true;
}
static_assert(tastiUnici(SONDE, SONDE_N), "due sonde hanno lo stesso tasto");
static int refreshPrevisti()
{
  int n = 0;
  for (uint8_t i = 0; i < SONDE_N; ++i)
  {
    const Sonda& s = SONDE[i];
    if (!(cfg.sonde & s.bit)) continue;
    if ((s.prereq & P_FFC2) && !cfg.secondFfc) continue;
    n += s.refresh;
    /**
     * Le passate condizionali. Il campo `refresh` della tabella dichiara il
     * MINIMO, cioè quello che la sonda spende con l'esaustivo spento; qui si
     * aggiunge quello che l'esaustivo — che è il default del menu — rimette in
     * gioco. Le passate rimosse dal conto minimo non sono state buttate: sono
     * quelle che rimisurano ciò che il 9.7", stesso silicio, ha già chiuso.
     */
    if (cfg.esaustivo && s.bit == S_AREA)  n += 4;   // 2/3 col bordo, 4, 5, Mode 1
    if (cfg.esaustivo && s.bit == S_DIFF)  n += 4;   // 1b, 3, 3a, 4
    if (cfg.esaustivo && s.bit == S_SEQ22) n += 2;   // 0xB1 e 0x91
    if (cfg.esaustivo && s.bit == S_DIE)   n += 1;   // il valore di MUX in specifica
    if (s.bit == S_DRIVER && cfg.secondFfc) n += 1;
  }
  return n;
}

/**
 * Filtro del registro delle sonde: true se la sonda va eseguita, altrimenti lo
 * dichiara nel log e ritorna false. Passare da qui invece che da un `if` nudo
 * serve a lasciare traccia di ogni salto, così il file di log spiega da sè
 * perchè un run è più corto di un altro.
 */
static bool sondaAttiva(uint16_t bit, const char* nome)
{
  if (cfg.sonde & bit) return true;
  Serial.printf("\n-- %s: non selezionata nel menu, saltata\n", nome);
  return false;
}


/** Sottomenu della selezione delle sonde. */
static void menuSonde()
{
  while (true)
  {
    Serial.println(F("\n  sonde da eseguire:"));
    for (uint8_t i = 0; i < SONDE_N; ++i)
      Serial.printf("   %2u) [%c] %s\n", i + 1,
                    (cfg.sonde & SONDE[i].bit) ? 'x' : ' ', SONDE[i].nome);
    Serial.println(F("    t) tutte      n) nessuna      0) torna al menu"));
    Serial.println(F("  [numero per invertire una voce]"));

    char buf[8];
    if (leggiRiga(buf, sizeof(buf)) <= 0) return;
    if (buf[0] == 't' || buf[0] == 'T') { cfg.sonde = S_TUTTE; continue; }
    if (buf[0] == 'n' || buf[0] == 'N') { cfg.sonde = 0;       continue; }
    const int v = atoi(buf);
    if (v == 0) return;
    if (v >= 1 && v <= SONDE_N) cfg.sonde ^= SONDE[v - 1].bit;
  }
}

/**
 * Sottomenu dei parametri di misura. I limiti vengono dal silicio: 0x1A porta
 * 12 bit in complemento a due, cioè gradi per sedici; il MUX non può superare
 * le gate line del controller, e 0 vuol dire "non scritto, default OTP".
 */
static void menuParametri()
{
  while (true)
  {
    Serial.println(F("\n  parametri di misura:"));
    Serial.printf ("   1) temperature forzate   %d, %d, %d, %d gradi (l'ultima è il controllo)\n",
                   (int)cfg.temp[0], (int)cfg.temp[1], (int)cfg.temp[2], (int)cfg.temp[3]);
    Serial.print  (F("   2) gate line del MUX    "));
    for (uint8_t m = 0; m < MUX_PASSES; ++m)
      Serial.printf(" %s%s", muxEtichetta(m), (m < MUX_PASSES - 1) ? "," : "\n");
    Serial.printf ("   3) clock delle sonde     %lu Hz\n", (unsigned long)cfg.spiBase);
    Serial.printf ("   4) clock del frame veloce %lu Hz\n", (unsigned long)cfg.spiFast);
    Serial.printf ("   5) clock della fase driver %lu Hz\n", (unsigned long)cfg.spiDriver);
    Serial.printf ("   6) timeout di un refresh %lu ms\n", (unsigned long)cfg.timeoutMs);
    Serial.printf ("   7) finestra del multimetro %lu s\n",
                   (unsigned long)(cfg.sleepMs / 1000));
    Serial.printf ("   8) gate line per controller %u\n", (unsigned)cfg.gate);
    Serial.printf ("  10) soft start 0x0C, 5o byte  0x%02X (%s)\n", cfg.softLevel,
                   cfg.softLevel == 0x40 ? "Level 1" : "Level 2");
    Serial.printf ("  11) entry mode 0x11           0x%02X\n", cfg.entryMode);
    Serial.printf ("  12) TB, reverse scan gate     %s\n", cfg.tbReverse ? "1" : "0 (POR)");
    Serial.printf ("  13) 0x21                      A=0x%02X, %s, %s\n", cfg.reg21A,
                   cfg.reg21Due ? "due byte" : "un byte",
                   cfg.reg21Dove == 0 ? "mai" : cfg.reg21Dove == 1 ? "nell'init"
                                                                   : "prima di 0x22");
    Serial.printf ("  14) reset                     %u ms x %u tentativi\n",
                   (unsigned)cfg.resetLowMs, (unsigned)cfg.resetTentativi);
    Serial.printf ("  15) border 0x3C sotto LUT     0x%02X\n", cfg.lutBorder);
    Serial.println(F("   9) riporta i valori di fabbrica"));
    Serial.println(F("   0) torna al menu"));
    Serial.println(F("  [numero]"));

    char buf[8];
    if (leggiRiga(buf, sizeof(buf)) <= 0) return;
    switch (atoi(buf))
    {
      case 1:
        for (uint8_t t = 0; t < TEMP_PASSES; ++t)
        {
          char d[64];
          snprintf(d, sizeof(d), "    punto %u%s, gradi", t + 1,
                   (t == TEMP_PASSES - 1) ? " (controllo: mettilo fuori da 0..40)" : "");
          cfg.temp[t] = (int16_t)chiediNumero(d, -128, 127, cfg.temp[t]);
        }
        break;
      case 2:
        for (uint8_t m = 0; m < MUX_PASSES; ++m)
        {
          char d[52];
          snprintf(d, sizeof(d), "    valore %u, gate line (0 = non scritto)", m + 1);
          cfg.mux[m] = (uint16_t)chiediNumero(d, 0, MUX_REG_MAX, cfg.mux[m]);
        }
        break;
      case 3: cfg.spiBase = (uint32_t)chiediNumero("    clock delle sonde, Hz", SPI_HZ_MIN,
                                                   SPI_HZ_MAX, cfg.spiBase); break;
      case 4: cfg.spiFast = (uint32_t)chiediNumero("    clock del frame veloce, Hz", SPI_HZ_MIN,
                                                   SPI_HZ_MAX, cfg.spiFast); break;
      case 5: cfg.spiDriver = (uint32_t)chiediNumero("    clock della fase driver, Hz", SPI_HZ_MIN,
                                                   SPI_HZ_MAX, cfg.spiDriver); break;
      case 6: cfg.timeoutMs = (uint32_t)chiediNumero("    timeout di un refresh, ms", 1000,
                                                     300000, cfg.timeoutMs); break;
      case 7: cfg.sleepMs = (uint32_t)chiediNumero("    finestra del multimetro, s", 0, 600,
                                                   cfg.sleepMs / 1000) * 1000UL; break;
      case 8:
        cfg.gate = (uint16_t)chiediNumero("    gate line per controller", 8,
                                          MUX_REG_MAX, cfg.gate);
        aggiornaGeometria();
        break;
      /**
       * I parametri di REGISTRO. Stanno qui e non fra i #define per la stessa
       * ragione di tutto il resto: provarne una combinazione non deve costare
       * un riflash. Ognuno ha il proprio riferimento nel datasheet, e i limiti
       * sono quelli del registro, non preferenze.
       */
      case 10:
      {
        static const char* const VOCI_SS[] = { "0x40 Level 1, il piu' debole",
                                               "0x80 Level 2" };
        cfg.softLevel = chiediScelta("    5o byte di 0x0C: forza dell'avvio del booster",
                                     VOCI_SS, 2, cfg.softLevel == 0x40 ? 0 : 1) == 0
                        ? 0x40 : 0x80;
        break;
      }
      case 11:
        cfg.entryMode = (uint8_t)chiediNumero(
            "    0x11: ID[1:0] verso del contatore, A[2] asse", 0x00, 0x07, cfg.entryMode);
        break;
      case 12:
        cfg.tbReverse = chiediSN("    0x01 B[0] TB = 1? Reserved sul 1677, reverse scan sul 1683",
                                 cfg.tbReverse);
        break;
      case 13:
      {
        static const char* const VOCI_21A[] = { "0x00 entrambi Normal (POR)",
                                                "0x08 Red Normal + BW Inverse",
                                                "0x40 Red Bypass-as-0",
                                                "0x48 Red Bypass-as-0 + BW Inverse" };
        static const uint8_t VAL_21A[] = { 0x00, 0x08, 0x40, 0x48 };
        uint8_t sel = 0;
        for (uint8_t k = 0; k < 4; ++k) if (VAL_21A[k] == cfg.reg21A) sel = k;
        cfg.reg21A = VAL_21A[chiediScelta("    0x21 primo byte: opzioni delle due RAM",
                                          VOCI_21A, 4, sel)];
        cfg.reg21Due = chiediSN("    mandare anche il SECONDO byte? (esiste solo dal 1683)",
                                cfg.reg21Due);
        if (cfg.reg21Due)
          cfg.reg21B = (uint8_t)chiediNumero("    0x21 secondo byte: B[4] = 0x10 e' ckouten",
                                             0x00, 0xFF, cfg.reg21B);
        static const char* const VOCI_DOVE[] = { "mai", "nell'init",
                                                 "prima di ogni 0x22" };
        cfg.reg21Dove = (uint8_t)chiediScelta("    dove mandarlo", VOCI_DOVE, 3,
                                              cfg.reg21Dove);
        break;
      }
      case 14:
        cfg.resetLowMs = (uint8_t)chiediNumero("    durata dell'impulso di reset, ms",
                                               1, 200, cfg.resetLowMs);
        cfg.resetTentativi = (uint8_t)chiediNumero(
            "    tentativi a durata crescente", 1, 10, cfg.resetTentativi);
        break;
      case 15:
        cfg.lutBorder = (uint8_t)chiediNumero(
            "    0x3C sotto LUT custom: 0xC0 HiZ (POR), 0x01 LUT1, 0x80 VCOM",
            0x00, 0xFF, cfg.lutBorder);
        break;
      case 9:
        memcpy(cfg.temp, CFG_DEFAULT.temp, sizeof(cfg.temp));
        memcpy(cfg.mux,  CFG_DEFAULT.mux,  sizeof(cfg.mux));
        cfg.spiBase   = CFG_DEFAULT.spiBase;
        cfg.spiFast   = CFG_DEFAULT.spiFast;
        cfg.spiDriver = CFG_DEFAULT.spiDriver;
        cfg.timeoutMs = CFG_DEFAULT.timeoutMs;
        cfg.sleepMs   = CFG_DEFAULT.sleepMs;
        cfg.gate      = CFG_DEFAULT.gate;
        cfg.softLevel = CFG_DEFAULT.softLevel;
        cfg.entryMode = CFG_DEFAULT.entryMode;
        cfg.tbReverse = CFG_DEFAULT.tbReverse;
        cfg.reg21A    = CFG_DEFAULT.reg21A;
        cfg.reg21Due  = CFG_DEFAULT.reg21Due;
        cfg.reg21B    = CFG_DEFAULT.reg21B;
        cfg.reg21Dove = CFG_DEFAULT.reg21Dove;
        cfg.resetLowMs     = CFG_DEFAULT.resetLowMs;
        cfg.resetTentativi = CFG_DEFAULT.resetTentativi;
        cfg.lutBorder = CFG_DEFAULT.lutBorder;
        aggiornaGeometria();
        Serial.println(F("  parametri riportati ai valori di fabbrica"));
        break;
      case 0: return;
      default: break;
    }
  }
}

/**
 * Sottomenu delle caratteristiche note. Senza NVS gli esiti durano quanto la
 * sessione, e questa è la via per ridichiarare quello che una sessione
 * precedente ha già determinato: una caratteristica dichiarata vale come
 * misurata ai fini dei prerequisiti, ed è marcata come dichiarata dappertutto,
 * perchè un esito detto a memoria non è un esito misurato.
 */
static void menuCaratteristiche()
{
  while (true)
  {
    Serial.println(F("\n  caratteristiche note di questa sessione:"));
    for (uint8_t c = 0; c < C_COUNT; ++c)
    {
      const Esito& e = esiti[c];
      if (e.risposta < 0)
        Serial.printf("   %u) %-34s da determinare\n", c + 1, NOME_CARATTERISTICA[c]);
      else
        Serial.printf("   %u) %-34s %s%s\n", c + 1, NOME_CARATTERISTICA[c],
                      VOCI_DI[c][e.risposta].risposta,
                      e.dichiarata ? "   (dichiarata)" : "   (misurata)");
    }
    Serial.println(F("   0) torna al menu"));
    Serial.println(F("  [numero per dichiarare una caratteristica]"));

    char buf[8];
    if (leggiRiga(buf, sizeof(buf)) <= 0) return;
    const int v = atoi(buf);
    if (v == 0) return;
    if (v < 1 || v > C_COUNT) continue;

    const uint8_t c = (uint8_t)(v - 1);
    const char* voci[8];
    for (uint8_t i = 0; i < QUANTE_DI[c]; ++i) voci[i] = VOCI_DI[c][i].risposta;
    const int scelta = chiediScelta("    cosa hai già determinato?", voci, QUANTE_DI[c],
                                    esiti[c].risposta >= 0 ? esiti[c].risposta : 0);
    registraEsito(c, (int8_t)scelta, 0, true);
    // La polarità ha un secondo posto in cui vive, perchè la leggono le
    // primitive di scrittura dei piani: bwByteFor() e bwPatternFor().
    if (c == C_POLARITA)
      cfg.bwPolarity = (scelta == 0) ? BW_DATASHEET : BW_INVERSE;
  }
}
// ---------------------------------------------------------------------------
// ESECUZIONE DELLE SONDE E MENU PRINCIPALE
//
// Il menu non è al boot e basta: ci si torna dopo ogni sonda, quindi una
// sessione di bring-up è una compilazione e un boot. Rilanciare una sonda,
// cambiare un parametro o dichiarare la coda appena infilata non costano nè un
// reset nè un riflash, ed è quello che riduce le esecuzioni al minimo.
// ---------------------------------------------------------------------------

static bool bancoPronto     = false;   // preparaBanco() già fatto in questa sessione
static bool candidateFatto  = false;   // sweepInitCandidates() già eseguito

/** Init con la candidata del driver, il caso normale delle sonde a frame. */
static void initPanelDriver()
{
  initPanel(CAND_DRIVER, cfg.gate);
}

/**
 * Soddisfa i prerequisiti di una sonda, e ritorna false se non è il caso di
 * eseguirla. Quelli elettrici si risolvono da sè, perchè non costano refresh;
 * quelli che costano refresh si chiedono, perchè eseguirli di nascosto vorrebbe
 * dire spendere venti secondi per volta senza averlo detto.
 */
static bool prerequisitiOk(const Sonda& s)
{
  if ((s.prereq & P_FFC2) && !cfg.secondFfc)
  {
    Serial.println(F("   serve il secondo connettore FFC cablato, e non lo è: con un solo"));
    Serial.println(F("   connettore lo slave non è sul bus e un frame non direbbe niente."));
    Serial.println(F("   Dichiaralo dal menu (voce c) se invece lo hai cablato."));
    return false;
  }
  if ((s.prereq & P_BANCO) && !bancoPronto)
  {
    Serial.println(F("   prerequisito: preparo il banco (pin, POR, BUSY a riposo)"));
    preparaBanco();
    bancoPronto = true;
  }
  if ((s.prereq & P_CANDIDATE) && !candidateFatto)
  {
    Serial.println(F("   prerequisito: sweep delle candidate di init, elettrico e senza refresh"));
    sweepInitCandidates();
    candidateFatto = true;
  }
  if (s.prereq & P_INIT)
    initPanelDriver();
  if ((s.prereq & P_POLARITA) && esiti[C_POLARITA].risposta < 0)
  {
    Serial.println(F("   la polarità del piano BW non è determinata: senza di lei ogni"));
    Serial.println(F("   etichetta che nomina un colore è un'ipotesi."));
    if (chiediSN("   eseguo prima la sonda della polarità (2 refresh)?", true))
    {
      iniziaSonda(SCH_POL);
      probeBwPolarity();
      initPanelDriver();
    }
    else
      Serial.println(F("   proseguo assumendo bit = 1 bianco, come fa il driver"));
  }
  return true;
}

/** Esegue una sonda: prerequisiti, attribuzione dei frame, corpo. */
static void eseguiSonda(const Sonda& s)
{
  logProbeHeader(s.tasto, s.nome, s.refresh);
  sondaAttivaTasto = s.tasto;
  /**
   * Il clock delle sonde si rilegge qui a ogni esecuzione, perchè il menu è
   * persistente e fra due sonde può averlo cambiato: applicarlo una volta sola
   * al boot lo lascerebbe indietro rispetto a cfg.
   */
  spiSettings = SPISettings(cfg.spiBase, MSBFIRST, SPI_MODE0);
  if (!prerequisitiOk(s)) return;
  iniziaSonda(s.sonda);
  s.esegui();
  iniziaSonda(SCH_COUNT);
  sondaAttivaTasto = '-';
}

/**
 * Tutte le sonde selezionate, nell'ordine della tabella: è il run completo, e
 * l'ordine è quello in cui le domande si escludono a vicenda — prima quello che
 * l'OTP offre da sè, poi i suoi altri banchi, e solo alla fine una waveform
 * scritta dall'MCU.
 */
static void eseguiTutte()
{
  Serial.println(F("\n=================================================="));
  Serial.println(F(" FASE PROBE - SPI diretta, nessuno strato software"));
  Serial.print  (F(" coda      : ")); Serial.println(tailLabel());
  Serial.printf (" pattern   : %u source x %u gate\n", SRC, cfg.gate);
  Serial.printf (" durata    : ~%d refresh da ~19 s, più il tempo che ci metti a\n"
                 "             guardare ogni schermata\n", refreshPrevisti());
  Serial.println(F("=================================================="));

  for (uint8_t i = 0; i < SONDE_N; ++i)
  {
    if (!sondaAttiva(SONDE[i].bit, SONDE[i].nome)) continue;
    eseguiSonda(SONDE[i]);
  }
  printRiepilogo();
}

/**
 * C_PARTIAL non la chiude una domanda sola: è la conclusione su una decina di
 * passate. La deriva dal registro, incrociando la risposta "l'area è cambiata"
 * con la durata misurata contro il refresh pieno — perchè una passata che
 * dipinge in venti secondi non è un partial, e una breve che non dipinge è un
 * refresh ingoiato.
 */
static void derivaPartial()
{
  bool qualcunaRisposta = false;
  for (uint8_t i = 0; i < registroFrameN; ++i)
  {
    const FrameOsservato& f = registroFrame[i];
    if (f.domanda != D_DIPINTO || f.risposta < 0) continue;
    qualcunaRisposta = true;
    if (f.risposta == 0 && f.ms > 0 && refreshMs > 0 && f.ms < refreshMs / 2)
    {
      registraEsito(C_PARTIAL, 1, f.numero, false);
      return;
    }
  }
  if (qualcunaRisposta && esiti[C_PARTIAL].risposta < 0)
    registraEsito(C_PARTIAL, 0, 0, false);
}

/** Riepilogo delle caratteristiche determinate e di quelle ancora aperte. */
static void statoCaratteristiche()
{
  derivaPartial();

  Serial.println(F("\ncaratteristiche del pannello, stato di questa sessione:"));
  uint8_t aperte = 0;
  for (uint8_t c = 0; c < C_COUNT; ++c)
  {
    const Esito& e = esiti[c];
    if (e.risposta < 0)
    {
      Serial.printf("  %-34s da determinare\n", NOME_CARATTERISTICA[c]);
      ++aperte;
      continue;
    }
    if (e.frame)
      Serial.printf("  %-34s %s   [schermata %u]\n", NOME_CARATTERISTICA[c],
                    VOCI_DI[c][e.risposta].risposta, (unsigned)e.frame);
    else
      Serial.printf("  %-34s %s   (dichiarata)\n", NOME_CARATTERISTICA[c],
                    VOCI_DI[c][e.risposta].risposta);
    Serial.printf("  %-34s -> %s\n", "", VOCI_DI[c][e.risposta].conseguenza);
  }
  if (aperte)
    Serial.printf("  restano %u caratteristiche aperte: le sonde che le chiudono stanno\n"
                  "  nel menu, e si lanciano una alla volta\n", (unsigned)aperte);
  else
    Serial.println(F("  tutte determinate: il driver si può correggere per intero"));
}


// ---------------------------------------------------------------------------
// LA SCHEDA PER IL DRIVER
//
// Ogni riga è DRV <voce> <valore> <- sonda <tasto> [<schermata>], e punta a un
// metodo o a una costante di src/GxEPD2_SOLUM_122c_960x768.h. È il prodotto
// della suite: sostituisce la scheda di osservazione in prosa, perchè una
// misura che non arriva a nominare la riga da cambiare non ha chiuso niente.
// ---------------------------------------------------------------------------

/** Prima misura la cui didascalia comincia col prefisso, nullptr se manca. */
static const Misura* trovaMisura(const char* prefisso)
{
  const uint8_t n = (uint8_t)strlen(prefisso);
  for (uint8_t i = 0; i < misureN; ++i)
    if (misure[i].caption && strncmp(misure[i].caption, prefisso, n) == 0)
      return &misure[i];
  return nullptr;
}

/** Una voce della scheda, con la sonda e la schermata da cui viene. */
static void drvLine(const char* voce, const char* valore, const Misura* m)
{
  Serial.printf("DRV  %-28s %-34s", voce, valore);
  if (m) Serial.printf(" <- sonda %c", m->sonda);
  if (m && m->frame) Serial.printf(" [%u]", (unsigned)m->frame);
  Serial.println();
}

static void printSchedaDriver()
{
  Serial.println(F("\n-- per il driver --"));
  char v[64];

  // ---- tarature dirette ----
  if (powerOnMs >= 0)  { snprintf(v, sizeof(v), "%ld ms, arrotondato per eccesso", (long)powerOnMs);
                         drvLine("power_on_time", v, trovaMisura("POWER ON")); }
  if (powerOffMs >= 0) { snprintf(v, sizeof(v), "%ld ms, arrotondato per eccesso", (long)powerOffMs);
                         drvLine("power_off_time", v, trovaMisura("POWER OFF")); }
  if (refreshMs > 0)
  {
    snprintf(v, sizeof(v), "%ld ms misurati a ambiente", (long)refreshMs);
    drvLine("full_refresh_time", v, nullptr);
  }

  /**
   * Il tempo al freddo è la taratura più a rischio del driver. Il produttore
   * dichiara l'esercizio a 0..40 °C, quindi lo zero è il pavimento DI
   * SPECIFICA; e allo zero il 9.7", stesso silicio, ha misurato 59067 ms. Il
   * driver oggi porta busy_timeout a 40 s e full_refresh_time a 30000: sotto il
   * caso peggiore in specifica, cioè un frame troncato al freddo.
   */
  int32_t peggiore = refreshMs;
  for (uint8_t t = 0; t < TEMP_PASSES; ++t)
    if (tempSweepMs[t] > peggiore) peggiore = tempSweepMs[t];
  if (peggiore > 0)
  {
    snprintf(v, sizeof(v), "> %ld ms, il peggiore misurato x2", (long)(peggiore * 2));
    drvLine("busy_timeout", v, nullptr);
    if (peggiore > 30000)
      drvLine("full_refresh_time", "il valore attuale 30000 e' sotto il misurato", nullptr);
  }

  // ---- geometria ----
  {
    snprintf(v, sizeof(v), "%u gate per controller", (unsigned)cfg.gate);
    drvLine("PART_HEIGHT", v, nullptr);
    if (cfg.mux[MUX_PASSES - 2] > MUX_SPEC_MAX)
      drvLine("famiglia del controller", "vedi il righello della sonda 0", nullptr);
  }

  // ---- riempimento dei piani ----
  if (patternMs47 > 0)
  {
    snprintf(v, sizeof(v), "0x47 in %ld ms contro un push di 46080 byte", (long)patternMs47);
    drvLine("_writeScreenBuffer per pattern", v, trovaMisura("PATTERN 0x47"));
  }

  // ---- indirizzamento e specchiatura ----
  drvLine("ADDRESSING_CASCADE",
          "la cascade estende le SORGENTI, non i gate", nullptr);
  if (slaveOpcodeMs >= 0)
  {
    snprintf(v, sizeof(v), "opcode|0x80 eseguiti, %ld ms", (long)slaveOpcodeMs);
    drvLine("bit 7 dell'opcode", v, nullptr);
  }
  for (uint8_t c = 0; c < C_COUNT; ++c)
    if (esiti[c].risposta >= 0)
      drvLine(NOME_CARATTERISTICA[c], VOCI_DI[c][esiti[c].risposta].conseguenza, nullptr);

  // ---- partial ----
  if (areaMsFirst > 0 && refreshMs > 0)
  {
    snprintf(v, sizeof(v), "%ld ms su finestra contro %ld pieni",
             (long)areaMsFirst, (long)refreshMs);
    drvLine(areaMsFirst * 2 >= refreshMs ? "hasFastPartialUpdate = false"
                                         : "hasFastPartialUpdate da rivedere", v, nullptr);
  }
  for (uint8_t k = 0; k < 2; ++k)
    if (lutPartialMs[k] > 0)
    {
      snprintf(v, sizeof(v), "variante %u in %ld ms", k + 1, (long)lutPartialMs[k]);
      drvLine("setPartialLut", v, nullptr);
    }

  // ---- deep sleep ----
  if (sleepParamOk)
  {
    snprintf(v, sizeof(v), "0x10 = 0x%02X%s", sleepParamOk,
             sleepIgnoresCmd ? ", dorme davvero" : ", esegue ancora");
    drvLine("hibernate", v, nullptr);
    if ((sleepParamOk & 0x03) == 0x01)
      drvLine("famiglia del controller",
              "0x10 A[1:0]=01 esiste solo dal SSD1683", nullptr);
  }
  drvLine("_initial_write al risveglio",
          "necessario: il deep sleep non ritiene la RAM", nullptr);

  // ---- bus ----
  {
    snprintf(v, sizeof(v), "%lu Hz usati, %lu il tetto del datasheet",
             (unsigned long)cfg.spiDriver, (unsigned long)SPI_HZ_MAX);
    drvLine("clock SPI", v, nullptr);
  }
}

/** Il confronto fra le due code: la diagnosi non sta in un valore ma qui. */
static void printImpronte()
{
  if (!impronte[TAIL_LUNGA].presa && !impronte[TAIL_CORTA].presa) return;
  Serial.println(F("\n-- impronta delle due code --"));
  Serial.println(F("                         LUNGA      CORTA"));
  const ImprontaCoda& a = impronte[TAIL_LUNGA];
  const ImprontaCoda& b = impronte[TAIL_CORTA];
  #define RIGA(et, campo, fmt) \
    Serial.printf("  %-22s " fmt "  " fmt "\n", et, \
                  a.presa ? a.campo : -1, b.presa ? b.campo : -1)
  RIGA("BUSY a riposo",        busyRiposo,     "%9d");
  RIGA("BUSY col pull-down",   busyPullDown,   "%9d");
  RIGA("SWRESET, ms",          swresetMs,      "%9ld");
  RIGA("pattern 0x47, ms",     pattern47Ms,    "%9ld");
  RIGA("pattern 0x46, ms",     pattern46Ms,    "%9ld");
  RIGA("power on 0xC0, ms",    powerOnMs,      "%9ld");
  RIGA("HV Ready 0x14, ms",    hvMs,           "%9ld");
  RIGA("VCI 0x15, ms",         vciMs,          "%9ld");
  RIGA("CRC 0x34, ms",         crcMs,          "%9ld");
  #undef RIGA
  Serial.println(F("  -> BUSY fermo su tutto = oscillatore assente; risponde ai comandi"));
  Serial.println(F("     ma HV al massimo = rail di pilotaggio mancanti"));
}

static void printRiepilogo()
{
  printMisure();
  printIndiceSchermate();
  printImpronte();
  statoCaratteristiche();
  printSchedaDriver();
}

// ---------------------------------------------------------------------------
// MENU
//
// loop() legge UN CARATTERE e non una riga: il menu è il ciclo principale e ci
// si torna dopo ogni sonda, quindi lanciare una sonda deve costare un tasto. I
// sottomenu e le domande del gate continuano invece a leggere una riga, perchè
// scelgono fra più di dieci voci, e il menu lo dice.
// ---------------------------------------------------------------------------

static void printMenu()
{
  Serial.println(F("\n-- sonde, fra parentesi i refresh da ~19 s --"));
  for (uint8_t i = 0; i < SONDE_N; ++i)
  {
    const Sonda& s = SONDE[i];
    Serial.printf(" %c%c %-34s (%u)", s.tasto,
                  (cfg.sonde & s.bit) ? ' ' : '-', s.nome, (unsigned)s.refresh);
    if ((i % 2) == 1 || i == SONDE_N - 1) Serial.println();
  }
  Serial.printf("-- sessione:  t tutte (%u di %u, %d refresh)   s riepilogo   k caratteristiche\n",
                (unsigned)__builtin_popcount(cfg.sonde), (unsigned)SONDE_N, refreshPrevisti());
  Serial.printf("   n selezione   p parametri   c coda=%s   f FFC2=%s   x esaustivo=%s\n",
                cfg.coda == TAIL_LUNGA ? "LUNGA" : "CORTA",
                cfg.secondFfc ? "si" : "no", cfg.esaustivo ? "si" : "no");
  Serial.printf("   w polarita'=%s   v dettagli=%s   i re-init   z fabbrica   h menu\n",
                polaritaLabel(), cfg.verbose ? "si" : "no");
  Serial.println(F("   [un tasto per le sonde; i sottomenu leggono una riga, chiusa da INVIO]"));
}

static void handleKey(char c)
{
  for (uint8_t i = 0; i < SONDE_N; ++i)
    if (SONDE[i].tasto == c) { eseguiSonda(SONDE[i]); printMenu(); return; }

  switch (c)
  {
    case 't': eseguiTutte(); break;
    case 's': printRiepilogo(); break;
    case 'k': menuCaratteristiche(); break;
    case 'n': menuSonde(); break;
    case 'p': menuParametri(); break;
    case 'c':
    {
      static const char* const VOCI_CODA[] = { "coda LUNGA", "coda CORTA" };
      cfg.coda = (chiediScelta("  quale coda è infilata nel connettore?", VOCI_CODA, 2,
                               cfg.coda == TAIL_LUNGA ? 0 : 1) == 0)
                 ? TAIL_LUNGA : TAIL_CORTA;
      /**
       * Cambiata la coda, quello che si sapeva del pannello vale ancora ma
       * quello che si sapeva della METÀ pilotata no: dipende da quale coda è
       * infilata. Le altre caratteristiche restano.
       */
      if (esiti[C_META].risposta >= 0)
      {
        esiti[C_META].risposta = -1;
        Serial.println(F("  la metà pilotata torna da determinare: dipende dalla coda"));
      }
      bancoPronto = false;
      break;
    }
    case 'f':
      cfg.secondFfc = chiediSN("  il secondo connettore FFC è cablato?", cfg.secondFfc);
      bancoPronto = false;
      break;
    case 'x':
      cfg.esaustivo = chiediSN("  eseguire anche le passate condizionali?", cfg.esaustivo);
      break;
    case 'w':
    {
      static const char* const VOCI_POL[] = { "da misurare", "bit=1 bianco (datasheet)",
                                              "bit=1 nero (inversa)" };
      const int scelta = chiediScelta("  polarità del piano BW", VOCI_POL, 3,
                                      cfg.bwPolarity + 1);
      cfg.bwPolarity = (int8_t)(scelta - 1);
      if (scelta == 0) esiti[C_POLARITA].risposta = -1;
      else             registraEsito(C_POLARITA, (int8_t)(scelta - 1), 0, true);
      break;
    }
    case 'v':
      cfg.verbose = !cfg.verbose;
      Serial.printf("  dettagli: %s\n", cfg.verbose ? "accesi" : "spenti");
      break;
    case 'i':
    {
      const char* voci[CAND_COUNT];
      for (uint8_t k = 0; k < CAND_COUNT; ++k) voci[k] = CAND_LABEL[k];
      const int scelta = chiediScelta("  quale candidata di init?", voci, CAND_COUNT,
                                      CAND_DRIVER);
      if (!bancoPronto) { preparaBanco(); bancoPronto = true; }
      initPanel((uint8_t)scelta,
                (scelta == CAND_MINIMAL) ? MUX_NOT_WRITTEN : cfg.gate);
      break;
    }
    case 'z':
      cfg = CFG_DEFAULT;
      aggiornaGeometria();
      Serial.println(F("  configurazione riportata ai valori di fabbrica"));
      break;
    case 'h': break;
    default:  Serial.printf("\ntasto '%c' sconosciuto\n", c); break;
  }
  printMenu();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  aggiornaGeometria();
  spiSettings = SPISettings(cfg.spiBase, MSBFIRST, SPI_MODE0);

  Serial.println(F("\n=== dual_panel_finder: SOLUM 12.2 pollici, due controller, SPI diretta ==="));
  Serial.printf("pannello %u x %u, banda in prova %u gate, clock %lu Hz, timeout %lu ms\n",
                (unsigned)SRC, 768u, (unsigned)cfg.gate,
                (unsigned long)cfg.spiBase, (unsigned long)cfg.timeoutMs);
  Serial.println(F("il numero fra parentesi quadre e' il riquadro in alto a destra"
                   " dell'area ridipinta"));

  /**
   * Il gancio va installato una volta sola e resta: la fase driver costruisce
   * l'oggetto alla prima chiamata, e da lì in poi ogni suo refresh passa dal
   * gate come i frame a SPI diretta.
   */
  driverBegin();
  printMenu();
}

void loop()
{
  if (!Serial.available()) { delay(20); return; }
  const char c = (char)Serial.read();
  if (c == '\r' || c == '\n' || c == ' ') return;
  scartaInputPendente();
  handleKey(c);
}
