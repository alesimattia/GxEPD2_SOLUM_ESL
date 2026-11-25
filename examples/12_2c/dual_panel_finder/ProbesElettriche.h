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
/**
 * Descrive a parole l'esito negativo di waitBusy(), per le righe che altrimenti
 * stamperebbero "alto per N ms" con un numero che non è una durata.
 */
static const char* busyFailureText(int32_t ms, uint32_t timeoutMs, char* buf, uint8_t size)
{
  if (ms == BUSY_NEVER_ROSE)
    snprintf(buf, size, "BUSY mai salito: il comando non ha avuto risposta");
  else
    snprintf(buf, size, "BUSY ancora alto a %lu ms", (unsigned long)timeoutMs);
  return buf;
}

static void probeLifeAndHighVoltage(bool readRegisters)
{
  Serial.println(F("  prova di vita e alte tensioni:"));

  patternMs47 = patternFill(0x47, 0xF7, "B/N   ");
  patternMs46 = patternFill(0x46, 0xF7, "accent");

  /**
   * In sola logica restano i pattern hardware e il CRC, che esercita l'OTP: power
   * on, HV Ready e VCI Detection vogliono clock e blocco analogico accesi, cioè un
   * 0x22 con bit analogici seguito da 0x20, che con i regolatori dello slave senza
   * condensatori non va mandato.
   */
  if (logicOnly())
  {
    logLine("modalità sola logica: power on, HV Ready 0x14, VCI 0x15 e power off non eseguiti");
    writeCommand(0x34);
    const int32_t crc = waitBusy(5000);
    measure("CRC 0x34", 0, crc);
    return;
  }

  char testo[64];
  writeCommand(0x22);
  writeData(0xC0);        // solo power on: abilita clock e blocco analogico
  writeCommand(0x20);
  powerOnMs = waitBusy(5000);
  if (powerOnMs < 0)
    Serial.printf("  power on 0xC0       %s\n", busyFailureText(powerOnMs, 5000, testo, sizeof(testo)));
  else
    logLine("power on 0xC0       BUSY alto per %ld ms", (long)powerOnMs);

  writeCommand(0x14);
  writeData(0x77);        // cool down 80 ms x 7 cicli -> massimo 560 ms
  hvDetectMs = waitBusy(3000);
  if (hvDetectMs == BUSY_NEVER_ROSE)
    Serial.println(F("  HV detect 0x14      BUSY mai salito: il comando non ha avuto risposta"));
  else if (hvDetectMs < 0)
    Serial.println(F("  HV detect 0x14      BUSY ancora alto a 3000 ms: HV non pronta"));
  else
    Serial.printf("  HV detect 0x14      BUSY alto per %ld ms (massimo programmato 560)\n",
                  (long)hvDetectMs);

  writeCommand(0x15);
  writeData(0x04);        // livello POR, 2.3 V
  vciDetectMs = waitBusy(3000);
  if (vciDetectMs < 0)
    Serial.printf("  VCI detect 0x15     %s\n", busyFailureText(vciDetectMs, 3000, testo, sizeof(testo)));
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
    Serial.printf("  power off 0xC3      %s\n", busyFailureText(powerOffMs, 5000, testo, sizeof(testo)));
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
    /**
     * In sola logica i tempi analogici e i pattern misurati qui sono del secondo
     * connettore e non devono sovrascrivere quelli che la scheda per il driver
     * attribuisce a J1: si salvano e si rimettono dopo la prova.
     */
    const int32_t salva47 = patternMs47, salva46 = patternMs46;
    probeLifeAndHighVoltage(c == CAND_COUNT - 1);

    CandProbe& p = candProbe[c];
    p.swresetMs   = sw;
    p.pattern47Ms = patternMs47;
    p.pattern46Ms = patternMs46;
    p.powerOnMs   = logicOnly() ? BUSY_NEVER_ROSE : powerOnMs;
    p.powerOffMs  = logicOnly() ? BUSY_NEVER_ROSE : powerOffMs;
    p.hvMs        = logicOnly() ? BUSY_NEVER_ROSE : hvDetectMs;
    p.vciMs       = logicOnly() ? BUSY_NEVER_ROSE : vciDetectMs;
    if (logicOnly()) { patternMs47 = salva47; patternMs46 = salva46; }
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
 * quello che il BUSY dice da solo, ed è la cosa che serve al driver: se il
 * controller CHE RISPONDE esegue gli opcode con il bit 7 alto. Si prendono i
 * comandi che alzano il BUSY per una durata misurabile, si manda prima
 * l'opcode nudo e poi lo stesso con |0x80, e si confrontano le due reazioni.
 *
 *   entrambi alzano il BUSY -> questo controller esegue anche l'opcode offset
 *        come se fosse nudo: il bit 7 non lo distingue, e in una coppia in
 *        cascade questo chip non si comporterebbe da master che ignora gli
 *        opcode destinati allo slave; la cascade non è esclusa ma perde questo
 *        indizio, e la prova vera è la sonda b a ponte fatto
 *   solo l'opcode nudo -> il controller RIFIUTA gli opcode offset: sono uno
 *        spazio di indirizzamento distinto, compatibile con il modello a
 *        cascade (slave a opcode|0x80) senza provarlo
 *
 * Esegue 0x22 = 0xC0 con 0x20, cioè accende l'analogico: in sola logica non si
 * lancia.
 *
 * I due gruppi sono separati da un reset: se un opcode offset viene ignorato il
 * suo parametro finisce in un registro che non abbiamo scelto, e non deve poter
 * falsare le misure del gruppo nudo.
 */
static void sweepOpcodeBit7()
{
  if (logicOnly())
  {
    logLine("modalità sola logica: la sonda manda 0x22 = 0xC0 + 0x20 (analogico), non si esegue");
    return;
  }
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
        if (digitalRead(busyPin()) == BUSY_ACTIVE) { rose = true; break; }
      }
      const int32_t ms = rose ? waitBusyFall(3000) : BUSY_NEVER_ROSE;
      (pass ? offset : nudo)[k] = ms;
      Serial.printf("   0x%02X %-22s %s\n", uint8_t(OPS[k].code | off), OPS[k].nome,
                    rose ? "BUSY mosso" : "BUSY fermo");
      if (rose && ms >= 0) Serial.printf("        alto per %ld ms\n", (long)ms);
      else if (rose)       Serial.println(F("        ancora alto a 3000 ms"));
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
    verdict("il bit 7 NON indirizza su questo controller: gli opcode offset vengono eseguiti come nudi, la cascade non è esclusa ma la decide la sonda b a ponte fatto");
  else if (eseguiti == 0)
    verdict("il bit 7 è uno spazio di indirizzamento distinto: compatibile con il modello a cascade (slave a opcode|0x80), che la sonda b a ponte fatto può confermare");
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
  const int pin = busyPin();
  /** Sui GPIO 34..39 dell'ESP32 non ci sono pull interni: le due pinMode qui
   *  sotto sarebbero inerti e il confronto darebbe sempre "pilotata bassa",
   *  cioè un verdetto inventato proprio dove serve di più. */
  if (pin >= 34)
  {
    logLine("BUSY su GPIO%d: i pin 34..39 non hanno pull interni, la prova pilotato/flottante qui non si fa; per averla portalo su 4, 21 o 22", pin);
    return;
  }
  pinMode(pin, INPUT_PULLDOWN);
  delay(5);
  const bool withPulldown = (digitalRead(pin) == HIGH);

  pinMode(pin, INPUT_PULLUP);
  delay(5);
  const bool withPullup = (digitalRead(pin) == HIGH);

  pinMode(pin, busyRestMode(pin));

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
  Serial.printf("bersaglio del bus: %s\n",
                cfg.busTarget == TARGET_SECOND ? "secondo connettore (CS 32, BUSY 22)"
                                               : "primo connettore J1 (CS 15, BUSY 25)");
  if (logicOnly())
    Serial.println(F("modalità sola logica: niente 0x22 con bit analogici seguito da 0x20"));

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
   * L'altro connettore: CS tenuto alto per tutto il test, così quel controller non
   * ascolta il bus; BUSY letto come testimone. Il BUSY del secondo connettore sta
   * in INPUT_PULLDOWN, quindi un pad scollegato si legge "basso" e non manda in
   * timeout ogni attesa. RST, SCK, MOSI e DC sono in parallelo per costruzione del
   * cablaggio, quindi l'altro controller subisce comunque il reset: è voluto,
   * parte da uno stato noto.
   */
  if (cfg.secondFfc || cfg.busTarget == TARGET_SECOND)
  {
    pinMode(PIN_CS_OTHER, OUTPUT);
    digitalWrite(PIN_CS_OTHER, HIGH);
    pinMode(PIN_BUSY_OTHER, INPUT_PULLDOWN);
    delay(5);
    Serial.printf("altro connettore a riposo: BUSY = %s\n",
                  digitalRead(otherBusyPin()) == BUSY_ACTIVE ? "ALTO (busy)" : "basso (idle)");
  }
  else
    Serial.println(F("altro connettore: secondo FFC non cablato, nessun testimone"));

  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  /**
   * BUSY prima di qualunque comando, letto dopo il settle della POR. È un solo
   * campione e non basta a concludere niente: un pin flottante e un controller
   * occupato danno lo stesso livello, e a distinguerli è reportBusyDrive()
   * subito dopo, che rilegge la linea contro i due pull interni.
   */
  busyStuckAtRest = (digitalRead(busyPin()) == BUSY_ACTIVE);
  Serial.printf("BUSY a riposo: %s\n",
                busyStuckAtRest ? "ALTO -> occupato o flottante: decide la riga dopo"
                                : "basso -> livello di riposo corretto");
  reportBusyDrive();
}

// =====================================================================
// LE SONDE DELLA CODA MUTA
//
// Rispondono al sintomo che blocca il bring-up: la coda lunga stampa
// correttamente su metà pannello, la corta infilata nello stesso connettore non
// stampa niente. Nessuna delle tre costa un refresh (se il chip non prende il
// comando il refresh aborta subito su "BUSY mai salito") e, puntate sul
// connettore giusto, discriminano fra le spiegazioni compatibili col sintomo.
// Il controller del 12.2" è sconosciuto: ciò che segue su cascade, strap e rail
// è per analogia con i datasheet SSD1677 e SSD1683, e nessuna misura dimostra
// che il die sia uno di quei chip.
//
// La coda corta muta in J1 con BS1 basso (fisso a GND via R11 sulla board)
// esclude soltanto un controller indipendente con la piedinatura standard, che
// avrebbe risposto come la lunga. Restano:
//
//   1. uno slave in cascade: sincronizzato al master (clock CL e rail comuni),
//      e il modo in cui gate e sorgenti si dividono fra i due lo decide il
//      cablaggio del vetro, non la cascade. Senza il master non ha oscillatore
//      e il BUSY resta fermo su tutto;
//   2. una piedinatura diversa, o un pin di enable o di M/S# su un pad che in
//      J1 non ha una net: il chip esiste ma non è abilitato;
//   3. un chip danneggiato.
//
// Le decide la fase 0 di cable_linking.html e, sulla breakout con la sola
// logica, il criterio "il BUSY sale dopo SWRESET sul CS dello slave", che la
// candidata per-CS e la procedura guidata (tasto t) misurano. Il reset
// ritentato e la prova a 3 fili restano per escludere i casi di tempi e di
// modo bus, il secondo solo sulla breakout dove BS1 è un pad libero.
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
 * la direttiva è inerte, perchè quei pin non hanno pull interni: la prova vale
 * sul BUSY del bersaglio, che sta su GPIO25 (J1) o GPIO22 (secondo connettore).
 * Al ritorno il pin torna al suo modo di riposo.
 */
static int8_t livelloBusy(uint8_t modo)
{
  const int pin = busyPin();
  pinMode(pin, modo);
  delay(5);
  const int8_t v = (int8_t)digitalRead(pin);
  pinMode(pin, busyRestMode(pin));
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
  ImprontaCoda& imp = impronte[activeTail()];
  imp.presa = true;

  /**
   * "Pilotato o flottante" si decide con i due pull: una linea che nessuno
   * pilota segue il pull (alta col pull-up, bassa col pull-down), una linea
   * pilotata no. La lettura nuda non decide, perchè a pin senza pull un
   * flottante e un pilotato danno lo stesso livello.
   */
  imp.busyRiposo   = livelloBusy(INPUT);
  imp.busyPullDown = livelloBusy(INPUT_PULLDOWN);
  imp.busyPullUp   = livelloBusy(INPUT_PULLUP);
  logLine("BUSY a riposo: nudo %d, pull-down %d, pull-up %d",
          imp.busyRiposo, imp.busyPullDown, imp.busyPullUp);
  if (imp.busyPullUp == 1 && imp.busyPullDown == 0)
    verdict("BUSY segue il pull (alto col pull-up, basso col pull-down): è FLOTTANTE, nessuno lo pilota");
  else if (imp.busyPullUp == 1 && imp.busyPullDown == 1)
    verdict("BUSY alto anche col pull-down: è PILOTATO alto, non flottante");
  else if (imp.busyPullUp == 0 && imp.busyPullDown == 0)
    verdict("BUSY basso anche col pull-up: è PILOTATO basso, il chip a riposo o un pull esterno forte");
  else
    verdict("BUSY incoerente (basso col pull-up, alto col pull-down): rumore sulla linea, ripeti la misura");

  imp.resetTentativo = resetPanelTentativi(cfg.resetTentativi, cfg.resetLowMs);
  imp.swresetMs = initPanel(CAND_MINIMAL, MUX_NOT_WRITTEN);

  imp.pattern47Ms = patternFill(0x47, bwPatternFor(true), "PATTERN 0x47 B/W");
  imp.pattern46Ms = patternFill(0x46, 0x00,               "PATTERN 0x46 ACCENT");

  /**
   * Power on, HV Ready e VCI Detection vogliono clock e analogico accesi, cioè un
   * 0x22 con bit analogici seguito da 0x20. In sola logica non si mandano, e le
   * tre colonne restano a -1, non misurato.
   */
  if (logicOnly())
  {
    logLine("modalità sola logica: power on, HV Ready 0x14, VCI 0x15 e power off non eseguiti");
    imp.powerOnMs = BUSY_NEVER_ROSE;
    imp.hvMs      = BUSY_NEVER_ROSE;
    imp.vciMs     = BUSY_NEVER_ROSE;
  }
  else
  {
    imp.powerOnMs = powerOnExplicit();
    measure("POWER ON 0x22=C0", 0xC0, imp.powerOnMs);

    writeCommand(0x14);            // HV Ready Detection, A = 0x77
    writeData(0x77);               //   cool down 10 ms x 8, 7 cicli: max 560 ms
    imp.hvMs = waitBusy(2000);
    measure("HV READY 0x14", 0, imp.hvMs);
    if (imp.hvMs == BUSY_NEVER_ROSE)
      verdict("HV Ready: il BUSY non è mai salito, il comando non ha avuto risposta");
    else if (imp.hvMs == BUSY_STILL_HIGH || imp.hvMs >= 540)
      verdict("HV Ready al massimo dei 560 ms: le alte tensioni non sono mai arrivate");
    else
      verdict("HV Ready conclusa in %ld ms su 560: le alte tensioni sono salite", (long)imp.hvMs);

    writeCommand(0x15);            // VCI Detection al POR, 2.3 V
    writeData(0x04);
    imp.vciMs = waitBusy(2000);
    measure("VCI 0x15", 0, imp.vciMs);
  }

  writeCommand(0x34);            // CRC calculation: alza il BUSY, esercita la OTP
  imp.crcMs = waitBusy(5000);
  measure("CRC 0x34", 0, imp.crcMs);
  if (imp.crcMs >= 0)
    verdict("il CRC ha alzato il BUSY: il chip è vivo e la sua OTP è raggiungibile");

  if (!logicOnly())
    measure("POWER OFF 0x22=C3", 0xC3, powerOffExplicit());
  logLine("colonna della %s riempita: il confronto è nel riepilogo (tasto s)",
          activeTail() == TAIL_LUNGA ? "coda LUNGA" : "coda CORTA");
}

/**
 * SPI a 3 fili. Distingue un chip senza rail da un chip in modalità bus
 * sbagliata solo dove BS1 può cambiare.
 *
 * Il datasheet SSD1677, per analogia, dà BS1 come ingresso: basso = 4 fili, alto
 * = 3 fili a 9 bit (Table 5-2 e §6.1.1). Sulla board Waveshare V3 BS1 è a GND via
 * R11 da 0 ohm, quindi il chip in J1 resta a 4 fili qualunque cosa mandi l'ESP32
 * e questa prova è INCONCLUDENTE: un silenzio in 3 fili non dice niente. Sulla
 * breakout della coda corta invece BS1 è un pad libero, che può flottare alto, e
 * in 3 fili il chip ignora tutto il traffico a 4 fili: lì la prova ha senso. In 3
 * fili il D/C# va tenuto BASSO e diventa il primo dei nove bit del frame; l'ESP32
 * li manda in hardware con transferBits.
 *
 * Se in 3 fili il chip risponde su una breakout, non serve nessun ponte di rail:
 * serve un pull-down su BS1.
 */
static void probeTreFili()
{
  const uint8_t salva = cfg.busMode;
  if (cfg.busTarget == TARGET_J1)
    logLine("BS1 è a GND via R11 sulla board: il chip in J1 resta a 4 fili qualunque cosa si mandi, la prova non discrimina");

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
    if (giro && pat >= 0)
      verdict("in 3 fili il chip RISPONDE: su una breakout con BS1 libero il rimedio è un pull-down su BS1, in J1 (BS1 a GND) l'esito è anomalo");
    else if (giro)
      verdict("in 3 fili non risponde: non conclude con BS1 a GND, il chip resta a 4 fili comunque");
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
    if (t == 0 && resetSawBusyStuckHigh)
      logLine("impulso da %u ms: BUSY salito e mai sceso (fisso alto), non conta come risposta al reset",
              (unsigned)DURATE[k]);
    else if (t == 0)
      logLine("impulso da %u ms: BUSY mai mosso in 4 tentativi", (unsigned)DURATE[k]);
    else
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
  else if (sw == BUSY_NEVER_ROSE)
    verdict("SWRESET: il BUSY non è mai salito, nessuna risposta");
  else
    verdict("SWRESET: BUSY ancora alto a 1000 ms, fisso alto e non una risposta");
}

#endif // DUAL_PANEL_FINDER_PROBES_ELETTRICHE_H
