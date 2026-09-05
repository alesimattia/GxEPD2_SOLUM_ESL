/**
 * dual_panel_finder - sonda del pannello SOLUM 12.2" (768w x 960h nativi,
 * pilotato landscape come 960x768, due controller SSD16xx da 960x384). Lo
 * stesso vetro sta nelle linee Newton PRO e Newton Core / M3.
 *
 * Non decide niente: mette il pannello nelle condizioni di rispondere e riporta
 * cosa si vede e quanto ci mette. Da lì si corregge
 * src/GxEPD2_SOLUM_122c_960x768.h.
 *
 * DUE FASI, sempre entrambe nella stessa esecuzione.
 *   FASE PROBE   a SPI diretta su una coda alla volta, senza GxEPD2 nè il
 *                driver custom: misura il SILICIO.
 *   FASE DRIVER  costruisce GxEPD2_SOLUM_DRIVER_CLASS e gli fa stampare un
 *                frame: misura il DRIVER.
 * Se il probe stampa e la fase driver no, il difetto è nel driver.
 *
 * Cosa misura ogni sonda a video, quanti refresh costa e con quali criteri sono
 * stati contati: README.md di questo example. Come si legge il log insieme al
 * vetro: il banner a runtime e la sezione SCHERMATE NUMERATE più sotto.
 *
 * Il resto lo misura senza spendere un refresh, ed è la parte che vale sulla
 * coda che non stampa: livello del BUSY a riposo contro i due pull interni,
 * HV Ready Detection 0x14 e VCI Detection 0x15 (alzano il BUSY per la durata
 * della misura, quindi la DURATA è la risposta anche senza linea di lettura),
 * registri 0x2F / 0x2E / 0x1B, pattern hardware 0x46 / 0x47, tempi di power on
 * e power off (0x22 = 0xC0 / 0xC3) e i due clock SPI, metà banda ciascuno.
 *
 * ---------------------------------------------------------------------------
 * COSA SI SA GIÀ, e non va rimisurato. Le fonti stanno in docs/fonti_esterne.md
 * (§3.1 dualssd.cpp, §4 la cascade, §6.2 il tetto dei 680 gate) e in
 * docs/122c/identificazione_pannello.md.
 *
 * - Una coda stampa: su un solo FFC esce un rettangolo 960x384, quindi ogni
 *   controller pilota 960 source x 384 gate e lo split cade sull'asse corto.
 * - Che i controller siano due, e divisi per righe, è un conto: l'SSD1677 ha
 *   960 source e 680 GATE, e delle taglie grandi solo la 12.2" sfora con 768
 *   gate nativi. Lo split DEVE stare sull'asse gate, perchè su quello source un
 *   chip solo basterebbe. Il limite dei 680 è usato tutto in commercio: il Good
 *   Display GDEM133T91 è 960x680 con un solo SSD1677 e MUX = 679.
 * - L'altra coda non risponde, e la spiegazione più probabile è che sia lo
 *   SLAVE di una coppia in CASCADE: nel datasheet SSD1683 §6.12 lo slave ha
 *   oscillatore e booster disabilitati e riceve CL e tensioni dal master,
 *   quindi un breakout con solo SPI e 3.3 V lo lascia muto per costruzione. I
 *   due pin della cascade, M/S# e CL, stanno nel pin table dell'SSD1677 stesso,
 *   classificati riservati. Il tag di fabbrica ha un solo chip select e una
 *   sola sezione di boost, mentre un pannello a controller indipendenti li paga
 *   in numero: il DESPI-C1248 del 12.48" Good Display, due FPC e quattro
 *   controller, ha quattro CS, quattro BUSY e due boost separati. Il trucco
 *   degli opcode sommati a 0x80 esiste PERCHÈ un secondo CS non c'è.
 * - Sul cavo c'è un pin BS (3 fili / 4 fili) che il firmware SOLUM tiene basso:
 *   se su una coda cablata a mano resta flottante, il controller può stare in 3
 *   fili e ignorare tutto.
 * - Il connettore della Waveshare V3 non porta fuori SDO (schematico in
 *   docs/): la lettura dei registri dirà sempre "nessuna linea dati", ed è una
 *   proprietà del CONNETTORE, non della coda. Su un secondo connettore cablato
 *   a mano un SDO darebbe 0x2E, lo User ID da OTP, e chiuderebbe la questione
 *   del part number. Attenzione: GPIO12 è di strapping (MTDI).
 * - L'UC8179 è escluso dall'aritmetica: 800x600 non copre nessuna spartizione
 *   possibile con due controller.
 *
 * ---------------------------------------------------------------------------
 * DUE COSE CHE IL RESTO DEL TEST NON POTREBBE DIRE
 *
 * LA POLARITÀ DEL PIANO BW non viene assunta ma misurata, e la sonda gira per
 * prima fra quelle a video perchè tutto ciò che nomina un colore dipende da
 * lei. La decodifica di 0x21 A[3:0] = 1000 come "BW inverse" viene dalla Rev
 * 1.0, la stessa revisione che definisce 0x21 con un parametro solo mentre
 * l'init di fabbrica gliene scrive due. Elettricamente non si può: il read-back
 * non esiste e nessuna grandezza leggibile dipende dai piani. Due frame con la
 * RAM identica e solo 0x21 diverso lo dicono a occhio; l'esito si mette in
 * BW_POLARITY, e da lì ogni sonda scrive il byte giusto.
 *
 * LA LUT VIA 0x32, cioè la waveform caricata da host invece che da OTP, la
 * misurano due sonde:
 *   - il PARTIAL: le passate d'area usano 0xFC e 0xF4, che col bit 4 di 0x22
 *     ricaricano la waveform dall'OTP, quindi misurano sempre quella. La sonda
 *     ne scrive una breve e la usa col bit 4 spento, come il GDEH116T91 sullo
 *     stesso command set ottiene 700 ms;
 *   - il QUARTO COLORE: il frame a bande non distingue un film a tre pigmenti
 *     da una waveform che ne pilota tre, perchè la Table 6-4 aliasa LUT3 su
 *     LUT2. Il probe dei livelli pilota LUT2 a VSH1 e LUT3 a VSH2, che è come
 *     un film BWRY separa rosso e giallo. Qui conta: il codice modello ha campo
 *     colore 4.
 * Nessuna delle due tocca le tensioni: 0x32 scrive i byte 0..104 della LUT,
 * mentre VGH, VSH1, VSH2, VSL e VCOM stanno ai byte 105..109. Alzarle con 0x04
 * resta fuori portata per scelta: è la sola leva che può danneggiare il film.
 *
 * ---------------------------------------------------------------------------
 * PROCEDURA
 *   1. Flasha una volta sola: non c'è più niente da configurare nel sorgente.
 *      Al boot compare un menu sul monitor seriale — sonde da eseguire, coda,
 *      secondo connettore, pause, polarità — e senza risposta entro dieci
 *      secondi il test parte col profilo salvato in NVS.
 *   2. Per provare l'altra coda: infilala nel connettore, riavvia e dichiarala
 *      nel menu. Su una coda che non stampa contano le prime righe, non i
 *      frame. Sono due esecuzioni, non due compilazioni.
 *   3. Il log lo registra il PC: tools/registra_run.py apre la porta, inoltra
 *      la tastiera e scrive tutto su un .txt.
 *
 * HARDWARE
 *   - Waveshare E-Paper ESP32 Driver Board, HSPI: SCK=13, MISO=12, MOSI=14. Ha
 *     UN SOLO connettore FFC, su CS=15 e BUSY=25, quindi le due code si provano
 *     una alla volta: risponde la LUNGA, e stampa la metà del pannello dal
 *     proprio lato.
 *   - Secondo connettore: breakout da cablare a mano come da
 *     docs/122c/connessioni.html, CS=GPIO32 e BUSY=GPIO35. GPIO33 non compare
 *     fra le net dello schematico, quindi il CS che la documentazione dava lì
 *     sta su GPIO32.
 *   - SPI a 4 MHz per il bring-up; il driver poi userà 20 MHz.
 *   - Lo switch n.1 della board non sceglie il pannello ma la resistenza di
 *     sense del booster, A = 3R e B = 0.47R: se il refresh è debole o pieno di
 *     ghosting, va provata prima di incolpare il driver.
 * ---------------------------------------------------------------------------
 */

#include <SPI.h>
#include <stdarg.h>
#include <Preferences.h>

// L'ombrello include il driver giusto e ne espone il nome come
// GxEPD2_SOLUM_DRIVER_CLASS: lo sketch non nomina mai la classe concreta.
// Sta qui in testa e non più giù perchè il preprocessore di Arduino inserisce
// i prototipi generati prima del primo include: una funzione che ritorna quel
// tipo, come driver(), non compilerebbe.
#define SOLUM_PANEL_122C
#include <GxEPD2_SOLUM.h>

/** Dichiarazione anticipata: il preprocessore Arduino inserisce i prototipi
 *  prima della definizione dell'enum, e serve un tipo di base esplicito. */
enum Schermata : uint8_t;

// ---------------------------------------------------------------------------
// CONFIGURAZIONE, tutta a runtime
//
// Niente qui dentro richiede una ricompilazione: le scelte si fanno dal menu
// che compare al boot sul monitor seriale, e restano in NVS fra un reboot e
// l'altro. Senza nessuno alla tastiera il test parte da solo col profilo
// salvato, quindi un run non presidiato si comporta come prima.
//
// L'unica cosa che resta fisica è quale coda del pannello è infilata nel
// connettore: quella si cambia con le dita, non col compilatore.
// ---------------------------------------------------------------------------

static const uint8_t TAIL_LUNGA = 1;
static const uint8_t TAIL_CORTA = 2;

/** Polarità del piano 0x24: la misura probeBwPolarity(), non si deduce. */
static const int8_t BW_UNKNOWN   = -1;   // non ancora osservata
static const int8_t BW_DATASHEET =  0;   // bit = 1 -> pixel BIANCO, Table 6-4
static const int8_t BW_INVERSE   =  1;   // bit = 1 -> pixel NERO

/**
 * Le sonde, una per bit: la maschera in Config dice quali eseguire, e il menu
 * la costruisce. Poterne eseguire una sola è quello che evita di rifare undici
 * minuti per rileggere una schermata.
 */
enum SondaBit : uint16_t
{
  S_CANDIDATE  = 1 << 0,   // sweep elettrico delle tre candidate di init
  S_BIT7       = 1 << 1,   // sonda elettrica del bit 7 dell'opcode
  S_MUX        = 1 << 2,   // sweep del MUX, 3 refresh cronometrati
  S_CLOCK      = 1 << 3,   // costo del push ai tre clock
  S_IDENT      = 1 << 4,   // frame di identificazione, uno per candidata
  S_POLARITA   = 1 << 5,   // polarità del piano BW, 2 frame
  S_BANDE      = 1 << 6,   // 4 bande dei piani più i box a finestra parziale
  S_AREA       = 1 << 7,   // partial d'area
  S_DIFF       = 1 << 8,   // differenziale approfondita
  S_LUT        = 1 << 9,   // partial con LUT custom
  S_LIVELLI    = 1 << 10,  // quarto colore per livello di sorgente
  S_SLAVE      = 1 << 11,  // secondo controller a opcode|0x80
  S_TEMP       = 1 << 12,  // banchi di waveform per temperatura
  S_PINGPONG   = 1 << 13,  // RAM ping-pong via 0x37
  S_SLEEP      = 1 << 14,  // deep sleep
  S_DRIVER     = 1 << 15,  // fase driver
  S_TUTTE      = 0xFFFF,
};

/**
 * Valori di MUX che il test prova, cioè quante gate line programma nel cmd
 * 0x01. MUX_NOT_WRITTEN significa "non scrivere il registro e lasciarlo al
 * default OTP", che è ciò che fa CAND_MINIMAL.
 */
static const uint16_t MUX_NOT_WRITTEN = 0;

/** Punti dello sweep di temperatura: quanti ne stanno sulla banda. */
static const uint8_t TEMP_PASSES = 4;
/** Valori dello sweep del MUX. */
static const uint8_t MUX_PASSES = 3;

struct Config
{
  uint8_t  coda;        // TAIL_LUNGA o TAIL_CORTA: solo l'etichetta del report
  bool     secondFfc;   // il secondo connettore FFC è cablato
  bool     esaustivo;   // nessuna passata condizionale viene saltata
  int8_t   bwPolarity;  // BW_UNKNOWN finché la sonda non la determina
  uint32_t pausaMs;     // durata massima di una pausa di osservazione
  uint16_t sonde;       // maschera di SondaBit

  /**
   * Parametri di misura: i numeri che le sonde spazzano. Stanno qui perchè
   * cambiarli non deve costare una ricompilazione — il §6.9 dà l'OTP per
   * capace di 34 banchi di waveform e sulla banda ne stanno quattro per run.
   */
  int16_t  temp[TEMP_PASSES];   // gradi forzati via 0x18 / 0x1A
  uint16_t mux[MUX_PASSES];     // gate line programmate in 0x01, 0 = non scritto
  uint32_t spiBase;             // clock delle sonde
  uint32_t spiFast;             // clock della metà bassa nel frame di verifica
  uint32_t spiDriver;           // clock della fase driver
  uint32_t timeoutMs;           // timeout di default di un refresh
  uint32_t sleepMs;             // finestra ferma per il multimetro
};

/**
 * Valori di partenza al primo boot, poi sovrascritti da quelli in NVS. La
 * fase driver sta nella maschera come le altre sonde (S_DRIVER).
 */
static const Config CFG_DEFAULT =
{
  TAIL_CORTA,     // coda
  false,          // secondo FFC non cablato
  true,           // esaustivo
  BW_UNKNOWN,     // polarità da misurare
  10000,          // pause da 10 s
  S_TUTTE,        // tutte le sonde
  { 0, 20, 40, 70 },        // temperature forzate
  { 0, 384, 680 },          // MUX: non scritto, misurato, POR
  4000000, 20000000, 10000000,
  40000,          // timeout di un refresh
  20000,          // finestra del multimetro
};

static Config cfg = CFG_DEFAULT;

/** Etichetta della coda per il report. */
static const char* tailLabel()
{
  return (cfg.coda == TAIL_LUNGA)
         ? "coda LUNGA nel connettore della board (CS=15, BUSY=25)"
         : "coda CORTA nel connettore della board (CS=15, BUSY=25)";
}


// ---------------------------------------------------------------------------
// INPUT DAL MONITOR SERIALE
//
// Ogni domanda ha un timeout e un valore preimpostato: se nessuno risponde il
// test prosegue da solo con quello, quindi lo sketch resta usabile con un
// monitor di sola lettura e un log catturato su file non si blocca mai.
//
// Niente qui tocca l'output. Si legge solo il buffer di RICEZIONE, e solo nei
// punti in cui il test pone una domanda; ogni risposta e ogni timeout vengono
// ristampati, così il file di log spiega da sè come è stato configurato il run.
// ---------------------------------------------------------------------------

/**
 * Butta i byte fermi nel buffer di ricezione, così un carattere digitato dieci
 * minuti prima non risponde da solo alla domanda successiva. Non tocca l'output.
 */
static void scartaInputPendente()
{
  while (Serial.available())
    Serial.read();
}

/**
 * Legge una riga dal seriale, al massimo timeout_ms. Ritorna i caratteri
 * letti, e -1 se non è arrivato niente: chi chiama distingue così un INVIO a
 * vuoto, che vale "tieni il preimpostato" e risponde subito, da un'assenza di
 * risposta. Accetta CR, LF o entrambi come fine riga.
 */
static int leggiRiga(char* buf, uint8_t max, uint32_t timeout_ms)
{
  uint8_t n = 0;
  bool riga = false;
  const uint32_t t0 = millis();
  while ((millis() - t0) < timeout_ms)
  {
    if (!Serial.available()) { delay(10); continue; }
    const int c = Serial.read();
    if (c == '\r' || c == '\n')
    {
      // Il LF che chiude un CRLF arriva subito dopo: va consumato qui, o
      // chiuderebbe a vuoto la domanda successiva.
      if (c == '\r')
      {
        const uint32_t tc = millis();
        while ((millis() - tc) < 20 && !Serial.available()) delay(1);
        if (Serial.available() && Serial.peek() == '\n') Serial.read();
      }
      riga = true;
      break;
    }
    if (n < max - 1) buf[n++] = (char)c;
  }
  buf[n] = '\0';
  return riga ? (int)n : -1;
}

/** Domanda sì / no. Senza risposta entro il timeout ritorna il preimpostato. */
static bool chiediSN(const char* domanda, bool preimpostato, uint32_t timeout_ms)
{
  scartaInputPendente();
  Serial.printf("%s [s/n, %lu s, invio = %s]\n",
                domanda, (unsigned long)(timeout_ms / 1000), preimpostato ? "s" : "n");
  char buf[8];
  const int n = leggiRiga(buf, sizeof(buf), timeout_ms);
  bool esito = preimpostato;
  if (n > 0 && (buf[0] == 's' || buf[0] == 'S' || buf[0] == 'y' || buf[0] == 'Y')) esito = true;
  else if (n > 0 && (buf[0] == 'n' || buf[0] == 'N'))                              esito = false;
  Serial.printf("  risposta: %s%s\n", esito ? "sì" : "no",
                n < 0 ? "  (nessuna risposta, preso il preimpostato)" : "");
  return esito;
}

/**
 * Domanda a scelta multipla, voci numerate da 1. Ritorna l'indice scelto
 * (0-based), oppure il preimpostato se nessuno risponde o la risposta è fuori
 * range.
 */
static int chiediScelta(const char* domanda, const char* const* voci, int quante,
                        int preimpostato, uint32_t timeout_ms)
{
  scartaInputPendente();
  Serial.printf("%s\n", domanda);
  for (int i = 0; i < quante; ++i)
    Serial.printf("   %d) %s%s\n", i + 1, voci[i], i == preimpostato ? "   <- attuale" : "");
  Serial.printf("  [1-%d, %lu s, invio = %d]\n",
                quante, (unsigned long)(timeout_ms / 1000), preimpostato + 1);

  char buf[8];
  const int n = leggiRiga(buf, sizeof(buf), timeout_ms);
  int scelta = preimpostato;
  if (n > 0)
  {
    const int v = atoi(buf);
    if (v >= 1 && v <= quante) scelta = v - 1;
    else Serial.println(F("  fuori range: tengo l'attuale"));
  }
  Serial.printf("  risposta: %s%s\n", voci[scelta],
                n < 0 ? "  (nessuna risposta, preso l'attuale)" : "");
  return scelta;
}

/**
 * Domanda numerica con limiti. Senza risposta entro il timeout, o con una
 * risposta fuori range, tiene il preimpostato: i limiti li impone il menu,
 * perchè da un campo numerico libero si esce facilmente con una
 * configurazione che non misura niente.
 */
static long chiediNumero(const char* domanda, long minimo, long massimo,
                         long preimpostato, uint32_t timeout_ms)
{
  scartaInputPendente();
  Serial.printf("%s [%ld..%ld, %lu s, invio = %ld]\n",
                domanda, minimo, massimo, (unsigned long)(timeout_ms / 1000), preimpostato);
  char buf[16];
  const int n = leggiRiga(buf, sizeof(buf), timeout_ms);
  long v = preimpostato;
  if (n > 0)
  {
    char* fine = nullptr;
    const long letto = strtol(buf, &fine, 0);
    if (fine == buf)                            Serial.println(F("  non è un numero: tengo l'attuale"));
    else if (letto < minimo || letto > massimo) Serial.println(F("  fuori range: tengo l'attuale"));
    else v = letto;
  }
  Serial.printf("  risposta: %ld%s\n", v,
                n < 0 ? "  (nessuna risposta, preso l'attuale)" : "");
  return v;
}

// ---------------------------------------------------------------------------
// CONFIGURAZIONE PERSISTENTE E MENU
// ---------------------------------------------------------------------------

static Preferences prefs;

/** Nome di ogni sonda, per il menu e per il riepilogo della selezione. */
struct VoceSonda { uint16_t bit; const char* nome; };
static const VoceSonda SONDE_NOMI[] =
{
  { S_CANDIDATE, "candidate di init (elettrico)" },
  { S_BIT7,      "bit 7 dell'opcode (elettrico)" },
  { S_MUX,       "sweep del MUX, 3 refresh" },
  { S_CLOCK,     "costo del push ai tre clock" },
  { S_IDENT,     "frame di identificazione, 1 per candidata" },
  { S_POLARITA,  "polarità del piano BW, 2 refresh" },
  { S_BANDE,     "4 bande dei piani più i box, 1 refresh" },
  { S_AREA,      "partial d'area, 5-7 refresh" },
  { S_DIFF,      "differenziale approfondita, 3-6 refresh" },
  { S_LUT,       "partial con LUT custom, 2 refresh" },
  { S_LIVELLI,   "quarto colore, 2 refresh" },
  { S_SLAVE,     "secondo controller |0x80, 1 refresh" },
  { S_TEMP,      "banchi per temperatura, 4 refresh" },
  { S_PINGPONG,  "RAM ping-pong 0x37, 1 refresh" },
  { S_SLEEP,     "deep sleep, 3 refresh" },
  { S_DRIVER,    "fase driver, 1-2 refresh" },
};
static const uint8_t SONDE_N = sizeof(SONDE_NOMI) / sizeof(SONDE_NOMI[0]);

/**
 * Etichetta di un valore del MUX, composta dal numero: i confronti col valore
 * atteso non valgono più da quando i tre valori si scelgono dal menu.
 */
static const char* muxEtichetta(uint8_t k)
{
  static char buf[MUX_PASSES][32];
  if (cfg.mux[k] == MUX_NOT_WRITTEN)
    snprintf(buf[k], sizeof(buf[k]), "non scritto (default OTP)");
  else
    snprintf(buf[k], sizeof(buf[k]), "= %u gate line", (unsigned)cfg.mux[k]);
  return buf[k];
}

/** Refresh previsti con la configurazione corrente, per il banner. */
static int refreshPrevisti()
{
  int n = 0;
  if (cfg.sonde & S_MUX)      n += MUX_PASSES;
  if (cfg.sonde & S_IDENT)    n += 3;
  if (cfg.sonde & S_POLARITA) n += 2;
  if (cfg.sonde & S_BANDE)    n += 1;
  if (cfg.sonde & S_AREA)     n += cfg.esaustivo ? 7 : 5;
  if (cfg.sonde & S_DIFF)     n += cfg.esaustivo ? 6 : 3;
  if (cfg.sonde & S_LUT)      n += 2;
  if (cfg.sonde & S_LIVELLI)  n += 2;
  if (cfg.sonde & S_SLAVE)    n += cfg.secondFfc ? 1 : 0;
  if (cfg.sonde & S_TEMP)     n += TEMP_PASSES;
  if (cfg.sonde & S_PINGPONG) n += 1;
  if (cfg.sonde & S_SLEEP)    n += 3;   // sordità, risveglio e frame di prova
  if (cfg.sonde & S_DRIVER)   n += cfg.secondFfc ? 2 : 1;
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

static void caricaConfig()
{
  prefs.begin("dpf", true);
  cfg.coda       = prefs.getUChar("coda", cfg.coda);
  cfg.secondFfc  = prefs.getBool("ffc2", cfg.secondFfc);
  cfg.esaustivo  = prefs.getBool("esaus", cfg.esaustivo);
  cfg.bwPolarity = prefs.getChar("bwpol", cfg.bwPolarity);
  cfg.pausaMs    = prefs.getULong("pausa", cfg.pausaMs);
  cfg.sonde      = prefs.getUShort("sonde", cfg.sonde);
  cfg.spiBase    = prefs.getULong("spib", cfg.spiBase);
  cfg.spiFast    = prefs.getULong("spif", cfg.spiFast);
  cfg.spiDriver  = prefs.getULong("spid", cfg.spiDriver);
  cfg.timeoutMs  = prefs.getULong("tmout", cfg.timeoutMs);
  cfg.sleepMs    = prefs.getULong("sleep", cfg.sleepMs);
  prefs.getBytes("temp", cfg.temp, sizeof(cfg.temp));
  prefs.getBytes("mux",  cfg.mux,  sizeof(cfg.mux));
  prefs.end();
}

static void salvaConfig()
{
  prefs.begin("dpf", false);
  prefs.putUChar ("coda",  cfg.coda);
  prefs.putBool  ("ffc2",  cfg.secondFfc);
  prefs.putBool  ("esaus", cfg.esaustivo);
  prefs.putChar  ("bwpol", cfg.bwPolarity);
  prefs.putULong ("pausa", cfg.pausaMs);
  prefs.putUShort("sonde", cfg.sonde);
  prefs.putULong ("spib",  cfg.spiBase);
  prefs.putULong ("spif",  cfg.spiFast);
  prefs.putULong ("spid",  cfg.spiDriver);
  prefs.putULong ("tmout", cfg.timeoutMs);
  prefs.putULong ("sleep", cfg.sleepMs);
  prefs.putBytes ("temp", cfg.temp, sizeof(cfg.temp));
  prefs.putBytes ("mux",  cfg.mux,  sizeof(cfg.mux));
  prefs.end();
}

/** Etichetta della polarità per il menu e per il report. */
static const char* polaritaLabel()
{
  switch (cfg.bwPolarity)
  {
    case BW_DATASHEET: return "bit=1 bianco (osservata)";
    case BW_INVERSE:   return "bit=1 NERO, piano invertito (osservata)";
    default:           return "da misurare (per ora assumo bit=1 bianco)";
  }
}

/** Sottomenu della selezione delle sonde. */
static void menuSonde()
{
  while (true)
  {
    Serial.println(F("\n  sonde da eseguire:"));
    for (uint8_t i = 0; i < SONDE_N; ++i)
      Serial.printf("   %2u) [%c] %s\n", i + 1,
                    (cfg.sonde & SONDE_NOMI[i].bit) ? 'x' : ' ', SONDE_NOMI[i].nome);
    Serial.println(F("    t) tutte      n) nessuna      0) torna al menu"));
    Serial.println(F("  [numero per invertire una voce, 15 s]"));

    char buf[8];
    if (leggiRiga(buf, sizeof(buf), 15000) <= 0) return;
    if (buf[0] == 't' || buf[0] == 'T') { cfg.sonde = S_TUTTE; continue; }
    if (buf[0] == 'n' || buf[0] == 'N') { cfg.sonde = 0;       continue; }
    const int v = atoi(buf);
    if (v == 0) return;
    if (v >= 1 && v <= SONDE_N) cfg.sonde ^= SONDE_NOMI[v - 1].bit;
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
    Serial.printf ("   2) gate line del MUX     %s, %s, %s\n",
                   muxEtichetta(0), muxEtichetta(1), muxEtichetta(2));
    Serial.printf ("   3) clock delle sonde     %lu Hz\n", (unsigned long)cfg.spiBase);
    Serial.printf ("   4) clock del frame veloce %lu Hz\n", (unsigned long)cfg.spiFast);
    Serial.printf ("   5) clock della fase driver %lu Hz\n", (unsigned long)cfg.spiDriver);
    Serial.printf ("   6) timeout di un refresh %lu ms\n", (unsigned long)cfg.timeoutMs);
    Serial.printf ("   7) finestra del multimetro %lu s\n",
                   (unsigned long)(cfg.sleepMs / 1000));
    Serial.println(F("   8) riporta i valori di fabbrica"));
    Serial.println(F("   0) torna al menu"));
    Serial.println(F("  [numero, 15 s]"));

    char buf[8];
    if (leggiRiga(buf, sizeof(buf), 15000) <= 0) return;
    switch (atoi(buf))
    {
      case 1:
        for (uint8_t t = 0; t < TEMP_PASSES; ++t)
        {
          char d[64];
          snprintf(d, sizeof(d), "    punto %u%s, gradi", t + 1,
                   (t == TEMP_PASSES - 1) ? " (controllo: mettilo fuori da 0..40)" : "");
          cfg.temp[t] = (int16_t)chiediNumero(d, -128, 127, cfg.temp[t], 15000);
        }
        break;
      case 2:
        for (uint8_t m = 0; m < MUX_PASSES; ++m)
        {
          char d[52];
          snprintf(d, sizeof(d), "    valore %u, gate line (0 = non scritto)", m + 1);
          cfg.mux[m] = (uint16_t)chiediNumero(d, 0, 680, cfg.mux[m], 15000);
        }
        break;
      case 3: cfg.spiBase = (uint32_t)chiediNumero("    clock delle sonde, Hz", 100000,
                                                   40000000, cfg.spiBase, 15000); break;
      case 4: cfg.spiFast = (uint32_t)chiediNumero("    clock del frame veloce, Hz", 100000,
                                                   40000000, cfg.spiFast, 15000); break;
      case 5: cfg.spiDriver = (uint32_t)chiediNumero("    clock della fase driver, Hz", 100000,
                                                     40000000, cfg.spiDriver, 15000); break;
      case 6: cfg.timeoutMs = (uint32_t)chiediNumero("    timeout di un refresh, ms", 1000,
                                                     300000, cfg.timeoutMs, 15000); break;
      case 7: cfg.sleepMs = (uint32_t)chiediNumero("    finestra del multimetro, s", 0, 600,
                                                   cfg.sleepMs / 1000, 15000) * 1000UL; break;
      case 8:
        memcpy(cfg.temp, CFG_DEFAULT.temp, sizeof(cfg.temp));
        memcpy(cfg.mux,  CFG_DEFAULT.mux,  sizeof(cfg.mux));
        cfg.spiBase   = CFG_DEFAULT.spiBase;
        cfg.spiFast   = CFG_DEFAULT.spiFast;
        cfg.spiDriver = CFG_DEFAULT.spiDriver;
        cfg.timeoutMs = CFG_DEFAULT.timeoutMs;
        cfg.sleepMs   = CFG_DEFAULT.sleepMs;
        Serial.println(F("  parametri riportati ai valori di fabbrica"));
        break;
      case 0: return;
      default: break;
    }
    salvaConfig();
  }
}

/**
 * Menu al boot. Senza risposta entro il timeout parte il profilo salvato,
 * quindi un run non presidiato si comporta come prima.
 */
static void menuIniziale()
{
  static const char* const VOCI_CODA[] = { "coda LUNGA", "coda CORTA" };
  static const char* const VOCI_POL[]  = { "da misurare", "bit=1 bianco (datasheet)",
                                           "bit=1 nero (inversa)" };
  while (true)
  {
    Serial.println(F("\n--- configurazione del run ---"));
    Serial.printf("   1) sonde ................ %u di %u selezionate\n",
                  (unsigned)__builtin_popcount(cfg.sonde), (unsigned)SONDE_N);
    Serial.printf("   2) coda ................. %s\n",
                  cfg.coda == TAIL_LUNGA ? "LUNGA" : "CORTA");
    Serial.printf("   3) secondo connettore FFC %s\n", cfg.secondFfc ? "cablato" : "non cablato");
    Serial.printf("   4) esaustivo ............ %s\n", cfg.esaustivo ? "sì" : "no");
    Serial.printf("   5) polarità del piano BW  %s\n", polaritaLabel());
    Serial.printf("   6) pause ................ %lu s\n", (unsigned long)(cfg.pausaMs / 1000));
    Serial.printf("   7) parametri di misura .. %d/%d/%d/%d gradi, MUX %u/%u/%u, %lu MHz\n",
                  (int)cfg.temp[0], (int)cfg.temp[1], (int)cfg.temp[2], (int)cfg.temp[3],
                  (unsigned)cfg.mux[0], (unsigned)cfg.mux[1], (unsigned)cfg.mux[2],
                  (unsigned long)(cfg.spiBase / 1000000UL));
    Serial.printf("   8) riporta tutto ai valori di fabbrica\n");
    Serial.printf("   0) parti  (%d refresh previsti)\n", refreshPrevisti());
    Serial.printf("  [numero, 10 s, invio = parti]\n");

    char buf[8];
    const int scelto = leggiRiga(buf, sizeof(buf), 10000);
    if (scelto <= 0)
    {
      if (scelto < 0) Serial.println(F("  nessuna risposta: parto col profilo salvato"));
      else            Serial.println(F("  parto col profilo salvato"));
      return;
    }
    switch (atoi(buf))
    {
      case 1: menuSonde(); break;
      case 2: cfg.coda = (chiediScelta("  quale coda è infilata nel connettore?", VOCI_CODA, 2,
                                       cfg.coda == TAIL_LUNGA ? 0 : 1, 15000) == 0)
                         ? TAIL_LUNGA : TAIL_CORTA; break;
      case 3: cfg.secondFfc = chiediSN("  il secondo connettore FFC è cablato?",
                                       cfg.secondFfc, 15000); break;
      case 4: cfg.esaustivo = chiediSN("  eseguire anche le passate condizionali?",
                                       cfg.esaustivo, 15000); break;
      case 5: cfg.bwPolarity = (int8_t)(chiediScelta("  polarità del piano BW", VOCI_POL, 3,
                                        cfg.bwPolarity + 1, 15000) - 1); break;
      case 6: {
        static const char* const VOCI_PAUSA[] = { "0 s (non presidiato)", "10 s", "30 s", "60 s" };
        static const uint32_t MS[] = { 0, 10000, 30000, 60000 };
        int pre = 1;
        for (int i = 0; i < 4; ++i) if (MS[i] == cfg.pausaMs) pre = i;
        cfg.pausaMs = MS[chiediScelta("  durata massima di una pausa", VOCI_PAUSA, 4, pre, 15000)];
        break;
      }
      case 7: menuParametri(); break;
      case 8: prefs.begin("dpf", false); prefs.clear(); prefs.end();
              cfg = CFG_DEFAULT;   // il salvataggio a fine ciclo riscrive i valori di fabbrica
              Serial.println(F("  profilo riportato ai valori di fabbrica")); break;
      case 0: salvaConfig(); return;
      default: break;
    }
    salvaConfig();
  }
}

// ---------------------------------------------------------------------
// PIN — Waveshare E-Paper ESP32 Driver Board V3
//   Sono il cablaggio, non impostazioni: si cambiano solo se il tuo è
//   diverso da quello di docs/122c/connessioni.html.
// ---------------------------------------------------------------------
// Segnali condivisi. La board usa i pin HSPI con SCK e MOSI scambiati, da cui
// il remap obbligatorio in hspi.begin(). MISO è un dummy: il FPC del
// connettore non ha SDO.
static const int PIN_DC   = 27;
static const int PIN_RST  = 26;
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;

// Il connettore FFC della board.
static const int PIN_CS   = 15;
static const int PIN_BUSY = 25;

/**
 * Il secondo connettore, usato quando cfg.secondFfc è vero.
 *
 * CS su GPIO32 e non 33: il 33 non è portato fuori su questa board, quindi un
 * digitalWrite su quel pin non arriva da nessuna parte.
 *
 * BUSY su 35 vale solo come lettura grezza: i GPIO 34..39 dell'ESP32 non hanno
 * pull interni, quindi su quel pin la prova "pilotato o flottante" non può
 * funzionare — ed è proprio la prova che servirebbe sulla coda che non
 * risponde, per questo il secondo connettore si dichiara dal menu e non si
 * autorileva. Per averla, portalo su 4, 21 o 22.
 */
static const int PIN_CS_OTHER   = 32;
static const int PIN_BUSY_OTHER = 35;

// ===== Costanti del pannello e del silicio, non impostazioni =========
// Sono fatti misurati: cambiarli qui non riconfigura la prova, la falsa.

/** true se il piano BW è invertito rispetto alla convenzione del datasheet. */
static inline bool bwIsInverse()
{
  return (cfg.bwPolarity == BW_INVERSE);
}

/** Byte da scrivere in 0x24 per ottenere bianco (white=true) o nero. */
static inline uint8_t bwByteFor(bool white)
{
  return (white != bwIsInverse()) ? 0xFF : 0x00;
}

/** Parametro di 0x47 (Auto Write B/W pattern) per bianco o nero. */
static inline uint8_t bwPatternFor(bool white)
{
  return (white != bwIsInverse()) ? 0xF7 : 0x77;
}

static const uint16_t SRC       = 960;           // source sull'asse RAM X
static const uint16_t ROW_BYTES = SRC / 8;       // 120 byte per riga piena
static const uint16_t PART_ROW_MAX = ROW_BYTES;  // buffer di riga più grande possibile

// Sul SSD16xx il BUSY è attivo alto, come il busy_level = HIGH del driver
static const int BUSY_ACTIVE = HIGH;

// Gate line per controller, misurate: il pattern le marca per renderle
// riconoscibili, e sono anche l'altezza della finestra RAM di ogni scrittura.
static const uint16_t EXPECTED_BAND = 384;

/**
 * Clock SPI usati dal test. Non sono impostazioni: sono i tre valori che
 * interessano al driver, e il test li prova tutti da sè.
 *   BASE    clock di lavoro della diagnostica, con margine per il bring-up
 *   DRIVER  quello che il driver custom usa per default
 *   FAST    il massimo dichiarato in scrittura dal datasheet SSD1677
 * Il frame di identificazione scrive metà banda a BASE e metà a FAST, così
 * l'integrità ai due clock si confronta guardando un solo schermo.
 */

/**
 * Finestra ferma in deep sleep per la misura di corrente col multimetro:
 * l'unica cosa che il firmware non può misurare da sè.
 */

// ===== Le tre candidate di init, come dato e non come #define ========
/**
 * Erano tre ricompilazioni, ora sono un array su cui il test itera. Quello che
 * cambia fra loro è quali registri l'init scrive; il pannello sta sullo stesso
 * silicio, quindi la domanda è quale sequenza il driver deve tenere.
 *
 *   CAND_MINIMAL  stile GxEPD2_1160c_GDEY116Z91: SWRESET e border, tutto il
 *                 resto ai default OTP. Il MUX non lo scrive.
 *   CAND_SOLUM    stile del driver 9.7" di questa libreria e di GxEPD2_1330c:
 *                 soft start, MUX, border, sensore, entry mode. È quella che il
 *                 driver 12.2" implementa in _InitDisplay().
 *   CAND_OEPL     init di fabbrica OEPL per il 9.7" SOLUM: parte dai pattern
 *                 hardware e chiude con 0x21 = 0x08 0x00, che in quel codice
 *                 raddrizza un'immagine altrimenti ribaltata. È la candidata
 *                 che può togliere il mirror dal data path del driver.
 */
enum InitCand : uint8_t
{
  CAND_MINIMAL = 0,
  CAND_SOLUM   = 1,
  CAND_OEPL    = 2,
  CAND_COUNT   = 3
};

static const char* const CAND_LABEL[CAND_COUNT] =
{
  "CAND_MINIMAL (SWRESET + border, resto ai default OTP)",
  "CAND_SOLUM   (soft start + MUX + border + sensore + entry mode)",
  "CAND_OEPL    (pattern hardware + MUX + 0x21 = 08 00)"
};

// La candidata su cui il driver custom è modellato: è quella che il test usa
// per i frame che non stanno spazzando le candidate.
static const uint8_t CAND_DRIVER = CAND_SOLUM;


// ===== Stato dello sweep, scritto dall'orchestrazione ================
// Sono le variabili che prima erano #define: la candidata di init in corso, il
// MUX che initPanel() programma, e il clock del prossimo push.
static uint8_t  g_cand  = CAND_DRIVER;
static uint16_t g_mux   = EXPECTED_BAND;

// ===== Oggetti di bus ================================================

SPIClass hspi(HSPI);
// Non const: il frame di verifica la riassegna a cfg.spiFast.
static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);   // riassegnata dal menu

// Tempi raccolti lungo il test, riassunti in coda
static int32_t  refreshMs = -1;
static bool     otherBusyMoved = false;   // BUSY dell'altra coda mosso durante un refresh
static int32_t  patternMs47 = -1;   // pattern hardware sul piano B/N
static int32_t  patternMs46 = -1;   // pattern hardware sull'accent
static int32_t  hvDetectMs = -1;    // BUSY della HV Ready Detection
static int32_t  vciDetectMs = -1;   // BUSY della VCI Detection
static int16_t  statusRead = -1;    // status 0x2F, se la lettura è valida
static int32_t  bandsRefreshMs = -1;  // refresh del frame delle bande e dei box
static int32_t  driverTilesMs = -1;   // refresh del frame della fase driver
static int32_t  powerOnMs = -1;       // BUSY del power on 0xC0
static int32_t  powerOffMs = -1;      // BUSY del power off 0xC3
static bool     busyStuckAtRest = false;  // BUSY già alto prima di ogni comando
static int32_t  slaveOpcodeMs = -1;   // refresh della sonda a video a opcode|0x80
static bool     sleepBusy03 = false;  // BUSY dopo 0x10 = 0x03
static bool     sleepBusy11 = false;  // BUSY dopo 0x10 = 0x11
static uint8_t  sleepParamOk = 0x00;  // parametro di 0x10 che addormenta, 0 se nessuno
static bool     sleepIgnoresCmd = false;  // da addormentato ignora anche un refresh
static int32_t  wakeInitMs = -1;      // reset hardware più init dopo il sonno
static int32_t  wakeRefreshMs = -1;   // refresh di prova dopo il risveglio

/**
 * Font 5x7 per le etichette dei righelli e per la lettera della coda. Una riga
 * per elemento, bit 4 = colonna più a sinistra. Scala e offset X sono sempre
 * multipli di 8, così ogni colonna del font copre byte RAM interi e la
 * composizione delle righe non richiede mascheramento a livello di bit.
 */
static const uint8_t GLYPH_W = 5;
static const uint8_t GLYPH_H = 7;

static const uint8_t GLYPHS[12][GLYPH_H] =
{
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
  { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },   // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   // 5
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   // 6
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   // 7
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   // 9
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   // C
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   // L
};

static const uint8_t GLYPH_C = 10;
static const uint8_t GLYPH_L = 11;

// --- primitive di bus ------------------------------------------------

/**
 * Offset sommato a ogni opcode. Vale 0 per tutto il test tranne che dentro la
 * sonda del secondo controller, dove diventa CMD_OFFSET: è il modo in cui
 * il firmware di fabbrica distingue i due controller sullo stesso bus. Va
 * sempre riportato a 0 prima di uscire da quella sonda, altrimenti anche le
 * letture di registro partirebbero offset.
 */
static uint8_t cmdOffset = 0x00;

/**
 * Con csBoth attivo il chip select dell'altra coda segue quello della coda
 * sotto test, così i due controller vedono lo stesso bus come nel cablaggio di
 * fabbrica. Fuori dalla sonda resta false e l'altra coda sta alta, cioè muta.
 */
static bool csBoth = false;

static inline void csAssert()
{
  digitalWrite(PIN_CS, LOW);
  if (csBoth)
    digitalWrite(PIN_CS_OTHER, LOW);
}

static inline void csRelease()
{
  digitalWrite(PIN_CS, HIGH);
  if (csBoth)
    digitalWrite(PIN_CS_OTHER, HIGH);
}

/**
 * Invia un byte di comando: D/C basso. Ordine delle operazioni identico a
 * GxEPD2_EPD::_writeCommand, D/C riportato alto in coda incluso.
 */
static void writeCommand(uint8_t c)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_DC, LOW);
  csAssert();
  hspi.transfer(c | cmdOffset);
  csRelease();
  digitalWrite(PIN_DC, HIGH);
  hspi.endTransaction();
}

// Invia un byte di parametro: D/C alto, come GxEPD2_EPD::_writeData
static void writeData(uint8_t d)
{
  hspi.beginTransaction(spiSettings);
  csAssert();
  hspi.transfer(d);
  csRelease();
  hspi.endTransaction();
}

/** Attende la discesa del BUSY. Ritorna i ms attesi, oppure -1 al timeout. */
static int32_t waitBusy(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
  {
    if ((millis() - t0) > timeout_ms)
      return -1;
    delay(1);
  }
  return (int32_t)(millis() - t0);
}

static void resetPanel()
{
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
}

// Finestra RAM e cursore. Stessa sequenza di comandi del driver custom.
/**
 * Finestra RAM in vigore. Il riquadro col numero della schermata la restringe
 * per un istante e deve rimetterla com'era: è la finestra presente alla master
 * activation a definire l'area che il refresh percorre.
 */
static uint16_t ramWinX = 0, ramWinY = 0, ramWinW = SRC, ramWinH = EXPECTED_BAND;

static void setRamWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  ramWinX = x;
  ramWinY = y;
  ramWinW = w;
  ramWinH = h;
  writeCommand(0x44);
  writeData(x % 256);
  writeData(x / 256);
  writeData((x + w - 1) % 256);
  writeData((x + w - 1) / 256);
  writeCommand(0x45);
  writeData(y % 256);
  writeData(y / 256);
  writeData((y + h - 1) % 256);
  writeData((y + h - 1) / 256);
  writeCommand(0x4E);
  writeData(x % 256);
  writeData(x / 256);
  writeCommand(0x4F);
  writeData(y % 256);
  writeData(y / 256);
}

// ---------------------------------------------------------------------------
// SCHERMATE NUMERATE
//
// Il numero della schermata lega il log al vetro: con una trentina di refresh e
// una decina di sonde che riusano le stesse fasce, è l'unico modo di sapere
// quale blocco di log commenta quello che si sta guardando.
// ---------------------------------------------------------------------------

/**
 * Le schermate da guardare, nell'ordine in cui il test le produce. Il numero
 * stampato lo assegna apriSchermata() al volo, così una sonda che si salta da
 * sè non consuma un numero e nella sequenza non restano buchi.
 */
enum Schermata : uint8_t
{
  SCH_IDENT_0,     // frame di identificazione, candidata 0
  SCH_IDENT_1,     // candidata 1
  SCH_IDENT_2,     // candidata 2
  SCH_POL_1,       // polarità BW, frame 1: 0x21 al POR
  SCH_POL_2,       // polarità BW, frame 2: 0x21 = 08 00
  SCH_BANDE,       // 4 bande dei piani più i box a finestra parziale
  SCH_AREA,        // partial d'area
  SCH_DIFF_DEEP,   // differenziale approfondita, le passate che dipingono
  SCH_LUT,         // partial con LUT caricata via 0x32
  SCH_LIVELLI,     // quarto colore per livello di sorgente
  SCH_SLAVE,       // secondo controller a opcode|0x80
  SCH_TEMP,        // banchi di waveform per temperatura
  SCH_PINGPONG,    // RAM ping-pong via 0x37
  SCH_SLEEP,       // refresh di prova dopo il risveglio dal deep sleep
  SCH_DRIVER_1,    // fase driver, primo modello di indirizzamento
  SCH_DRIVER_2,    // fase driver, secondo modello
  SCH_COUNT
};

/** Numero di ogni schermata, 0 se la sonda non è arrivata a produrla: lo cita
 *  la scheda finale. */
static uint8_t schermate[SCH_COUNT] = { 0 };

static uint8_t schermataCorrente = 0;   // schermata aperta, 0 = nessuna
static uint8_t numeroSulVetro    = 0;   // ultimo riquadro disegnato
static bool    riquadroSulVetro  = true;   // la schermata aperta porta il riquadro

/**
 * Apre una schermata: assegna il numero successivo, che fino a
 * chiudiSchermata() ogni refresh disegna sul vetro e ogni riga di osservazione
 * stampa in testa.
 *
 * conRiquadro a false lascia il numero al solo log. Serve dove disegnare il
 * riquadro cambierebbe la misura: nella sonda dello slave la RAM viene scritta
 * con gli opcode offset, mentre il riquadro passerebbe da opcode nudi e
 * finirebbe sul master, sporcandone la banda proprio nell'esperimento che deve
 * dire quale controller ha eseguito.
 */
static void apriSchermata(Schermata quale, bool conRiquadro = true)
{
  static uint8_t assegnati = 0;
  schermataCorrente = ++assegnati;
  riquadroSulVetro  = conRiquadro;
  schermate[quale]  = schermataCorrente;
}

/** Chiude la schermata: i refresh successivi non portano più il riquadro. */
static void chiudiSchermata()
{
  schermataCorrente = 0;
  riquadroSulVetro  = true;
}

/**
 * Numero in testa alle righe: la schermata aperta se c'è, altrimenti l'ultimo
 * riquadro disegnato, cioè quello che in questo momento sta sul vetro. Il
 * secondo caso serve ai blocchi che commentano una schermata già chiusa.
 */
static uint8_t numeroDiRiga()
{
  return schermataCorrente ? schermataCorrente : numeroSulVetro;
}

// --- il riquadro sul vetro -------------------------------------------------

static const uint8_t BADGE_BORDO     = 2;   // spessore della cornice, in px
static const uint8_t BADGE_ARIA      = 3;   // aria fra cornice e cifre
static const uint8_t BADGE_MARGINE   = 8;   // distanza dal bordo della finestra
static const uint8_t BADGE_SCALA_MAX = 6;   // cifre 30x42 px
static const uint8_t BADGE_SCALA_MIN = 2;   // cifre 10x14 px, il minimo leggibile

// Ingombro massimo: tre cifre al corpo massimo, arrotondate al byte
static const uint8_t BADGE_MAX_H     = GLYPH_H * BADGE_SCALA_MAX
                                       + 2 * (BADGE_BORDO + BADGE_ARIA);
static const uint8_t BADGE_MAX_BYTES = 16;

/**
 * Righe del riquadro, contigue e con stride pari alla sua larghezza in byte:
 * così lo stesso buffer serve sia al push SPI della fase probe sia a
 * writeImage() della fase driver, che pretende righe contigue.
 */
static uint8_t badgeBuf[BADGE_MAX_H * BADGE_MAX_BYTES];
static uint8_t badgeStride = 0;

/** Prima riga del riquadro nel buffer. */
static inline uint8_t* badgeRiga(uint8_t r)
{
  return badgeBuf + (uint16_t)r * badgeStride;
}

/**
 * Accende n pixel consecutivi a partire da x. Il riquadro è l'unica cosa in
 * questo sketch che lavora a livello di BIT: paintSpan() riempie byte interi,
 * quindi il corpo minimo dei suoi glifi è 8 e un riquadro così non entrerebbe
 * nelle finestre basse della sonda d'area.
 */
static void badgeSpan(uint8_t* row, uint16_t x, uint8_t n, uint8_t fg)
{
  for (uint8_t i = 0; i < n; ++i)
  {
    const uint16_t px = x + i;
    if (px >= (uint16_t)BADGE_MAX_BYTES * 8) return;
    const uint8_t m = (uint8_t)(0x80 >> (px % 8));
    uint8_t& b = row[px / 8];
    b = (uint8_t)((b & ~m) | (fg & m));
  }
}

/**
 * Spinge il riquadro su un piano: le righe di badgeRows sul piano B/N, accent
 * spento sul piano 0x26. Passa da csAssert/csRelease come writePlane, così
 * onora csBoth e finisce su entrambi i controller quando i due CS sono uniti.
 */
static void pushBadgePiano(uint8_t plane, uint16_t bx, uint16_t by,
                           uint8_t bw, uint8_t bh, bool cifre)
{
  static const uint8_t spento[BADGE_MAX_BYTES] = { 0 };
  const uint8_t nbyte = (uint8_t)(bw / 8);

  setRamWindow(bx, by, bw, bh);
  writeCommand(plane);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  csAssert();
  for (uint8_t r = 0; r < bh; ++r)
    hspi.writeBytes(cifre ? badgeRiga(r) : spento, nbyte);
  csRelease();
  hspi.endTransaction();
}

/**
 * Compone in badgeBuf il riquadro col numero della schermata: cifre nere su
 * fondo bianco con cornice. Il corpo è il più grande che entra in maxW x maxH,
 * da 30x42 px a 10x14; sotto quello ritorna false.
 *
 * bianco e nero sono parametri perchè i due chiamanti hanno convenzioni
 * diverse: la fase probe passa bwByteFor(), così il riquadro resta leggibile
 * anche se BW_POLARITY risultasse inversa, mentre la fase driver passa le
 * costanti della convenzione di GxEPD2.
 */
static bool componiBadge(uint8_t numero, uint16_t maxW, uint16_t maxH,
                         uint8_t bianco, uint8_t nero, uint8_t* bwOut, uint8_t* bhOut)
{
  char testo[4];
  const int len = snprintf(testo, sizeof(testo), "%u", (unsigned)numero);

  uint8_t scala = 0;
  uint8_t bw = 0, bh = 0;
  for (uint8_t sc = BADGE_SCALA_MAX; sc >= BADGE_SCALA_MIN; --sc)
  {
    const uint16_t testoW = (uint16_t)(len * (GLYPH_W + 1) - 1) * sc;
    const uint16_t larga  = (uint16_t)((testoW + 2 * (BADGE_BORDO + BADGE_ARIA) + 7) & ~7);
    const uint16_t alta   = (uint16_t)(GLYPH_H * sc + 2 * (BADGE_BORDO + BADGE_ARIA));
    if (larga + BADGE_MARGINE > maxW) continue;
    if (alta > maxH) continue;
    scala = sc;
    bw = (uint8_t)larga;
    bh = (uint8_t)alta;
    break;
  }
  if (scala == 0)
    return false;

  badgeStride = (uint8_t)(bw / 8);

  // fondo bianco, cornice nera sui quattro lati
  for (uint8_t r = 0; r < bh; ++r)
  {
    uint8_t* row = badgeRiga(r);
    if (r < BADGE_BORDO || r >= bh - BADGE_BORDO)
      memset(row, nero, badgeStride);
    else
    {
      memset(row, bianco, badgeStride);
      badgeSpan(row, 0, BADGE_BORDO, nero);
      badgeSpan(row, (uint16_t)(bw - BADGE_BORDO), BADGE_BORDO, nero);
    }
  }

  // cifre centrate nel riquadro, stessi glifi 5x7 delle fasce
  const uint16_t testoW = (uint16_t)(len * (GLYPH_W + 1) - 1) * scala;
  const uint16_t x0 = (uint16_t)((bw - testoW) / 2);
  const uint8_t  y0 = (uint8_t)((bh - GLYPH_H * scala) / 2);
  for (int k = 0; k < len; ++k)
  {
    const uint8_t* g = GLYPHS[testo[k] - '0'];
    for (uint8_t fr = 0; fr < GLYPH_H; ++fr)
      for (uint8_t sy = 0; sy < scala; ++sy)
      {
        uint8_t* row = badgeRiga((uint8_t)(y0 + fr * scala + sy));
        for (uint8_t c = 0; c < GLYPH_W; ++c)
          if (g[fr] & (0x10 >> c))
            badgeSpan(row, (uint16_t)(x0 + ((uint16_t)k * (GLYPH_W + 1) + c) * scala),
                      scala, nero);
      }
  }

  *bwOut = bw;
  *bhOut = bh;
  return true;
}

/**
 * Disegna il riquadro in alto a destra della finestra RAM corrente e rimette la
 * finestra com'era: è quella a decidere l'area ridipinta. Chiamata da
 * runRefresh() a ogni passata di una schermata aperta.
 */
static void disegnaBadge(uint8_t numero)
{
  uint8_t bw = 0, bh = 0;
  if (!componiBadge(numero, ramWinW, ramWinH, bwByteFor(true), bwByteFor(false), &bw, &bh))
  {
    Serial.printf("   finestra %ux%u troppo piccola per il riquadro della schermata %u:\n"
                  "   questa passata la riconosci dalla cifra sulla fascia\n",
                  (unsigned)ramWinW, (unsigned)ramWinH, (unsigned)numero);
    return;
  }

  // in alto a destra della finestra, allineato al byte sull'asse source
  uint16_t bx = (uint16_t)((ramWinX + ramWinW - bw - BADGE_MARGINE) & ~7);
  if (bx < ramWinX) bx = ramWinX;
  const uint16_t scarto = (uint16_t)(ramWinH - bh);
  const uint16_t by = (uint16_t)(ramWinY + (scarto > 8 ? 8 : scarto));

  const uint16_t salvaX = ramWinX, salvaY = ramWinY;
  const uint16_t salvaW = ramWinW, salvaH = ramWinH;
  pushBadgePiano(0x24, bx, by, bw, bh, true);
  pushBadgePiano(0x26, bx, by, bw, bh, false);
  setRamWindow(salvaX, salvaY, salvaW, salvaH);

  numeroSulVetro = numero;
}

// --- blocchi di osservazione sul seriale -----------------------------------

/** Riga di un blocco di osservazione, col numero della schermata in testa. */
static void rigaOsservazione(const char* riga)
{
  Serial.printf("[%u] %s\n", (unsigned)numeroDiRiga(), riga);
}

/** Riga formattata di un blocco di osservazione. */
static void rigaOsservazioneF(const char* fmt, ...)
{
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  rigaOsservazione(buf);
}

/** Blocco di righe già impaginate, una per elemento dell'array. */
static void righeOsservazione(const char* const* righe, uint8_t quante)
{
  for (uint8_t i = 0; i < quante; ++i)
    rigaOsservazione(righe[i]);
}

/**
 * Apre un blocco di osservazione: barra di frecce in basso, numero che in quel
 * momento sta sul vetro e titolo di cosa guardare. Le righe fino a
 * fineOsservazione() portano lo stesso numero in testa.
 */
static void inizioOsservazione(const char* titolo)
{
  const unsigned n = numeroDiRiga();
  Serial.println();
  Serial.printf("[%u] vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n", n);
  if (riquadroSulVetro)
  {
    Serial.printf("[%u] GUARDA IL PANNELLO: SCHERMATA %u, il numero sta nel riquadro\n", n, n);
    Serial.printf("[%u] in alto a destra dell'area appena ridipinta\n", n);
  }
  else
    Serial.printf("[%u] GUARDA IL PANNELLO: SCHERMATA %u, che sul vetro non porta il\n"
                  "[%u] riquadro col numero\n", n, n, n);
  Serial.printf("[%u] %s\n", n, titolo);
  Serial.printf("[%u]\n", n);
}

/** Chiude il blocco di osservazione con la barra di frecce in alto. */
static void fineOsservazione()
{
  const unsigned n = numeroDiRiga();
  Serial.printf("[%u] ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n", n);
  Serial.println();
}

/** Nome di ogni schermata, per l'indice e le voci della scheda finale. */
static const char* const NOME_SCHERMATA[SCH_COUNT] =
{
  "frame di identificazione, candidata 1",
  "frame di identificazione, candidata 2",
  "frame di identificazione, candidata 3",
  "polarità del piano BW, frame 1 (0x21 al POR)",
  "polarità del piano BW, frame 2 (0x21 = 08 00)",
  "4 bande dei piani più i box a finestra parziale",
  "partial d'area",
  "differenziale approfondita",
  "partial con LUT custom",
  "quarto colore, livelli di sorgente",
  "secondo controller a opcode|0x80",
  "banchi di waveform per temperatura",
  "RAM ping-pong via 0x37",
  "deep sleep, refresh di prova dopo il risveglio",
  "fase driver, primo modello di indirizzamento",
  "fase driver, secondo modello di indirizzamento",
};

/**
 * Voce della scheda finale: titolo col numero della schermata a cui si
 * riferisce e righe già impaginate. Una sonda che non è arrivata a produrre la
 * schermata stampa [--]; `SCH_COUNT` è la voce che non riguarda una schermata
 * e va senza numero.
 */
static void voceScheda(Schermata quale, const char* titolo,
                       const char* const* righe, uint8_t quante)
{
  const bool senzaSchermata = (quale >= SCH_COUNT);
  const uint8_t n = senzaSchermata ? 0 : schermate[quale];

  char pre[8];
  if (senzaSchermata)  snprintf(pre, sizeof(pre), "   ");
  else if (n)          snprintf(pre, sizeof(pre), "[%u]", (unsigned)n);
  else                 snprintf(pre, sizeof(pre), "[--]");

  Serial.println();
  Serial.printf("%s %s%s\n", pre, titolo,
                (!senzaSchermata && !n) ? "   (schermata non prodotta)" : "");
  for (uint8_t i = 0; i < quante; ++i)
    Serial.printf("%s   %s\n", pre, righe[i]);
}

// --- init: le tre candidate, scelte a runtime ------------------------

/**
 * Soft start: l'unico blocco che le candidate condividono senza varianti.
 */
static void writeSoftStart()
{
  writeCommand(0x0C);
  writeData(0xAE);
  writeData(0xC7);
  writeData(0xC3);
  writeData(0xC0);
  writeData(0x80);
}

/**
 * Programma il MUX, cioè quante gate line il controller scandisce. Il registro
 * 0x01 vuole (linee - 1) su 10 bit in due byte little endian, più un terzo byte
 * di direzione di scansione a 0.
 *
 * Con mux == MUX_NOT_WRITTEN il registro NON viene toccato e resta al default
 * della OTP. Non è un caso degenere: è quello che fa CAND_MINIMAL, ed è uno dei
 * tre valori dello sweep del MUX.
 */
static void writeMux(uint16_t mux)
{
  if (mux == MUX_NOT_WRITTEN) return;
  const uint16_t v = mux - 1;
  writeCommand(0x01);
  writeData(v % 256);
  writeData(v / 256);
  writeData(0x00);
}

/**
 * Reset hardware più la sequenza di init della candidata `cand`, programmando
 * `mux` gate line, cronometrando ogni passo. Il BUSY dopo lo SWRESET viene
 * osservato: la durata reale del reset interno diventa un dato invece di una
 * stima, ed è anche la prima prova che il controller ha preso un comando.
 *
 * Candidata e MUX sono parametri e non #define: le tre candidate e i tre valori
 * di MUX erano sei ricompilazioni, e ora sono due cicli dentro la stessa
 * esecuzione. Chi chiama passa anche per `g_cand` e `g_mux`, che restano lo
 * stato corrente per le funzioni che compongono i pattern e per il report.
 *
 * Ritorna i ms di BUSY dello SWRESET, -1 se il BUSY non si è mosso.
 */
static int32_t initPanel(uint8_t cand, uint16_t mux)
{
  g_cand = cand;
  g_mux  = mux;

  const uint32_t tReset = micros();
  resetPanel();
  const uint32_t usReset = micros() - tReset;

  writeCommand(0x12);   // SWRESET
  const uint32_t tSw = millis();
  uint32_t riseMs = 0;
  bool rose = false;
  while ((millis() - tSw) < 50)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      rose = true;
      riseMs = millis() - tSw;
      break;
    }
  }
  int32_t swBusy = -1;
  if (rose)
    swBusy = waitBusy(1000);
  // il controller ignora i comandi per ~100-300 ms dopo lo SWRESET
  const uint32_t swElapsed = millis() - tSw;
  if (swElapsed < 200)
    delay(200 - swElapsed);

  const uint32_t tCfg = micros();

  switch (cand)
  {
    case CAND_MINIMAL:
      /**
       * Init corta: solo il border waveform, tutto il resto ai default POR. Non
       * scrive nemmeno il MUX, di proposito: è la candidata che dice cosa fa il
       * silicio quando gli si tocca il minimo indispensabile.
       */
      writeCommand(0x3C);
      writeData(0x01);      // LUT1, bianco
      break;

    case CAND_SOLUM:
      // È la sequenza che il driver custom implementa in _InitDisplay().
      writeSoftStart();
      writeMux(mux);
      writeCommand(0x3C);   // border waveform
      writeData(0x01);
      writeCommand(0x18);   // sensore di temperatura interno
      writeData(0x80);
      writeCommand(0x11);   // entry mode: X e Y crescenti
      writeData(0x03);
      break;

    case CAND_OEPL:
      /**
       * Init di fabbrica del 9.7" SOLUM secondo OEPL. Parte dai due pattern
       * hardware, che riempiono le RAM senza passare dal bus, e chiude con
       * 0x21 = 0x08 0x00: in quel codice è il fix di un'immagine che altrimenti
       * esce ribaltata, quindi è la candidata che tocca il verso della banda.
       */
      writeCommand(0x46);   // auto write RED RAM for regular pattern
      writeData(0xF7);
      waitBusy(2000);
      writeCommand(0x47);   // auto write B/W RAM for regular pattern
      writeData(0xF7);
      waitBusy(2000);
      writeSoftStart();
      writeMux(mux);
      writeCommand(0x11);   // entry mode
      writeData(0x02);
      setRamWindow(0, 0, SRC, EXPECTED_BAND);
      writeCommand(0x3C);   // border waveform
      writeData(0x01);
      writeCommand(0x18);   // sensore di temperatura interno
      writeData(0x80);
      writeCommand(0x22);   // display update sequence, caricata ma non attivata
      writeData(0xF7);
      writeCommand(0x21);   // display update control 1
      writeData(0x08);
      writeData(0x00);
      break;
  }

  const uint32_t usCfg = micros() - tCfg;

  Serial.printf("  init %-12s reset %lu us, config %lu us, MUX %s\n",
                cand == CAND_MINIMAL ? "CAND_MINIMAL" :
                cand == CAND_SOLUM   ? "CAND_SOLUM"   : "CAND_OEPL",
                (unsigned long)usReset, (unsigned long)usCfg,
                (mux == MUX_NOT_WRITTEN) ? "non scritto (default OTP)" : "scritto");
  if (mux != MUX_NOT_WRITTEN)
    Serial.printf("       MUX = %u gate line\n", mux);
  if (!rose)
    Serial.println(F("       SWRESET: BUSY non è salito entro 50 ms"));
  else if (swBusy < 0)
    Serial.printf("       SWRESET: BUSY salito dopo %lu ms, ancora alto a 1000 ms\n",
                  (unsigned long)riseMs);
  else
    Serial.printf("       SWRESET: BUSY salito dopo %lu ms, alto per %ld ms\n",
                  (unsigned long)riseMs, (long)swBusy);
  return rose ? swBusy : -1;
}

// --- composizione del pattern ---------------------------------------

/**
 * Dipinge un intervallo orizzontale di byte interi dentro la riga. x0 e w
 * devono essere multipli di 8: tutti gli elementi del pattern sono allineati al
 * byte, quindi qui non serve mascheramento.
 */
static void paintSpan(uint8_t* row, int32_t x0, int32_t w, uint8_t value)
{
  if (w <= 0) return;
  int32_t from = x0 / 8;
  int32_t to   = (x0 + w) / 8;
  if (from < 0) from = 0;
  if (to > ROW_BYTES) to = ROW_BYTES;
  for (int32_t i = from; i < to; ++i)
    row[i] = value;
}

/**
 * Dipinge la riga y di un glifo scalato, se quella riga lo attraversa. scale
 * multiplo di 8: ogni colonna del font diventa un numero intero di byte.
 */
static void paintGlyph(uint8_t* row, int16_t y, uint8_t idx,
                       int32_t gx, int32_t gy, int32_t scale, uint8_t value)
{
  const int32_t h = (int32_t)GLYPH_H * scale;
  if (y < gy || y >= gy + h) return;
  const uint8_t bits = GLYPHS[idx][(y - gy) / scale];
  for (uint8_t c = 0; c < GLYPH_W; ++c)
  {
    if (bits & (0x10 >> c))
      paintSpan(row, gx + (int32_t)c * scale, scale, value);
  }
}

/**
 * Dipinge un numero decimale come sequenza di glifi. Ritorna la larghezza in
 * px occupata, così il chiamante sa dove finisce l'etichetta.
 */
static int32_t paintNumber(uint8_t* row, int16_t y, uint16_t n,
                           int32_t gx, int32_t gy, int32_t scale, uint8_t value)
{
  uint8_t digits[5];
  uint8_t nd = 0;
  do {
    digits[nd++] = n % 10;
    n /= 10;
  } while (n > 0 && nd < sizeof(digits));
  const int32_t advance = (int32_t)(GLYPH_W + 1) * scale;
  for (uint8_t i = 0; i < nd; ++i)
    paintGlyph(row, y, digits[nd - 1 - i], gx + i * advance, gy, scale, value);
  return nd * advance;
}

// Geometria del pattern, tutta allineata al byte
static const int32_t CORNER      = 96;   // lato dei blocchi d'angolo
static const int32_t LABEL_SCALE = 8;    // glifi dei righelli: 40x56 px
static const int32_t BIG_SCALE   = 16;   // lettera della coda: 80x112 px
static const int32_t YRULE_TICK  = 64;   // lunghezza del tick del righello Y
static const int32_t YLABEL_X    = 104;  // oltre il blocco d'angolo
static const int32_t XLABEL_Y    = 8;

/**
 * Compone la riga y del piano B/N. Polarità SSD16xx del cmd 0x24: bit 1 =
 * bianco, bit 0 = nero, quindi si parte da 0xFF e si spegne dove serve nero.
 */
static void composeRowBW(int16_t y, uint8_t* row, uint16_t gate)
{
  memset(row, 0xFF, ROW_BYTES);

  // Cornice di 4 px: delimita l'area programmata. Il bordo inferiore si vede
  // solo se il controller pilota davvero tutte le gate line programmate.
  if (y < 4 || y >= (int16_t)gate - 4)
  {
    memset(row, 0x00, ROW_BYTES);
    return;
  }
  paintSpan(row, 0, 8, 0x00);
  paintSpan(row, SRC - 8, 8, 0x00);

  // Blocco nero nell'origine RAM (0,0): ancora del verso su entrambi gli assi.
  if (y < CORNER)
    paintSpan(row, 0, CORNER, 0x00);

  // Scaletta diagonale, un byte per riga: uno shift di stride la spezza.
  paintSpan(row, ((int32_t)y * ROW_BYTES / gate) * 8, 8, 0x00);

  // Righello Y: tick di 4 righe ogni 64, con etichetta numerica. La riga più
  // alta ancora leggibile sul pannello è il numero di gate line reali.
  for (int32_t ty = 0; ty < (int32_t)gate; ty += 64)
  {
    if (y >= ty && y < ty + 4)
      paintSpan(row, 0, YRULE_TICK, 0x00);
    if (ty > 0)
      paintNumber(row, y, (uint16_t)ty, YLABEL_X, ty, LABEL_SCALE, 0x00);
  }

  // Righello X: tick ogni 128 px con etichetta, nella fascia alta.
  for (int32_t tx = 128; tx < SRC; tx += 128)
  {
    if (y >= XLABEL_Y && y < XLABEL_Y + 24)
      paintSpan(row, tx, 8, 0x00);
    paintNumber(row, y, (uint16_t)tx, tx + 16, XLABEL_Y + 32, LABEL_SCALE, 0x00);
  }

  /**
   * Glifo grande al centro della banda: il numero della candidata di init che ha
   * prodotto questo frame, 1 = MINIMAL, 2 = SOLUM, 3 = OEPL. Le tre passate
   * stampano lo stesso pattern e una cancella l'altra, quindi sul vetro devono
   * restare distinguibili senza contare i frame.
   */
  paintGlyph(row, y, (uint8_t)(g_cand + 1),
             SRC / 2 - 40, EXPECTED_BAND / 2 - 56, BIG_SCALE, 0x00);
}

/**
 * Compone la riga y del piano accent. Polarità del cmd 0x26: bit 1 = accent
 * acceso, quindi si parte da 0x00.
 *
 * Due elementi soltanto, entrambi fuori dai righelli per non sovrapporre
 * accent e nero sugli stessi pixel: la combinazione dei due piani a 1 non è
 * ciò che questo test vuole misurare.
 */
static void composeRowRED(int16_t y, uint8_t* row, uint16_t gate)
{
  memset(row, 0x00, ROW_BYTES);

  // Blocco accent nell'angolo opposto in X, alla stessa altezza dell'origine:
  // resta visibile qualunque sia il numero di gate line reali.
  if (y < CORNER)
    paintSpan(row, SRC - CORNER, CORNER, 0xFF);

  // Barra accent sulle ultime 64 righe della banda attesa: se il controller
  // pilota esattamente 384 gate, questa barra chiude il bordo inferiore.
  if (y >= (int16_t)EXPECTED_BAND - 64 && y < (int16_t)EXPECTED_BAND)
    paintSpan(row, 256, SRC - 256, 0xFF);
}

/**
 * Riversa un piano riga per riga dentro la finestra RAM piena. Blocchi da una
 * riga, 120 byte, che è il percorso di _writeImage del driver. Ritorna i ms
 * spesi, che sono il costo per piano di un frame.
 */
static uint32_t writePlane(uint8_t planeCommand, uint16_t gate,
                           void (*compose)(int16_t, uint8_t*, uint16_t))
{
  uint8_t row[ROW_BYTES];
  setRamWindow(0, 0, SRC, gate);
  writeCommand(planeCommand);

  const uint32_t t0 = millis();
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  csAssert();
  for (int16_t y = 0; y < (int16_t)gate; ++y)
  {
    compose(y, row, gate);
    hspi.writeBytes(row, ROW_BYTES);
  }
  csRelease();
  hspi.endTransaction();
  return millis() - t0;
}

// --- refresh ---------------------------------------------------------

/**
 * Attende la fine del refresh riportando le fasi: il BUSY che si rialza entro
 * 800 ms dice che il refresh procede in più passate.
 */
static int32_t waitRefresh(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  int phase = 0;
  while (true)
  {
    ++phase;
    const uint32_t tPhase = millis();
    uint32_t nextLog = 2500;
    while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      if ((millis() - t0) > timeout_ms)
        return -1;
      if ((millis() - tPhase) >= nextLog)
      {
        Serial.printf("    fase %d in corso da %lu ms\n",
                      phase, (unsigned long)(millis() - tPhase));
        nextLog += 2500;
      }
      // il BUSY dell'altra coda è un testimone: se si muove, i due
      // controller condividono qualcosa oltre al bus
      if (cfg.secondFfc && digitalRead(PIN_BUSY_OTHER) == BUSY_ACTIVE)
        otherBusyMoved = true;
      delay(1);
    }
    Serial.printf("    fase %d conclusa: BUSY alto per %lu ms\n",
                  phase, (unsigned long)(millis() - tPhase));
    const uint32_t tIdle = millis();
    bool again = false;
    while ((millis() - tIdle) < 800)
    {
      if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
      {
        again = true;
        break;
      }
      delay(1);
    }
    if (!again)
      break;
    Serial.printf("    BUSY risalito dopo %lu ms: refresh multi-fase\n",
                  (unsigned long)(millis() - tIdle));
  }
  Serial.printf("    fasi rilevate: %d\n", phase);
  return (int32_t)(millis() - t0);
}

/**
 * Lancia la display update sequence e ne misura i tempi. Verifica prima di
 * tutto che il BUSY salga: se non sale, il controller non ha preso il comando e
 * niente di quanto segue ha valore.
 *
 * I default sono il refresh pieno, l'unico che serviva finchè non c'era la
 * sonda del partial. Gli altri valori di 0x22 che il test usa: 0xFC è Mode 2
 * senza power down, cioè la waveform differenziale, e 0xF4 è Mode 1 senza
 * power down. Attesa e timeout sono parametri perchè una passata su finestra,
 * se il banco esiste, dura tre ordini di grandezza meno di una piena, e
 * annunciare venti secondi per poi attenderne quaranta non avrebbe senso.
 *
 * Se una schermata è aperta, la riga parte col suo numero fra parentesi quadre
 * e lo stesso numero viene disegnato sul vetro prima della master activation.
 */
static int32_t runRefresh(uint8_t updateSequence = 0xF7,
                          const char* attesa = "una ventina di secondi",
                          uint32_t timeout_ms = 0)   // 0 = cfg.timeoutMs
{
  if (timeout_ms == 0) timeout_ms = cfg.timeoutMs;
  Serial.println();
  if (schermataCorrente)
    Serial.printf("[%u] refresh (0x22 = 0x%02X + 0x20), %s\n",
                  (unsigned)schermataCorrente, updateSequence, attesa);
  else
    Serial.printf("refresh (0x22 = 0x%02X + 0x20), %s\n", updateSequence, attesa);

  // il riquadro va scritto in RAM prima della master activation, o non si vede
  if (schermataCorrente && riquadroSulVetro)
    disegnaBadge(schermataCorrente);

  writeCommand(0x22);
  writeData(updateSequence);
  writeCommand(0x20);   // master activation

  const uint32_t tAct = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - tAct) < 1000)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    Serial.println(F("ATTENZIONE: BUSY non è mai salito, il controller non risponde"));
    return -1;
  }
  Serial.printf("BUSY salito dopo %lu ms\n", (unsigned long)(millis() - tAct));

  const int32_t ms = waitRefresh(timeout_ms);
  if (ms < 0)
    Serial.printf("ATTENZIONE: timeout BUSY a %lu ms, refresh non concluso\n",
                  (unsigned long)timeout_ms);
  else
    Serial.printf("refresh concluso in %ld ms\n", (long)ms);
  return ms;
}

// --- registri in lettura --------------------------------------------
/**
 * Clock ridotto per la lettura: in lettura il controller è molto più lento che
 * in scrittura.
 */
static SPISettings spiReadSettings(2500000, MSBFIRST, SPI_MODE0);

// Legge n byte dopo un comando. Stessa convenzione di panel_diagnostic.
static void readRegister(uint8_t cmd, uint8_t* out, uint8_t n)
{
  writeCommand(cmd);
  hspi.beginTransaction(spiReadSettings);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t i = 0; i < n; ++i)
    out[i] = hspi.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Tenta i tre registri in lettura. Lo status 0x2F ha un POR noto (0x01, chip
 * ID 01) e fa da prova di validità del percorso: se non torna, gli altri due
 * vengono dichiarati inattendibili invece di stampare numeri senza senso. I
 * suoi bit 5 e 4 sono i flag di HV Ready e VCI, cioè l'esito esplicito dei
 * rilevatori che la sezione precedente misura col BUSY.
 *
 * Sul FPC 24 pin del 9.7" la linea dati in uscita non c'è, quindi l'esito
 * atteso è "nessuna risposta"; su questa coda a 21 pin non si sa, e se invece
 * risponde il modulo si identifica senza ambiguità.
 */
static void reportRegisters()
{
  uint8_t st = 0xFF;
  readRegister(0x2F, &st, 1);
  const bool plausible = (st != 0x00) && (st != 0xFF);
  Serial.printf("  status 0x2F         0x%02X  %s\n", st,
                plausible ? "plausibile" : "NON plausibile: nessuna linea dati in lettura");
  if (!plausible)
  {
    Serial.println(F("    -> 0x2E e 0x1B non vengono interpretati: senza 0x2F valido"));
    Serial.println(F("       qualunque byte letto è rumore del bus"));
    return;
  }
  statusRead = st;
  Serial.printf("    bit5 HV Ready     %s\n", (st & 0x20) ? "1 = NON pronta" : "0 = pronta");
  Serial.printf("    bit4 VCI          %s\n", (st & 0x10) ? "1 = fuori norma" : "0 = normale");
  Serial.printf("    bit1:0 chip ID    %u\n", (unsigned)(st & 0x03));

  uint8_t id[10];
  memset(id, 0, sizeof(id));
  readRegister(0x2E, id, sizeof(id));
  Serial.print(F("  user ID 0x2E        "));
  for (uint8_t i = 0; i < sizeof(id); ++i) Serial.printf("%02X ", id[i]);
  Serial.print(F(" ascii \""));
  for (uint8_t i = 0; i < sizeof(id); ++i)
    Serial.print((char)(id[i] >= 0x20 && id[i] < 0x7F ? id[i] : '.'));
  Serial.println(F("\""));

  uint8_t t[2] = { 0, 0 };
  readRegister(0x1B, t, sizeof(t));
  const int16_t raw = (int16_t)(((uint16_t)t[0] << 4) | (t[1] >> 4));
  Serial.printf("  temperatura 0x1B    raw 0x%03X = %d C\n",
                (unsigned)(raw & 0x0FFF), (int)(raw / 16));
}

// --- prova di vita e alte tensioni ----------------------------------

/**
 * Riempie un piano col pattern hardware (0x47 per il B/N, 0x46 per l'accent):
 * il controller scrive la propria RAM da sè, senza passare dal bus. Serve a due
 * cose: è una prova di vita che non dipende da 46 KB di push SPI, e alza il
 * BUSY per una durata misurabile. Ritorna i ms di BUSY, -1 al timeout.
 */
static int32_t patternFill(uint8_t patternCommand, uint8_t value, const char* label)
{
  writeCommand(patternCommand);
  writeData(value);
  const uint32_t tAct = millis();
  bool rose = false;
  while ((millis() - tAct) < 200)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { rose = true; break; }
  }
  if (!rose)
  {
    Serial.printf("  pattern %s (0x%02X)  BUSY non è salito: comando non accettato\n",
                  label, patternCommand);
    return -1;
  }
  const int32_t ms = waitBusy(5000);
  if (ms < 0)
    Serial.printf("  pattern %s (0x%02X)  BUSY ancora alto a 5000 ms\n", label, patternCommand);
  else
    Serial.printf("  pattern %s (0x%02X)  BUSY alto per %ld ms\n", label, patternCommand, (long)ms);
  return ms;
}

/**
 * Prova di vita del controller e stato delle alte tensioni, senza guardare un
 * pixel: è la sezione che conta sulla coda che non stampa. I due rilevatori
 * vogliono CLKEN=1 e ANALOGEN=1, cioè il solo power on (0x22 = 0xC0 + 0x20).
 *
 *   0x14 HV Ready Detection, A = 0x77: cool down 10 ms x 8, 7 cicli, massimo
 *        560 ms. Il datasheet dà la detection per conclusa quando HV è pronta,
 *        quindi un BUSY molto più corto del massimo dice HV arrivata presto e
 *        uno che arriva al massimo dice che non è mai arrivata.
 *   0x15 VCI Detection al POR (2.3 V): qui il datasheet non promette una
 *        conclusione anticipata, e conta solo che il BUSY reagisca.
 *
 * L'esito esplicito sta nei bit 5 e 4 dello status 0x2F, leggibile solo se
 * questa coda porta fuori la linea dati: per questo i registri si leggono
 * subito dopo, con la detection ancora fresca.
 */
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
    Serial.printf("  power on 0xC0       BUSY alto per %ld ms\n", (long)powerOnMs);

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
    Serial.printf("  VCI detect 0x15     BUSY alto per %ld ms\n", (long)vciDetectMs);

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
    Serial.printf("  power off 0xC3      BUSY alto per %ld ms\n", (long)powerOffMs);
}

// --- frame ----------------------------------------------------------

/**
 * Pausa di osservazione: il pannello mostra un risultato che il frame
 * successivo sovrascrive, e il test non può vederlo. Aspetta INVIO, oppure ms
 * se nessuno risponde — così una lettura lenta non costa più un run intero, e
 * un run non presidiato scorre da solo come prima. Con ms = 0 non si ferma.
 */
static void observePause(uint32_t ms, const char* cosaSuccede)
{
  if (ms == 0) return;
  const unsigned n = numeroDiRiga();
  scartaInputPendente();
  Serial.println();
  Serial.printf("[%u] GUARDA IL PANNELLO E ANNOTA: %s\n", n, cosaSuccede);
  Serial.printf("[%u] premi INVIO quando hai finito, o aspetta %lu s\n",
                n, (unsigned long)(ms / 1000));

  const uint32_t t0 = millis();
  uint32_t prossimo = 5000;
  bool anticipata = false;
  while ((millis() - t0) < ms)
  {
    if (Serial.available()) { anticipata = true; break; }
    if ((millis() - t0) >= prossimo)
    {
      Serial.printf("[%u]   %lu s\n", n, (unsigned long)((ms - prossimo) / 1000));
      prossimo += 5000;
    }
    delay(20);
  }
  if (anticipata)
  {
    scartaInputPendente();
    Serial.printf("[%u]   ripartito su richiesta dopo %lu s\n",
                  n, (unsigned long)((millis() - t0) / 1000));
  }
}

/**
 * Frame a bande: le quattro combinazioni dei due piani, una per banda, con la
 * cifra della banda disegnata dentro. Le prime tre sono quelle che il driver
 * genera (bianco, nero, accent); la quarta, BW=0 con accent acceso, non la
 * genera mai, e se rende un colore distinto dalle altre tre allora sul film
 * esiste un quarto stato.
 *
 * La cifra è disegnata sul piano B/N col valore opposto a quello della banda,
 * così resta leggibile sia su fondo bianco sia su fondo nero.
 */
static void composeRowBandsBW(int16_t y, uint8_t* row, uint16_t gate)
{
  const uint16_t bandH = gate / 4;
  const uint8_t  band  = bandH ? (uint8_t)(y / bandH) : 0;
  const bool     bwOn  = (band == 0) || (band == 2);   // bande 1 e 3: BW = 1
  memset(row, bwOn ? 0xFF : 0x00, ROW_BYTES);
  const int32_t gy = (int32_t)band * bandH + bandH - GLYPH_H * LABEL_SCALE - 16;
  // x = 160 e non 32: la colonna sinistra ospita i box a finestra parziale che
  // questo frame scrive sopra le bande, e le due cose non devono sovrapporsi.
  paintGlyph(row, y, (uint8_t)(band + 1), 160, gy, LABEL_SCALE, bwOn ? 0x00 : 0xFF);
}

static void composeRowBandsRED(int16_t y, uint8_t* row, uint16_t gate)
{
  const uint16_t bandH = gate / 4;
  const uint8_t  band  = bandH ? (uint8_t)(y / bandH) : 0;
  const bool     redOn = (band == 2) || (band == 3);   // bande 3 e 4: accent = 1
  memset(row, redOn ? 0xFF : 0x00, ROW_BYTES);
}

// --- finestre parziali e box -----------------------------------------

/**
 * Scrive un rettangolo di valore costante passando da una finestra RAM
 * parziale. x e w multipli di 8: la RAM è organizzata a byte, e allinearsi
 * evita di dover mascherare i bit ai bordi.
 *
 * Serve a esercitare l'addressing con **x diverso da zero**, che è il percorso
 * di _setPartialRamArea + writeImagePart del driver e che nessun altro frame di
 * questo test tocca: tutti gli altri scrivono finestre a larghezza piena. Se
 * questa parte non funziona, il driver sbaglia ogni scrittura che non parta dal
 * bordo sinistro, e senza questa prova non si saprebbe.
 */
static void writeBoxConst(uint8_t planeCommand, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h, uint8_t value)
{
  setRamWindow(x, y, w, h);
  writeCommand(planeCommand);
  const uint16_t rowBytes = w / 8;
  uint8_t row[PART_ROW_MAX];
  memset(row, value, rowBytes);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint16_t i = 0; i < h; ++i)
    hspi.writeBytes(row, rowBytes);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Frame delle finestre parziali: fondo bianco a larghezza piena, poi tre box
 * neri da 64x64 scritti uno per uno con la propria finestra, a x = 0, 448, 896,
 * e un quarto box accent a x = 224. Le posizioni sono equispaziate di proposito:
 * tre quadrati allineati e equidistanti dicono che l'addressing X funziona; se
 * scivolano, si sovrappongono o si smarginano, il difetto è nella finestra
 * parziale e il driver va corretto lì prima di ogni altra cosa.
 */
static int32_t showBandsAndBoxesFrame()
{
  Serial.println(F("\n--- frame: 4 bande dei piani + 4 box a finestra parziale ---"));
  /**
   * Le etichette delle bande nominano il valore del BIT, non il colore, e la
   * traduzione fra i due la dà probeBwPolarity(), che gira prima di questo
   * frame. Va detto qui perchè il frame gira dopo initPanel, e la candidata
   * CAND_SOLUM scrive 0x21 = 0x08 0x00: se quel valore inverte il piano BW su
   * questo silicio, le bande 1 e 2 escono scambiate rispetto alla Table 6-4,
   * e con loro la 3 e la 4.
   */
  Serial.printf("  polarità del piano BW: %s\n", polaritaLabel());
  if (cfg.bwPolarity == BW_UNKNOWN)
    Serial.println(F("  (la sonda della polarità non l'ha determinata: le etichette"
                     " BW = 1 e BW = 0\n  seguono la convenzione del datasheet)"));
  const uint32_t t0 = millis();
  writePlane(0x24, EXPECTED_BAND, composeRowBandsBW);
  writePlane(0x26, EXPECTED_BAND, composeRowBandsRED);

  /**
   * I box vanno sopra le bande, non in un frame a parte: sono due domande
   * indipendenti — che colore rendono le quattro combinazioni dei piani, e se
   * l'addressing con x diverso da zero funziona — e su un pannello dove ogni
   * refresh costa venti secondi e una pausa di osservazione, tenerle separate
   * era un frame e uno sguardo buttati.
   *
   * Tre box neri equispaziati dentro la banda 1, che è bianca: se escono
   * allineati ed equidistanti la finestra parziale in X funziona, e con essa
   * ogni writeImagePart del driver. Un quarto box accent dentro la banda 2, che
   * è nera: prova la stessa finestra sul piano 0x26.
   */
  const uint16_t bandH = EXPECTED_BAND / 4;
  writeBoxConst(0x24, 0,   8, 64, 64, 0x00);
  writeBoxConst(0x24, 448, 8, 64, 64, 0x00);
  writeBoxConst(0x24, 896, 8, 64, 64, 0x00);
  writeBoxConst(0x26, 224, bandH + 16, 64, 64, 0xFF);
  Serial.printf("  bande + 4 box scritti in %lu ms\n", (unsigned long)(millis() - t0));

  /**
   * Finestra piena prima del refresh: writeBoxConst la lascia sull'ultimo box,
   * e con quella in vigore il refresh coprirebbe 64x64 invece della banda.
   */
  setRamWindow(0, 0, SRC, EXPECTED_BAND);
  return runRefresh();
}

// --- refresh parziale d'area -----------------------------------------

// Durate della sonda, -1 se la passata non si è conclusa o non è stata fatta
static int32_t areaMsFirst = -1;   // prima passata 0xFC, finestra alta
static int32_t areaMsThin  = -1;   // passata 0xFC su 24 righe
static int32_t areaMsMode1 = -1;   // passata 0xF4, Mode 1 su finestra

/**
 * Mappa verticale della sonda del partial d'area, espressa in frazioni della
 * banda, così restano disgiunte qualunque sia il conteggio gate reale. A sonda
 * finita si leggono tutte insieme sullo stesso schermo.
 *
 * Il riquadro è l'unica finestra ristretta anche lungo X. x e w multipli di 8:
 * sull'asse source la RAM è organizzata a byte.
 */
static const uint16_t AREA_BAND   = EXPECTED_BAND;
static const uint16_t AREA_P1_Y   = 0;                    // passata 1, poi la 4
static const uint16_t AREA_P1_H   = AREA_BAND / 6;
static const uint16_t AREA_TRAP_Y = AREA_P1_H + 16;       // fascia di trappola
static const uint16_t AREA_TRAP_H = AREA_BAND / 12;
static const uint16_t AREA_THIN_Y = AREA_BAND / 3;        // finestra sottile
static const uint16_t AREA_THIN_H = 24;
static const uint16_t AREA_M1_Y   = AREA_BAND / 3 + 40;   // passata Mode 1
static const uint16_t AREA_M1_H   = AREA_BAND / 12;
static const uint16_t AREA_P2_Y   = AREA_BAND / 2 + 32;   // passata 2
static const uint16_t AREA_P2_H   = AREA_BAND / 6;
static const uint16_t AREA_BOX_X  = 448;                  // riquadro, ristretto in X
static const uint16_t AREA_BOX_W  = 128;
static const uint16_t AREA_BOX_Y  = AREA_BAND * 3 / 4 + 32;
static const uint16_t AREA_BOX_H  = AREA_BAND / 8;

// Banda minima perchè le fasce della mappa restino disgiunte
static const uint16_t AREA_BAND_MIN = 320;

// Valore di digit che significa "fascia uniforme, senza cifra"
static const uint8_t AREA_NO_DIGIT = 0xFF;

/**
 * Scrive una fascia a larghezza piena su un piano: fondo uniforme con la cifra
 * della passata sovraimpressa, per riconoscere a colpo d'occhio quale passata
 * ha dipinto cosa. Riusa paintGlyph, cioè lo stesso font dei righelli del
 * pattern di identificazione. La cifra viene disegnata solo se la fascia è
 * abbastanza alta da contenerla: sotto quell'altezza la fascia esce uniforme e
 * la si riconosce dalla posizione.
 */
static void writeStripeWithDigit(uint8_t planeCommand, uint16_t y, uint16_t h,
                                 uint8_t bg, uint8_t digit, uint8_t fg)
{
  uint8_t row[ROW_BYTES];
  const int32_t glyphH = (int32_t)GLYPH_H * LABEL_SCALE;
  const bool fits = (digit < 10) && ((int32_t)h >= glyphH + 8);
  const int32_t gy = fits ? ((int32_t)h - glyphH) / 2 : -glyphH;

  setRamWindow(0, y, SRC, h);
  writeCommand(planeCommand);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (int16_t r = 0; r < (int16_t)h; ++r)
  {
    memset(row, bg, ROW_BYTES);
    if (fits)
      paintGlyph(row, r, digit, 32, gy, LABEL_SCALE, fg);
    hspi.writeBytes(row, ROW_BYTES);
  }
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Esito di una passata della sonda d'area, tenuto per il riepilogo: il
 * confronto fra passate serve più del valore singolo.
 */
struct AreaPass
{
  const char* label;
  uint16_t x, y, w, h;
  uint8_t  sequence;   // parametro di 0x22 usato per aggiornare
  uint32_t pushMs;     // scrittura della finestra su 0x24
  int32_t  ms;         // BUSY del refresh, -1 se non conclusa
};

static AreaPass areaPasses[6];
static uint8_t  areaPassCount = 0;

/**
 * Una passata della sonda d'area: scrive la finestra su 0x24, aggiorna con la
 * sequenza indicata e poi riallinea la stessa finestra su 0x26, perchè la
 * passata successiva trovi come frame precedente quello che il pannello sta
 * davvero mostrando. È lo stesso schema di writeImagePartToPrevious del driver
 * monocromatico GxEPD2_1330_GDEM133T91, che gira sullo stesso silicio.
 *
 * La finestra viene reimpostata subito prima della master activation: sono i
 * registri 0x44/0x45 presenti in quel momento a definire l'area che il refresh
 * percorre, ed è esattamente il punto in prova.
 *
 * Con digit sotto 10 la fascia porta la cifra e deve essere a larghezza piena;
 * con AREA_NO_DIGIT esce uniforme e può essere ristretta anche in X. Ritorna i
 * ms del refresh, -1 se non si è concluso.
 */
static int32_t areaPass(const char* label, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint8_t value, uint8_t digit, uint8_t fg,
                        uint8_t updateSequence, uint32_t timeout_ms)
{
  const bool withDigit = (digit < 10) && (w == SRC);

  Serial.printf("\n-- %s\n   finestra x=%u..%u  y=%u..%u  %u righe  %lu byte\n",
                label, (unsigned)x, (unsigned)(x + w - 1),
                (unsigned)y, (unsigned)(y + h - 1), (unsigned)h,
                (unsigned long)((uint32_t)(w / 8) * h));

  const uint32_t t0 = millis();
  if (withDigit)
    writeStripeWithDigit(0x24, y, h, value, digit, fg);
  else
    writeBoxConst(0x24, x, y, w, h, value);
  const uint32_t pushMs = millis() - t0;
  Serial.printf("   0x24 scritto in %lu ms\n", (unsigned long)pushMs);

  setRamWindow(x, y, w, h);
  const char* attesa = (updateSequence == 0xF4)
                       ? "Mode 1 su finestra: fra meno di un secondo e una ventina, si misura"
                       : "atteso sotto il secondo se finestra e banco differenziale valgono";
  const int32_t ms = runRefresh(updateSequence, attesa, timeout_ms);

  // frame precedente allineato a quello che si vede adesso, nella stessa finestra
  if (withDigit)
    writeStripeWithDigit(0x26, y, h, value, digit, fg);
  else
    writeBoxConst(0x26, x, y, w, h, value);

  if (areaPassCount < (uint8_t)(sizeof(areaPasses) / sizeof(areaPasses[0])))
  {
    AreaPass& p = areaPasses[areaPassCount++];
    p.label = label;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.sequence = updateSequence;
    p.pushMs = pushMs;
    p.ms = ms;
  }
  return ms;
}

/**
 * Riepilogo della sonda d'area: la tabella delle passate e le letture che
 * contano, cioè se la durata scala con l'altezza della finestra e quanto
 * costerebbe un aggiornamento B/N fatto per finestre su questa banda.
 */
static void reportAreaPasses()
{
  Serial.println(F("\nesito della sonda del partial d'area:"));
  if (areaPassCount == 0)
  {
    Serial.println(F("  nessuna passata eseguita"));
    return;
  }

  Serial.println(F("  finestra                    righe  0x22   push   refresh"));
  for (uint8_t k = 0; k < areaPassCount; ++k)
  {
    const AreaPass& p = areaPasses[k];
    Serial.printf("  x=%3u..%3u y=%3u..%3u    %5u  0x%02X  %4lu ms  ",
                  (unsigned)p.x, (unsigned)(p.x + p.w - 1),
                  (unsigned)p.y, (unsigned)(p.y + p.h - 1), (unsigned)p.h,
                  p.sequence, (unsigned long)p.pushMs);
    if (p.ms < 0)
      Serial.println(F("non conclusa"));
    else
      Serial.printf("%ld ms\n", (long)p.ms);
  }

  /**
   * Scala con l'altezza. Se la durata è proporzionale alle gate line, al
   * driver conviene passare la finestra minima che contiene il disegno
   * cambiato; se è costante, il costo è tutto della waveform e restringere la
   * finestra fa risparmiare solo push SPI.
   */
  if (areaMsFirst > 0 && areaMsThin > 0)
  {
    Serial.printf("  %u righe %ld ms contro %u righe %ld ms: tempi 1:%.2f, righe 1:%.2f\n",
                  (unsigned)AREA_P1_H, (long)areaMsFirst,
                  (unsigned)AREA_THIN_H, (long)areaMsThin,
                  (double)areaMsFirst / (double)areaMsThin,
                  (double)AREA_P1_H / (double)AREA_THIN_H);
    if ((double)areaMsFirst / (double)areaMsThin > 1.5)
      Serial.println(F("  la durata scala con le gate line coinvolte"));
    else
      Serial.println(F("  la durata non dipende dall'altezza: il costo è della waveform"));
  }

  if (areaMsFirst > 0 && refreshMs > 0)
    Serial.printf("  passata d'area %ld ms contro %ld ms del refresh pieno: %.1fx\n",
                  (long)areaMsFirst, (long)refreshMs,
                  (double)refreshMs / (double)areaMsFirst);

  /** Il valore si consiglia solo se la passata d'area è davvero più corta di un
   *  frame intero. Se costa uguale non c'è niente da tarare, e proporre il
   *  doppio del misurato — che starebbe anche sopra il busy_timeout del driver —
   *  sarebbe un consiglio sbagliato. */
  const bool partialConviene = (areaMsFirst > 0) && (refreshMs > 0) &&
                               (areaMsFirst * 2 < refreshMs);
  if (partialConviene)
    Serial.printf("  partial_refresh_time da mettere nel driver: %ld ms con margine\n",
                  (long)(areaMsFirst * 2 + 200));
  else if (areaMsFirst > 0)
    Serial.println(F("  la passata d'area costa come un frame intero: non c'è nessun\n"
                     "  partial_refresh_time da tarare. Nel driver resta pari a\n"
                     "  full_refresh_time e hasFastPartialUpdate resta falso"));

  if (areaMsMode1 > 0)
  {
    Serial.printf("  Mode 1 su finestra (0x22 = 0xF4) %ld ms su %u righe",
                  (long)areaMsMode1, (unsigned)AREA_M1_H);
    if (refreshMs > 0)
      Serial.printf(", contro %ld ms del pieno", (long)refreshMs);
    Serial.println();
    if (refreshMs > 0 && areaMsMode1 * 2 < refreshMs)
      Serial.println(F("  Mode 1 si accorcia con la finestra: anche senza banco\n"
                       "  differenziale un refresh d'area costa meno di uno pieno, e non\n"
                       "  consuma 0x26, quindi resterebbe compatibile con l'accent"));
    else if (refreshMs > 0)
      Serial.println(F("  Mode 1 dura come il pieno: la finestra non accorcia la waveform"));
  }

  Serial.println(F("  le passate 0xFC valgono per frame in bianco e nero: lì 0x26 fa da\n"
                   "  frame precedente, quindi in quella modalità l'accent non esiste"));
  if (partialConviene)
    Serial.println(F("  su due controller c'è un secondo guadagno: una finestra tutta dentro\n"
                     "  una banda impegna un solo controller, e l'altro non va nemmeno svegliato"));
}

/**
 * Sonda del partial d'area: se restringendo la finestra RAM il controller
 * aggiorna davvero solo quella porzione della banda, e a che prezzo in tempo.
 *
 * showPartialWindowFrame() verifica l'addressing in SCRITTURA, cioè dove
 * finiscono i byte; qui il REFRESH, cioè quali gate line vengono scandite alla
 * master activation. Sul fratello monocromatico dello stesso silicio,
 * GxEPD2_1330_GDEM133T91, refresh(x,y,w,h) fa _setPartialRamArea più 0x22 =
 * 0xFC, oppure 0xF4 se il banco differenziale non c'è; il driver 122c manda
 * sempre _Update_Full a finestra piena, quindi qui non c'è niente di già
 * verificato sul campo.
 *
 * Il contenuto finale non distingue una finestra rispettata da una ignorata: la
 * RAM accumula le scritture e le due ipotesi danno la stessa immagine. Serve
 * una discordanza voluta, ed è la fascia di trappola, nera in 0x24 e mai
 * compresa in una finestra di refresh.
 *
 * Le passate sono disgiunte lungo Y, così a sonda finita si leggono insieme:
 *   fascia 1   passata 1, nera con la cifra 1, poi la passata 4 la riporta a
 *              bianca con la cifra 3: dice se una catena di partial regge
 *   fascia 2   trappola, scritta in RAM e mai refreshata
 *   fascia 3   finestra sottile di 24 righe: dice se la durata scala con
 *              l'altezza o è tutta della waveform
 *   fascia 4   passata Mode 1 (0x22 = 0xF4), la strada che resta se 0xFC non va
 *   fascia 5   passata 2, nera con la cifra 2, con bordo 0x3C = 0x80
 *   riquadro   ristretto anche in X, x = 448..575
 *
 * Con cfg.pausaMs = 0 la pausa prima della passata Mode 1 salta, e allora una
 * trappola nera a fine sonda non dice più chi l'ha fatta comparire: qui la
 * pausa non è decorativa.
 *
 * Prezzo accettato in partenza: nelle passate 0xFC la 0x26 fa da frame
 * precedente e non da accent, quindi quella misura vale per un pannello bianco
 * e nero.
 */
static void probePartialProbe()
{
  Serial.println(F("\n=== sonda del partial d'area: la finestra RAM limita il refresh? ==="));
  Serial.println(F("nelle passate 0xFC si misura un pannello B/N: 0x26 fa da frame precedente"));

  /**
   * Con una banda troppo bassa le fasce della mappa si sovrapporrebbero e il
   * risultato non sarebbe leggibile: meglio non misurare che misurare male.
   */
  if (AREA_BAND < AREA_BAND_MIN)
  {
    Serial.printf("banda di %u righe sotto il minimo di %u: sonda saltata\n",
                  (unsigned)AREA_BAND, (unsigned)AREA_BAND_MIN);
    return;
  }

  // baseline bianca col pattern hardware: fondo su cui il nero si legge subito
  Serial.println(F("\nbaseline: banda bianca col pattern, poi refresh pieno Mode 1"));
  patternFill(0x47, 0xF7, "B/N   ");   // 0x24 tutto bianco
  patternFill(0x46, 0x77, "accent");   // 0x26 spento: il refresh pieno lo legge come accent
  if (runRefresh() < 0)
  {
    Serial.println(F("baseline non riuscita: sonda del partial d'area abbandonata"));
    return;
  }

  // una sola schermata per tutte le passate: si leggono insieme alla fine, e
  // il riquadro finisce dentro la finestra di quella che dipinge per ultima
  apriSchermata(SCH_AREA);

  /**
   * Ora che la banda è bianca, 0x26 va portata a bianco: da questo punto non è
   * più l'accent ma il frame precedente, e deve contenere quello che si vede.
   * Il pattern hardware lo fa senza spingere 46 KB sul bus.
   */
  patternFill(0x46, 0xF7, "prec. ");

  Serial.printf("\ntrappola: fascia nera scritta in 0x24 a y=%u..%u, che nessuna finestra\n"
                "di refresh comprende. Se compare, la finestra non è rispettata\n",
                (unsigned)AREA_TRAP_Y, (unsigned)(AREA_TRAP_Y + AREA_TRAP_H - 1));
  writeBoxConst(0x24, 0, AREA_TRAP_Y, SRC, AREA_TRAP_H, 0x00);

  // bordo come lo lascia l'init, cioè come lo tiene il driver oggi
  writeCommand(0x3C);
  writeData(0x01);
  areaMsFirst = areaPass("passata 1: fascia alta a nero, cifra 1, bordo 0x3C = 0x01",
                         0, AREA_P1_Y, SRC, AREA_P1_H, 0x00, 1, 0xFF, 0xFC, 30000);

  /**
   * Se la prima passata non si è conclusa, le altre quattro con 0xFC sarebbero
   * solo altrettanti timeout: si va diritti a Mode 1, che è la strada che resta.
   */
  if (areaMsFirst > 0)
  {
    /**
     * Bordo su VCOM invece che sulla LUT: su questa famiglia di controller è il
     * valore che tiene ferma la cornice durante un partial. Le due passate
     * differiscono solo per questo, quindi il confronto è pulito.
     */
    writeCommand(0x3C);
    writeData(0x80);
    areaPass("passata 2: fascia centrale a nero, cifra 2, bordo 0x3C = 0x80",
             0, AREA_P2_Y, SRC, AREA_P2_H, 0x00, 2, 0xFF, 0xFC, 30000);

    areaPass("passata 3: riquadro ristretto anche in X",
             AREA_BOX_X, AREA_BOX_Y, AREA_BOX_W, AREA_BOX_H, 0x00,
             AREA_NO_DIGIT, 0x00, 0xFC, 30000);

    /**
     * Le due passate che seguono si fanno solo se il partial guadagna tempo. La
     * 4 chiede se una catena di partial sulla stessa area regge, la 5 se la
     * durata scala con l'altezza della finestra: se la passata 1 è durata come
     * un refresh pieno, la catena non interessa a nessuno e il tempo non scala
     * per definizione, quindi sono quaranta secondi che non dicono niente.
     */
    const bool partialGuadagna = (refreshMs <= 0) || (areaMsFirst * 2 < refreshMs);
    if (!partialGuadagna && !cfg.esaustivo)
    {
      Serial.println(F("\n-- passate 4 e 5 saltate: la passata 1 è durata come un refresh"));
      Serial.println(F("   pieno, quindi non c'è nessun tempo da far scalare e nessuna"));
      Serial.println(F("   catena di partial che valga la pena provare"));
    }

    // seconda scrittura sulla stessa area della passata 1: la catena regge?
    if (partialGuadagna || cfg.esaustivo)
      areaPass("passata 4: la fascia alta torna bianca, cifra 3",
               0, AREA_P1_Y, SRC, AREA_P1_H, 0xFF, 3, 0x00, 0xFC, 30000);

    if (partialGuadagna || cfg.esaustivo)
      areaMsThin = areaPass("passata 5: finestra sottile di 24 righe",
                          0, AREA_THIN_Y, SRC, AREA_THIN_H, 0x00,
                          AREA_NO_DIGIT, 0x00, 0xFC, 30000);
  }
  else
    Serial.println(F("\nla prima passata 0xFC non si è conclusa: le altre quattro\n"
                     "sarebbero altrettanti timeout, si passa a Mode 1 su finestra"));

  inizioOsservazione("PARTIAL D'AREA, prima della passata Mode 1");
  static const char* const OSS_AREA_MID[] =
  {
    "la fascia di trappola è nera in 0x24 e nessuna finestra la comprende: se",
    "è ancora bianca, le finestre sono rispettate",
    "",
    "guardala ADESSO: la passata Mode 1 che sta per partire potrebbe farla",
    "comparire, e a quel punto non si saprebbe più chi l'ha fatta comparire",
  };
  righeOsservazione(OSS_AREA_MID, sizeof(OSS_AREA_MID) / sizeof(OSS_AREA_MID[0]));
  observePause(cfg.pausaMs, "parte la passata Mode 1 su finestra");
  fineOsservazione();

  /**
   * Mode 1 su finestra. 0xF4 è 0xF7 senza il power down finale, ed è quello che
   * il driver monocromatico manda quando hasFastPartialUpdate è false: waveform
   * piena, ma sempre delimitata dalla finestra. Vale la misura in ogni caso, e
   * a differenza di 0xFC non consuma 0x26, quindi resterebbe compatibile con
   * l'accent.
   */
  writeCommand(0x3C);
  writeData(0x01);
  areaMsMode1 = areaPass("passata Mode 1 su finestra (0x22 = 0xF4)",
                         0, AREA_M1_Y, SRC, AREA_M1_H, 0x00,
                         AREA_NO_DIGIT, 0x00, 0xF4, cfg.timeoutMs);

  // 0xFC e 0xF4 lasciano clock e analogico accesi: si spengono come fa _PowerOff
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  waitBusy(5000);

  reportAreaPasses();

  inizioOsservazione("PARTIAL D'AREA: fasce, fascia di trappola e riquadro");
  static const char* const OSS_AREA[] =
  {
    "ogni passata ha lasciato la propria cifra sulla fascia che ha dipinto, e",
    "il riquadro col numero compare in alto a destra di OGNI fascia ridipinta",
    "",
    "la fascia di trappola è BIANCA o NERA?",
    "  bianca -> la finestra RAM limita davvero l'area ridipinta",
    "  nera   -> il refresh percorre tutta la banda, qualunque finestra sia",
    "    impostata",
    "è comparsa solo DOPO la passata Mode 1? allora solo 0xFC rispetta la",
    "  finestra, e un partial d'area costa l'accent",
    "",
    "il riquadro ristretto anche in X ha i bordi verticali netti? se no, lungo",
    "  X la finestra non vale e nel driver il partial va allargato a tutta la",
    "  riga",
    "la fascia alta porta la cifra 3 su fondo bianco? se no, la catena di",
    "  partial sulla stessa area non passa pulita e serve un refresh pieno",
    "  periodico",
    "ha lampeggiato solo la cornice della passata 1 (0x3C = 0x01)? allora il",
    "  driver deve mandare 0x3C = 0x80 prima di un partial e rimettere 0x01",
  };
  righeOsservazione(OSS_AREA, sizeof(OSS_AREA) / sizeof(OSS_AREA[0]));
  fineOsservazione();
  chiudiSchermata();
}

// --- fasce delle sonde con LUT custom --------------------------------

/** Quattro fasce, una per variante della sonda del partial con LUT custom. */
static const uint16_t LUTP_H = EXPECTED_BAND / 4;
/** Split fra le due metà del probe dei livelli di sorgente. */
static const uint16_t LEVELS_SPLIT = EXPECTED_BAND / 2;

// --- interruttori del differenziale sul banco dell'OTP ---------------

/** Esiti delle passate della sonda differenziale approfondita. */
static const uint8_t DIFF_PASSES_MAX = 6;
static const char*   diffLabel[DIFF_PASSES_MAX]    = { nullptr };
static uint8_t       diffSequence[DIFF_PASSES_MAX] = { 0 };
static int32_t       diffMs[DIFF_PASSES_MAX]       = { -1, -1, -1, -1, -1, -1 };
static uint8_t       diffCount = 0;

/** Registra una passata nella tabella di riepilogo. */
static void recordDiffPass(const char* label, uint8_t sequence, int32_t ms)
{
  if (diffCount >= DIFF_PASSES_MAX)
    return;
  diffLabel[diffCount]    = label;
  diffSequence[diffCount] = sequence;
  diffMs[diffCount]       = ms;
  ++diffCount;
}

/**
 * Sonda differenziale approfondita: gli interruttori che la sonda d'area non
 * tocca, tutti sul banco di waveform dell'OTP. Sono le quattro domande che sul
 * 9.7" hanno permesso di dire che il differenziale non esiste come fatto
 * misurato invece che come prudenza.
 *
 *   1. il controller CONFRONTA i due piani? Lo stesso 0xFC coi piani identici e
 *      poi coi piani opposti: se la durata non cambia, il contenuto non entra
 *      nel conto e nessun differenziale esiste. È il test minimo e chiude la
 *      questione da solo;
 *   2. quanto costa la RICARICA della LUT? 0xCF e 0xC7 sono Mode 2 e Mode 1 col
 *      bit 5 e il bit 4 spenti, cioè display senza ricaricare LUT e
 *      temperatura;
 *   3. quanto costa il solo CARICO? 0x99 carica la LUT di Mode 2 e non dipinge:
 *      è il tetto al guadagno che le due sopra possono cercare;
 *   4. la RAM rosso entra nel conto? 0x21 = 0x40 0x00 la bypassa come zero: se
 *      la durata cambia, 0x26 pesa sul refresh.
 *
 * Ogni passata lascia una cifra diversa sul vetro, così la cifra che resta è
 * l'ultima che ha davvero pilotato il pannello.
 */
static void probeDifferentialDeeper()
{
  Serial.println(F("\n=== sonda differenziale approfondita: gli interruttori non provati ==="));
  Serial.println(F("la sonda d'area misura l'indirizzamento; qui si misura la waveform, con"));
  Serial.println(F("gli interruttori che restano: se il controller confronta i due piani,"));
  Serial.println(F("quanto costa ricaricare la LUT, e se la RAM rosso entra nel conto."));

  const uint16_t fasciaH = (uint16_t)(EXPECTED_BAND / 4);

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  forceRamOptionsNormal();

  /**
   * 1a. Differenza zero: i due piani con lo stesso contenuto. Se il controller
   * confronta 0x24 e 0x26 per decidere quali pixel muovere, qui non ha niente
   * da muovere e la passata dovrebbe essere quasi istantanea.
   */
  Serial.println(F("\n-- 1a. differenza zero: 0x24 e 0x26 con lo stesso contenuto"));
  patternFill(0x47, bwPatternFor(true), "B/N   ");
  patternFill(0x46, bwPatternFor(true), "prec. ");
  setRamWindow(0, 0, SRC, EXPECTED_BAND);
  int32_t ms = runRefresh(0xFC, "molto corta se il motore confronta i piani");
  recordDiffPass("differenza zero", 0xFC, ms);
  const int32_t msZero = ms;

  /**
   * 1b. Differenza massima: si inverte solo 0x24, quindi ogni pixel differisce.
   * È il riferimento contro cui leggere la passata precedente.
   */
  Serial.println(F("\n-- 1b. differenza massima: si inverte solo 0x24"));
  patternFill(0x47, bwPatternFor(false), "B/N   ");
  setRamWindow(0, 0, SRC, EXPECTED_BAND);
  ms = runRefresh(0xFC, "riferimento per il confronto con la 1a");
  recordDiffPass("differenza massima", 0xFC, ms);
  const int32_t msMax = ms;

  if (msZero > 0 && msMax > 0)
  {
    Serial.printf("\n  differenza zero %ld ms contro differenza massima %ld ms: scarto %ld ms\n",
                  (long)msZero, (long)msMax, (long)(msMax - msZero));
    if (labs((long)(msMax - msZero)) < 2000)
      Serial.println(F("  la durata NON dipende dal contenuto dei piani: il controller non li\n"
                       "  confronta, quindi su questo pannello il refresh differenziale non\n"
                       "  esiste, e hasFastPartialUpdate = false è un fatto e non prudenza"));
    else
      Serial.println(F("  scarto grande: il controller confronta i due piani, quindi un\n"
                       "  meccanismo differenziale c'è e va perseguito"));
  }

  /**
   * 2. Display senza ricarica. Il bit 5 di 0x22 è load temperature e il bit 4
   * load LUT: 0xCF e 0xC7 li hanno spenti, quindi dipingono con la LUT che è
   * già in RAM. Se la durata cala, il costo era il carico e non la waveform.
   * Ogni passata porta la sua cifra, così si sa quale ha dipinto.
   */
  /**
   * 2. Solo carico della LUT di Mode 2, senza dipingere: 0x99 ha il bit 4 e il
   * bit 3 ma non il bit 2. Costa un secondo, e serve da misura del TETTO al
   * guadagno che le due passate dopo possono cercare: quelle dipingono senza
   * ricaricare la LUT, quindi al massimo risparmiano quello che il carico
   * costa. Se il carico è breve non c'è niente da risparmiare, e quelle due
   * passate si saltano.
   */
  Serial.println(F("\n-- 2. 0x99: carica la LUT di Mode 2 e non dipinge"));
  ms = runRefresh(0x99, "breve: non dipinge, carica", 10000);
  recordDiffPass("carico LUT Mode 2", 0x99, ms);
  const int32_t msCarico = ms;

  /**
   * 3. Display senza ricarica, e solo se il carico pesa. Il bit 5 di 0x22 è
   * load temperature e il bit 4 load LUT: 0xCF e 0xC7 li hanno spenti, quindi
   * dipingono con la LUT già in RAM. Il loro guadagno massimo è msCarico:
   * sotto i tre secondi non vale due refresh da venti.
   */
  const bool displaySenzaRicarica = (cfg.esaustivo || msCarico > 3000);

  /**
   * Solo le passate che dipingono portano il numero. Le due di differenza zero
   * e differenza massima pretendono i piani identici bit per bit, e il riquadro
   * li renderebbe diversi: falserebbe la misura centrale della sonda.
   */
  if (displaySenzaRicarica)
  {
    apriSchermata(SCH_DIFF_DEEP);
    Serial.println(F("\n-- 3. solo display, senza ricarica di LUT e temperatura (cifra 1)"));
    patternFill(0x47, bwPatternFor(true), "B/N   ");
    writeStripeWithDigit(0x24, 0, fasciaH, bwByteFor(true), 1, bwByteFor(false));
    setRamWindow(0, 0, SRC, EXPECTED_BAND);
    ms = runRefresh(0xCF, "sotto il secondo se il costo era la ricarica");
    recordDiffPass("solo display Mode 2", 0xCF, ms);

    Serial.println(F("\n-- 3a. controllo Mode 1 nelle stesse condizioni (cifra 2)"));
    writeStripeWithDigit(0x24, fasciaH, fasciaH, bwByteFor(true), 2, bwByteFor(false));
    setRamWindow(0, 0, SRC, EXPECTED_BAND);
    ms = runRefresh(0xC7, "controllo di Mode 1 nelle stesse condizioni");
    recordDiffPass("solo display Mode 1", 0xC7, ms);
  }
  else
  {
    Serial.printf("\n-- 3. 0xCF e 0xC7 saltate: il carico della LUT costa %ld ms, quindi\n"
                  "   non ricaricarla non può far risparmiare più di quello\n",
                  (long)msCarico);
  }

  /**
   * 4. RAM rosso bypassata, e solo se serve. 0x21 A[7:4] = 0100 tratta il
   * contenuto di 0x26 come zero: dice se quel piano entra nel conto del
   * refresh. Ma se la differenza zero e la differenza massima hanno dato la
   * stessa durata, che il contenuto dei piani non entri nel conto è già
   * dimostrato, e questa passata sarebbe una conferma di una misura.
   */
  const bool pianiContano = (msZero > 0 && msMax > 0
                             && labs((long)(msMax - msZero)) >= 2000);
  if ((pianiContano || cfg.esaustivo) && !schermataCorrente)
    apriSchermata(SCH_DIFF_DEEP);

  if (pianiContano || cfg.esaustivo)
  {
    Serial.println(F("\n-- 4. 0x21 = 0x40 0x00, RAM rosso bypassata come zero (cifra 3)"));
    writeCommand(0x21);
    writeData(0x40);
    writeData(0x00);
    writeStripeWithDigit(0x24, (uint16_t)(fasciaH * 2), fasciaH,
                         bwByteFor(true), 3, bwByteFor(false));
    setRamWindow(0, 0, SRC, EXPECTED_BAND);
    ms = runRefresh(0xFC, "diverso dalla 1b se 0x26 entra nel conto");
    recordDiffPass("0x26 bypassata via 0x21", 0xFC, ms);
    forceRamOptionsNormal();
  }
  else
  {
    Serial.println(F("\n-- 4. bypass di 0x26 saltato: differenza zero e differenza massima\n"
                     "   coincidono, quindi il contenuto dei piani non entra nel conto ed\n"
                     "   è già dimostrato senza spendere un altro refresh"));
  }

  Serial.println(F("\nesito della sonda differenziale approfondita:"));
  Serial.println(F("  passata                       0x22    BUSY"));
  for (uint8_t k = 0; k < diffCount; ++k)
  {
    if (diffMs[k] < 0)
      Serial.printf("  %-28s 0x%02X   non conclusa\n", diffLabel[k], diffSequence[k]);
    else
      Serial.printf("  %-28s 0x%02X   %6ld ms\n", diffLabel[k], diffSequence[k],
                    (long)diffMs[k]);
  }
  if (refreshMs > 0)
    Serial.printf("  riferimento: refresh pieno 0xF7 %ld ms\n", (long)refreshMs);
  if (schermataCorrente)
  {
    inizioOsservazione("DIFFERENZIALE APPROFONDITA: quale cifra è rimasta");
    static const char* const OSS_DEEP[] =
    {
      "cifre lasciate: 1 = 0xCF, 2 = 0xC7, 3 = 0xFC con 0x21 di bypass",
      "",
      "la cifra che vedi sul vetro è l'ultima passata che ha davvero dipinto:",
      "se è la 1, le due dopo non hanno fatto niente",
      "",
      "il riquadro col numero lo portano solo queste passate: quelle di",
      "differenza zero e differenza massima confrontano i due piani, e un",
      "riquadro li renderebbe diversi",
    };
    righeOsservazione(OSS_DEEP, sizeof(OSS_DEEP) / sizeof(OSS_DEEP[0]));
    fineOsservazione();
    chiudiSchermata();
  }

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
}

// --- waveform LUT: partial e quarto colore ---------------------------

/**
 * Layout della waveform LUT, dal datasheet SSD1677 §6.7 Figure 6-6. Il comando
 * 0x32 scrive i byte 0..104, cioè tutto tranne le tensioni:
 *
 *   byte   0.. 9   LUT0, dieci gruppi da quattro fasi, 2 bit per fase
 *   byte  10..19   LUT1        byte  20..29   LUT2
 *   byte  30..39   LUT3        byte  40..49   LUT4
 *   byte  50..99   dieci gruppi da { TP[nA], TP[nB], TP[nC], TP[nD], RP[n] }
 *   byte 100..104  frame rate
 *   byte 105..109  VGH, VSH1, VSH2, VSL, VCOM: NON scritti da 0x32, arrivano
 *                  da 0x03 / 0x04 / 0x2C e restano quelli dell'OTP.
 *
 * È il motivo per cui queste due sonde sono a rischio contenuto: cambiano la
 * sequenza delle fasi, non le tensioni con cui il film viene pilotato.
 *
 * I 2 bit di VS[nX-LUTm], Table 6-6: 00 = VSS, 01 = VSH1, 10 = VSL, 11 = VSH2.
 * TP[nX] = durata della fase in frame, 0 = fase saltata. RP[n] = ripetizioni
 * del gruppo meno una.
 */
static const uint16_t LUT_BYTES = 105;

/**
 * LUT di partial update del GDEH116T91, copiata da GxEPD2 1.6.9,
 * src/epd/GxEPD2_1160_T91.cpp (GPL-3.0, come questa libreria).
 *
 * Perchè proprio questa: quel pannello è 960x640 su SSD1677, cioè stesso
 * command set e stessi 960 source di questo, e con essa dichiara e ottiene
 * partial_refresh_time = 700 ms contro i 6200 del refresh pieno.
 *
 * Decodificata col layout sopra, e il conto torna:
 *
 *   LUT0, LUT3, LUT4  tutte a zero
 *   LUT1              0D = VSH1, poi 1B 1C 1D = VSL
 *   LUT2              0C 0D = VSL, poi 1A 1B 1C 1D = VSH1
 *   gruppo 0          TP = 0, 0, 5, 5   RP = 0 (una passata)
 *   gruppo 1          TP = 5, 3, 5, 5   RP = 0
 *
 * Su un pannello monocromatico in Mode 2 le due RAM sono (frame precedente,
 * frame nuovo): LUT1 e LUT2 sono le due transizioni, una per verso, e si vede
 * che sono simmetriche. LUT0 e LUT3 a zero sono i pixel che non cambiano, e
 * non venendo pilotati non consumano tempo.
 *
 * La verifica che il layout è interpretato bene: 10 + 18 = 28 frame in tutto,
 * che a un frame rate di ~50 Hz fanno ~560 ms, lo stesso ordine di grandezza
 * dei 700 ms dichiarati con il margine che ci si aspetta.
 */
static const uint8_t LUT_PARTIAL_1160[LUT_BYTES] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0
  0x01, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1
  0x0A, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x03, 0x05, 0x05, 0x00, // TP/RP gruppi 0-1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 2-3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 4-5
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 6-7
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 8-9
  0x22, 0x22, 0x22, 0x22, 0x22                                // frame rate
};

/**
 * La stessa waveform, riassegnata alle LUT che la Table 6-4 usa su un pannello
 * a 3 colori.
 *
 * Serve perchè qui le due RAM non sono (precedente, nuovo) ma (accent,
 * bianco/nero), e l'indice di LUT esce dalla Table 6-4: (0,0) nero = LUT0,
 * (0,1) bianco = LUT1, (1,x) accent = LUT2 e LUT3. La LUT del 1160 lascia LUT0
 * a zero, quindi con essa i pixel neri non verrebbero pilotati affatto: qui
 * LUT0 prende la waveform che nel 1160 stava in LUT2, cioè quella che spinge
 * nel verso opposto a LUT1, e LUT2 / LUT3 restano a zero perchè un frame
 * aggiornato in partial è per forza senza accent.
 *
 * I valori sono gli stessi, spostati di posto: niente è inventato.
 */
static const uint8_t LUT_PARTIAL_T64[LUT_BYTES] =
{
  0x0A, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0 nero
  0x01, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1 bianco
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2 accent
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3 accent
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x03, 0x05, 0x05, 0x00, // TP/RP gruppi 0-1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

/**
 * LUT del probe del quarto colore: separa LUT2 e LUT3 per livello di sorgente.
 *
 * LUT2 pilota a VSH1 (VS = 01), LUT3 a VSH2 (VS = 11), stessi tempi. Nero e
 * bianco restano fermi (LUT0 e LUT1 a zero), così l'unica differenza fra le
 * due metà stampate è quale delle due tensioni positive tocca il film.
 *
 * Su QUESTO pannello è la misura che conta più di tutte. Il codice modello
 * EL122H6W4A ha campo colore 4, cioè BWRY nominale, mentre il vetro dice
 * "Newton PRO 12.2" BWR normal": il frame a bande da solo non scioglie la
 * contraddizione, perchè se il film avesse quattro pigmenti ma l'OTP aliasasse
 * LUT3 su LUT2 — che è esattamente quello che la Table 6-4 descrive — le bande
 * 3 e 4 uscirebbero identiche e si concluderebbe "tre colori" per un difetto
 * della waveform, non del film.
 *
 * Tempi: quattro fasi da 50 frame, gruppo non ripetuto, cioè 200 frame in
 * tutto. Un ordine di grandezza sotto la waveform di produzione: abbastanza
 * per muovere il pigmento, non abbastanza per sovra-pilotarlo.
 */
static const uint8_t LUT_LEVELS[LUT_BYTES] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0 nero, fermo
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1 bianco, fermo
  0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2 = VSH1 x4 fasi
  0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3 = VSH2 x4 fasi
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x32, 0x32, 0x32, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // TP 50x4, RP 1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

/** Varianti della sonda con LUT custom: le due diagonali della matrice. */
static const uint8_t VARIANTI_LUT = 2;
/** Durate del BUSY delle varianti di partial con LUT custom. */
static int32_t lutPartialMs[VARIANTI_LUT] = { -1, -1 };
/** Durata del BUSY del probe dei livelli di sorgente. */
static int32_t levelsMs = -1;

/**
 * Carica una waveform LUT via 0x32 e imposta il border.
 *
 * Per un partial il border va a 0xC0, HiZ, e non al valore di produzione 0x01
 * che aggancia la cornice a LUT1: agganciata, verrebbe pilotata a ogni passata
 * e lampeggerebbe. È quello che fa _Init_Part() del GDEH116T91.
 */
static void loadWaveformLut(const uint8_t* lut, uint8_t border)
{
  writeCommand(0x3C);
  writeData(border);
  writeCommand(0x32);
  for (uint16_t i = 0; i < LUT_BYTES; ++i)
    writeData(lut[i]);
}

/**
 * Riporta a Normal le due opzioni di contenuto RAM di 0x21.
 *
 * Serve perchè initPanel con CAND_SOLUM riproduce l'init di fabbrica, che
 * scrive 0x21 = 0x08 0x00: A[3:0] = 1000 è **BW Inverse**, cioè il controller
 * legge il complemento del piano 0x24. Con quel registro attivo un byte 0xFF
 * scritto in 0x24 vale BW = 0 e non BW = 1, e la Table 6-4 si applica a
 * rovescio: le due sonde qui sotto ragionano sulle coppie di bit, quindi la
 * prima cosa che fanno è togliere l'inversione. Così l'esito non dipende dalla
 * candidata di init con cui girano, e i due bit sono quelli che la tabella
 * nomina.
 *
 * A[7:4] = opzione Red, A[3:0] = opzione BW: 0000 Normal per entrambe. Il
 * driver in uso normale non scrive mai 0x21 e resta al POR, che è questo.
 */
static void forceRamOptionsNormal()
{
  writeCommand(0x21);
  writeData(0x00);
  writeData(0x00);
}

/** Power on esplicito, come lo fa il GDEH116T91 prima di ogni _Update_Part(). */
static void powerOnExplicit()
{
  writeCommand(0x22);
  writeData(0xC0);
  writeCommand(0x20);
  waitBusy(2000);
}

/**
 * Sonda del partial con LUT caricata dall'MCU, l'unica strada che le altre non
 * potevano provare: le passate della sonda d'area usano 0xFC e 0xF4, che hanno
 * il bit 4 di 0x22 attivo e ricaricano la waveform dall'OTP. Misurano sempre
 * quella, e una durata pari al refresh pieno dice che in OTP ce n'è una sola,
 * non che il partial sia impossibile.
 *
 * La sequenza è quella del GDEH116T91, stesso command set con gli stessi 960
 * source, che così fa 700 ms:
 *
 *   0x3C = 0xC0        border HiZ
 *   0x32 + 105 byte    waveform breve, scritta dall'MCU
 *   0x26 <- frame precedente, 0x24 <- frame nuovo
 *   0x22 = 0xCC        bit 4 SPENTO: non ricarica l'OTP sopra la LUT custom
 *   0x20
 *
 * Due varianti, le DIAGONALI della matrice LUT per Display Mode, ognuna sulla
 * propria fascia con la propria cifra:
 *
 *   fascia 1  cifra 1  LUT del 1160, 0x22 = 0xCC (Mode 2)
 *   fascia 2  cifra 2  LUT riassegnata alla Table 6-4, 0x22 = 0xC4 (Mode 1)
 *
 * Mode 1 contro Mode 2 conta perchè cambia il significato delle due RAM: in
 * Mode 2 sono (precedente, nuovo), in Mode 1 la Table 6-4 le legge come
 * (accent, BW). Le due LUT sono costruite per le due letture, e quale dipinge
 * dice quale delle due il silicio applica. Le celle incrociate sono mute per
 * costruzione — in Mode 2 la fascia nera cade su LUT2, a zero nella
 * riassegnata, e in Mode 1 su LUT0, a zero in quella del 1160 — quindi non si
 * provano: sarebbero due refresh da venti secondi che non possono dipingere.
 *
 * Esito, in ordine di importanza: durata sotto i 3 s CON la sua cifra sul vetro
 * = partial trovato; sotto i 3 s SENZA niente = la waveform non pilota, va
 * aggiustata ma la strada è quella; durata piena = il controller ha ricaricato
 * l'OTP e la LUT custom è stata ignorata.
 *
 * Ogni variante riparte da reset più init, perchè il SWRESET rimette le LUT
 * dell'OTP e nessuna deve trovare i registri sporchi dalla precedente.
 */
static void probePartialLut()
{
  Serial.println(F("\n=== sonda del partial con LUT caricata via 0x32 ==="));
  Serial.println(F("le passate 0xFC e 0xF4 della sonda d'area ricaricano la waveform"));
  Serial.println(F("dall'OTP (bit 4 di 0x22), quindi misurano sempre quella. Qui la"));
  Serial.println(F("waveform la scrive l'MCU e il bit 4 è spento, come fa il"));
  Serial.println(F("GDEH116T91 sullo stesso command set per stare a 700 ms."));

  if (LUTP_H < GLYPH_H * LABEL_SCALE + 8)
  {
    Serial.printf("fasce di %u righe: troppo basse per la cifra, sonda saltata\n",
                  (unsigned)LUTP_H);
    return;
  }

  /**
   * Nessuna baseline, ed è un refresh risparmiato senza perdere niente: se una
   * variante dipinge, ridipinge lei tutto lo schermo secondo la RAM, che il
   * pattern porta a bianco più le fasce; se non dipinge, il vetro resta come
   * l'ha lasciato la sonda precedente, ed è esattamente l'esito da leggere.
   */
  struct Variante
  {
    const uint8_t* lut;
    const char*    lutName;
    uint8_t        sequence;
    uint16_t       y;
    uint8_t        digit;
  };
  /**
   * Le due diagonali della matrice, e solo quelle. Le celle incrociate — LUT
   * del 1160 in Mode 1 e LUT riassegnata in Mode 2 — sono MUTE PER
   * COSTRUZIONE: in Mode 1 la fascia nera cade su LUT0, che nella LUT del 1160
   * è a zero, e in Mode 2 cade su LUT2, che nella riassegnata è a zero. Non
   * potendo dipingere, servivano da controllo del modello di lettura delle due
   * RAM; ma quel controllo lo dà già quale delle due diagonali dipinge, e a
   * costo zero. Due refresh da venti secondi in meno.
   */
  const Variante varianti[VARIANTI_LUT] =
  {
    { LUT_PARTIAL_1160, "LUT 1160 (Mode 2)",      0xCC, (uint16_t)(LUTP_H * 0), 1 },
    { LUT_PARTIAL_T64,  "LUT Table 6-4 (Mode 1)", 0xC4, (uint16_t)(LUTP_H * 1), 2 },
  };

  /**
   * Entrambe le varianti ridipingono, quindi il riquadro esce col numero della
   * passata che ha davvero dipinto. Con una LUT custom può uscire incompleto,
   * perchè la waveform non pilota tutte le LUT.
   */
  apriSchermata(SCH_LUT);

  for (int v = 0; v < VARIANTI_LUT; ++v)
  {
    const Variante& t = varianti[v];
    Serial.printf("\n-- variante %d: %s, 0x22 = 0x%02X, cifra %u\n",
                  v + 1, t.lutName, t.sequence, (unsigned)t.digit);

    /**
     * Reset più init a ogni variante: il SWRESET rimette in RAM le LUT
     * dell'OTP, quindi ognuna parte dallo stesso stato e la LUT custom
     * dell'iterazione precedente non sopravvive per sbaglio. Il pattern
     * ricarica i due piani, che il reset ha azzerato.
     */
    resetPanel();
    initPanel(CAND_DRIVER, EXPECTED_BAND);
    forceRamOptionsNormal();
    patternFill(0x47, bwPatternFor(true), "B/N   ");

    /**
     * 0x26 secondo il Display Mode della passata, e non è un dettaglio.
     *
     * In Mode 2 quella RAM è il frame precedente e va scritta con la polarità
     * del piano BW, cioè a bianco. In Mode 1 invece è ancora l'accent, e
     * lasciarla accesa manderebbe ogni pixel su LUT2 o LUT3 per la Table 6-4:
     * nella LUT riassegnata quelle due sono a zero, quindi le varianti Mode 1
     * non dipingerebbero niente e la misura sarebbe muta per costruzione.
     */
    const bool mode2 = (t.sequence & 0x08) != 0;
    patternFill(0x46, mode2 ? bwPatternFor(true) : 0x77,
                mode2 ? "prec. " : "accent");

    /**
     * Le fasce delle varianti già passate vengono riscritte in RAM: il refresh
     * ridipinge secondo la RAM, quindi la sola fascia corrente cancellerebbe
     * le precedenti, e la scheda chiede di confrontarle fra loro. Va fatto
     * prima di areaPass, che scrive la propria e poi lancia il refresh.
     */
    for (uint8_t d = 1; d < t.digit; ++d)
      writeStripeWithDigit(0x24, (uint16_t)(LUTP_H * (d - 1)), LUTP_H,
                           bwByteFor(false), d, bwByteFor(true));

    loadWaveformLut(t.lut, 0xC0);
    powerOnExplicit();

    lutPartialMs[v] = areaPass("passata con LUT custom", 0, t.y, SRC, LUTP_H,
                               bwByteFor(false), t.digit, bwByteFor(true),
                               t.sequence, 30000);
  }

  Serial.println(F("\nesito della sonda del partial con LUT custom:"));
  Serial.println(F("  variante                     0x22    BUSY    cifra"));
  for (int v = 0; v < VARIANTI_LUT; ++v)
  {
    const Variante& t = varianti[v];
    if (lutPartialMs[v] < 0)
      Serial.printf("  %-24s   0x%02X   non conclusa   %u\n",
                    t.lutName, t.sequence, (unsigned)t.digit);
    else
      Serial.printf("  %-24s   0x%02X   %6ld ms   %u\n",
                    t.lutName, t.sequence, (long)lutPartialMs[v], (unsigned)t.digit);
  }
  if (refreshMs > 0)
    Serial.printf("  riferimento: refresh pieno 0xF7 %ld ms\n", (long)refreshMs);

  int veloci = 0;
  for (int v = 0; v < VARIANTI_LUT; ++v)
    if (lutPartialMs[v] >= 0 && lutPartialMs[v] < 3000)
      ++veloci;
  inizioOsservazione("PARTIAL CON LUT CUSTOM: quale fascia è nera");
  static const char* const OSS_LUT[] =
  {
    "fascia con la cifra 1   LUT 1160, Mode 2, 0x22 = 0xCC",
    "fascia con la cifra 2   LUT Table 6-4, Mode 1, 0x22 = 0xC4",
    "",
    "quale delle due fasce è diventata nera?",
    "  una fascia NERA con la sua durata sotto i 3 s -> IL PARTIAL ESISTE, e",
    "    va nel driver come _Init_Part più _Update_Part",
    "  NESSUNA nera con durate sotto i 3 s -> la LUT viene accettata ma non",
    "    pilota: la waveform va aggiustata, la strada è giusta",
    "  NESSUNA nera con durate piene -> il silicio ricarica l'OTP anche col bit",
    "    4 spento, oppure ignora la LUT scritta via 0x32",
    "",
    "guarda anche la CORNICE: con 0x3C = 0xC0 non deve lampeggiare",
    "il riquadro col numero può uscire incompleto: la LUT custom non pilota",
    "tutte le LUT, e quello che non pilota resta come stava",
  };
  rigaOsservazioneF("passate sotto i 3 s: %d", veloci);
  righeOsservazione(OSS_LUT, sizeof(OSS_LUT) / sizeof(OSS_LUT[0]));
  fineOsservazione();
  chiudiSchermata();

  // I registri restano con una LUT custom in RAM: un reset rimette l'OTP.
  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  Serial.println(F("  reset hardware finale: le LUT tornano quelle dell'OTP"));
}

/**
 * Probe del quarto colore per livello di sorgente, la misura che il frame a
 * bande non può fare. Su questo pannello conta: il codice modello EL122H6W4A ha
 * campo colore 4, cioè BWRY nominale, contro un vetro che dice "BWR normal".
 * Le bande mostrano cosa rende ogni coppia di bit sotto la waveform dell'OTP,
 * ma se lì LUT3 è aliasata su LUT2 — quello che la Table 6-4 descrive per un
 * film a tre colori — le bande 3 e 4 escono identiche anche su un film a
 * quattro pigmenti.
 *
 * Qui LUT2 va a VSH1 e LUT3 a VSH2, stessi tempi, e le due metà della banda
 * portano la coppia di bit che le seleziona: metà alta (accent=1, BW=1) su
 * LUT3, metà bassa (accent=1, BW=0) su LUT2.
 *
 * Mode 1 (0x22 = 0xC4) e non Mode 2, perchè è Mode 1 che legge le due RAM
 * secondo la Table 6-4; in Mode 2 sarebbero (precedente, nuovo) e la coppia di
 * bit non selezionerebbe più la LUT che interessa. Bit 4 spento, o l'OTP
 * sovrascrive la LUT appena caricata. Le tensioni restano quelle di fabbrica:
 * VSH1 e VSH2 stanno ai byte 106 e 107, fuori dalla portata di 0x32.
 */
static void probeFourthColorLevels()
{
  Serial.println(F("\n=== probe del quarto colore: LUT2 a VSH1 contro LUT3 a VSH2 ==="));
  Serial.println(F("il frame a bande dice cosa rende ogni coppia di bit sotto la waveform"));
  Serial.println(F("dell'OTP, ma non distingue un film a tre pigmenti da una waveform che"));
  Serial.println(F("ne pilota solo tre: la Table 6-4 aliasa LUT3 su LUT2 proprio così."));
  Serial.println(F("Qui le due LUT vanno a due tensioni diverse, che è il modo in cui un"));
  Serial.println(F("film BWRY separa rosso e giallo."));

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  forceRamOptionsNormal();

  /**
   * Schermata aperta sul fondo bianco e non sulla passata dei livelli: LUT0 e
   * LUT1 sono a zero in LUT_LEVELS, quindi lì un riquadro bianco e nero non
   * verrebbe pilotato. Disegnato adesso, con la waveform dell'OTP, resta sul
   * vetro perchè la passata dei livelli non tocca quei pixel.
   */
  apriSchermata(SCH_LIVELLI);

  // Fondo bianco, così le due metà partono dallo stesso stato.
  patternFill(0x47, 0xF7, "B/N   ");
  patternFill(0x46, 0x77, "accent");
  if (runRefresh() < 0)
  {
    Serial.println(F("fondo bianco non riuscito: probe dei livelli abbandonato"));
    chiudiSchermata();
    return;
  }

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  forceRamOptionsNormal();
  loadWaveformLut(LUT_LEVELS, 0x01);

  /**
   * Accent acceso su tutta la banda, e il piano BW che distingue le due metà:
   * la metà alta con BW=1 seleziona LUT3, quella bassa con BW=0 seleziona LUT2.
   * Con LUT0 e LUT1 a zero nessun pixel bianco o nero viene pilotato, quindi
   * qualunque cosa si veda viene dalle due LUT dell'accent.
   */
  patternFill(0x46, 0xF7, "accent");                          // accent = 1 ovunque
  patternFill(0x47, 0xF7, "B/N   ");                          // BW = 1 ovunque
  writeBoxConst(0x24, 0, LEVELS_SPLIT, SRC,
                (uint16_t)(EXPECTED_BAND - LEVELS_SPLIT), 0x00);  // metà bassa: BW = 0

  powerOnExplicit();
  setRamWindow(0, 0, SRC, EXPECTED_BAND);
  levelsMs = runRefresh(0xC4, "Mode 1 senza ricarica di LUT, qualche secondo");
  if (levelsMs >= 0)
    Serial.printf("  BUSY %ld ms (200 frame attesi: la LUT non ripete il gruppo)\n",
                  (long)levelsMs);

  inizioOsservazione("QUARTO COLORE: le due metà della banda");
  rigaOsservazioneF("split a y=%u", (unsigned)LEVELS_SPLIT);
  static const char* const OSS_LIVELLI[] =
  {
    "metà ALTA  accent=1 BW=1 -> LUT3 -> VSH2",
    "metà BASSA accent=1 BW=0 -> LUT2 -> VSH1",
    "",
    "DOMANDA: le due metà hanno colore diverso?",
    "  DIVERSO  -> il film separa VSH1 da VSH2: esiste un quarto stato, e il",
    "    driver può arrivarci con la coppia di bit che ha già più una LUT",
    "    custom. Su questo pannello sarebbe la conferma del campo colore 4",
    "  IDENTICO -> tre pigmenti, ed è la prova diretta che il vetro dice il",
    "    vero: BWR, e il campo colore 4 marca la linea",
    "  ENTRAMBE BIANCHE -> 200 frame non bastano, oppure la LUT custom non",
    "    viene applicata: se nemmeno la sonda del partial ha dipinto, il probe",
    "    è inconcludente",
    "",
    "il riquadro col numero è stato dipinto dal fondo bianco: la passata dei",
    "livelli non tocca i pixel bianchi e neri, quindi lì resta invariato",
  };
  righeOsservazione(OSS_LIVELLI, sizeof(OSS_LIVELLI) / sizeof(OSS_LIVELLI[0]));
  fineOsservazione();
  chiudiSchermata();

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  Serial.println(F("  reset hardware finale: le LUT tornano quelle dell'OTP"));
}

/**
 * Determina la polarità del piano BW e se 0x21 = 0x08 la inverte davvero.
 *
 * Due frame consecutivi con la RAM IDENTICA e solo 0x21 diverso: è l'unico modo
 * di isolare quel registro, e rende la risposta indipendente dal datasheet, che
 * qui non è affidabile — la Rev 1.0 definisce 0x21 con un parametro solo mentre
 * l'init di fabbrica SOLUM gliene scrive due, e la decodifica di A[3:0] = 1000
 * come "BW inverse" viene da quella revisione.
 *
 * Frame 1, 0x21 al POR, cioè la condizione in cui gira il driver: fascia 1 con
 * 0x24 = 0xFF, fascia 2 con 0x24 = 0x00. La più chiara dice quale bit rende
 * bianco, e il riferimento è nello stesso frame.
 * Frame 2, 0x21 = 0x08 0x00 e RAM non riscritta: se i colori si scambiano, 0x08
 * inverte il piano BW; se restano dov'erano, quel valore non inverte niente su
 * questo silicio e l'init di fabbrica non correggeva nessuna polarità.
 *
 * Due refresh e due pause: è la sonda più economica del test, e senza il suo
 * esito ogni riga della scheda che nomini un colore è un'ipotesi.
 */
static void probeBwPolarity()
{
  Serial.println(F("\n=== polarità del piano BW: due frame, cambia solo 0x21 ==="));
  Serial.printf("configurazione attuale del test: %s\n", polaritaLabel());
  Serial.println(F("il datasheet su questo punto non basta: la Rev 1.0 definisce 0x21 con"));
  Serial.println(F("un solo parametro, l'init di fabbrica gliene scrive due, e la"));
  Serial.println(F("decodifica di A[3:0] = 1000 come BW inverse viene da quella revisione."));
  Serial.println(F("Qui la RAM resta identica fra i due frame: l'unica variabile è 0x21."));

  const uint16_t fasciaH = (uint16_t)(EXPECTED_BAND / 3);
  if (fasciaH < GLYPH_H * LABEL_SCALE + 8)
  {
    Serial.printf("fasce di %u righe: troppo basse per la cifra, sonda saltata\n",
                  (unsigned)fasciaH);
    return;
  }

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  forceRamOptionsNormal();

  /**
   * Accent spento e le due fasce scritte una volta sola. I byte sono espressi
   * come costanti, 0xFF e 0x00, e non passano dagli helper della polarità: è
   * proprio la loro resa che questa sonda deve misurare, quindi assumerla qui
   * sarebbe circolare.
   */
  patternFill(0x46, 0x77, "accent");
  patternFill(0x47, 0xF7, "B/N   ");
  writeStripeWithDigit(0x24, 0, fasciaH, 0xFF, 1, 0x00);
  writeStripeWithDigit(0x24, fasciaH, fasciaH, 0x00, 2, 0xFF);

  /**
   * Finestra piena: writeStripeWithDigit la lascia sulla propria fascia, e le
   * due fasce devono essere ridipinte entrambe, altrimenti il confronto fra
   * loro non esiste. Il frame 2 non riscrive niente, quindi eredita questa.
   */
  setRamWindow(0, 0, SRC, EXPECTED_BAND);

  apriSchermata(SCH_POL_1);
  const int32_t msPor = runRefresh(0xF7, "frame 1, 0x21 al POR");

  inizioOsservazione("POLARITÀ DEL PIANO BW, frame 1: 0x21 al POR");
  static const char* const OSS_POL1[] =
  {
    "fascia con la cifra 1 (in alto):  0x24 pieno di 0xFF",
    "fascia con la cifra 2 (in mezzo): 0x24 pieno di 0x00",
    "",
    "DOMANDA 1: quale delle due è BIANCA?",
    "  la cifra 1 -> bit = 1 rende bianco, la convenzione del datasheet",
    "  la cifra 2 -> bit = 1 rende NERO, il piano è invertito",
    "la risposta te la chiede il test qui sotto e la applica subito: non c'è",
    "niente da mettere nel sorgente",
    "",
    "qui il riquadro col numero usa le stesse costanti nude delle fasce, quindi",
    "se la polarità è inversa esce in negativo: non disturba il confronto",
  };
  righeOsservazione(OSS_POL1, sizeof(OSS_POL1) / sizeof(OSS_POL1[0]));

  /**
   * La risposta si chiede qui e si applica subito: è quello che toglie la
   * ricompilazione a metà bring-up. Senza risposta entro il timeout la polarità
   * resta quella che era, e il test prosegue come prima.
   */
  static const char* const VOCI_FRAME1[] = { "la cifra 1", "la cifra 2",
                                             "non lo so / non si vede" };
  /**
   * Con le pause a 0 il run è dichiaratamente non presidiato: non si chiede
   * niente, così non blocca nessuno, e la polarità resta quella di prima.
   */
  const int rispostaBianca = (cfg.pausaMs == 0)
                             ? 2
                             : chiediScelta("  quale fascia è BIANCA?", VOCI_FRAME1, 3, 2,
                                            cfg.pausaMs);

  observePause(cfg.pausaMs, "parte il frame 2, identico ma con 0x21 = 0x08 0x00");
  fineOsservazione();
  chiudiSchermata();

  /**
   * Solo 0x21 cambia. Nessun init, nessuna riscrittura dei piani: se i colori
   * si muovono, il merito è di quel registro e di nient'altro.
   */
  writeCommand(0x21);
  writeData(0x08);
  writeData(0x00);
  apriSchermata(SCH_POL_2);
  const int32_t msInv = runRefresh(0xF7, "frame 2, 0x21 = 0x08 0x00, RAM invariata");

  inizioOsservazione("POLARITÀ DEL PIANO BW, frame 2: la RAM non è stata toccata");
  static const char* const OSS_POL2[] =
  {
    "DOMANDA 2: le due fasce si sono SCAMBIATE di colore rispetto al frame 1?",
    "  SI -> 0x21 = 0x08 inverte davvero il piano BW su questo silicio. L'init",
    "    di fabbrica SOLUM lo scrive, quindi chi riproduce quell'init lavora su",
    "    un piano invertito e la Table 6-4 va letta a rovescio. Il driver non",
    "    scrive 0x21 e non ha il problema",
    "  NO -> quel valore non inverte niente qui: o il secondo byte cambia il",
    "    significato del primo, o A[3:0] non è l'opzione BW su questo silicio.",
    "    In entrambi i casi l'init di fabbrica non stava correggendo una",
    "    polarità, e le due letture coincidono",
  };
  righeOsservazione(OSS_POL2, sizeof(OSS_POL2) / sizeof(OSS_POL2[0]));
  rigaOsservazioneF("durate: frame 1 %ld ms, frame 2 %ld ms; devono essere uguali,",
                    (long)msPor, (long)msInv);
  rigaOsservazione("0x21 non tocca la waveform e uno scarto grande è misura sporca");

  const bool scambiate = (cfg.pausaMs == 0)
                         ? false
                         : chiediSN("  le due fasce si sono SCAMBIATE?", false, cfg.pausaMs);

  /**
   * Il frame 1 gira con 0x21 al POR, che è la condizione del driver: è quello a
   * dare la polarità. La risposta al frame 2 non la cambia, dice se 0x21 = 0x08
   * inverte, ed è un'informazione sull'init di fabbrica.
   */
  if (rispostaBianca == 0 || rispostaBianca == 1)
  {
    cfg.bwPolarity = (rispostaBianca == 0) ? BW_DATASHEET : BW_INVERSE;
    salvaConfig();
    rigaOsservazioneF("polarità determinata e salvata: %s", polaritaLabel());
    rigaOsservazione("vale da adesso per ogni sonda, e la ritrovi al prossimo boot:");
    rigaOsservazione("niente da mettere nel sorgente, niente da ricompilare");
  }
  else
    rigaOsservazione("polarità non determinata: resta da misurare, e le etichette");

  rigaOsservazioneF("0x21 = 0x08 %s il piano BW su questo silicio",
                    scambiate ? "INVERTE" : "non inverte");

  observePause(cfg.pausaMs, "riparte il test");
  fineOsservazione();
  chiudiSchermata();

  forceRamOptionsNormal();
}

// --- banchi di waveform per temperatura ------------------------------

/**
 * Range di esercizio dichiarato dal produttore, concorde in tre fonti di
 * docs/: Newton-PRO_Data-sheet §3.1 "Operating Temperature — BWRY: 0°C ~ 40°C"
 * e le due Specifications. Fuori di qui nessun TR dell'OTP copre la richiesta,
 * ed è la condizione che rende la passata di controllo un rifiuto atteso.
 */
static const int16_t TEMP_RANGE_MIN = 0;
static const int16_t TEMP_RANGE_MAX = 40;

/** Vero se la temperatura chiesta cade fuori dal range di esercizio. */
static bool tempFuoriRange(int16_t degC)
{
  return degC < TEMP_RANGE_MIN || degC > TEMP_RANGE_MAX;
}

/**
 * Pavimento del timeout per le passate di questa sonda, l'unica che ne chiede
 * uno più lungo. Un banco per temperatura può essere più corto di quello
 * ambiente, che è la ragione della sonda, ma può anche essere più LUNGO: al
 * freddo il pigmento migra più lentamente e le waveform dei range bassi sono
 * più lunghe. Coi 40 s del default la passata fredda scade prima di
 * concludersi, e la misura va persa proprio dove il banco è diverso dagli
 * altri.
 */
static const uint32_t TEMP_TIMEOUT_MS = 120000;

/**
 * Timeout effettivo della sonda: il pavimento sopra, o il timeout scelto dal
 * menu se è più alto. Alzare cfg.timeoutMs non deve accorciare proprio la
 * passata che ne ha più bisogno.
 */
static uint32_t tempTimeoutMs()
{
  return cfg.timeoutMs > TEMP_TIMEOUT_MS ? cfg.timeoutMs : TEMP_TIMEOUT_MS;
}

/**
 * Cifra della fascia di ogni passata: le prime tre ne dipingono una, l'ultima
 * è la passata di controllo e non ha cifra propria — riscrive a bianco quella
 * della terza, vedi probeTemperatureBanks(). Tre punti dentro il range bastano
 * alla domanda "l'OTP ha più di un banco?": se gli estremi e il centro danno la
 * stessa durata, un quarto in mezzo non cambierebbe la conclusione.
 */
static const uint8_t TEMP_SWEEP_DIGIT[TEMP_PASSES] = { 1, 2, 3, 0 };
static int32_t tempSweepMs[TEMP_PASSES]            = { -1, -1, -1, -1 };

/**
 * Forza la temperatura con cui il controller sceglie la waveform.
 *
 * 0x18 = 0x48 mette il sensore su esterno: da quel momento il controller non
 * campiona il proprio sensore ma legge il registro scritto con 0x1A. Formato
 * del paragrafo 6.8.3: 12 bit in complemento a due, valore = gradi per sedici,
 * quindi 25 gradi = 0x190 e 50 gradi = 0x320. Sul bus i 12 bit vanno come li
 * restituisce 0x1B in lettura, A[11:4] nel primo byte e A[3:0] nei bit alti
 * del secondo.
 */
static void setForcedTemperature(int16_t degC)
{
  const int16_t raw = (int16_t)(degC * 16);
  writeCommand(0x18);
  writeData(0x48);
  writeCommand(0x1A);
  writeData((uint8_t)((raw >> 4) & 0xFF));
  writeData((uint8_t)((raw << 4) & 0xF0));
}

/** Rimette il sensore interno, che è quello che il driver usa. */
static void restoreInternalTemperature()
{
  writeCommand(0x18);
  writeData(0x80);
}

/**
 * Sonda dei banchi di waveform per temperatura.
 *
 * Il §6.9 del datasheet, "Waveform LUT Searching Mechanism", dà l'OTP per
 * capace di 34 set, WS0..WS33, uno per range di temperatura TR0..TR33, che il
 * controller sceglie leggendo la temperatura e caricando il set dell'ULTIMO
 * range che corrisponde. Ogni passata di questo test ha girato col sensore
 * interno a temperatura ambiente, quindi ha esercitato un set su 34.
 *
 * Perchè conta per il partial: qui non si inventa una waveform, si chiede al
 * silicio una di quelle di fabbrica, tarate su questo film. Sui pannelli a
 * inchiostro le waveform calde sono più corte, perchè il pigmento migra prima:
 * se una lo è, il partial esiste senza rischio e senza LUT non qualificate.
 *
 * Le prime tre passate stampano una fascia numerata; l'ultima è la passata di
 * controllo e va messa fuori dal range dichiarato 0..40 gradi: il datasheet
 * avverte che senza un range corrispondente "display will not be updated",
 * quindi un BUSY brevissimo là è un rifiuto e non una waveform veloce. Quella
 * passata riscrive a bianco la fascia dell'ultima riuscita, così il
 * discriminante è la sparizione della cifra 3. Con un valore dentro 0..40 il
 * rifiuto non è atteso e il controllo non discrimina: la sonda lo dice.
 *
 * Rischio nessuno: sono waveform di fabbrica, e il reset riporta il sensore a
 * quello interno.
 */
static void probeTemperatureBanks()
{
  Serial.println(F("\n=== sonda dei banchi di waveform per temperatura ==="));
  Serial.println(F("il paragrafo 6.9 dà l'OTP per capace di 34 set di waveform, uno per"));
  Serial.println(F("range di temperatura, scelti dal silicio in base alla temperatura"));
  Serial.println(F("letta. Tutte le passate di questo test hanno girato col sensore interno"));
  Serial.println(F("a temperatura ambiente: qui si prova a chiederne altri, e sono waveform"));
  Serial.println(F("di fabbrica tarate su questo film. Una più LUNGA non è un fallimento"));
  Serial.println(F("della sonda: dice che i banchi esistono e che verso il freddo la"));
  Serial.printf ("waveform si allunga, e per questo il timeout qui è %lu ms.\n",
                 (unsigned long)tempTimeoutMs());

  const uint16_t fasciaH = (uint16_t)(EXPECTED_BAND / 4);
  if (fasciaH < GLYPH_H * LABEL_SCALE + 8)
  {
    Serial.printf("fasce di %u righe: troppo basse per la cifra, sonda saltata\n",
                  (unsigned)fasciaH);
    return;
  }

  // le fasce si accumulano su un'unica schermata: il riquadro lo ridisegna
  // ogni passata, quindi resta anche se l'ultima viene rifiutata
  apriSchermata(SCH_TEMP);

  /**
   * Nessuna baseline: ogni passata porta la RAM a bianco più le proprie fasce,
   * quindi se dipinge stabilisce lei il fondo, e se non dipinge il vetro resta
   * quello di prima, che è l'esito. Un refresh in meno.
   */
  for (int t = 0; t < TEMP_PASSES; ++t)
  {
    const int16_t degC  = cfg.temp[t];
    const uint8_t digit = TEMP_SWEEP_DIGIT[t];
    const int16_t raw   = (int16_t)(degC * 16);

    Serial.printf("\n-- %d gradi (0x1A = 0x%03X)%s\n", (int)degC, (unsigned)(raw & 0xFFF),
                  digit ? ""
                        : (tempFuoriRange(degC)
                             ? ", fuori range: qui un BUSY corto è un rifiuto"
                             : ", passata di controllo, ma dentro 0..40: il rifiuto"
                               " non è atteso"));

    resetPanel();
    initPanel(CAND_DRIVER, EXPECTED_BAND);
    forceRamOptionsNormal();
    setForcedTemperature(degC);
    patternFill(0x47, bwPatternFor(true), "B/N   ");
    patternFill(0x46, 0x77, "accent");

    if (digit)
    {
      /**
       * Le fasce si accumulano: 0xF7 è un refresh pieno e ridipinge tutto
       * secondo la RAM, quindi la sola fascia corrente cancellerebbe le
       * precedenti. Con l'accumulo una passata rifiutata lascia sul vetro le
       * fasce dell'ultima riuscita, ed è il confronto che serve.
       */
      for (uint8_t d = 1; d <= digit; ++d)
        writeStripeWithDigit(0x24, (uint16_t)(fasciaH * (d - 1)), fasciaH,
                             bwByteFor(false), d, bwByteFor(true));
    }
    else
    {
      /**
       * Le fasce 1 e 2 come sono, e la 3 riportata a bianco: se la passata
       * viene eseguita la cifra 3 sparisce dal vetro, se viene rifiutata
       * resta. Il discriminante è quello, e non serve una fascia in più.
       */
      for (uint8_t d = 1; d <= 2; ++d)
        writeStripeWithDigit(0x24, (uint16_t)(fasciaH * (d - 1)), fasciaH,
                             bwByteFor(false), d, bwByteFor(true));
      writeBoxConst(0x24, 0, (uint16_t)(fasciaH * 2), SRC, fasciaH, bwByteFor(true));
    }

    // finestra piena: la variabile di questa sonda è la waveform, non l'area
    setRamWindow(0, 0, SRC, EXPECTED_BAND);

    /**
     * 0xF7 e non una sequenza ridotta: i bit 5 e 4 sono "load temperature" e
     * "load LUT", e servono entrambi, perchè è proprio la ricarica che deve
     * andare a cercare il set del range forzato.
     */
    tempSweepMs[t] = runRefresh(0xF7, "0xF7 con temperatura forzata", tempTimeoutMs());

    /**
     * Timeout non vuol dire misura persa: il refresh è ancora in corso, e il
     * reset della passata successiva a metà transizione lascerebbe il pigmento
     * in uno stato non noto e quella passata senza baseline. Si attende la
     * discesa e si stampa la durata vera, che è il dato interessante proprio
     * perchè esce dal quadro delle altre.
     */
    if (tempSweepMs[t] < 0 && digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      const uint32_t scaduto = tempTimeoutMs();
      const int32_t  coda    = waitBusy(scaduto);
      if (coda >= 0)
        Serial.printf("   BUSY sceso altri %ld ms dopo il timeout: il banco di questo range"
                      "\n   dura %ld ms, cioè più di quello a temperatura ambiente\n",
                      (long)coda, (long)((int32_t)scaduto + coda));
      else
        Serial.printf("   BUSY ancora attivo dopo altri %lu ms: il pannello resta a metà"
                      " transizione\n", (unsigned long)scaduto);
    }
    restoreInternalTemperature();
  }

  Serial.println(F("\nesito della sonda dei banchi per temperatura:"));
  Serial.println(F("  temperatura   0x1A    BUSY       cifra"));
  for (int t = 0; t < TEMP_PASSES; ++t)
  {
    const int16_t raw = (int16_t)(cfg.temp[t] * 16);
    if (tempSweepMs[t] < 0)
      Serial.printf("  %4d gradi   0x%03X   non conclusa   %u\n",
                    (int)cfg.temp[t], (unsigned)(raw & 0xFFF),
                    (unsigned)TEMP_SWEEP_DIGIT[t]);
    else
      Serial.printf("  %4d gradi   0x%03X   %6ld ms   %u\n",
                    (int)cfg.temp[t], (unsigned)(raw & 0xFFF),
                    (long)tempSweepMs[t], (unsigned)TEMP_SWEEP_DIGIT[t]);
  }
  if (refreshMs > 0)
    Serial.printf("  riferimento: refresh pieno a temperatura ambiente %ld ms\n",
                  (long)refreshMs);

  int32_t minMs = -1, maxMs = -1;
  for (int t = 0; t < TEMP_PASSES - 1; ++t)   // le passate che dipingono la propria fascia
  {
    if (tempSweepMs[t] < 0) continue;
    if (minMs < 0 || tempSweepMs[t] < minMs) minMs = tempSweepMs[t];
    if (maxMs < 0 || tempSweepMs[t] > maxMs) maxMs = tempSweepMs[t];
  }
  if (minMs > 0 && maxMs > 0)
  {
    Serial.printf("  scarto fra la più corta e la più lunga: %ld ms\n",
                  (long)(maxMs - minMs));
    if ((maxMs - minMs) > 2000)
      Serial.println(F("  l'OTP ha più di un set, e uno è più corto: la fascia di quella\n"
                       "  passata lo dice, vedi il blocco qui sotto"));
    else
      Serial.println(F("  scarto trascurabile: un solo set di waveform per tutto il range\n"
                       "  utile, e la temperatura non è una leva su questo pannello"));
  }

  inizioOsservazione("BANCHI PER TEMPERATURA: quali fasce hanno dipinto");
  static const char* const OSS_TEMP[] =
  {
    "ogni passata dentro il range lascia la propria cifra sulla sua fascia, e",
    "la tabella qui sopra le elenca con la loro durata",
    "",
    "una fascia dipinta con una durata molto sotto le altre è un banco più",
    "veloce, e il driver può forzarne la temperatura via 0x18 / 0x1A prima del",
    "refresh",
    "",
    "la cifra 3 è ancora sul vetro dopo la passata di controllo?",
    "  sì -> quel refresh è stato rifiutato, che è quello che il datasheet",
    "    descrive quando nessun range corrisponde: la sua durata breve non è",
    "    una waveform veloce",
    "  no -> quella waveform ha dipinto in una frazione del tempo",
  };
  righeOsservazione(OSS_TEMP, sizeof(OSS_TEMP) / sizeof(OSS_TEMP[0]));
  fineOsservazione();
  chiudiSchermata();
}

// --- RAM ping-pong: l'interruttore che il datasheet lega al differenziale ---

/**
 * Sonda del RAM ping-pong via 0x37, F[6].
 *
 * 0x37 sono dieci byte che di norma vengono dall'OTP: B[7:0]..F[3:0] portano il
 * bit di Display Mode per ognuno dei 36 stadi della waveform, F[6] abilita il
 * RAM ping-pong per Mode 2, e G..J sono module ID e versione della waveform. È
 * l'unico interruttore che il datasheet lega esplicitamente al differenziale,
 * con la nota che il ping-pong "is not support for Display Mode 1".
 *
 * TENTATIVO ALLA CIECA, e va letto come tale: senza read-back non si sa cosa
 * l'OTP abbia scritto in quei dieci byte, quindi scriverli significa
 * sostituirli con un'ipotesi. Solo un esito POSITIVO vale qualcosa; un esito
 * negativo non distingue "il ping-pong non serve" da "abbiamo scritto i bit
 * sbagliati". Per questo la sonda chiude con un reset hardware, che rimette i
 * valori dell'OTP.
 *
 * La fascia porta la cifra 1 su fondo bianco: se dopo la passata sul vetro
 * resta la cifra della sonda precedente, questa non ha dipinto.
 */
static void probePingPong()
{
  Serial.println(F("\n=== sonda del RAM ping-pong via 0x37 F[6]: TENTATIVO ALLA CIECA ==="));
  Serial.println(F("0x37 sono dieci byte che di norma vengono dall'OTP e portano i bit di"));
  Serial.println(F("Display Mode dei 36 stadi della waveform. Senza read-back non sappiamo"));
  Serial.println(F("cosa ci sia scritto: solo un esito POSITIVO qui vale qualcosa."));

  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  forceRamOptionsNormal();

  /**
   * Nessuna baseline: la passata scrive una fascia BIANCA su fondo nero, e se
   * dipinge è lei a stabilire entrambi. 0x26 fa da frame precedente e riceve
   * il fondo nero, cioè quello che il frame nuovo sostituisce.
   */
  patternFill(0x47, bwPatternFor(false), "B/N   ");
  patternFill(0x46, bwPatternFor(false), "prec. ");

  writeCommand(0x37);
  writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF);
  writeData(0x40);   // F[6] = RAM ping-pong
  writeData(0x00); writeData(0x00); writeData(0x00); writeData(0x00);

  const uint16_t fasciaH = (uint16_t)(EXPECTED_BAND / 4);
  writeStripeWithDigit(0x24, 0, fasciaH, bwByteFor(true), 1, bwByteFor(false));

  // finestra piena, come nelle altre sonde che misurano una waveform
  setRamWindow(0, 0, SRC, EXPECTED_BAND);

  apriSchermata(SCH_PINGPONG);
  const int32_t ms = runRefresh(0xFC,
                                "sotto il secondo solo se l'ipotesi sui bit è giusta",
                                40000);
  if (ms >= 0)
    Serial.printf("  BUSY %ld ms\n", (long)ms);
  if (refreshMs > 0)
    Serial.printf("  riferimento: refresh pieno %ld ms\n", (long)refreshMs);
  if (!(ms > 0 && refreshMs > 0 && ms < refreshMs / 4))
    Serial.println(F("  nessun guadagno: ma essendo un tentativo alla cieca su dieci byte\n"
                     "  che vengono dall'OTP, questo esito non prova niente"));

  inizioOsservazione("RAM PING-PONG: la fascia con la cifra 1");
  static const char* const OSS_PP[] =
  {
    "la fascia in cima porta la cifra 1 ed è stata scritta bianca su fondo",
    "nero",
    "",
    "è diventata bianca?",
    "  sì, con una durata molto sotto il refresh pieno -> il ping-pong funziona",
    "    e il differenziale esiste",
    "  no -> la passata non ha dipinto, e una durata breve lì è un refresh",
    "    ingoiato. Essendo un tentativo alla cieca su dieci byte che vengono",
    "    dall'OTP, un esito negativo non prova niente",
  };
  righeOsservazione(OSS_PP, sizeof(OSS_PP) / sizeof(OSS_PP[0]));
  fineOsservazione();
  chiudiSchermata();

  Serial.println(F("\nreset hardware: rimetto i valori OTP dopo il tentativo su 0x37"));
  resetPanel();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
}

// --- deep sleep: quale parametro accetta il modulo -------------------

/**
 * Prova decisiva di sordità: si manda al controller un'operazione che da
 * sveglio si vedrebbe di sicuro sul BUSY — riempimento della RAM col pattern
 * più la master activation di un refresh — e si guarda se il BUSY fa quello
 * che farebbe se il comando fosse stato eseguito.
 *
 * Serve perchè il livello del BUSY da solo non prova niente: il datasheet lo
 * dà alto durante il deep sleep, ma su questa board un pin non cablato sta alto
 * lo stesso, ed è proprio il caso che il test deve saper distinguere sulla coda
 * che non risponde. Il criterio dipende quindi da come il BUSY sta mentre il
 * controller dorme:
 *
 *   BUSY alto  -> da sveglio il refresh finirebbe e il BUSY scenderebbe. Se
 *                 non scende entro la finestra, il comando non è stato eseguito.
 *   BUSY basso -> da sveglio il refresh alzerebbe il BUSY entro un secondo. Se
 *                 non si alza, il comando non è stato eseguito.
 *
 * Se il controller esegue, la banda diventa nera e si vede: è comunque un
 * esito, non un danno. Ritorna true se ha ignorato tutto, cioè se dorme.
 */
static bool deepSleepIgnoresCommands(uint32_t window_ms)
{
  const bool busyAlto = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  Serial.printf("   prova di sordità: BUSY %s, si manda pattern nero + refresh\n",
                busyAlto ? "alto" : "basso");

  writeCommand(0x47);   // pattern sul piano B/N: da sveglio riempirebbe la RAM
  writeData(0x77);      // tutto nero
  writeCommand(0x22);
  writeData(0xF7);
  writeCommand(0x20);   // master activation

  if (busyAlto)
  {
    const int32_t ms = waitBusy(window_ms);
    if (ms < 0)
    {
      Serial.printf("   BUSY mai sceso in %lu ms: il comando non è stato eseguito, dorme\n",
                    (unsigned long)window_ms);
      return true;
    }
    Serial.printf("   BUSY sceso dopo %ld ms: il refresh è stato eseguito, NON dorme\n",
                  (long)ms);
    return false;
  }

  const uint32_t t0 = millis();
  while ((millis() - t0) < 2000)
  {
    if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      // ha preso il comando: si attende la fine per non lasciare il pannello a metà
      const int32_t ms = waitBusy(window_ms);
      Serial.printf("   BUSY salito e refresh durato %ld ms: NON dorme\n", (long)ms);
      return false;
    }
    delay(1);
  }
  Serial.println(F("   BUSY mai salito in 2000 ms: la master activation è stata"
                   " ignorata, dorme"));
  return true;
}

/**
 * Sonda del deep sleep: non "quale parametro alza il BUSY", che è solo un
 * livello su un pin, ma se il controller si addormenta davvero e se poi si
 * risveglia e torna a stampare. Un deep sleep da cui non si torna non è un
 * risparmio, è una banda morta fino al power cycle.
 *
 * Il datasheet SSD1677 definisce per 0x10 solo A[1:0] = 00 (normale) e 11, cioè
 * 0x03, e dà il BUSY alto in deep sleep; 0x11 ha A[1:0] = 01, che nella tabella
 * non c'è ed è il valore del driver stock 1160c. Quale dei due il modulo
 * accetti decide il byte che hibernate() deve mandare.
 *
 * In ordine: prova i due parametri, ognuno da reset più init perchè dal deep
 * sleep non si esce altrimenti e senza ripartire da sveglio la seconda misura
 * leggerebbe la coda della prima; sul parametro scelto manda da addormentato un
 * refresh intero e guarda il BUSY più a lungo di quanto un refresh duri; lascia
 * una finestra ferma per il multimetro, che è l'unica prova del risparmio;
 * cronometra il risveglio e stampa una banda nera, che o arriva o no; e infine
 * riaddormenta, così il test finisce nello stato in cui hibernate() lascerebbe
 * il pannello.
 *
 * QUESTA SONDA VEDE UN SOLO CONTROLLER: il CS dell'altra coda resta alto, quindi
 * l'altro non riceve mai 0x10 e resta sveglio. Nel driver hibernate() deve
 * mandarlo a entrambi; il reset invece è in parallelo sulle due code.
 */
static void probeDeepSleep()
{
  Serial.println(F("\ndeep sleep: si addormenta, e soprattutto si sveglia?"));

  // finestra di attesa più lunga di un refresh pieno, che è il metro della prova
  const uint32_t finestra = (uint32_t)(refreshMs > 0 ? refreshMs : 20000) + 8000;

  static const uint8_t PARAM[2] = { 0x03, 0x11 };
  static const char* PARAM_DESC[2] =
  {
    "A[1:0]=11, l'unico valore che il datasheet definisce",
    "A[1:0]=01, fuori tabella, ed è quello del driver stock 1160c"
  };
  bool candidato[2] = { false, false };

  for (uint8_t k = 0; k < 2; ++k)
  {
    Serial.printf("\n-- 0x10 = 0x%02X  (%s)\n", PARAM[k], PARAM_DESC[k]);
    // reset hardware più init: si riparte svegli e da uno stato noto
    initPanel(CAND_DRIVER, EXPECTED_BAND);
    writeCommand(0x10);
    writeData(PARAM[k]);
    delay(50);

    const bool busyAlto = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
    if (PARAM[k] == 0x03)
      sleepBusy03 = busyAlto;
    else
      sleepBusy11 = busyAlto;
    Serial.printf("   BUSY dopo il comando: %s\n",
                  busyAlto ? "ALTO, come il datasheet descrive il deep sleep"
                           : "basso, che il datasheet non prevede per il deep sleep");

    if (busyAlto)
    {
      candidato[k] = true;
      Serial.println(F("   livello compatibile col deep sleep, ma un pin non cablato fa"
                       " lo stesso:\n   la conferma arriva dalla prova lunga più sotto"));
      continue;
    }

    /**
     * Col BUSY basso la prova costa poco: da sveglio il pattern hardware lo
     * alza entro qualche decina di ms, e se non lo alza i comandi non arrivano.
     *
     * Vale però solo dove il pattern è supportato, e la prova di vita fatta a
     * inizio test lo dice: senza quel comando un BUSY fermo non distingue
     * "dorme" da "il comando non esiste", e prenderlo per sonno sarebbe un
     * falso positivo. In quel caso si rimanda alla prova decisiva, che usa la
     * master activation e quella un controller vivo la esegue sempre.
     */
    if (patternMs47 < 0)
    {
      candidato[k] = true;
      Serial.println(F("   il pattern hardware non è supportato su questo pannello: la"
                       "\n   prova rapida non decide, risponde la prova lunga più sotto"));
      continue;
    }
    writeCommand(0x47);
    writeData(0x77);
    const uint32_t t0 = millis();
    bool reagisce = false;
    while ((millis() - t0) < 300)
    {
      if (digitalRead(PIN_BUSY) == BUSY_ACTIVE) { reagisce = true; break; }
      delay(1);
    }
    if (reagisce)
    {
      const int32_t ms = waitBusy(5000);
      Serial.printf("   il pattern ha alzato il BUSY per %ld ms: il controller esegue"
                    " ancora, NON dorme\n", (long)ms);
    }
    else
    {
      candidato[k] = true;
      Serial.println(F("   il pattern non ha alzato il BUSY: i comandi non arrivano più,"
                       " dorme"));
    }
  }

  // si preferisce 0x03, che è il valore del datasheet, se entrambi sono plausibili
  int scelto = -1;
  for (uint8_t k = 0; k < 2; ++k)
  {
    if (candidato[k]) { scelto = k; break; }
  }
  if (scelto < 0)
  {
    Serial.println(F("\nnessuno dei due parametri addormenta il controller: su questo"
                     " modulo\nil deep sleep non è osservabile, e hibernate() potrebbe"
                     " non fare niente.\nPrima di contarci come risparmio, misura la"
                     " corrente col multimetro."));
    sleepParamOk = 0x00;
    return;
  }
  sleepParamOk = PARAM[scelto];
  Serial.printf("\nparametro scelto per il resto della sonda: 0x10 = 0x%02X\n", sleepParamOk);

  // prova decisiva sul parametro scelto, ripartendo da sveglio
  Serial.println(F("\n-- prova decisiva: da addormentato, un refresh intero viene eseguito?"));
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  writeCommand(0x10);
  writeData(sleepParamOk);
  delay(50);
  sleepIgnoresCmd = deepSleepIgnoresCommands(finestra);

  if (!sleepIgnoresCmd)
  {
    Serial.println(F("\nil controller esegue anche dopo 0x10: quello che si vede sul BUSY"
                     "\nnon è deep sleep. La banda adesso è nera perchè il refresh di"
                     "\nprova è passato davvero."));
    return;
  }

  /**
   * Finestra ferma per il multimetro. È l'unico modo di sapere se il deep sleep
   * serve a qualcosa: il firmware può dire che il controller ignora i comandi,
   * non quanto assorbe. Si misura sul 3V3 che alimenta il pannello, e va tenuto
   * presente che l'altro controller qui è sveglio, quindi il valore letto è il
   * consumo di uno che dorme più uno che sta fermo.
   */
  inizioOsservazione("DEEP SLEEP: l'immagine resta intatta mentre dorme?");
  static const char* const OSS_SLEEP1[] =
  {
    "sul vetro c'è ancora la schermata di prima: entrare in deep sleep non",
    "deve sporcarla",
    "",
    "MISURA ORA LA CORRENTE sul 3V3 del pannello e confrontala con quella a",
    "controller sveglio e fermo: è l'unica prova del risparmio, e il firmware",
    "non può farla",
  };
  righeOsservazione(OSS_SLEEP1, sizeof(OSS_SLEEP1) / sizeof(OSS_SLEEP1[0]));
  rigaOsservazioneF("il controller resta addormentato per %lu s",
                    (unsigned long)(cfg.sleepMs / 1000));
  for (uint32_t left = cfg.sleepMs / 1000; left >= 5; left -= 5)
  {
    rigaOsservazioneF("  %lu s", (unsigned long)left);
    delay(5000);
  }
  fineOsservazione();

  // risveglio cronometrato: è il costo che il firmware paga a ogni sonno
  Serial.println(F("\n-- risveglio: reset hardware più init"));
  const uint32_t tWake = millis();
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  wakeInitMs = (int32_t)(millis() - tWake);
  Serial.printf("   reset più init in %ld ms\n", (long)wakeInitMs);

  /**
   * Refresh di prova a banda nera. Il nero è distinguibile da qualunque cosa ci
   * fosse prima, quindi se arriva il ciclo dormi / svegliati / stampa funziona
   * per intero, che è la sola cosa che il firmware deve sapere.
   */
  apriSchermata(SCH_SLEEP);
  patternFill(0x47, 0x77, "B/N   ");   // piano B/N tutto nero
  patternFill(0x46, 0x77, "accent");   // accent spento
  wakeRefreshMs = runRefresh(0xF7, "refresh di prova dopo il risveglio, una ventina di secondi");

  inizioOsservazione("DEEP SLEEP: il refresh di prova dopo il risveglio");
  static const char* const OSS_SLEEP2[] =
  {
    "la banda è diventata NERA?",
    "  sì -> il ciclo dormi / svegliati / stampa funziona e hibernate() è",
    "    utilizzabile: dopo il risveglio il driver rifà init e riscrive i",
    "    piani, perchè il deep sleep non conserva la RAM",
    "  no -> da lì non si torna, e hibernate() lascerebbe la banda morta fino",
    "    al power cycle",
  };
  righeOsservazione(OSS_SLEEP2, sizeof(OSS_SLEEP2) / sizeof(OSS_SLEEP2[0]));
  fineOsservazione();
  chiudiSchermata();

  // stato finale come lo lascerebbe hibernate()
  writeCommand(0x10);
  writeData(sleepParamOk);
  delay(50);
  Serial.printf("\ncontroller riaddormentato con 0x10 = 0x%02X, BUSY %s\n",
                sleepParamOk,
                digitalRead(PIN_BUSY) == BUSY_ACTIVE ? "alto" : "basso");
}

/**
 * Prova a pilotare il secondo controller senza un secondo chip select,
 * sommando CMD_OFFSET a ogni opcode indirizzato. È la trascrizione
 * dell'idioma di docs/openepaperlink/nrf52811_tag_fw/dualssd.cpp, dove un
 * pannello a due controller SSD sta su un solo CS e il secondo si distingue
 * solo per il bit 7 dell'opcode.
 *
 * Cosa aspettarsi, e come leggerlo:
 *   - si accende la banda che questa coda NON serve  -> ipotesi confermata: il
 *     driver va rifatto su un solo CS con gli opcode offset, e la coda muta non
 *     era rotta;
 *   - si accende la stessa banda di sempre           -> il controller di questa
 *     coda risponde a entrambe le forme, quindi l'offset non seleziona niente;
 *   - non si accende niente                          -> nessuna conferma, e
 *     restano in piedi le tre ipotesi di cablaggio.
 *
 * Il BUSY non è un testimone affidabile qui: appartiene al controller che
 * risponde ai comandi normali, e quello non viene attivato. Per questo il
 * timeout di runRefresh non è un errore ma un esito previsto, e l'attesa
 * prosegue a tempo.
 */
/**
 * Offset degli opcode diretti allo slave: è il valore del firmware di fabbrica
 * SOLUM, e la sonda elettrica del bit 7 ha già misurato se questo controller lo
 * interpreta o lo esegue.
 */
static const uint8_t CMD_OFFSET = 0x80;

static int32_t probeSlaveByOpcodeOffset()
{
  Serial.println(F("\n--- sonda a video: secondo controller via opcode|0x80 ---"));
  Serial.println(F("configurazione: la più conclusiva delle quattro che si potevano provare"));
  Serial.println(F("  - i due chip select tenuti bassi insieme, come sul tag di fabbrica"));
  Serial.println(F("  - comandi comuni in broadcast, come dualssd.cpp di OEPL"));
  Serial.println(F("  - master messo in cascade con 0x21 B[4] = 1, così emette CL"));
  Serial.println(F("Le varianti non si provano più a ricompilazione: quello che distingueva"));
  Serial.println(F("un caso dall'altro era se il bit 7 indirizza, e la sonda elettrica del"));
  Serial.println(F("bit 7 lo ha già misurato senza spendere un refresh."));
  Serial.println(F("Resta il promemoria che conta: senza un ponte che porti CL e i rail dal"));
  Serial.println(F("connettore master a quello slave, un esito negativo non prova niente."));

  // comandi comuni in broadcast: senza offset, come fa dualssd.cpp
  const uint8_t comune = 0x00;

  // i due chip select bassi insieme riproducono il cablaggio di fabbrica
  csBoth = true;

  /**
   * Reset e SWRESET senza offset. Attenzione al significato: con i due
   * controller su un solo chip select "senza offset" vuol dire "al master", non
   * "a entrambi". Il broadcast vero esiste solo finchè i due CS sono separati e
   * si tengono bassi insieme, cioè con SLAVE_OPCODE_BOTH_CS a 1.
   */
  cmdOffset = 0x00;
  resetPanel();
  writeCommand(0x12);
  delay(200);

  /**
   * Configurazione del MASTER. Non è un di più: è lui che genera clock e alte
   * tensioni per entrambi, ed è la sua master activation che chiude la sequenza,
   * quindi la sua banda viene scandita comunque. Lasciarlo ai default POR
   * significherebbe fargli scandire 680 gate line invece delle 384 misurate.
   */
  writeSoftStart();
  writeMux(EXPECTED_BAND);                // le 384 gate line misurate
  writeCommand(0x11);                     // entry mode
  writeData(0x03);
  writeCommand(0x3C);                     // border waveform
  writeData(0x01);
  writeCommand(0x18);                     // sensore di temperatura interno
  writeData(0x80);
  /**
   * Cascade selection: senza questo bit il master non emette CL e lo slave resta
   * senza clock, quindi non esegue niente per quanto bene lo si indirizzi.
   * A = 0x00 lascia entrambi i piani in Normal, B[4] = 1 accende la cascade.
   *
   * Va sempre al MASTER, quindi senza offset anche quando i comandi comuni sono
   * offsettati: è una proprietà del chip che genera il clock, non dello slave.
   */
  cmdOffset = 0x00;
  writeCommand(0x21);
  writeData(0x00);
  writeData(0x10);
  cmdOffset = comune;

  /**
   * Configurazione dello SLAVE, con l'offset. I due riferimenti — dualssd.cpp di
   * OEPL e GxEPD2_579c_GDEY0579Z93 di GxEPD2 — allo slave mandano soltanto entry
   * mode, finestra RAM e dati: in cascade l'analogico è disabilitato, quindi
   * soft start e sensore di temperatura non avrebbero destinatario.
   *
   * Il MUX invece glielo mandiamo, ed è una differenza voluta: in quei due
   * pannelli lo split è sulle sorgenti e le gate line sono comuni, mentre qui
   * ogni controller ha le sue 384 gate. Se non gliele si programma resta al POR
   * di 680. Se il bring-up mostrasse che lo slave non deve avere un MUX
   * proprio, è la prima riga da togliere.
   */
  cmdOffset = CMD_OFFSET;
  const uint16_t muxSlave = EXPECTED_BAND - 1;
  writeCommand(0x01);                     // driver output control -> 0x81
  writeData(uint8_t(muxSlave & 0xFF));
  writeData(uint8_t(muxSlave >> 8));
  writeData(0x00);
  writeCommand(0x11);                     // entry mode -> 0x91
  writeData(0x03);

  const uint32_t msBW  = writePlane(0x24, EXPECTED_BAND, composeRowBW);
  const uint32_t msRED = writePlane(0x26, EXPECTED_BAND, composeRowRED);
  Serial.printf("piani scritti con 0x%02X / 0x%02X: %lu + %lu ms\n",
                (unsigned)(0x24 | CMD_OFFSET), (unsigned)(0x26 | CMD_OFFSET),
                (unsigned long)msBW, (unsigned long)msRED);

  cmdOffset = comune;
  apriSchermata(SCH_SLAVE, false);
  const int32_t ms = runRefresh(0xF7,
                                "una ventina di secondi, se il secondo controller ha preso i comandi");
  cmdOffset = 0x00;
  csBoth = false;
  if (cfg.secondFfc)
    digitalWrite(PIN_CS_OTHER, HIGH);

  if (ms < 0)
  {
    Serial.println(F("BUSY fermo: è l'esito previsto se il refresh è partito sull'altro"));
    Serial.println(F("controller, il cui BUSY non è su questa coda. Attesa a tempo."));
    /**
     * Il BUSY della coda sotto test appartiene al controller che risponde ai
     * comandi normali: se il refresh è partito sull'altro, qui non si vede
     * niente e l'unico modo di attenderlo è a tempo.
     */
    delay(25000);
  }

  inizioOsservazione("SECONDO CONTROLLER a opcode|0x80");
  static const char* const OSS_SLAVE[] =
  {
    "questa schermata non porta il riquadro col numero: la RAM è stata scritta",
    "con gli opcode offset, e un riquadro scritto con opcode nudi finirebbe sul",
    "master, sporcando proprio la banda che deve dire chi ha eseguito",
    "",
    "il pattern di identificazione è comparso, e su quale metà del pannello?",
    "  sulla metà che questa coda NON serve -> gli opcode offset hanno",
    "    indirizzato l'altro controller: la cascade è confermata",
    "  sulla metà di sempre -> il bit 7 non indirizza niente su questo silicio,",
    "    e gli opcode sono stati eseguiti come se fossero nudi",
    "  niente di nuovo -> i comandi offset sono stati rifiutati, che è la prima",
    "    evidenza a favore dell'addressing in cascade ma non una prova: senza",
    "    CL e senza i rail lo slave non potrebbe stampare comunque",
  };
  righeOsservazione(OSS_SLAVE, sizeof(OSS_SLAVE) / sizeof(OSS_SLAVE[0]));
  fineOsservazione();
  chiudiSchermata();

  slaveOpcodeMs = ms;
  return ms;
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
 * Sweep delle tre candidate di init, tutto elettrico: nessun refresh, nessuna
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
  Serial.println(F("\n=== sweep delle candidate di init: elettrico, nessun refresh ==="));

  for (uint8_t c = 0; c < CAND_COUNT; ++c)
  {
    Serial.printf("\n-- candidata %u/%u: %s\n", c + 1, CAND_COUNT, CAND_LABEL[c]);
    // MUX al conteggio reale per tutte, tranne MINIMAL che di suo non lo scrive
    const int32_t sw = initPanel(c, (c == CAND_MINIMAL) ? MUX_NOT_WRITTEN : EXPECTED_BAND);
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

  Serial.println(F("\n=== sonda del bit 7 dell'opcode: elettrica, nessun refresh ==="));
  Serial.println(F("confronto fra opcode e opcode|0x80 sulla reazione del BUSY"));

  for (uint8_t pass = 0; pass < 2; ++pass)
  {
    const uint8_t off = pass ? 0x80 : 0x00;
    Serial.printf("\n-- gruppo %s\n", pass ? "OFFSET (opcode | 0x80)" : "NUDO (opcode come da datasheet)");
    initPanel(CAND_DRIVER, EXPECTED_BAND);
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
    Serial.println(F("  nessun opcode nudo ha reagito: la sonda non decide, il controller\n"
                     "  non sta eseguendo niente e il problema è a monte"));
  else if (eseguiti == confrontabili)
    Serial.println(F("  questo controller ESEGUE gli opcode offset: il bit 7 non seleziona\n"
                     "  niente su questo silicio. ADDRESSING_CASCADE del driver non ha un\n"
                     "  meccanismo di indirizzamento, e il modello a due chip select resta\n"
                     "  l'unica strada per la seconda banda"));
  else if (eseguiti == 0)
    Serial.println(F("  questo controller RIFIUTA gli opcode offset: il bit 7 è uno spazio\n"
                     "  di indirizzamento distinto, ed è la prima evidenza positiva a favore\n"
                     "  della cascade. Nel driver ADDRESSING_CASCADE ha senso, e per provarlo\n"
                     "  serve il secondo connettore con il ponte per CL e i rail"));
  else
    Serial.println(F("  esito misto: alcuni opcode offset passano e altri no. Va guardato\n"
                     "  opcode per opcode nella tabella qui sopra prima di concludere"));
}

/**
 * Quanto pesa il MUX sulla durata del refresh? Tre refresh e nessuna pausa: la
 * risposta sta nel BUSY, non sul vetro.
 *
 * Per ogni valore di MUX_SWEEP si rifà l'init, si riempiono le due RAM con i
 * pattern hardware 0x47/0x46 — che il controller scrive da sè, senza push SPI,
 * così il cronometro misura solo il refresh — e si lancia un refresh pieno.
 *
 * Era il "ripeti con MUX_LINES = 680" della vecchia procedura, cioè una seconda
 * esecuzione intera. Cosa decide nel driver: se la durata scala con le gate
 * programmate, _InitDisplay() deve tenere il MUX al conteggio reale per non
 * buttare tempo a ogni frame; se non scala, il MUX si scrive solo per avere la
 * mappatura gate giusta e full_refresh_time può restare conservativo.
 */
static int32_t muxSweepMs[3] = { -1, -1, -1 };

static void sweepMuxTiming()
{
  Serial.println(F("\n=== sweep del MUX: 3 refresh cronometrati, nessuna pausa ==="));
  Serial.println(F("le RAM le riempiono i pattern hardware, così il tempo è tutto del refresh"));

  for (uint8_t k = 0; k < 3; ++k)
  {
    const uint16_t mux = cfg.mux[k];
    Serial.printf("\n-- MUX %s\n", muxEtichetta(k));
    initPanel(CAND_DRIVER, mux);
    patternFill(0x47, 0xF7, "B/N   ");
    patternFill(0x46, 0xF7, "accent");
    muxSweepMs[k] = runRefresh(0xF7, "cronometraggio, niente da guardare", 60000);
  }

  Serial.println(F("\nesito dello sweep del MUX:"));
  for (uint8_t k = 0; k < 3; ++k)
    Serial.printf("  MUX %-26s %ld ms\n",
                  muxEtichetta(k),
                  (long)muxSweepMs[k]);
  if (muxSweepMs[1] > 0 && muxSweepMs[2] > 0)
  {
    const double r = (double)muxSweepMs[2] / (double)muxSweepMs[1];
    Serial.printf("  680 contro 384: %.2fx a fronte di 1.77x di gate line\n", r);
    if (r > 1.35)
      Serial.println(F("  la durata scala con le gate programmate: nel driver il MUX va\n"
                       "  tenuto al conteggio reale, ogni riga in più è tempo a ogni frame"));
    else
      Serial.println(F("  la durata NON dipende dalle gate programmate: la lunghezza della\n"
                       "  waveform è fissata dalla OTP. Il MUX si scrive per la mappatura\n"
                       "  gate, non per il tempo, e full_refresh_time resta conservativo"));
  }
  if (muxSweepMs[0] > 0 && muxSweepMs[1] > 0)
  {
    const double r = (double)muxSweepMs[0] / (double)muxSweepMs[1];
    Serial.printf("  default OTP contro 384: %.2fx\n", r);
    if (r < 1.12 && r > 0.89)
      Serial.println(F("  uguali: o la OTP programma già 384, o il MUX non pesa. Per il\n"
                       "  driver le due cose sono equivalenti"));
  }
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
  Serial.println(F("\n=== costo del push di un piano ai tre clock ==="));
  for (uint8_t k = 0; k < 3; ++k)
  {
    spiSettings = SPISettings(CLOCKS[k], MSBFIRST, SPI_MODE0);
    pushMsAtClock[k] = writePlane(0x24, EXPECTED_BAND, composeRowBW);
    Serial.printf("  %2lu MHz   %lu ms per piano (%lu byte)\n",
                  (unsigned long)(CLOCKS[k] / 1000000UL),
                  (unsigned long)pushMsAtClock[k],
                  (unsigned long)ROW_BYTES * EXPECTED_BAND);
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
 */
static void writePlaneDualClock(uint8_t planeCommand,
                                void (*compose)(int16_t, uint8_t*, uint16_t))
{
  const uint32_t CLOCKS[2] = { cfg.spiBase, cfg.spiFast };
  const uint16_t half = EXPECTED_BAND / 2;
  uint8_t row[ROW_BYTES];

  for (uint8_t k = 0; k < 2; ++k)
  {
    const uint16_t y0 = k ? half : 0;
    const uint16_t h  = k ? (uint16_t)(EXPECTED_BAND - half) : half;
    spiSettings = SPISettings(CLOCKS[k], MSBFIRST, SPI_MODE0);
    setRamWindow(0, y0, SRC, h);
    writeCommand(planeCommand);
    digitalWrite(PIN_DC, HIGH);
    hspi.beginTransaction(spiSettings);
    csAssert();
    for (uint16_t y = y0; y < y0 + h; ++y)
    {
      compose((int16_t)y, row, EXPECTED_BAND);
      hspi.writeBytes(row, ROW_BYTES);
    }
    csRelease();
    hspi.endTransaction();
  }
  spiSettings = SPISettings(cfg.spiBase, MSBFIRST, SPI_MODE0);
}

/**
 * Frame di identificazione della candidata corrente: lo stesso pattern del
 * vecchio frame 1 (cornice, righelli numerati, blocchi d'angolo, scaletta
 * diagonale) con il numero della candidata al centro, scritto ai due clock.
 *
 * È il solo frame che si ripete per ogni candidata, ed è quello che risponde
 * alle domande che il BUSY non può toccare: se l'immagine esce, con quale
 * verso, quante gate line sono davvero pilotate, e se il bus tiene i 20 MHz.
 */
static int32_t showIdentityFrame()
{
  Serial.printf("\n--- frame: identificazione, candidata %u (%u MHz sopra, %u MHz sotto) ---\n",
                g_cand + 1,
                (unsigned)(cfg.spiBase / 1000000UL), (unsigned)(cfg.spiFast / 1000000UL));
  const uint32_t t0 = millis();
  writePlaneDualClock(0x24, composeRowBW);
  writePlaneDualClock(0x26, composeRowRED);
  Serial.printf("  due piani scritti in %lu ms\n", (unsigned long)(millis() - t0));
  return runRefresh();
}

// =====================================================================
// FASE PROBE: diagnostica a SPI diretta, una coda alla volta
// =====================================================================
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
    Serial.printf("BUSY su GPIO%d: i pin 34..39 non hanno pull interni, la prova\n"
                  "  \"pilotato o flottante\" non è eseguibile su questo pin.\n"
                  "  Per averla, portalo su 4, 21 o 22.\n", PIN_BUSY);
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
static int32_t identityMs[CAND_COUNT] = { -1, -1, -1 };

/**
 * Ordine dell'esecuzione, e perchè è questo.
 *
 *   1. cablaggio: BUSY a riposo e prova dei pull. Se il BUSY non è pilotato,
 *      niente di quello che segue significa qualcosa, e si scopre in 100 ms.
 *   2. sweep elettrici, senza refresh e senza pause: le tre candidate di init e
 *      il bit 7 dell'opcode. Sono ~12 s e rispondono a due delle domande che
 *      prima costavano quattro esecuzioni.
 *   3. sweep del MUX: tre refresh cronometrati, nessuna pausa. Non c'è niente
 *      da guardare, quindi non ci si ferma.
 *   4. costo del push ai tre clock del driver.
 *   5. frame di identificazione, uno per candidata che al punto 2 ha risposto.
 *      Sono le prove che richiedono l'occhio, e ognuna si paga una pausa.
 *   6. frame delle bande e dei box, con la candidata su cui il driver è
 *      modellato: colori dei quattro stati dei piani e addressing in X.
 *   7. sonda del partial d'area, che riparte da una baseline bianca e quindi
 *      cancella tutto: viene dopo tutti i frame che vanno guardati.
 *   8. sonda del bit 7 a video, solo se il secondo connettore è cablato: con
 *      un solo FFC l'ha già chiusa il punto 2, elettricamente.
 *   9. deep sleep, che lascia il pannello nello stato in cui hibernate() lo
 *      lascerebbe.
 */
static void runProbePhase()
{
  Serial.println(F("\n=================================================="));
  Serial.println(F(" FASE PROBE - SPI diretta, nessuno strato software"));
  Serial.print  (F(" coda      : ")); Serial.println(tailLabel());
  Serial.printf (" pattern   : %u source x %u gate\n", SRC, EXPECTED_BAND);
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

  // ---- 2) sweep elettrici: nessun refresh, nessuna pausa ----
  if (sondaAttiva(S_CANDIDATE, "sweep delle candidate di init"))
    sweepInitCandidates();
  if (sondaAttiva(S_BIT7, "sonda elettrica del bit 7"))
    sweepOpcodeBit7();

  // ---- 3) sweep del MUX: refresh cronometrati, nessuna pausa ----
  if (sondaAttiva(S_MUX, "sweep del MUX"))
    sweepMuxTiming();
  /**
   * Il refresh di riferimento per tutti i confronti del report è quello con la
   * candidata del driver e il MUX al conteggio reale, cioè la configurazione
   * che il driver custom usa davvero.
   */
  refreshMs = muxSweepMs[1];

  // ---- 4) costo del push ai tre clock ----
  initPanel(CAND_DRIVER, EXPECTED_BAND);
  if (sondaAttiva(S_CLOCK, "costo del push ai tre clock"))
    sweepPushClocks();

  // ---- 5) frame di identificazione, uno per candidata ----
  if (sondaAttiva(S_IDENT, "frame di identificazione"))
  {
    Serial.println(F("\n=== frame di identificazione, uno per candidata ==="));
    Serial.println(F("da qui in avanti serve l'occhio: ogni frame si paga una pausa"));

    /** Righe del blocco di osservazione, uguali per tutte e tre le candidate. */
    static const char* const OSS_IDENT[] =
    {
      "si vede qualcosa, e quale metà del pannello ha reagito?",
      "l'ultima etichetta del righello Y è il conteggio delle gate line pilotate",
      "il blocco NERO sta nell'origine RAM e quello ACCENT nell'angolo opposto in",
      "  X: se sono scambiati la banda è specchiata, e va detto al driver con",
      "  setMasterMirror() / setSlaveMirror()",
      "i numeri sono diritti e crescono verso il basso e verso destra?",
      "la metà bassa è scritta a 20 MHz: se esce sporca solo lei, il driver deve",
      "  stare più basso",
      "",
      "il riquadro col numero finisce dentro il blocco accent dell'angolo, e lo",
      "punzona: il blocco resta comunque largo 96 px al bordo destro",
    };
    for (uint8_t c = 0; c < CAND_COUNT; ++c)
    {
      if (!candProbe[c].vivo)
      {
        Serial.printf("\ncandidata %u (%s) saltata: elettricamente muta, un frame\n"
                      "e una pausa sarebbero buttati\n", c + 1, CAND_LABEL[c]);
        continue;
      }
      initPanel(c, (c == CAND_MINIMAL) ? MUX_NOT_WRITTEN : EXPECTED_BAND);
      apriSchermata((Schermata)(SCH_IDENT_0 + c));
      identityMs[c] = showIdentityFrame();

      inizioOsservazione("FRAME DI IDENTIFICAZIONE");
      rigaOsservazioneF("candidata %u di init: %s", c + 1, CAND_LABEL[c]);
      righeOsservazione(OSS_IDENT, sizeof(OSS_IDENT) / sizeof(OSS_IDENT[0]));
      observePause(cfg.pausaMs, (c + 1 < CAND_COUNT)
                   ? "arriva lo stesso pattern con la candidata di init successiva"
                   : "arriva il frame delle bande e dei box");
      fineOsservazione();
      chiudiSchermata();
    }
  }

  /**
   * 5b) polarità del piano BW, prima di ogni frame che nomini un colore. Due
   * refresh con la RAM identica e solo 0x21 diverso: senza questo esito le
   * etichette "BW = 1" delle bande sono un'ipotesi, perchè l'init di fabbrica
   * scrive 0x21 = 0x08 0x00 e la decodifica di quel valore viene da una
   * revisione del datasheet più vecchia del silicio.
   */
  if (sondaAttiva(S_POLARITA, "polarità del piano BW"))
    probeBwPolarity();

  // ---- 6) bande dei piani + box a finestra parziale ----
  if (sondaAttiva(S_BANDE, "frame delle 4 bande e dei box"))
  {
    initPanel(CAND_DRIVER, EXPECTED_BAND);
    apriSchermata(SCH_BANDE);
    bandsRefreshMs = showBandsAndBoxesFrame();

    inizioOsservazione("4 BANDE DEI PIANI più i box a finestra parziale");
    static const char* const OSS_BANDE[] =
    {
      "le quattro bande sono le quattro combinazioni dei piani 0x24 e 0x26, e",
      "ognuna porta la propria cifra: che colore rende ognuna?",
      "  la quarta combinazione è quella che il driver non genera mai: se è un",
      "  colore a sè, esiste un quarto stato",
      "",
      "i tre box neri nella banda 1 sono allineati ed equidistanti? se scivolano,",
      "  si sovrappongono o si smarginano, la finestra parziale in X è rotta e",
      "  con essa ogni writeImagePart del driver",
      "il box accent nella banda 2 prova la stessa finestra sul piano 0x26",
      "",
      "le bande sono uniformi, senza ghosting del frame precedente?",
    };
    righeOsservazione(OSS_BANDE, sizeof(OSS_BANDE) / sizeof(OSS_BANDE[0]));
    observePause(cfg.pausaMs,
                 "parte la sonda del partial d'area, che riporta la banda a bianco");
    fineOsservazione();
    chiudiSchermata();
  }

  // ---- 7) partial d'area ----
  if (sondaAttiva(S_AREA, "partial d'area"))
    probePartialProbe();

  /**
   * 7a-bis) interruttori del differenziale sul banco dell'OTP. Viene prima
   * della LUT custom perchè è l'ordine in cui le domande si escludono: prima si
   * esaurisce quello che l'OTP offre da sè, e solo se non basta si prova a
   * scrivere una waveform.
   */
  if (sondaAttiva(S_DIFF, "differenziale approfondita"))
    probeDifferentialDeeper();

  /**
   * 7b) partial con LUT caricata dall'MCU. Sta dopo la sonda d'area perchè
   * quella stabilisce cosa fa il banco di waveform dell'OTP: solo a quel punto
   * ha senso provare a scriverne una breve via 0x32, che è la strada rimasta.
   */
  if (sondaAttiva(S_LUT, "partial con LUT custom"))
  {
    observePause(cfg.pausaMs,
                 "parte la sonda del partial con LUT custom, che riporta la banda a bianco");
    probePartialLut();
  }

  /**
   * 7c) probe del quarto colore per livello di sorgente. È la misura che il
   * frame a bande non può fare, e su questo pannello è quella che conta: il
   * codice modello ha campo colore 4 contro un vetro che dice BWR.
   */
  if (sondaAttiva(S_LIVELLI, "quarto colore per livello di sorgente"))
  {
    observePause(cfg.pausaMs,
                 "parte il probe del quarto colore, che pilota LUT2 e LUT3 a tensioni diverse");
    probeFourthColorLevels();
    observePause(cfg.pausaMs, "riparte il test");
  }

  // ---- 8) sonda a video del bit 7, solo con il secondo connettore ----
  if (cfg.secondFfc && sondaAttiva(S_SLAVE, "secondo controller a opcode|0x80"))
  {
    observePause(cfg.pausaMs, "parte la sonda del secondo controller a opcode|0x80");
    probeSlaveByOpcodeOffset();
  }
  else
  {
    Serial.println(F("\nsonda |0x80 a video: saltata, e non è una rinuncia. Con un solo"));
    Serial.println(F("connettore lo slave non è sul bus, quindi un frame non potrebbe"));
    Serial.println(F("dire niente su di lui; quello che si poteva misurare l'ha misurato"));
    Serial.println(F("la sonda elettrica del bit 7, senza refresh e senza pause."));
  }

  /**
   * 8b) banchi di waveform per temperatura. È l'unica strada del partial che non
   * chiede di inventare una waveform: il paragrafo 6.9 dà l'OTP per capace di
   * 34 set, uno per range di temperatura, e finora ne è stato esercitato uno
   * solo, quello della stanza.
   */
  if (sondaAttiva(S_TEMP, "banchi per temperatura"))
  {
    observePause(cfg.pausaMs,
                 "parte la sonda dei banchi per temperatura, che riscrive la banda");
    probeTemperatureBanks();
    observePause(cfg.pausaMs,
                 "riparte il test: leggi ORA le fasce e se la cifra 3 c'è ancora");
  }

  /**
   * 8c) RAM ping-pong via 0x37. Chiude l'elenco delle leve del differenziale:
   * è l'unico interruttore che il datasheet lega esplicitamente al Mode 2, ma
   * scrivere quei dieci byte senza read-back è un tentativo alla cieca, quindi
   * vale solo un esito positivo.
   */
  if (sondaAttiva(S_PINGPONG, "RAM ping-pong via 0x37"))
    probePingPong();

  // ---- 9) deep sleep ----
  if (sondaAttiva(S_SLEEP, "deep sleep"))
    probeDeepSleep();

  Serial.println(F("\n============ RIEPILOGO DELLA FASE PROBE ============"));
  Serial.println(F("Le righe marcate -> sono quelle che si traducono direttamente in una"));
  Serial.println(F("costante o in una scelta di GxEPD2_SOLUM_122c_960x768.h."));

  // ---- candidate di init ----
  Serial.println(F("\ncandidate di init, esito elettrico:"));
  Serial.println(F("  candidata     SWRESET  0x47   0x46   pwON  pwOFF   HV   VCI  frame"));
  for (uint8_t c = 0; c < CAND_COUNT; ++c)
  {
    const CandProbe& p = candProbe[c];
    Serial.printf("  %-12s %6ld %6ld %6ld %6ld %6ld %5ld %5ld  ",
                  c == CAND_MINIMAL ? "MINIMAL" : c == CAND_SOLUM ? "SOLUM" : "OEPL",
                  (long)p.swresetMs, (long)p.pattern47Ms, (long)p.pattern46Ms,
                  (long)p.powerOnMs, (long)p.powerOffMs, (long)p.hvMs, (long)p.vciMs);
    if (identityMs[c] > 0) Serial.printf("%ld ms\n", (long)identityMs[c]);
    else                   Serial.println(F("saltato"));
  }
  Serial.println(F("  -> la candidata da tenere in _InitDisplay() è la più corta fra"));
  Serial.println(F("     quelle che stampano un frame corretto: meno registri scritti"));
  Serial.println(F("     sono meno cose su cui i due controller possono divergere"));

  // ---- tarature dirette del driver ----
  Serial.println(F("\ntarature, dal controller che risponde:"));
  Serial.printf("  power on 0x22=0xC0    %ld ms  -> power_on_time, arrotondato per eccesso\n",
                (long)powerOnMs);
  Serial.printf("  power off 0x22=0xC3   %ld ms  -> power_off_time, arrotondato per eccesso\n",
                (long)powerOffMs);
  if (refreshMs > 0)
  {
    Serial.printf("  refresh pieno         %ld ms  -> full_refresh_time con margine, e\n",
                  (long)refreshMs);
    Serial.printf("                                 busy_timeout sopra di lui\n");
  }
  else
    Serial.println(F("  refresh pieno         non concluso"));
  Serial.printf("  HV detect 0x14        %ld ms su 560 massimi%s\n", (long)hvDetectMs,
                (hvDetectMs >= 0 && hvDetectMs < 500) ? "  -> HV arrivata presto"
                                                      : "  -> ciclo completo o timeout");
  Serial.printf("  VCI detect 0x15       %ld ms\n", (long)vciDetectMs);
  if (statusRead >= 0)
    Serial.printf("  status 0x2F           0x%02X (HV %s, VCI %s)\n", (unsigned)statusRead,
                  (statusRead & 0x20) ? "NON pronta" : "pronta",
                  (statusRead & 0x10) ? "fuori norma" : "normale");
  else
    Serial.println(F("  status 0x2F           non leggibile: il connettore non porta SDO"));

  // ---- MUX ----
  Serial.println(F("\nMUX, dallo sweep:"));
  for (uint8_t k = 0; k < 3; ++k)
    Serial.printf("  %-26s %ld ms\n",
                  muxEtichetta(k), (long)muxSweepMs[k]);
  if (muxSweepMs[1] > 0 && muxSweepMs[2] > 0)
  {
    const double r = (double)muxSweepMs[2] / (double)muxSweepMs[1];
    if (r > 1.35)
      Serial.println(F("  -> la durata scala con le gate programmate: _InitDisplay() deve\n"
                       "     tenere il MUX al conteggio reale"));
    else
      Serial.println(F("  -> la durata non dipende dalle gate programmate: il MUX si scrive\n"
                       "     per la mappatura gate, non per il tempo"));
  }

  // ---- clock SPI ----
  Serial.println(F("\nclock SPI, push di un piano:"));
  Serial.printf("  %2lu MHz  %lu ms      %2lu MHz  %lu ms      %2lu MHz  %lu ms\n",
                (unsigned long)(cfg.spiBase / 1000000UL),   (unsigned long)pushMsAtClock[0],
                (unsigned long)(cfg.spiDriver / 1000000UL), (unsigned long)pushMsAtClock[1],
                (unsigned long)(cfg.spiFast / 1000000UL),   (unsigned long)pushMsAtClock[2]);
  Serial.println(F("  -> l'integrità al clock alto la dice il frame di identificazione,"));
  Serial.println(F("     che ha la metà bassa della banda scritta a quel clock"));

  // ---- partial d'area ----
  if (areaMsFirst > 0 || areaMsMode1 > 0)
  {
    Serial.println(F("\npartial d'area:"));
    Serial.printf("  0xFC  %ld ms su %u righe, %ld ms su %u righe\n",
                  (long)areaMsFirst, (unsigned)AREA_P1_H,
                  (long)areaMsThin, (unsigned)AREA_THIN_H);
    Serial.printf("  0xF4  %ld ms su %u righe\n", (long)areaMsMode1, (unsigned)AREA_M1_H);
    if (refreshMs > 0 && areaMsFirst > 0 && areaMsFirst * 2 >= refreshMs)
      Serial.println(F("  -> una passata su finestra costa come un frame intero:\n"
                       "     hasFastPartialUpdate resta false e partial_refresh_time resta\n"
                       "     pari a full_refresh_time. Valgono solo se la fascia di\n"
                       "     trappola è rimasta bianca"));
  }
  else
    Serial.println(F("\npartial d'area: nessuna passata conclusa"));

  // ---- bit 7 dell'opcode ----
  if (cfg.secondFfc)
  {
    if (slaveOpcodeMs >= 0)
      Serial.printf("\nsonda |0x80 a video: refresh concluso in %ld ms, BUSY mosso\n",
                    (long)slaveOpcodeMs);
    else
      Serial.println(F("\nsonda |0x80 a video: BUSY fermo, guarda il pannello e non il tempo"));
  }

  // ---- deep sleep ----
  Serial.printf("\ndeep sleep   BUSY alto: 0x03 %s, 0x11 %s\n",
                sleepBusy03 ? "si" : "no", sleepBusy11 ? "si" : "no");
  if (sleepParamOk == 0x00)
    Serial.println(F("             nessun parametro addormenta il controller: hibernate()\n"
                     "             sarebbe un rischio senza guadagno"));
  else
  {
    Serial.printf("             -> hibernate() deve mandare 0x10 = 0x%02X, che %s\n",
                  sleepParamOk,
                  sleepIgnoresCmd ? "ignora anche un refresh intero: dorme davvero"
                                  : "esegue ancora i comandi: non è deep sleep");
    if (wakeInitMs >= 0)
      Serial.printf("             risveglio %ld ms di reset più init\n", (long)wakeInitMs);
    if (wakeRefreshMs > 0)
      Serial.printf("             primo frame %ld ms -> ciclo completo %.1f s\n",
                    (long)wakeRefreshMs,
                    ((double)(wakeInitMs > 0 ? wakeInitMs : 0) + (double)wakeRefreshMs) / 1000.0);
    else if (sleepIgnoresCmd)
      Serial.println(F("             primo frame NON arrivato: dal deep sleep non si torna"));
  }
}

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
static void runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::AddressingMode mode, bool ultima)
{
  const bool cascade = (mode == GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE);
  apriSchermata(cascade ? SCH_DRIVER_2 : SCH_DRIVER_1);

  Serial.println(F("\n=================================================="));
  Serial.println(F(" FASE DRIVER - GxEPD2_SOLUM_122c_960x768"));
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
    Serial.println(F("  indirizzamento     DUAL CS: un opcode solo, un CS per controller"));
  Serial.printf ("  clock              %lu MHz, il default del driver\n",
                 (unsigned long)(cfg.spiDriver / 1000000UL));
  Serial.println(F("=================================================="));

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
    Serial.println(F("  con un controller solo compaiono i due tile degli angoli alti e la"));
    Serial.println(F("  metà superiore di quello sulla giunzione: gli altri tre cadono nella"));
    Serial.println(F("  banda slave, che non è pilotata. Non è un difetto del dispatch"));
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
  if (componiBadge(schermataCorrente, (uint16_t)W, (uint16_t)SPLIT, 0xFF, 0x00, &bw, &bh))
    driver().writeImage(badgeBuf, (int16_t)(W - bw - 8), 8, bw, bh);

  const uint32_t t0 = millis();
  driver().refresh(false);
  driverTilesMs = (int32_t)(millis() - t0);
  Serial.printf("  refresh in %ld ms\n", (long)driverTilesMs);

  driver().hibernate();
  Serial.println(F("driver in hibernate"));

  inizioOsservazione("FASE DRIVER: fondo, 5 tile e barra accent");
  static const char* const OSS_DRIVER[] =
  {
    "il fondo è bianco e l'accent spento: è la polarità dei due piani come la",
    "scrive il driver",
    "i tile stanno ai quattro angoli e a cavallo della giunzione fra le bande:",
    "  hanno il bordo pieno e un angolo marcato, quindi uno specchiato si",
    "  riconosce. Se cadono fuori posto il dispatch per righe è sbagliato",
    "la barra accent lungo il bordo basso della banda master è l'unico",
    "  elemento che passa dal piano 0x26 del driver",
  };
  righeOsservazione(OSS_DRIVER, sizeof(OSS_DRIVER) / sizeof(OSS_DRIVER[0]));
  if (!ultima)
    observePause(cfg.pausaMs, "la stessa prova riparte con l'altro modello di indirizzamento");
  fineOsservazione();
  chiudiSchermata();
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
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_DUAL_CS, false);
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_CASCADE, true);
  }
  else
  {
    runDriverPass(GxEPD2_SOLUM_DRIVER_CLASS::ADDRESSING_DUAL_CS, true);
    Serial.println(F("\nADDRESSING_CASCADE non provato: con un solo connettore lo slave non"));
    Serial.println(F("è sul bus, quindi la passata darebbe lo stesso frame di DUAL_CS."));
    Serial.println(F("Quello che si poteva sapere sul suo indirizzamento lo ha misurato la"));
    Serial.println(F("sonda elettrica del bit 7 dell'opcode."));
  }
}

/**
 * Scheda di osservazione. Il test non può vedere il pannello, quindi si ferma
 * qui e lascia le domande e la mappa esito -> conseguenza.
 */
static void printObservationSheet()
{
  Serial.println(F("\n============ SCHEDA DI OSSERVAZIONE ============"));
  Serial.println(F("Il numero di ogni voce è quello del riquadro sul vetro e delle righe di"));
  Serial.println(F("log della schermata: [--] vuol dire che quella schermata non è stata"));
  Serial.println(F("prodotta."));

  Serial.println(F("\nschermate prodotte:"));
  for (int i = 0; i < SCH_COUNT; ++i)
  {
    if (schermate[i])
      Serial.printf("  [%u] %s\n", (unsigned)schermate[i], NOME_SCHERMATA[i]);
    else
      Serial.printf("  [--] %s   non prodotta\n", NOME_SCHERMATA[i]);
  }

  static const char* const SCHEDA_IDENT[] =
  {
    "si vede qualcosa?                        SI / NO",
    "quale metà del pannello ha reagito ...............",
    "ultima etichetta del righello Y leggibile ........",
    "blocco NERO: in quale angolo .....................",
    "blocco ACCENT: in quale angolo ...................",
    "colore dell'accent (rosso / giallo / altro) ......",
    "i numeri dei righelli sono diritti?      SI / NO",
    "la barra accent chiude il bordo della banda? SI / NO",
    "la metà bassa, scritta a 20 MHz, è pulita come l'alta?  SI / NO",
  };
  voceScheda(SCH_IDENT_0, "FRAME DI IDENTIFICAZIONE (le stesse domande per ogni candidata)",
             SCHEDA_IDENT, sizeof(SCHEDA_IDENT) / sizeof(SCHEDA_IDENT[0]));

  static const char* const SCHEDA_POL1[] =
  {
    "quale fascia è BIANCA?",
    "  la cifra 1 (0x24 = 0xFF)  ....",
    "  la cifra 2 (0x24 = 0x00)  ....",
  };
  voceScheda(SCH_POL_1, "POLARITÀ DEL PIANO BW, frame 1 con 0x21 al POR",
             SCHEDA_POL1, sizeof(SCHEDA_POL1) / sizeof(SCHEDA_POL1[0]));

  static const char* const SCHEDA_POL2[] =
  {
    "le fasce si sono SCAMBIATE rispetto al frame 1?   SI / NO",
  };
  voceScheda(SCH_POL_2, "POLARITÀ DEL PIANO BW, frame 2 con 0x21 = 0x08",
             SCHEDA_POL2, sizeof(SCHEDA_POL2) / sizeof(SCHEDA_POL2[0]));

  static const char* const SCHEDA_BANDE[] =
  {
    "banda 1  (BW=1 RED=0) .......................",
    "banda 2  (BW=0 RED=0) .......................",
    "banda 3  (BW=1 RED=1) .......................",
    "banda 4  (BW=0 RED=1) .......................",
    "le bande sono uniformi?                  SI / NO",
    "ghosting del frame precedente?           SI / NO",
    "i tre box neri sono allineati ed equidistanti?   SI / NO",
  };
  voceScheda(SCH_BANDE, "FRAME A BANDE più i box a finestra parziale",
             SCHEDA_BANDE, sizeof(SCHEDA_BANDE) / sizeof(SCHEDA_BANDE[0]));

  static const char* const SCHEDA_LIVELLI[] =
  {
    "metà alta   (accent=1 BW=1, LUT3, VSH2) ....",
    "metà bassa  (accent=1 BW=0, LUT2, VSH1) ....",
    "i due colori sono diversi?             SI / NO",
  };
  voceScheda(SCH_LIVELLI, "PROBE DEI LIVELLI DI SORGENTE",
             SCHEDA_LIVELLI, sizeof(SCHEDA_LIVELLI) / sizeof(SCHEDA_LIVELLI[0]));

  static const char* const SCHEDA_LUT[] =
  {
    "quale fascia è diventata nera:",
    "  cifra 1  LUT 1160, Mode 2       .....",
    "  cifra 2  LUT Table 6-4, Mode 1  .....",
    "la cornice ha lampeggiato?             SI / NO",
  };
  voceScheda(SCH_LUT, "PARTIAL CON LUT CUSTOM",
             SCHEDA_LUT, sizeof(SCHEDA_LUT) / sizeof(SCHEDA_LUT[0]));

  static char temp1[96], temp2[96];
  snprintf(temp1, sizeof(temp1), "  cifra 1  %d gradi ....   cifra 2  %d gradi ....",
           (int)cfg.temp[0], (int)cfg.temp[1]);
  snprintf(temp2, sizeof(temp2), "  cifra 3  %d gradi ....   controllo a %d gradi%s",
           (int)cfg.temp[2], (int)cfg.temp[3],
           tempFuoriRange(cfg.temp[3]) ? "" : " (dentro 0..40!)");
  const char* const SCHEDA_TEMP[] =
  {
    "quali fasce hanno dipinto:",
    temp1,
    temp2,
    "la cifra 3 è ancora sul vetro dopo la passata di controllo?   SI / NO",
  };
  voceScheda(SCH_TEMP, "BANCHI PER TEMPERATURA",
             SCHEDA_TEMP, sizeof(SCHEDA_TEMP) / sizeof(SCHEDA_TEMP[0]));

  static const char* const SCHEDA_PP[] =
  {
    "la fascia con la cifra 1 è bianca?     SI / NO",
  };
  voceScheda(SCH_PINGPONG, "PING-PONG VIA 0x37",
             SCHEDA_PP, sizeof(SCHEDA_PP) / sizeof(SCHEDA_PP[0]));

  Serial.println(F(""));
  Serial.println(F("--- cosa fare, secondo cosa hai osservato ---"));
  Serial.println(F("solo la conseguenza operativa: il perchè sta nei commenti del file,"));
  Serial.println(F("nel README dell'example e nelle memorie del progetto."));

  static const char* const C_VITA[] =
  {
    "BUSY segue i pull interni  -> non arriva al GPIO: FFC, pinout o",
    "                              alimentazione. Nient'altro vale",
    "niente e BUSY mai salito   -> cablaggio di questa coda",
    "niente ma BUSY salito      -> mancano i rail di boost su questo attacco",
    "HV 0x14 molto sotto 560 ms -> alimentazione a posto, guarda altrove",
    "HV 0x14 a 560 ms o timeout -> rail di boost assenti",
    "HV 0x14 non alza il BUSY   -> il controller non parla",
    "0x2F valido                -> annota 0x2E: chiude il part number",
    "0x46/0x47 muti, SWRESET ok -> sospetta il parametro, non il cablaggio",
  };
  voceScheda(SCH_COUNT, "IL CONTROLLER RISPONDE?",
             C_VITA, sizeof(C_VITA) / sizeof(C_VITA[0]));

  static const char* const C_IDENT[] =
  {
    "etichetta Y 320 + barra    -> PART_HEIGHT = 384 è giusto",
    "etichetta Y diversa        -> PART_HEIGHT = etichetta + 64, HEIGHT il",
    "                              doppio",
    "nero sx, accent dx, dritti -> setMasterMirror(false, false)",
    "nero e accent scambiati    -> specchia X",
    "blocchi giù e ribaltati    -> specchia Y",
    "entrambe                   -> 180 gradi, il default della coda slave",
    "metà bassa sporca (20 MHz) -> abbassa il clock in selectSPI",
    "stampano tutte e tre       -> base CAND_MINIMAL, ma scrivi il MUX",
    "stampa solo CAND_SOLUM     -> base con soft start e MUX espliciti",
    "stampa solo CAND_OEPL      -> porta pattern hardware e 0x21 nel driver",
    "CAND_OEPL raddrizza        -> 0x21 = 08 00 nell'init, togli il mirror",
  };
  voceScheda(SCH_IDENT_0, "GEOMETRIA, VERSO, CLOCK, INIT",
             C_IDENT, sizeof(C_IDENT) / sizeof(C_IDENT[0]));

  static const char* const C_POL[] =
  {
    "bianca la cifra 1          -> bit = 1 bianco, la convenzione del datasheet",
    "bianca la cifra 2          -> bit = 1 nero, piano invertito",
    "con 0x21 = 08 si scambiano -> le etichette BW del frame a bande vanno",
    "                              lette scambiate, e con loro le bande 3 e 4",
    "non si scambiano           -> etichette come sono scritte",
  };
  voceScheda(SCH_POL_1, "POLARITÀ DEL PIANO BW, leggila per prima",
             C_POL, sizeof(C_POL) / sizeof(C_POL[0]));

  static const char* const C_BANDE[] =
  {
    "banda 3 rossa              -> 0x26 = rosso, il driver è giusto",
    "banda 3 gialla             -> esemplare BWRY: 0x26 pilota il giallo",
    "banda 4 diversa da 2 e 3   -> quarto stato sulla coppia di bit",
    "banda 4 uguale alla 3      -> risponde il probe dei livelli; nel driver",
    "                              forza BW = 1 dove c'è accent",
    "bande 1 e 2 uguali         -> 0x24 non arriva, nient'altro vale",
    "fondo non uniforme         -> guarda il soft start 0x0C",
    "ghosting fra i frame       -> LUT via 0x32, o due passate di refresh",
    "box disallineati           -> correggi _setPartialRamArea",
    "box ok ma fondo a strisce  -> il difetto è nel push dei piani",
  };
  voceScheda(SCH_BANDE, "FRAME A BANDE, ACCENT E FINESTRE PARZIALI",
             C_BANDE, sizeof(C_BANDE) / sizeof(C_BANDE[0]));

  static const char* const C_LIV[] =
  {
    "le due metà DIVERSE        -> quarto stato: coppia di bit più LUT custom",
    "le due metà IDENTICHE      -> BWR, la questione si chiude",
    "entrambe bianche           -> inconcludente: confronta con la sonda LUT",
  };
  voceScheda(SCH_LIVELLI, "QUARTO COLORE, LIVELLI DI SORGENTE",
             C_LIV, sizeof(C_LIV) / sizeof(C_LIV[0]));

  static const char* const C_AREA[] =
  {
    "trappola BIANCA            -> hasFastPartialUpdate a true,",
    "                              partial_refresh_time alla durata misurata,",
    "                              refresh(x,y,w,h) con _Update_Part 0xFC e",
    "                              writeImageAgain su 0x26; il dispatch salta",
    "                              la banda non toccata",
    "trappola NERA              -> resta _Update_Full",
    "nera solo dopo la Mode 1   -> solo 0xFC rispetta la finestra: il partial",
    "                              costa l'accent",
    "Mode 1 breve e trappola ok -> _Update_Part con 0xF4, accent salvo",
    "fasce color accent         -> 0xFC non seleziona Mode 2: resta Mode 1",
    "bordi X non netti          -> partial solo a x = 0 e w = WIDTH",
    "manca la cifra 3           -> contatore che forzi _Update_Full ogni N",
    "lampeggia la cornice 1     -> 0x3C = 0x80 prima del partial, 0x01",
    "                              prima del refresh pieno",
  };
  voceScheda(SCH_AREA, "PARTIAL D'AREA",
             C_AREA, sizeof(C_AREA) / sizeof(C_AREA[0]));

  static const char* const C_LUT[] =
  {
    "fascia nera sotto 1 s      -> hasFastPartialUpdate a true, _Init_Part con",
    "                              0x3C = 0xC0 e la LUT via 0x32, _Update_Part",
    "                              con la sequenza vincente",
    "breve ma nessuna nera      -> aggiusta la waveform, la strada è giusta",
    "durate piene               -> questa strada è chiusa",
  };
  voceScheda(SCH_LUT, "PARTIAL CON LUT CUSTOM",
             C_LUT, sizeof(C_LUT) / sizeof(C_LUT[0]));

  static const char* const C_DIFF[] =
  {
    "zero e massima diverse     -> il controller confronta i piani: il",
    "                              differenziale c'è e va perseguito",
    "zero e massima uguali      -> non li confronta: hasFastPartialUpdate =",
    "                              false è un fatto misurato",
    "0xCF e 0xC7 più corte      -> il costo era la ricarica della LUT",
    "una passata sotto 3 s      -> guarda quale cifra è rimasta sul vetro",
  };
  voceScheda(SCH_DIFF_DEEP, "DIFFERENZIALE APPROFONDITA",
             C_DIFF, sizeof(C_DIFF) / sizeof(C_DIFF[0]));

  static const char* const C_TEMP2[] =
  {
    "una fascia molto più corta -> forza quella temperatura via 0x18 / 0x1A",
    "                              prima del refresh: è la strada migliore",
    "tutte e quattro uguali     -> la temperatura non è una leva",
    "cifra 3 ancora sul vetro   -> la passata a 70 gradi è stata rifiutata",
  };
  voceScheda(SCH_TEMP, "BANCHI PER TEMPERATURA",
             C_TEMP2, sizeof(C_TEMP2) / sizeof(C_TEMP2[0]));

  static const char* const C_PP[] =
  {
    "breve e cifra 1 bianca     -> il ping-pong funziona: è una scoperta",
    "lunga                      -> non prova niente, era un tentativo alla",
    "                              cieca su dieci byte dell'OTP",
  };
  voceScheda(SCH_PINGPONG, "PING-PONG VIA 0x37",
             C_PP, sizeof(C_PP) / sizeof(C_PP[0]));

  static const char* const C_SLEEP[] =
  {
    "BUSY alto solo con 0x03    -> hibernate() manda 0x03, come oggi",
    "BUSY alto solo con 0x11    -> cambia il byte in hibernate()",
    "alto con entrambi          -> tieni quello del datasheet",
    "basso con entrambi         -> hibernate() è un rischio senza guadagno",
    "banda NERA a fine test     -> ciclo completo ok: dopo il risveglio il",
    "                              driver rifà init e riscrive i piani",
    "banda non tornata          -> verifica RST, o rinuncia al deep sleep",
    "immagine sporcata          -> dormi solo dopo un refresh completo",
    "corrente uguale sveglio    -> nessun risparmio: togli alimentazione",
    "in ogni caso               -> hibernate() manda 0x10 a ENTRAMBI i CS, e",
    "                              _hibernating va azzerato per entrambe",
  };
  voceScheda(SCH_SLEEP, "DEEP SLEEP",
             C_SLEEP, sizeof(C_SLEEP) / sizeof(C_SLEEP[0]));

  static const char* const C_SLAVE[] =
  {
    "compare il pattern         -> il bit 7 non indirizza: ADDRESSING_CASCADE",
    "                              non ha un meccanismo, resta DUAL_CS",
    "resta l'immagine di prima  -> gli opcode offset sono rifiutati: prima",
    "                              evidenza a favore della cascade",
    "si accende l'altra metà    -> un solo chip select, driver in cascade",
    "non si accende niente      -> non prova niente senza il ponte di CL e",
    "                              dei rail fra i due FFC",
    "l'altro BUSY si è mosso    -> i due controller condividono più del bus",
  };
  voceScheda(SCH_SLAVE, "SECONDO CONTROLLER a opcode|0x80",
             C_SLAVE, sizeof(C_SLAVE) / sizeof(C_SLAVE[0]));

  Serial.println();
  Serial.println(F("================================================"));
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  caricaConfig();
  menuIniziale();
  spiSettings = SPISettings(cfg.spiBase, MSBFIRST, SPI_MODE0);

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F(" dual_panel_finder - SOLUM 12.2\" 960x768 BWR"));
  Serial.print  (F(" coda      : ")); Serial.println(tailLabel());
  Serial.printf (" cablaggio : secondo connettore FFC %s\n",
                 cfg.secondFfc ? "CABLATO" : "non cablato");
  Serial.printf (" pattern   : %u source x %u gate\n", SRC, EXPECTED_BAND);
  Serial.printf (" pause     : %lu s ciascuna\n", (unsigned long)(cfg.pausaMs / 1000));
  Serial.printf (" parametri : temperature %d/%d/%d/%d gradi, MUX %u/%u/%u,\n"
                 "             clock %lu/%lu/%lu Hz, timeout %lu ms, multimetro %lu s\n",
                 (int)cfg.temp[0], (int)cfg.temp[1], (int)cfg.temp[2], (int)cfg.temp[3],
                 (unsigned)cfg.mux[0], (unsigned)cfg.mux[1], (unsigned)cfg.mux[2],
                 (unsigned long)cfg.spiBase, (unsigned long)cfg.spiFast,
                 (unsigned long)cfg.spiDriver, (unsigned long)cfg.timeoutMs,
                 (unsigned long)(cfg.sleepMs / 1000));
  /**
   * Durata: una quarantina di passate di refresh, e su questo pannello una
   * costa ~19 s, più le pause di osservazione. Vale la pena saperlo prima di
   * cominciare, perchè i riepiloghi finali confrontano fra loro passate che
   * stanno in sonde diverse: interrompere a metà rende metà delle misure muta.
   */
  Serial.printf (" durata    : ~%d refresh da ~19 s più %d pause\n",
                 refreshPrevisti(), 13);
  if (cfg.esaustivo)
    Serial.println(F("             esaustivo attivo: nessuna passata viene saltata"));
  Serial.println(F("             Ogni refresh del pannello risponde a una domanda che"));
  Serial.println(F("             nient'altro poteva rispondere: le passate deducibili da"));
  Serial.println(F("             un'altra misura sono condizionali e si saltano da sè."));
  Serial.println(F("             Non interrompere: i riepiloghi finali confrontano fra"));
  Serial.println(F("             loro passate che stanno in sonde diverse."));
  if (!(cfg.sonde & S_DRIVER))
    Serial.println(F("             fase driver disattivata: solo capabilities del pannello"));
  Serial.println(F(" sweep     : 3 candidate di init, 3 valori di MUX, bit 7 su 5"));
  Serial.println(F("             opcode, 2 parametri di deep sleep, 3 clock SPI,"));
  Serial.printf ("             %s modello%s di indirizzamento del driver\n",
                 cfg.secondFfc ? "2" : "1", cfg.secondFfc ? "i" : "");
  Serial.printf ("ambiente  : CPU %lu MHz, APB %lu MHz, heap %lu B\n",
                 (unsigned long)(getCpuFrequencyMhz()),
                 (unsigned long)(getApbFrequency() / 1000000UL),
                 (unsigned long)ESP.getFreeHeap());
  Serial.println(F("=================================================="));
  Serial.println(F("\ncome leggere il log insieme al pannello:"));
  Serial.println(F("  ogni schermata da guardare porta un numero in un riquadro in alto a"));
  Serial.println(F("  destra, e le righe che la riguardano iniziano con lo stesso numero"));
  Serial.println(F("  fra parentesi quadre. Dove il test chiede di guardare il vetro, il"));
  Serial.println(F("  blocco sta fra una barra di v e una barra di ^"));

  runProbePhase();
  if (cfg.sonde & S_DRIVER)
  {
    observePause(cfg.pausaMs, "comincia la fase driver, che riparte da un reset");
    runDriverPhase();
  }
  else
  {
    Serial.println(F("\nfase driver saltata: non determina capabilities, valida il"));
    Serial.println(F("driver. Le misure del pannello sono tutte qui sopra."));
  }
  printObservationSheet();
}

void loop()
{
  delay(60000);
}
