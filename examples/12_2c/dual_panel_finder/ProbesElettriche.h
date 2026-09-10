// =============================================================================
// ProbesElettriche.h — le sonde che non spendono un refresh
//
// Ogni sonda finisce in una riga che nomina il metodo di
// src/GxEPD2_SOLUM_122c_960x768.h da cambiare: il log e quello che si vede sul
// vetro devono bastare a correggere il driver, e una sonda che non produce una
// riga di quel tipo sta spendendo refresh senza rispondere a niente.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_PROBES_ELETTRICHE_H
#define DUAL_PANEL_FINDER_PROBES_ELETTRICHE_H

#include <Arduino.h>
#include "Config.h"
#include "Report.h"
#include "Graphics.h"
#include "Controller.h"
static void probeLifeAndHighVoltage(bool readRegisters)
{
  Serial.println(F("  prova di vita e alte tensioni:"));

  patternMs47 = patternFill(0x47, 0xF7, "B/N   ");
  patternMs46 = patternFill(0x46, 0xF7, "accent");

  writeCommand(0x22);
  writeData(0xC0);        // solo power on: abilita clock e blocco analogico
  writeCommand(0x20);
  powerOnMs = waitBusy(5000);
  if (powerOnMs < 0)
    Serial.println(F("  power on 0xC0       BUSY ancora alto a 5000 ms"));
  else
    logLine("power on 0xC0       BUSY alto per %ld ms", (long)powerOnMs);

  writeCommand(0x14);
  writeData(0x77);        // cool down 80 ms x 7 cicli -> massimo 560 ms
  hvDetectMs = waitBusy(3000);
  if (hvDetectMs < 0)
    Serial.println(F("  HV detect 0x14      BUSY ancora alto a 3000 ms: HV non pronta"));
  else
    Serial.printf("  HV detect 0x14      BUSY alto per %ld ms (massimo programmato 560)\n",
                  (long)hvDetectMs);

  writeCommand(0x15);
  writeData(0x04);        // livello POR, 2.3 V
  vciDetectMs = waitBusy(3000);
  if (vciDetectMs < 0)
    Serial.println(F("  VCI detect 0x15     BUSY ancora alto a 3000 ms"));
  else
    logLine("VCI detect 0x15     BUSY alto per %ld ms", (long)vciDetectMs);

  /** La lettura si fa una volta sola, e con la detection ancora fresca: i bit
   *  5 e 4 dello status sono l'esito esplicito di 0x14 e 0x15. Ripeterla per
   *  ogni candidata riempirebbe il report di tre volte lo stesso "nessuna
   *  linea dati". */
  if (readRegisters)
  {
    Serial.println(F("  registri in lettura:"));
    reportRegisters();
  }

  writeCommand(0x22);
  writeData(0xC3);        // solo power off
  writeCommand(0x20);
  powerOffMs = waitBusy(5000);
  if (powerOffMs < 0)
    Serial.println(F("  power off 0xC3      BUSY ancora alto a 5000 ms"));
  else
    logLine("power off 0xC3      BUSY alto per %ld ms", (long)powerOffMs);
}
// =====================================================================
// SWEEP AUTONOMI
//
// Sono le prove che prima erano parametri di compilazione. Le prime tre non
// toccano un pixel e non costano un refresh: misurano il BUSY, che è l'unico
// canale di ritorno che questo pannello ha (la linea dati in lettura non è
// cablata sul connettore). Solo lo sweep del MUX consuma refresh, e nemmeno
// quello chiede di guardare lo schermo.
// =====================================================================

/**
 * Esito elettrico di una candidata di init: tutto quello che si può sapere di
 * lei senza accendere un pixel.
 */
struct CandProbe
{
  int32_t swresetMs;    // BUSY dopo lo SWRESET
  int32_t pattern47Ms;  // BUSY del pattern hardware sul piano B/N
  int32_t pattern46Ms;  // BUSY del pattern hardware sull'accent
  int32_t powerOnMs;
  int32_t powerOffMs;
  int32_t hvMs;
  int32_t vciMs;
  bool    vivo;         // il controller ha eseguito almeno un comando lungo
};

static CandProbe candProbe[CAND_COUNT];

/**
 * Sweep delle sei candidate di init, tutto elettrico: nessun refresh, nessuna
 * pausa, una decina di secondi in tutto.
 *
 * Per ognuna: reset hardware, la sua sequenza di init, e le prove che non
 * chiedono un pixel — BUSY dello SWRESET, i due pattern hardware 0x47/0x46,
 * power on e power off, i due rilevatori di alte tensioni.
 *
 * A cosa serve, oltre che a confrontarle: una candidata che non alza il BUSY sui
 * pattern hardware non ha fatto arrivare i suoi comandi al controller, e
 * portarla a video sarebbe un refresh e una pausa buttati. I frame di
 * identificazione poi si fanno solo per le candidate che qui hanno risposto.
 *
 * Cosa decide nel driver: quale sequenza tenere in _InitDisplay(). Se
 * rispondono tutte, vince la più corta a parità di risultato a video, perchè
 * sono meno registri su cui i due controller possono divergere.
 */
static void sweepInitCandidates()
{
  for (uint8_t c = 0; c < CAND_COUNT; ++c)
  {
    Serial.printf("\n-- candidata %u/%u: %s\n", c + 1, CAND_COUNT, CAND_LABEL[c]);
    // MUX al conteggio reale per tutte, tranne MINIMAL che di suo non lo scrive
    const int32_t sw = initPanel(c, (c == CAND_MINIMAL) ? MUX_NOT_WRITTEN : cfg.gate);
    probeLifeAndHighVoltage(c == CAND_COUNT - 1);

    CandProbe& p = candProbe[c];
    p.swresetMs   = sw;
    p.pattern47Ms = patternMs47;
    p.pattern46Ms = patternMs46;
    p.powerOnMs   = powerOnMs;
    p.powerOffMs  = powerOffMs;
    p.hvMs        = hvDetectMs;
    p.vciMs       = vciDetectMs;
    /**
     * "Vivo" non vuol dire che stampa: vuol dire che ha eseguito almeno un
     * comando la cui esecuzione dura un tempo misurabile. Il power on è il
     * criterio più solido dei due pattern, perchè accende il blocco analogico e
     * dura decine di ms; i pattern hardware sono la conferma che le due RAM
     * esistono.
     */
    p.vivo = (p.powerOnMs > 0) || (p.pattern47Ms > 0);
    Serial.printf("  verdetto elettrico: %s\n",
                  p.vivo ? "il controller esegue" : "NESSUNA reazione, comandi non arrivati");
  }
}

/**
 * Il bit 7 dell'opcode indirizza un secondo controller, o non significa niente?
 *
 * Con un solo FFC cablato lo slave non è sul bus, perchè le sue linee SPI
 * passano dal suo connettore: un frame non potrebbe rispondere. Qui si misura
 * quello che il BUSY dice da solo, ed è la cosa che serve al driver — se il
 * controller CHE RISPONDE esegue gli opcode con il bit 7 alto. Si prendono i
 * comandi che alzano il BUSY per una durata misurabile, si manda prima
 * l'opcode nudo e poi lo stesso con |0x80, e si confrontano le due reazioni.
 *
 *   entrambi alzano il BUSY -> questo controller esegue anche l'opcode offset,
 *        quindi il bit 7 non seleziona niente su questo silicio e l'ipotesi
 *        cascade perde il proprio meccanismo di indirizzamento: in
 *        GxEPD2_SOLUM_122c_960x768.h ADDRESSING_CASCADE non avrebbe modo di
 *        funzionare, e il default a due chip select resta l'unica strada
 *   solo l'opcode nudo -> il controller RIFIUTA gli opcode offset: sono uno
 *        spazio di indirizzamento distinto, ed è la prima evidenza a favore
 *        della cascade
 *
 * I due gruppi sono separati da un reset: se un opcode offset viene ignorato il
 * suo parametro finisce in un registro che non abbiamo scelto, e non deve poter
 * falsare le misure del gruppo nudo.
 */
static void sweepOpcodeBit7()
{
  struct Op { uint8_t code; uint8_t param; bool hasParam; const char* nome; };
  static const Op OPS[] = {
    { 0x47, 0xF7, true,  "pattern B/N   0x47" },
    { 0x46, 0xF7, true,  "pattern accent 0x46" },
    { 0x15, 0x04, true,  "VCI detect    0x15" },
    { 0x14, 0x77, true,  "HV detect     0x14" },
    { 0x12, 0x00, false, "SWRESET       0x12" },
  };
  const uint8_t N = sizeof(OPS) / sizeof(OPS[0]);
  int32_t nudo[5], offset[5];

  for (uint8_t pass = 0; pass < 2; ++pass)
  {
    const uint8_t off = pass ? 0x80 : 0x00;
    Serial.printf("\n-- gruppo %s\n", pass ? "OFFSET (opcode | 0x80)" : "NUDO (opcode come da datasheet)");
    initPanel(CAND_DRIVER, cfg.gate);
    // power on: 0x14 e 0x15 vogliono clock e blocco analogico accesi
    writeCommand(0x22 | off);
    writeData(0xC0);
    writeCommand(0x20 | off);
    waitBusy(5000);

    for (uint8_t k = 0; k < N; ++k)
    {
      writeCommand(uint8_t(OPS[k].code | off));
      if (OPS[k].hasParam) writeData(OPS[k].param);
      const uint32_t t0 = millis();
      bool rose = false;
      while ((millis() - t0) < 300)
      {
        if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { rose = true; break; }
      }
      const int32_t ms = rose ? waitBusy(3000) : -1;
      (pass ? offset : nudo)[k] = ms;
      Serial.printf("   0x%02X %-22s %s\n", uint8_t(OPS[k].code | off), OPS[k].nome,
                    rose ? "BUSY mosso" : "BUSY fermo");
      if (rose) Serial.printf("        alto per %ld ms\n", (long)ms);
    }
  }

  // Verdetto: quanti opcode offset hanno prodotto la stessa reazione del nudo
  uint8_t eseguiti = 0, confrontabili = 0;
  for (uint8_t k = 0; k < N; ++k)
  {
    if (nudo[k] < 0) continue;      // il nudo non reagisce: il confronto non dice niente
    ++confrontabili;
    if (offset[k] >= 0) ++eseguiti;
  }
  Serial.printf("\nesito: %u opcode su %u confrontabili hanno reagito anche con il bit 7 alto\n",
                eseguiti, confrontabili);
  if (confrontabili == 0)
    logLine("nessun opcode nudo ha reagito: la sonda non decide, il controller non sta eseguendo niente e il problema è a monte");
  else if (eseguiti == confrontabili)
    verdict("il bit 7 NON indirizza: gli opcode offset vengono eseguiti come nudi, e ADDRESSING_CASCADE non ha un meccanismo");
  else if (eseguiti == 0)
    verdict("il bit 7 e' uno spazio di indirizzamento distinto: e' il meccanismo della cascade, che pero' su questo pannello estende le sorgenti e non i gate");
  else
    logLine("esito misto: alcuni opcode offset passano e altri no. Va guardato opcode per opcode nella tabella qui sopra prima di concludere");
}

/**
 * Il MUX: che valori il controller ACCETTA, senza spendere un refresh.
 *
 * Spendeva tre refresh per cronometrare la durata a MUX diversi, e quella
 * domanda è chiusa — sul 9.7", stesso silicio, la durata NON scala: 24010,
 * 24031 e 24033 ms a MUX 671, 335 e 167. Il periodo di frame lo fissa il frame
 * rate e la scansione non lo satura, quindi rimisurarlo qui costerebbe un
 * minuto per riconfermare un fatto.
 *
 * Quello che resta da sapere sul MUX è SPAZIALE — fino a dove il controller
 * scandisce davvero — e lo legge la sonda dell'identità del die, che al valore
 * fuori specifica associa un righello. Qui si verifica solo che il registro
 * prenda il valore: l'init dopo ogni scrittura dice se il chip ha risposto, e
 * un valore rifiutato si vede subito senza guardare il vetro.
 */
static int32_t muxSweepMs[MUX_PASSES] = { -1, -1, -1, -1 };

static void sweepMuxTiming()
{
  logLine("la durata non scala col MUX: misurato sul 9.7\", 24010 / 24031 / 24033 ms");
  logLine("a MUX 671 / 335 / 167. Qui si verifica solo che il registro accetti");

  for (uint8_t k = 0; k < MUX_PASSES; ++k)
  {
    const uint16_t mux = cfg.mux[k];
    logLine("-- MUX %s%s", muxEtichetta(k),
            (mux > MUX_SPEC_MAX) ? "  (fuori dalla specifica 300..680)" : "");
    muxSweepMs[k] = initPanel(CAND_DRIVER, mux);
    if (muxSweepMs[k] < 0)
      verdict("con MUX %u il controller non ha risposto allo SWRESET", (unsigned)mux);
  }

  logLine("i valori accettati sono quelli con uno SWRESET misurato: la lettura");
  logLine("spaziale la fa la sonda dell'identita' del die (tasto 0)");
  initPanel(CAND_DRIVER, cfg.gate);
}

/**
 * Costo del push di un piano ai tre clock che interessano al driver. Nessun
 * refresh: è solo il tempo di riversare 46080 byte sul bus.
 *
 * L'integrità a 20 MHz non la dice questo sweep ma il frame di identificazione,
 * che scrive metà banda a cfg.spiBase e metà a cfg.spiFast: se la metà bassa
 * esce con righe sporche o byte spostati, il bus non tiene quel clock.
 */
static uint32_t pushMsAtClock[3] = { 0, 0, 0 };

static void sweepPushClocks()
{
  const uint32_t CLOCKS[3] = { cfg.spiBase, cfg.spiDriver, cfg.spiFast };
  for (uint8_t k = 0; k < 3; ++k)
  {
    spiSettings = SPISettings(CLOCKS[k], MSBFIRST, SPI_MODE0);
    pushMsAtClock[k] = writePlane(0x24, cfg.gate, composeRowBW);
    Serial.printf("  %2lu MHz   %lu ms per piano (%lu byte)\n",
                  (unsigned long)(CLOCKS[k] / 1000000UL),
                  (unsigned long)pushMsAtClock[k],
                  (unsigned long)ROW_BYTES * cfg.gate);
  }
  spiSettings = SPISettings(cfg.spiBase, MSBFIRST, SPI_MODE0);
}

/**
 * Scrive un piano a due clock diversi: la metà alta della banda a cfg.spiBase,
 * la metà bassa a cfg.spiFast. Due finestre RAM, due transazioni.
 *
 * Serve a togliere un frame e una pausa dal test: prima l'integrità del bus al
 * clock alto costava un frame intero da confrontare a memoria con il
 * precedente, e il confronto a memoria fra due schermi visti a un minuto di
 * distanza è la parte più debole di una prova visiva. Qui le due metà stanno
 * sullo stesso schermo, con lo stesso pattern e la stessa candidata: la
 * scaletta diagonale e i righelli attraversano entrambe, quindi un byte
 * spostato nella metà bassa si vede confrontandolo con la metà alta.
// =====================================================================
// FASE PROBE: diagnostica a SPI diretta, una coda alla volta
// =====================================================================
 */
/**
 * Distingue un BUSY pilotato da un BUSY non contattato, che a pin nudo danno
 * lo stesso identico livello e sono la prima ambiguità da togliere quando il
 * controller non risponde a niente.
 *
 * Il metodo è rileggere il pin con i due pull interni dell'ESP32 (~45 kohm):
 * una linea che nessuno pilota segue il pull, una linea pilotata no. Non
 * scrive sul bus e non tocca il controller, quindi si può chiamare prima di
 * qualunque comando. Usata da runProbePhase().
 *
 * Limite da conoscere prima di credere all'esito: se la board avesse un
 * pull-down esterno di valore basso su BUSY, vincerebbe sul pull-up interno e
 * la linea sembrerebbe pilotata bassa anche da scollegata. Il caso "flottante"
 * è quindi una diagnosi certa, il caso "pilotata bassa" resta da confermare
 * sullo schematico.
 */
static void reportBusyDrive()
{
  /** Sui GPIO 34..39 dell'ESP32 non ci sono pull interni: le due pinMode qui
   *  sotto sarebbero inerti e il confronto darebbe sempre "pilotata bassa",
   *  cioè un verdetto inventato proprio dove serve di più. */
  if (PIN_BUSY >= 34)
  {
    logLine("BUSY su GPIO%d: i pin 34..39 non hanno pull interni, la prova pilotato/flottante qui non si fa; per averla portalo su 4, 21 o 22", PIN_BUSY);
    return;
  }
  pinMode(PIN_BUSY, INPUT_PULLDOWN);
  delay(5);
  const bool withPulldown = (digitalRead(PIN_BUSY) == HIGH);

  pinMode(PIN_BUSY, INPUT_PULLUP);
  delay(5);
  const bool withPullup = (digitalRead(PIN_BUSY) == HIGH);

  pinMode(PIN_BUSY, INPUT);

  Serial.printf("BUSY con pull interni: pulldown -> %s, pullup -> %s\n",
                withPulldown ? "alto" : "basso",
                withPullup ? "alto" : "basso");

  if (withPullup && !withPulldown)
  {
    Serial.println(F("  -> la linea SEGUE il pull: nessuno la pilota. Il BUSY del"));
    Serial.println(F("     controller non arriva al GPIO: FFC non contattato, pinout"));
    Serial.println(F("     dell'adattatore sfalsato, o pannello non alimentato. Ogni"));
    Serial.println(F("     misura che segue è priva di significato finchè non cambia."));
  }
  else if (!withPullup && !withPulldown)
  {
    Serial.println(F("  -> qualcosa tiene BASSA la linea contro il pull-up: il BUSY è"));
    Serial.println(F("     pilotato, quindi il pannello è collegato e a riposo. Se il"));
    Serial.println(F("     controller resta muto il guasto è sui pin dati (SCK/MOSI/"));
    Serial.println(F("     DC/CS) o sull'alimentazione del boost, non sul BUSY."));
    Serial.println(F("     Da confermare che sulla board non ci sia un pull-down esterno."));
  }
  else if (withPullup && withPulldown)
  {
    Serial.println(F("  -> qualcosa tiene ALTA la linea contro il pull-down: BUSY_ACTIVE,"));
    Serial.println(F("     cioè controller occupato prima di ogni comando, oppure un"));
    Serial.println(F("     conflitto di cablaggio sul pin."));
  }
  else
  {
    Serial.println(F("  -> lettura incoerente (bassa col pullup, alta col pulldown):"));
    Serial.println(F("     rumore sulla linea, ripetere la misura."));
  }
}

/**
 * Durata del refresh del frame di identificazione, una per candidata: -1 se la
 * candidata è stata saltata perchè elettricamente muta.
 */
// CAND_COUNT vale sei da quando l'audit del driver base ha aggiunto tre
// candidate: l'inizializzatore le copre tutte, o le ultime uscirebbero a zero
// invece che a "non misurata".
static int32_t identityMs[CAND_COUNT] = { -1, -1, -1, -1, -1, -1 };

/**
 * Prepara il banco: pin, settle della POR e livello del BUSY a riposo. Non
 * misura capabilities, mette il pannello nelle condizioni di poter rispondere,
 * quindi gira una volta per sessione e prima di qualunque sonda.
 */
static void preparaBanco()
{
  Serial.println(F("\n=================================================="));
  Serial.println(F(" FASE PROBE - SPI diretta, nessuno strato software"));
  Serial.print  (F(" coda      : ")); Serial.println(tailLabel());
  Serial.printf (" pattern   : %u source x %u gate\n", SRC, cfg.gate);
  Serial.println(F("=================================================="));

  Serial.printf("ambiente: CPU %lu MHz, APB %lu MHz, SPI %lu MHz, heap %lu B\n",
                (unsigned long)(getCpuFrequencyMhz()),
                (unsigned long)(getApbFrequency() / 1000000UL),
                (unsigned long)(cfg.spiBase / 1000000UL),
                (unsigned long)ESP.getFreeHeap());

  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_BUSY, INPUT);

  /**
   * Prima di leggere il BUSY "a riposo" bisogna dare al controller il tempo di
   * finire la propria POR. Fino a un attimo prima RST era un ingresso
   * flottante, quindi il pannello poteva essere tenuto in reset, e la riga qui
   * sopra lo ha appena rilasciato: il BUSY sale per qualche ms come fa dopo ogni
   * SWRESET. Letto subito dava ALTO, e quel falso positivo faceva dichiarare
   * invalide tutte le misure successive.
   */
  delay(50);

  /**
   * L'altra coda: CS tenuto alto per tutto il test, così quel controller non
   * ascolta il bus; BUSY letto come testimone. RST, SCK, MOSI e DC sono in
   * parallelo per costruzione del cablaggio, quindi l'altro controller subisce
   * comunque il reset: è voluto, parte da uno stato noto.
   */
  if (cfg.secondFfc)
  {
    pinMode(PIN_CS_OTHER, OUTPUT);
    digitalWrite(PIN_CS_OTHER, HIGH);
    pinMode(PIN_BUSY_OTHER, INPUT);
    Serial.printf("altra coda a riposo: BUSY = %s\n",
                  digitalRead(PIN_BUSY_OTHER) == BUSY_ACTIVE ? "ALTO (busy)" : "basso (idle)");
  }
  else
    Serial.println(F("altra coda: secondo connettore FFC non cablato, nessun testimone"));

  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  /**
   * BUSY prima di qualunque comando, letto dopo il settle della POR. È un solo
   * campione e non basta a concludere niente: un pin flottante e un controller
   * occupato danno lo stesso livello, e a distinguerli è reportBusyDrive()
   * subito dopo, che rilegge la linea contro i due pull interni.
   */
  busyStuckAtRest = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  Serial.printf("BUSY a riposo: %s\n",
                busyStuckAtRest ? "ALTO -> occupato o flottante: decide la riga dopo"
                                : "basso -> livello di riposo corretto");
  reportBusyDrive();
}

// =====================================================================
// LE TRE SONDE DELLA CODA MUTA
//
// Rispondono al sintomo che blocca il bring-up: la coda lunga stampa
// correttamente su metà pannello, la corta infilata nello stesso connettore non
// stampa niente. Nessuna delle tre costa un refresh — se il chip non prende il
// comando il refresh aborta subito su "BUSY mai salito" — e insieme
// discriminano fra le tre spiegazioni compatibili col sintomo:
//
//   1. il secondo chip è alimentato ma senza i RAIL di pilotaggio, che il
//      datasheet dà per alimentabili dall'esterno (Features p.5, Table 5-4:
//      "VGH, VGL, VSH1, VSH2, VSL can be connected to external power supply").
//      Ha oscillatore proprio, quindi RISPONDE ai comandi e non dipinge;
//   2. BS1 flotta alto e il chip è in SPI a 3 fili, quindi ignora tutto il
//      traffico a 4 fili. È una spiegazione completa e non ha niente a che
//      vedere con la topologia;
//   3. il reset non lo ingaggia, perchè un impulso solo da 10 ms non basta.
//
// L'ipotesi che il secondo chip sia uno SLAVE IN CASCADE è invece esclusa dal
// datasheet, non da queste misure: la cascade dell'SSD1683 (§6.12) porta a
// "800 (sources) x 300 (gates)", cioè raddoppia le sorgenti e lascia i gate. Un
// pannello che divide 768 gate fra due chip non può essere una coppia in
// cascade, e la banda larga 960 px che la coda lunga dipinge lo conferma: in
// cascade il master coprirebbe metà delle sorgenti, cioè 480 px.
// =====================================================================

/** L'impronta elettrica di una coda: le colonne che il confronto affianca. */
struct ImprontaCoda
{
  bool    presa;          // questa coda è stata misurata
  int8_t  busyRiposo;     // livello a riposo senza pull, -1 non misurato
  int8_t  busyPullDown;   // livello con pull-down applicato
  int8_t  busyPullUp;     // livello con pull-up applicato
  int32_t swresetMs;      // BUSY dello SWRESET
  int32_t pattern47Ms;    // pattern hardware sul piano B/N
  int32_t pattern46Ms;    // pattern hardware sull'accent
  int32_t powerOnMs;      // 0x22 = 0xC0
  int32_t hvMs;           // HV Ready Detection 0x14
  int32_t vciMs;          // VCI Detection 0x15
  int32_t crcMs;          // CRC calculation 0x34
  uint8_t resetTentativo; // al quale tentativo il BUSY si è mosso
};

static ImprontaCoda impronte[3];   // indicizzate da cfg.coda: 1 lunga, 2 corta

/**
 * Livello del BUSY a riposo con la modalità di pull indicata. Su GPIO 34..39
 * la direttiva è inerte — quei pin non hanno pull interni — e per questo la
 * prova vale sul BUSY della coda sotto test, che sta su GPIO25.
 */
static int8_t livelloBusy(uint8_t modo)
{
  pinMode(PIN_BUSY, modo);
  delay(5);
  const int8_t v = (int8_t)digitalRead(PIN_BUSY);
  pinMode(PIN_BUSY, INPUT);
  return v;
}

/**
 * Misura l'impronta della coda infilata adesso e la mette in colonna. Non
 * decide da sè: la diagnosi sta nel CONFRONTO fra le due colonne, che oggi va
 * fatto a mano su due log diversi, e che il riepilogo stampa affiancate.
 *
 * 0x14 è la misura più informativa: il datasheet dà "the detection will be
 * completed when HV is ready", quindi la DURATA del BUSY è l'esito, leggibile
 * anche senza la linea dati. Molto sotto il massimo vuol dire alte tensioni
 * salite presto; uguale al massimo vuol dire che non sono mai salite, cioè che
 * i rail non ci sono. 0x34 invece esercita la OTP: se alza il BUSY, il chip è
 * vivo e la sua memoria è raggiungibile.
 */
static void probeImprontaCoda()
{
  ImprontaCoda& imp = impronte[cfg.coda];
  imp.presa = true;

  imp.busyRiposo   = livelloBusy(INPUT);
  imp.busyPullDown = livelloBusy(INPUT_PULLDOWN);
  imp.busyPullUp   = livelloBusy(INPUT_PULLUP);
  logLine("BUSY a riposo: nudo %d, pull-down %d, pull-up %d",
          imp.busyRiposo, imp.busyPullDown, imp.busyPullUp);
  if (imp.busyRiposo == 1 && imp.busyPullDown == 1)
    verdict("BUSY alto anche col pull-down: e' PILOTATO alto, non flottante");
  else if (imp.busyRiposo == 1)
    verdict("BUSY alto a vuoto ma basso col pull-down: e' FLOTTANTE, il chip non lo pilota");

  imp.resetTentativo = resetPanelTentativi(cfg.resetTentativi, cfg.resetLowMs);
  imp.swresetMs = initPanel(CAND_MINIMAL, MUX_NOT_WRITTEN);

  imp.pattern47Ms = patternFill(0x47, bwPatternFor(true), "PATTERN 0x47 B/W");
  imp.pattern46Ms = patternFill(0x46, 0x00,               "PATTERN 0x46 ACCENT");

  imp.powerOnMs = powerOnExplicit();
  measure("POWER ON 0x22=C0", 0xC0, imp.powerOnMs);

  writeCommand(0x14);            // HV Ready Detection, A = 0x77
  writeData(0x77);               //   cool down 10 ms x 8, 7 cicli: max 560 ms
  imp.hvMs = waitBusy(2000);
  measure("HV READY 0x14", 0, imp.hvMs);
  if (imp.hvMs >= 540)
    verdict("HV Ready al massimo dei 560 ms: le alte tensioni non sono mai arrivate");
  else if (imp.hvMs > 0)
    verdict("HV Ready conclusa in %ld ms su 560: le alte tensioni sono salite", (long)imp.hvMs);

  writeCommand(0x15);            // VCI Detection al POR, 2.3 V
  writeData(0x04);
  imp.vciMs = waitBusy(2000);
  measure("VCI 0x15", 0, imp.vciMs);

  writeCommand(0x34);            // CRC calculation: alza il BUSY, esercita la OTP
  imp.crcMs = waitBusy(5000);
  measure("CRC 0x34", 0, imp.crcMs);
  if (imp.crcMs > 0)
    verdict("il CRC ha alzato il BUSY: il chip e' vivo e la sua OTP e' raggiungibile");

  measure("POWER OFF 0x22=C3", 0xC3, powerOffExplicit());
  logLine("colonna della %s riempita: il confronto e' nel riepilogo (tasto s)",
          cfg.coda == TAIL_LUNGA ? "coda LUNGA" : "coda CORTA");
}

/**
 * SPI a 3 fili. È l'unica sonda che DISCRIMINA fra un chip senza rail e un chip
 * in modalità bus sbagliata, e la differenza è tutta nel cablaggio.
 *
 * Il datasheet dà BS1 come ingresso: basso = 4 fili, alto = 3 fili a 9 bit
 * (Table 5-2 e §6.1.1). Sul FFC della board Waveshare quel pin non è cablato,
 * quindi può flottare, e in 3 fili il chip ignora tutto il traffico a 4 fili —
 * che è esattamente il sintomo. In 3 fili il D/C# va tenuto BASSO e diventa il
 * primo dei nove bit del frame; l'ESP32 li manda in hardware con transferBits.
 *
 * Se in 3 fili il chip risponde, non serve nessun ponte di rail: serve un
 * pull-down su BS1.
 */
static void probeTreFili()
{
  const uint8_t salva = cfg.busMode;

  for (uint8_t giro = 0; giro < 2; ++giro)
  {
    cfg.busMode = giro ? BUS_3WIRE : BUS_4WIRE;
    const char* nome = giro ? "3 FILI" : "4 FILI";
    logLine("-- %s", nome);

    resetPanelTentativi(cfg.resetTentativi, cfg.resetLowMs);
    writeCommand(0x12);                        // SWRESET
    const int32_t sw = waitBusy(1000);
    measure(giro ? "SWRESET A 3 FILI" : "SWRESET A 4 FILI", 0, sw);
    delay(200);

    const int32_t pat = patternFill(0x47, 0xF7,
                                    giro ? "PATTERN A 3 FILI" : "PATTERN A 4 FILI");
    if (giro && pat > 0)
      verdict("in 3 fili il chip RISPONDE: BS1 flotta alto, e il rimedio e' un pull-down su BS1");
    else if (giro)
      verdict("in 3 fili non risponde: il modo bus non spiega il silenzio");
  }

  cfg.busMode = salva;
  logLine("modo bus riportato a %s", salva == BUS_3WIRE ? "3 fili" : "4 fili");
}

/**
 * Reset a ritentativi con attese crescenti, come fa il firmware di fabbrica
 * OEPL (`for (attempt = 10;; attempt += 20)`), contro il singolo impulso da
 * 10 ms del driver. Se una coda ha bisogno di più tempo o di più tentativi per
 * uscire dal reset, un impulso solo la lascia muta e non c'è niente nel log che
 * lo dica.
 *
 * Lo SWRESET, che segue, alza il BUSY per 2 ms misurati sul 9.7": i 200 ms che
 * il driver aspetta dopo di lui sono cento volte tanto.
 */
static void probeResetRitentativi()
{
  static const uint8_t DURATE[] = { 2, 10, 20, 50 };
  for (uint8_t k = 0; k < sizeof(DURATE) / sizeof(DURATE[0]); ++k)
  {
    const uint8_t t = resetPanelTentativi(4, DURATE[k]);
    logLine("impulso da %u ms: BUSY mosso al tentativo %u su 4",
            (unsigned)DURATE[k], (unsigned)t);
    if (t == 1)
    {
      verdict("un impulso da %u ms basta al primo colpo: il reset del driver va bene",
              (unsigned)DURATE[k]);
      break;
    }
    if (t > 1)
      verdict("servono %u tentativi con impulso da %u ms: il driver ne fa uno solo",
              (unsigned)t, (unsigned)DURATE[k]);
  }

  writeCommand(0x12);
  const int32_t sw = waitBusy(1000);
  measure("SWRESET DOPO IL RESET", 0, sw);
  if (sw >= 0)
    verdict("SWRESET: BUSY alto per %ld ms, contro i 200 che il driver aspetta", (long)sw);
}

#endif // DUAL_PANEL_FINDER_PROBES_ELETTRICHE_H
