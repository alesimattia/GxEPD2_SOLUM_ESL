/**
 * panel_diagnostic - strumento di misura sul pannello SOLUM ESL 9.7" (672w x
 * 960h nativi, controller SSD1677): determina QUALI colori e QUALI combinazioni
 * dei piani RAM il display rende davvero, e raccoglie le misure che servono a
 * tarare il driver.
 *
 * Non decide niente: mette il pannello nelle condizioni di rispondere e riporta
 * cosa si vede. Le conclusioni si tirano guardando lo schermo, e da quelle si
 * correggono il driver e il firmware. Sketch autonomo: non usa GxEPD2, nè il
 * driver custom, nè Adafruit_GFX, così l'esito non dipende da nessuno strato
 * software.
 *
 * Costo del test, criteri con cui i refresh sono contati, tabella delle dieci
 * leve del partial e cosa il test non esercita di proposito: README.md di questo
 * example. Come si legge il log insieme al vetro: il banner a runtime e la
 * sezione SCHERMATE NUMERATE più sotto.
 *
 * ---------------------------------------------------------------------------
 * COSA MISURA
 *
 * 1. Le quattro combinazioni dei due piani RAM, una banda alta 168 px per
 *    ognuna, con il proprio numero disegnato in basso a sinistra:
 *
 *      banda 1  y=  0..167   0x24=0xFF  0x26=0x00   -> LUT1, bianco
 *      banda 2  y=168..335   0x24=0x00  0x26=0x00   -> LUT0, nero
 *      banda 3  y=336..503   0x24=0xFF  0x26=0xFF   -> LUT3, in uso oggi
 *      banda 4  y=504..671   0x24=0x00  0x26=0xFF   -> LUT2, mai esercitata
 *
 *    I nomi delle LUT vengono dalla Table 6-4 del datasheet SSD1677, con la nota
 *    "LUT 3 = LUT 2". Due conseguenze che reggono tutta la sonda. **LUT3 è una
 *    LUT distinta nel silicio**, che la waveform a 3 colori si limita ad
 *    aliasare su LUT2: l'SSD1677 ha LUT0..LUT4 e quattro livelli di sorgente,
 *    quindi non vale l'argomento "due piani a 1 bit, al massimo tre colori" — a
 *    decidere è la waveform in OTP. E **quella che il firmware scrive per il
 *    rosso è LUT3**, non LUT2: sul vetro esce rosso, quindi l'unica LUT mai
 *    esercitata è LUT2, la banda 4, l'unico code point dove un quarto colore
 *    possa ancora nascondersi in una coppia di bit.
 *
 * 2. Il quarto colore per LIVELLO DI SORGENTE, l'ultimo test possibile. Se le
 *    due LUT venissero pilotate a tensioni diverse un quarto pigmento si
 *    separerebbe per soglia, che è come un BWRY distingue rosso e giallo: la
 *    sonda carica via 0x32 una waveform in cui LUT2 va a VSH1 e LUT3 a VSH2,
 *    stessi tempi, e stampa le due bande adiacenti. Rischio contenuto: 0x32
 *    scrive i byte 0..104 della LUT, mentre VGH, VSH1, VSH2, VSL e VCOM stanno
 *    ai byte 105..109 e arrivano da 0x03 / 0x04 / 0x2C, che la sonda non manda
 *    mai. Cambia solo quale delle tensioni già presenti tocca ciascuna LUT.
 *
 * 3. La waveform alternativa: le stesse bande rifatte con 0x22 = 0xFF invece di
 *    0xF7, cioè DISPLAY Mode 2 invece di Mode 1 — stessa RAM, banco di waveform
 *    diverso preso da OTP. Le bande si misurano sotto Mode 1 perchè è la
 *    waveform della produzione, ed è sotto quella che LUT2 va vista.
 *
 * 4. Il refresh differenziale, candidato a sostituire i 24 s con ~600 ms. Il
 *    fratello monocromatico dello stesso silicio, GxEPD2_1330_GDEM133T91,
 *    dichiara partial_refresh_time = 600 ms tenendo il frame precedente in 0x26
 *    e lanciando 0x22 = 0xFC. Qui si porta il pannello a nero su entrambi i
 *    piani, così 0x26 è un frame precedente coerente, poi si riscrive solo 0x24
 *    e si misurano due inversioni piene.
 *
 *    Poi tutto il resto, in probeDifferentialDeeper: la DIFFERENZA ZERO fra i
 *    due piani, che è il test minimo e da solo dice se il controller li
 *    confronta; 0xCF e 0xC7, che dipingono senza ricaricare LUT e temperatura;
 *    0x21 con la RAM rosso bypassata; e 0x37 F[6], il RAM ping-pong, l'unico
 *    interruttore che il datasheet lega esplicitamente al differenziale. Ogni
 *    passata lascia sul vetro la propria frase, la stessa che stampa sul
 *    seriale, così una passata veloce si distingue da una che non ha dipinto.
 *
 * 5. Il refresh parziale d'area: se restringendo la finestra RAM il pannello
 *    aggiorna davvero solo quella porzione, e a che prezzo. Il punto 4 misura la
 *    waveform, questo l'indirizzamento. Il discriminante è una fascia di
 *    trappola scritta in 0x24 che nessuna finestra di refresh comprende: se
 *    resta bianca le finestre sono rispettate.
 *
 *    Qui 0x26 resta a 0 per tutta la sonda, e non è un dettaglio: se il
 *    differenziale non esiste 0x26 è ancora il piano accent, e a 0xFF lo
 *    accende su tutti i 960 x 672 — ogni pixel diventa (1,1) o (0,1), le
 *    strisce non si distinguono e la trappola non è leggibile.
 *
 * 6. Il deep sleep per intero: non quale parametro di 0x10 alza il BUSY, che è
 *    solo un livello su un pin e lo dà alto anche un cavo scollegato, ma se il
 *    controller si addormenta e soprattutto se poi si risveglia e torna a
 *    stampare. La prova di sordità gli manda da addormentato un refresh intero e
 *    guarda il BUSY più a lungo di quanto un refresh duri; il risveglio è
 *    cronometrato e chiuso da un refresh a schermo nero, che o arriva o no. Una
 *    finestra ferma lascia il tempo di misurare la corrente col multimetro, che
 *    è l'unica prova del risparmio e in firmware non si può fare.
 *
 * ---------------------------------------------------------------------------
 * ATTESE DALLA DOCUMENTAZIONE, da confermare o smentire col pannello. Servono a
 * riconoscere una divergenza quando la si vede, non a decidere l'esito. Le fonti
 * per esteso — pratiche FCC, firmware OpenEPaperLink, cataloghi Good Display,
 * UICR di fabbrica, part number del vetro — stanno in docs/fonti_esterne.md e
 * nelle memorie del progetto.
 *
 * Datasheet SSD1677 Rev 1.0, comandi usati qui: 0x24 Write RAM B/W (1 = bianco),
 * 0x26 Write RAM RED (1 = rosso), 0x25 dithering (non è un piano), 0x28 VCOM
 * Sense (non è un piano), 0x2E / 0x2F User ID e Status da OTP, 0x46 / 0x47 auto
 * write pattern.
 *
 * ATTENZIONE ALLA REVISIONE: quel PDF è del 2018 e c'è un indizio che il silicio
 * SOLUM sia più recente — l'init di fabbrica della 9.7" scrive 0x21 con DUE
 * parametri, mentre la Rev 1.0 lo definisce con uno solo, e la forma a due byte
 * è quella dell'SSD1683, dove il secondo porta il bit di cascade. Quindi "non è
 * nel datasheet" non equivale a "non esiste nel chip", ed è un'altra ragione per
 * misurare invece che dedurre.
 *
 * La documentazione converge su BWR — il datasheet non nomina il giallo, OEPL
 * cataloga il 9.7" come 3 colori, e sulla linea grande di Good Display i 4
 * colori stanno sempre su SSD2677 e con un formato a 2 bit per pixel che questo
 * pannello non parla. Dall'altra parte il datasheet SOLUM del modulo donor
 * dichiara BWRY. La contraddizione la scioglie questo test, non la carta.
 * ---------------------------------------------------------------------------
 */

#include <SPI.h>
#include <stdarg.h>
#include <Preferences.h>

/**
 * Dichiarazione anticipata, e serve davvero: il preprocessore di Arduino genera
 * da sè i prototipi di tutte le funzioni e li inserisce prima della prima
 * dichiarazione del file, cioè prima della definizione della struct. Senza
 * questa riga i prototipi di layoutText() e writeRowsWithText() nominerebbero
 * un tipo non ancora dichiarato e la compilazione fallisce. Un tipo incompleto
 * basta: in una dichiarazione di funzione non serve la definizione.
 */
struct TextLayout;

/** Stessa ragione, e serve un tipo di base esplicito perchè una enum si possa
 *  dichiarare senza definirla. */
enum Schermata : uint8_t;


// ---------------------------------------------------------------------------
// CONFIGURAZIONE A RUNTIME, INPUT DAL MONITOR SERIALE
//
// Niente qui dentro richiede una ricompilazione: le scelte si fanno dal menu
// che compare al boot e restano in NVS fra un reboot e l'altro. Ogni domanda ha
// un timeout e un valore preimpostato, quindi senza nessuno alla tastiera il
// test parte da solo e un log catturato su file non si blocca mai.
//
// Niente qui tocca l'output: si legge solo il buffer di RICEZIONE, e ogni
// risposta e ogni timeout vengono ristampati, così il file di log spiega da sè
// com'è stato configurato il run.
// ---------------------------------------------------------------------------

/** Le sonde, una per bit: la maschera dice quali eseguire. */
enum SondaBit : uint16_t
{
  S_BANDE     = 1 << 0,   // le 4 combinazioni dei piani, Mode 1
  S_MODE2     = 1 << 1,   // le stesse bande in Display Mode 2
  S_LIVELLI   = 1 << 2,   // quarto colore per livello di sorgente
  S_DIFF      = 1 << 3,   // sonda differenziale
  S_DIFFDEEP  = 1 << 4,   // differenziale approfondita
  S_LUT       = 1 << 5,   // partial con LUT custom
  S_AREA      = 1 << 6,   // partial d'area
  S_TEMP      = 1 << 7,   // banchi di waveform per temperatura
  S_MUX       = 1 << 8,   // MUX ridotto
  S_SLEEP     = 1 << 9,   // deep sleep
  S_BENCH     = 1 << 10,  // benchmark del bus e registri in lettura
  S_TUTTE     = 0x07FF,
};

// Geometria nativa: 960 source sull'asse RAM X, 672 gate sull'asse RAM Y
static const uint16_t SRC       = 960;
static const uint16_t GATE      = 672;
static const uint16_t ROW_BYTES = SRC / 8;   // 120

// Byte di un piano a schermo pieno: quanto il driver spinge per canale
static const uint32_t PLANE_BYTES = (uint32_t)ROW_BYTES * GATE;   // 80.640

static const uint16_t BAND_H = GATE / 4;     // 168

/** Punti dello sweep di temperatura: quanti ne stanno sul vetro. */
static const uint8_t TEMP_PASSES = 4;
/** Valori dello sweep del MUX. */
static const uint8_t MUX_PASSES = 3;

struct Config
{
  bool     esaustivo;   // nessuna passata condizionale viene saltata
  uint32_t pausaMs;     // durata massima di una pausa di osservazione
  uint16_t sonde;       // maschera di SondaBit

  /**
   * Parametri di misura: sono i numeri che le sonde spazzano, e stanno qui
   * perchè cambiarli non deve costare una ricompilazione. Il caso che pesa è
   * la temperatura: il §6.9 dà l'OTP per capace di 34 banchi e sul vetro ne
   * stanno quattro per run, quindi coprirli tutti è questione di riavvii, non
   * di riflash.
   */
  int16_t  temp[TEMP_PASSES];   // gradi forzati via 0x18 / 0x1A
  uint16_t mux[MUX_PASSES];     // gate line programmate in 0x01
  uint32_t spiHz;               // clock del bus per le sonde
  uint32_t timeoutMs;           // timeout di default di un refresh
  uint32_t sleepMs;             // finestra ferma per il multimetro
};

/** Valori di fabbrica: quelli con cui il test è stato tarato. */
static const Config CFG_DEFAULT =
{
  false, 10000, S_TUTTE,
  { 0, 20, 40, 70 },
  { GATE, GATE / 2, GATE / 4 },
  10000000, 40000, 10000,
};

static Config cfg = CFG_DEFAULT;

static Preferences prefs;

static void scartaInputPendente()
{
  while (Serial.available())
    Serial.read();
}

/**
 * Legge una riga, al massimo timeout_ms. Ritorna i caratteri letti, e -1 se
 * non è arrivato niente: chi chiama distingue così un INVIO a vuoto, che vale
 * "tieni il preimpostato" e risponde subito, da un'assenza di risposta.
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
    if (fine == buf)                      Serial.println(F("  non è un numero: tengo l'attuale"));
    else if (letto < minimo || letto > massimo) Serial.println(F("  fuori range: tengo l'attuale"));
    else v = letto;
  }
  Serial.printf("  risposta: %ld%s\n", v,
                n < 0 ? "  (nessuna risposta, preso l'attuale)" : "");
  return v;
}

struct VoceSonda { uint16_t bit; const char* nome; };
static const VoceSonda SONDE_NOMI[] =
{
  { S_BENCH,    "benchmark del bus e registri (nessun refresh)" },
  { S_BANDE,    "4 bande dei piani, Mode 1, 1 refresh" },
  { S_MODE2,    "le stesse bande in Mode 2, 1 refresh" },
  { S_LIVELLI,  "quarto colore, 2 refresh" },
  { S_DIFF,     "sonda differenziale, 3 refresh" },
  { S_DIFFDEEP, "differenziale approfondita, 4-12 refresh" },
  { S_LUT,      "partial con LUT custom, 2 refresh" },
  { S_AREA,     "partial d'area, 4-6 refresh" },
  { S_TEMP,     "banchi per temperatura, 4 refresh" },
  { S_MUX,      "MUX ridotto, 3 refresh" },
  { S_SLEEP,    "deep sleep, 3 refresh" },
};
static const uint8_t SONDE_N = sizeof(SONDE_NOMI) / sizeof(SONDE_NOMI[0]);

/** Refresh previsti con la configurazione corrente, per il banner. */
static int refreshPrevisti()
{
  int n = 0;
  if (cfg.sonde & S_BANDE)    n += 1;
  if (cfg.sonde & S_MODE2)    n += 1;
  if (cfg.sonde & S_LIVELLI)  n += 2;
  if (cfg.sonde & S_DIFF)     n += 3;
  if (cfg.sonde & S_DIFFDEEP) n += cfg.esaustivo ? 12 : 4;   // +3 di 0xCF/0xC7/0xFF, +5 di 3, 3b e 4
  if (cfg.sonde & S_LUT)      n += 2;
  if (cfg.sonde & S_AREA)     n += cfg.esaustivo ? 6 : 4;
  if (cfg.sonde & S_TEMP)     n += TEMP_PASSES;
  if (cfg.sonde & S_MUX)      n += MUX_PASSES;
  if (cfg.sonde & S_SLEEP)    n += 3;   // sordità, risveglio e frame di prova
  return n;
}

/**
 * Filtro del registro delle sonde: true se va eseguita, altrimenti lo dichiara
 * nel log. Passare da qui invece che da un `if` nudo lascia traccia di ogni
 * salto, così il file di log spiega perchè un run è più corto di un altro.
 */
static bool sondaAttiva(uint16_t bit, const char* nome)
{
  if (cfg.sonde & bit) return true;
  Serial.printf("\n-- %s: non selezionata nel menu, saltata\n", nome);
  return false;
}

static void caricaConfig()
{
  prefs.begin("pd097", true);
  cfg.esaustivo = prefs.getBool  ("esaus", cfg.esaustivo);
  cfg.pausaMs   = prefs.getULong ("pausa", cfg.pausaMs);
  cfg.sonde     = prefs.getUShort("sonde", cfg.sonde);
  cfg.spiHz     = prefs.getULong ("spihz", cfg.spiHz);
  cfg.timeoutMs = prefs.getULong ("tmout", cfg.timeoutMs);
  cfg.sleepMs   = prefs.getULong ("sleep", cfg.sleepMs);
  prefs.getBytes("temp", cfg.temp, sizeof(cfg.temp));
  prefs.getBytes("mux",  cfg.mux,  sizeof(cfg.mux));
  prefs.end();
}

static void salvaConfig()
{
  prefs.begin("pd097", false);
  prefs.putBool  ("esaus", cfg.esaustivo);
  prefs.putULong ("pausa", cfg.pausaMs);
  prefs.putUShort("sonde", cfg.sonde);
  prefs.putULong ("spihz", cfg.spiHz);
  prefs.putULong ("tmout", cfg.timeoutMs);
  prefs.putULong ("sleep", cfg.sleepMs);
  prefs.putBytes ("temp", cfg.temp, sizeof(cfg.temp));
  prefs.putBytes ("mux",  cfg.mux,  sizeof(cfg.mux));
  prefs.end();
}

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
 * 12 bit in complemento a due, cioè gradi per sedici, quindi il range utile è
 * -128..+127 gradi; il MUX non può superare le gate line del controller; il
 * clock SPI oltre l'APB non è producibile dal divisore.
 */
static void menuParametri()
{
  while (true)
  {
    Serial.println(F("\n  parametri di misura:"));
    Serial.printf ("   1) temperature forzate  %d, %d, %d, %d gradi (l'ultima è il controllo)\n",
                   (int)cfg.temp[0], (int)cfg.temp[1], (int)cfg.temp[2], (int)cfg.temp[3]);
    Serial.printf ("   2) gate line del MUX    %u, %u, %u\n",
                   (unsigned)cfg.mux[0], (unsigned)cfg.mux[1], (unsigned)cfg.mux[2]);
    Serial.printf ("   3) clock SPI            %lu Hz\n", (unsigned long)cfg.spiHz);
    Serial.printf ("   4) timeout di un refresh %lu ms\n", (unsigned long)cfg.timeoutMs);
    Serial.printf ("   5) finestra del multimetro %lu s\n",
                   (unsigned long)(cfg.sleepMs / 1000));
    Serial.println(F("   6) riporta i valori di fabbrica"));
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
          char d[40];
          snprintf(d, sizeof(d), "    valore %u, gate line", m + 1);
          cfg.mux[m] = (uint16_t)chiediNumero(d, 8, GATE, cfg.mux[m], 15000);
        }
        break;
      case 3: cfg.spiHz = (uint32_t)chiediNumero("    clock SPI in Hz", 100000, 40000000,
                                                 cfg.spiHz, 15000); break;
      case 4: cfg.timeoutMs = (uint32_t)chiediNumero("    timeout di un refresh, ms",
                                                     1000, 300000, cfg.timeoutMs, 15000); break;
      case 5: cfg.sleepMs = (uint32_t)chiediNumero("    finestra del multimetro, s",
                                                   0, 600, cfg.sleepMs / 1000, 15000) * 1000UL;
              break;
      case 6:
        memcpy(cfg.temp, CFG_DEFAULT.temp, sizeof(cfg.temp));
        memcpy(cfg.mux,  CFG_DEFAULT.mux,  sizeof(cfg.mux));
        cfg.spiHz     = CFG_DEFAULT.spiHz;
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

/** Menu al boot. Senza risposta entro 10 s parte il profilo salvato. */
static void menuIniziale()
{
  while (true)
  {
    Serial.println(F("\n--- configurazione del run ---"));
    Serial.printf("   1) sonde ....... %u di %u selezionate\n",
                  (unsigned)__builtin_popcount(cfg.sonde), (unsigned)SONDE_N);
    Serial.printf("   2) esaustivo ... %s\n", cfg.esaustivo ? "sì" : "no");
    Serial.printf("   3) pause ....... %lu s\n", (unsigned long)(cfg.pausaMs / 1000));
    Serial.printf ("   4) parametri di misura  %d/%d/%d/%d gradi, MUX %u/%u/%u, %lu MHz\n",
                  (int)cfg.temp[0], (int)cfg.temp[1], (int)cfg.temp[2], (int)cfg.temp[3],
                  (unsigned)cfg.mux[0], (unsigned)cfg.mux[1], (unsigned)cfg.mux[2],
                  (unsigned long)(cfg.spiHz / 1000000UL));
    Serial.println(F("   5) riporta tutto ai valori di fabbrica"));
    Serial.printf("   0) parti  (%d refresh previsti, ~%d minuti)\n",
                  refreshPrevisti(), (refreshPrevisti() * 25) / 60 + 1);
    Serial.println(F("  [numero, 10 s, invio = parti]"));

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
      case 2: cfg.esaustivo = chiediSN("  eseguire anche le passate condizionali?",
                                       cfg.esaustivo, 15000); break;
      case 3: {
        static const char* const VOCI_PAUSA[] = { "0 s (non presidiato)", "10 s", "30 s", "60 s" };
        static const uint32_t MS[] = { 0, 10000, 30000, 60000 };
        int pre = 1;
        for (int i = 0; i < 4; ++i) if (MS[i] == cfg.pausaMs) pre = i;
        cfg.pausaMs = MS[chiediScelta("  durata massima di una pausa", VOCI_PAUSA, 4, pre, 15000)];
        break;
      }
      case 4: menuParametri(); break;
      case 5: prefs.begin("pd097", false); prefs.clear(); prefs.end();
              cfg = CFG_DEFAULT;   // il salvataggio a fine ciclo riscrive i valori di fabbrica
              Serial.println(F("  profilo riportato ai valori di fabbrica")); break;
      case 0: salvaConfig(); return;
      default: break;
    }
    salvaConfig();
  }
}

// Pin del pannello, identici a Layout_097c.h
static const int PIN_CS   = 15;
static const int PIN_DC   = 27;
static const int PIN_RST  = 26;
static const int PIN_BUSY = 25;

/**
 * Bus HSPI della Waveshare E-Paper ESP32 Driver Board. PIN_MISO è un dummy: su
 * questo connettore la linea dati esiste solo in ingresso.
 *
 * Sulla stessa board lo switch n.1 NON sceglie il tipo di pannello: sceglie la
 * resistenza di sense del booster, A = 3R e B = 0.47R. Il wiki Waveshare mette
 * in A i 13.3", che sono il riferimento più vicino a questo pannello per
 * silicio e geometria. Se i colori pieni escono deboli o pieni di ghosting, la
 * posizione dello switch va provata prima di dare la colpa alla waveform.
 *
 * Tre pin del controller che il connettore della board gestisce per noi e che
 * invece vanno guardati su qualunque coda cablata a mano (datasheet SSD1677,
 * tabella dei pin): BS1 sceglie l'interfaccia — L = 4 fili, H = 3 fili a 9 bit —
 * quindi se resta flottante il controller può ignorare tutto il traffico; M/S#
 * va a VDDIO su un pannello a chip singolo come questo, e la Rev 1.0 lo
 * classifica "reserved for testing" pur essendo, nell'SSD1683, la selezione
 * master/slave; CL è dichiarato "left open in application" ma è di tipo I/O e
 * compare sia fra i VIH d'ingresso sia fra i VOH d'uscita, cioè è il pin da cui
 * un master in cascade emette il clock. Sulla 9.7", che è a chip singolo, non
 * cambia niente: conta per il 12.2" (docs/fonti_esterne.md §4).
 */
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;



/**
 * Testo della fascia, disegnato dentro la fascia stessa. Ogni passata scrive
 * sul vetro LA STESSA STRINGA che stampa sul seriale, così quello che si legge
 * sul pannello si ritrova nel log e viceversa: è il solo modo di sapere quale
 * passata ha davvero dipinto, e con nove sonde che riusano le stesse quattro
 * fasce le cifre 1..4 di prima non bastavano.
 *
 * Font 5x7, sette byte per carattere, bit 4 = colonna più a sinistra. Il corpo
 * NON è fisso: lo sceglie layoutText() in modo che la frase stia dentro la
 * fascia, quindi un pixel del font non copre più byte RAM interi e la
 * composizione delle righe passa da una maschera di bit.
 *
 * Posizione: in basso nella fascia, con un margine dal bordo inferiore, così il
 * testo sta lontano dalla linea di contatto fra due fasce adiacenti e non
 * confonde la lettura del confine.
 */
static const uint8_t  FONT_W     = 5;   // colonne del glifo
static const uint8_t  FONT_H     = 7;   // righe del glifo
static const uint8_t  FONT_PITCH = 6;   // 5 colonne più una di spazio

static const uint16_t TEXT_MARGIN_X      = 24;   // margine laterale, per lato
static const uint16_t TEXT_MARGIN_BOTTOM = 16;   // distanza voluta dal fondo fascia
static const uint8_t  TEXT_LINE_GAP      = 2;    // interlinea, in unità di font
static const uint8_t  TEXT_MAX_LINES     = 3;    // oltre, la frase è troppo lunga
static const uint8_t  TEXT_SCALE_MAX     = 8;    // 40x56 px per carattere
static const uint8_t  TEXT_SCALE_MIN     = 2;    // 10x14 px, il minimo leggibile

/**
 * Font 5x7 per i codici ASCII 0x20..0x5F: maiuscole, cifre, spazio e la
 * punteggiatura usata nelle frasi. I codici non serviti restano a zero, cioè
 * escono come spazio; le minuscole si mappano sulle maiuscole in glyphFor().
 */
static const uint8_t FONT5X7[96 - 32][FONT_H] =
{
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x20 spazio
  { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },   // !
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // "
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // #
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // $
  { 0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13 },   // %
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // &
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // '
  { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },   // (
  { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 },   // )
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // *
  { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 },   // +
  { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 },   // ,
  { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },   // -
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },   // .
  { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },   // /
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   // 0
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
  { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   // 3
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   // 5
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   // 6
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   // 7
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   // 8
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   // 9
  { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 },   // :
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // ;
  { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 },   // <
  { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 },   // =
  { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 },   // >
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 },   // ?
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // @
  { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   // A
  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },   // B
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   // C
  { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },   // D
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },   // E
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },   // F
  { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F },   // G
  { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   // H
  { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },   // I
  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },   // J
  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },   // K
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   // L
  { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 },   // M
  { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },   // N
  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   // O
  { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },   // P
  { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },   // Q
  { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },   // R
  { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },   // S
  { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },   // T
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   // U
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },   // V
  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },   // W
  { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },   // X
  { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 },   // Y
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },   // Z
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // [
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // backslash
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // ]
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // ^
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00 },   // _
};

/** Glifo di un carattere, con le minuscole mappate sulle maiuscole. Fuori
 *  tabella ritorna lo spazio, così una frase con un carattere non previsto
 *  resta leggibile invece di stampare rumore. */
static const uint8_t* glyphFor(char c)
{
  uint8_t u = (uint8_t)c;
  if (u >= 'a' && u <= 'z') u = (uint8_t)(u & ~0x20);
  if (u < 0x20 || u > 0x5F) u = 0x20;
  return FONT5X7[u - 0x20];
}

/**
 * Disposizione del testo dentro una fascia: quale corpo, quante righe, dove
 * spezzarle e a che altezza parte il blocco. scale == 0 vuol dire che la frase
 * non ci sta nemmeno al corpo minimo, e la fascia va riempita uniforme.
 */
struct TextLayout
{
  uint8_t     scale = 0;
  uint8_t     lines = 0;
  const char* start[TEXT_MAX_LINES] = { nullptr, nullptr, nullptr };
  uint8_t     len[TEXT_MAX_LINES]   = { 0, 0, 0 };
  int16_t     top = 0;              // prima riga del blocco dentro la fascia
};

/**
 * Manda a capo la frase sugli spazi in righe da al massimo maxChars caratteri.
 * Ritorna false se servono più di TEXT_MAX_LINES righe, che è il segnale per
 * provare un corpo più piccolo. Una parola più lunga di maxChars viene spezzata
 * a metà parola: non capita con le frasi di questo test, ma senza quel ramo il
 * ciclo non terminerebbe.
 */
static bool wrapText(const char* text, uint8_t maxChars, TextLayout& out)
{
  out.lines = 0;
  const char* p = text;
  while (*p)
  {
    while (*p == ' ') ++p;         // niente spazi in testa a una riga
    if (!*p) break;
    if (out.lines >= TEXT_MAX_LINES) return false;

    uint8_t take = 0;              // caratteri presi da questa riga
    uint8_t lastSpace = 0;         // ultimo spazio utile per andare a capo
    while (p[take] && take < maxChars)
    {
      if (p[take] == ' ') lastSpace = take;
      ++take;
    }
    if (p[take] && lastSpace > 0) take = lastSpace;   // taglio sull'ultimo spazio

    out.start[out.lines] = p;
    out.len[out.lines]   = take;
    ++out.lines;
    p += take;
  }
  return out.lines > 0;
}

/**
 * Sceglie il corpo più grande con cui la frase sta dentro la fascia: prova le
 * scale dalla massima alla minima e si ferma alla prima che entra in larghezza
 * e in altezza. Frasi corte restano grandi, frasi lunghe scendono di corpo da
 * sè, e le fasce basse (le passate del MUX sono da 84 e 42 righe) prendono il
 * corpo che ci sta.
 */
static TextLayout layoutText(const char* text, uint16_t h)
{
  TextLayout lay;
  if (!text || !*text) return lay;
  for (uint8_t scale = TEXT_SCALE_MAX; scale >= TEXT_SCALE_MIN; --scale)
  {
    const uint16_t usable  = SRC - 2 * TEXT_MARGIN_X;
    const uint8_t  maxChar = (uint8_t)(usable / ((uint16_t)FONT_PITCH * scale));
    if (maxChar == 0) continue;
    TextLayout probe;
    if (!wrapText(text, maxChar, probe)) continue;
    const int16_t lineH  = (int16_t)FONT_H * scale;
    const int16_t gap    = (int16_t)TEXT_LINE_GAP * scale;
    const int16_t blockH = probe.lines * lineH + (probe.lines - 1) * gap;
    const int16_t spare  = (int16_t)h - blockH;
    if (spare < 0) continue;
    /**
     * Margine inferiore adattivo: quello voluto se c'è spazio, metà dello
     * spazio che resta se la fascia è bassa. Con un margine fisso una fascia da
     * 24 righe non avrebbe portato nessun testo, e proprio le fasce basse — le
     * passate del MUX e la finestra sottile della sonda d'area — sono quelle
     * dove serve sapere quale passata ha dipinto.
     */
    const int16_t margin = (spare / 2 < (int16_t)TEXT_MARGIN_BOTTOM)
                           ? spare / 2 : (int16_t)TEXT_MARGIN_BOTTOM;
    const int16_t top = spare - margin;
    probe.scale = scale;
    probe.top   = top;
    return probe;
  }
  return lay;
}

// Sul SSD1677 il BUSY è attivo alto, come il busy_level=HIGH del driver
static const int BUSY_ACTIVE = HIGH;


/**
 * Mappa verticale della sonda del partial d'area: fasce disgiunte sui 672
 * gate, così a sonda finita si leggono tutte insieme sullo stesso schermo.
 * Le altezze sono multipli comodi e le X del riquadro sono multiple di 8,
 * perchè la finestra lavora per byte sull'asse source e un bordo a metà byte
 * non è rappresentabile.
 */
static const uint16_t AREA_P1_Y   = 0;         // 0..167,   passata 1 e poi 4
static const uint16_t AREA_P1_H   = BAND_H;
static const uint16_t AREA_TRAP_Y = 176;       // 176..215, fascia di trappola
static const uint16_t AREA_TRAP_H = 40;
static const uint16_t AREA_THIN_Y = 224;       // 224..247, finestra sottile
static const uint16_t AREA_THIN_H = 24;
static const uint16_t AREA_M1_Y   = 264;       // 264..311, passata Mode 1
static const uint16_t AREA_M1_H   = 48;
static const uint16_t AREA_P2_Y   = 336;       // 336..503, passata 2
static const uint16_t AREA_P2_H   = BAND_H;
static const uint16_t AREA_BOX_X  = 256;       // 504..671 x 256..511, riquadro
static const uint16_t AREA_BOX_W  = 256;
static const uint16_t AREA_BOX_Y  = 504;
static const uint16_t AREA_BOX_H  = BAND_H;

SPIClass hspi(HSPI);
static SPISettings spiSettings(10000000, MSBFIRST, SPI_MODE0);   // riassegnata dal menu

/**
 * Clock ridotto per i tentativi di lettura registro: in lettura il
 * controller è molto più lento che in scrittura.
 */
static SPISettings spiReadSettings(2500000, MSBFIRST, SPI_MODE0);

// Contatori cumulativi alimentati dalle primitive: base del riepilogo finale
static uint32_t totalSpiBytes = 0;
static uint32_t totalSpiMicros = 0;
static uint32_t totalBusyMillis = 0;
static uint32_t totalCommands = 0;
static uint32_t totalParamBytes = 0;

// Costi per byte misurati dal benchmark, usati per le stime di frame
static double bulkUsPerByte = 0.0;   // blocco più grande, limite del bus
static double rowUsPerByte = 0.0;    // blocchi da 120, la riga immagine del driver

// Durata dei pattern hardware, -1 se non supportati
static int32_t patternMs24 = -1;
static int32_t patternMs26 = -1;
static int32_t diffMs1 = -1;
static int32_t diffMs2 = -1;

// Durate della sonda d'area, -1 se la passata non si è conclusa o non è stata fatta
static int32_t areaMsFirst = -1;   // prima passata 0xFC, finestra alta BAND_H
static int32_t areaMsThin  = -1;   // passata 0xFC su 24 righe, per la scala con l'altezza
static int32_t areaMsMode1 = -1;   // passata 0xF4, Mode 1 su finestra

// Esiti della sonda del deep sleep
static uint8_t sleepParamOk    = 0x00;    // parametro di 0x10 che addormenta, 0 se nessuno
static bool    sleepBusyHigh   = false;   // BUSY alto mentre dorme
static bool    sleepIgnoresCmd = false;   // da addormentato ignora anche un refresh
static int32_t wakeInitMs      = -1;      // reset hardware più init dopo il sonno
static int32_t wakeRefreshMs   = -1;      // refresh di prova dopo il risveglio

/**
 * Invia un byte di comando: D/C basso. Ordine delle operazioni identico a
 * GxEPD2_EPD::_writeCommand, D/C riportato alto in coda incluso, così il
 * costo misurato è quello che paga il driver.
 */
static void writeCommand(uint8_t c)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  hspi.transfer(c);
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  hspi.endTransaction();
  ++totalCommands;
}

// Invia un byte di parametro: D/C alto, come GxEPD2_EPD::_writeData
static void writeData(uint8_t d)
{
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  hspi.transfer(d);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  ++totalParamBytes;
}

/**
 * Riempie la finestra RAM corrente con un byte costante, a blocchi da 256:
 * è il percorso di _writeScreenBuffer del driver, stessa dimensione di chunk.
 * Ritorna i microsecondi del solo transfer: serve a misurare il costo reale
 * per byte del bus invece di dedurlo dal clock.
 */
static uint32_t writeConst(uint8_t value, uint32_t count)
{
  uint8_t buf[256];
  memset(buf, value, sizeof(buf));
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  const uint32_t total = count;
  const uint32_t t0 = micros();
  while (count > 0)
  {
    uint32_t chunk = count > sizeof(buf) ? (uint32_t)sizeof(buf) : count;
    hspi.writeBytes(buf, chunk);
    count -= chunk;
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes += total;
  totalSpiMicros += dt;
  return dt;
}

/** Attende la discesa del BUSY. Ritorna i ms attesi, oppure -1 al timeout. */
static int32_t waitBusy(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
  {
    if ((millis() - t0) > timeout_ms)
    {
      totalBusyMillis += (millis() - t0);
      return -1;
    }
    delay(1);
  }
  const uint32_t dt = millis() - t0;
  totalBusyMillis += dt;
  return (int32_t)dt;
}

/**
 * Porta il BUSY a riposo prima di una misura che lo usa come testimone. Senza
 * questa guardia un BUSY rimasto alto da un comando precedente verrebbe letto
 * come "il comando appena inviato è stato accettato", e si misurerebbe la coda
 * dell'operazione precedente invece dell'operazione nuova.
 *
 * Ritorna false se il BUSY resta occupato: il chiamante NON deve misurare. Il
 * valore va consumato, non ignorato — un run precedente ha stampato
 * "misura non attendibile" e subito sotto un numero, e quel numero era la coda
 * di un comando analogico letta come se fosse un refresh.
 */
static bool ensureBusyLow(const char* dove)
{
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
    return true;
  const int32_t ms = waitBusy(3000);
  if (ms < 0)
  {
    Serial.printf("   BUSY ancora alto a 3000 ms prima di [%s]: il controller è\n"
                  "   occupato e non prenderà il comando. NIENTE DA MISURARE.\n", dove);
    return false;
  }
  Serial.printf("   BUSY era alto prima di [%s]: attesi %ld ms prima di misurare\n",
                dove, (long)ms);
  return true;
}

/**
 * Costo per byte di riferimento per le stime: il migliore misurato dal
 * benchmark, con fallback al limite teorico del clock se il benchmark non ha
 * ancora girato.
 */
static double usPerByteReference()
{
  if (bulkUsPerByte > 0.0)
    return bulkUsPerByte;
  return 8.0 * 1000000.0 / (double)cfg.spiHz;
}

/**
 * Impulso di reset. Il firmware chiama display.init(..., 2, false), quindi
 * GxEPD2_EPD::_reset tiene RST alto 10 ms, basso 2 ms e alto altri 10 ms: qui
 * il livello basso dura 10 ms invece di 2, più margine e nessun effetto sulla
 * misura.
 */
static void resetPanel()
{
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
}

/**
 * Finestra RAM in vigore. Il riquadro col numero della schermata la restringe
 * per un istante e deve rimetterla com'era: è la finestra presente alla master
 * activation a definire l'area che il refresh percorre.
 */
static uint16_t ramWinX = 0, ramWinY = 0, ramWinW = SRC, ramWinH = GATE;

// Finestra RAM e cursore. Stessa sequenza di comandi del driver custom.
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

/**
 * Init minima: la stessa del driver custom, MUX incluso a 672 gate line.
 *
 * ATTENZIONE a cosa vale questo. Il driver custom non è la misura di
 * riferimento: è l'oggetto che questa sonda serve a correggere. Replicarne
 * l'init tiene le misure confrontabili con quello che il firmware fa davvero
 * oggi, ma se l'init fosse sbagliata ogni misura erediterebbe l'errore. Per
 * questo esiste initPanelFactory(), l'init di fabbrica SOLUM/OEPL verbatim, e
 * la sonda differenziale approfondita ripete sotto quell'init il confronto che
 * conta: se un esito cambia, il colpevole è l'init di qui.
 *
 * Due dei valori scritti qui hanno una conferma esterna, utile perchè un init
 * sbagliato falserebbe tutte le misure di questa sonda. Il demo Arduino del
 * Good Display GDEM102Z91, che è un 960x640 BWR sullo stesso SSD1677
 * (docs/097c/gooddisplay_GDEM102Z91_arduino/), scrive lo stesso soft start
 * 0x0C = AE C7 C3 C0 80, ultimo byte compreso — GxEPD2 GDEH116T91 lì mette
 * 0x40, ed è l'eccezione — e commenta 0x3C = 0x01 con "LUT1, for white", cioè
 * conferma la numerazione delle LUT della Table 6-4 su cui è costruita la mappa
 * delle quattro bande.
 * Ogni passo è cronometrato. Il BUSY dopo lo SWRESET viene osservato dentro
 * la finestra di 200 ms che il driver spende comunque a delay: la durata
 * reale del reset interno diventa un dato invece di una stima, e sul bus non
 * cambia niente perchè in quella finestra non si inviano comandi.
 */
static void initPanel()
{
  const uint32_t tReset = micros();
  resetPanel();
  delay(10);   // stesso delay di cortesia di _InitDisplay dopo il reset
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
  // il controller ignora i comandi per ~100-300 ms: la finestra resta piena
  const uint32_t swElapsed = millis() - tSw;
  if (swElapsed < 200)
    delay(200 - swElapsed);

  const uint32_t tCfg = micros();
  writeCommand(0x0C);   // soft start
  writeData(0xAE);
  writeData(0xC7);
  writeData(0xC3);
  writeData(0xC0);
  writeData(0x80);
  writeCommand(0x01);   // MUX 671 -> 672 gate line
  writeData(0x9F);
  writeData(0x02);
  writeData(0x00);
  writeCommand(0x3C);   // border waveform: LUT1, bianco
  writeData(0x01);
  writeCommand(0x18);   // sensore di temperatura interno
  writeData(0x80);
  writeCommand(0x11);   // entry mode: X e Y crescenti
  writeData(0x03);
  const uint32_t usCfg = micros() - tCfg;

  const uint32_t tWin = micros();
  setRamWindow(0, 0, SRC, GATE);
  const uint32_t usWin = micros() - tWin;

  Serial.printf("  reset hardware      %6lu us\n", (unsigned long)usReset);
  if (!rose)
    Serial.println(F("  SWRESET             BUSY non è salito entro 50 ms (il driver non lo verifica)"));
  else if (swBusy < 0)
    Serial.printf("  SWRESET             BUSY salito dopo %lu ms, ancora alto a 1000 ms\n",
                  (unsigned long)riseMs);
  else
    Serial.printf("  SWRESET             BUSY salito dopo %lu ms, alto per %ld ms (il driver attende 200 ms fissi)\n",
                  (unsigned long)riseMs, (long)swBusy);
  Serial.printf("  config 0x0C..0x11   %6lu us  (5 comandi, 9 parametri)\n", (unsigned long)usCfg);
  Serial.printf("  finestra RAM piena  %6lu us  (4 comandi, 12 parametri)\n", (unsigned long)usWin);
}

/**
 * Misura il costo del bus separando l'overhead fisso per transazione dal
 * costo per byte: scrive sempre lo stesso totale cambiando la dimensione del
 * blocco passato a writeBytes. I due blocchi che contano per il driver sono
 * 120 byte, una riga immagine di _writeImage, e 256 byte, il chunk di
 * _writeScreenBuffer; gli altri servono a vedere dove la curva si appiattisce.
 *
 * Lavora sul piano 0x26 dentro una finestra di una sola gate row: gli
 * indirizzi ciclano dentro la finestra, e il piano viene comunque riscritto
 * per intero subito dopo dal pattern hardware, quindi il test non lascia
 * traccia sull'immagine. Non usa 0x28 di proposito: il datasheet lo dà come
 * VCOM Sense, cioè un comando che alza il BUSY, e un BUSY lasciato alto in
 * eredità falserebbe la validazione del pattern che viene subito dopo.
 */
static void benchmarkBus()
{
  static const uint16_t chunk_sizes[] = { 1, 8, 32, 120, 256, 1024, 4096 };
  static const uint32_t BENCH_BYTES = 8192;
  static uint8_t buf[4096];   // statico: 4 KB sullo stack del task Arduino sono troppi
  memset(buf, 0x00, sizeof(buf));

  // il benchmark non deve inquinare il profilo delle operazioni del test
  const uint32_t cmdSnapshot = totalCommands;
  const uint32_t paramSnapshot = totalParamBytes;

  const double theoretical = (double)cfg.spiHz / 8.0 / 1000000.0;   // MB/s al clock nominale
  Serial.println(F("\nbenchmark del bus: 8192 byte per riga, blocco crescente"));
  Serial.printf("  limite teorico a %lu Hz: %.2f MB/s (%.3f us/byte)\n",
                (unsigned long)cfg.spiHz, theoretical, 1.0 / theoretical);
  Serial.println(F("  blocco  chiamate      us   us/byte     MB/s  %teorico  note"));
  for (uint8_t i = 0; i < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); ++i)
  {
    const uint16_t chunk = chunk_sizes[i];
    const uint32_t calls = BENCH_BYTES / chunk;
    const uint32_t written = calls * chunk;   // 8192 non è multiplo di 120
    setRamWindow(0, 0, SRC, 1);
    writeCommand(0x26);
    digitalWrite(PIN_DC, HIGH);
    hspi.beginTransaction(spiSettings);
    digitalWrite(PIN_CS, LOW);
    const uint32_t t0 = micros();
    for (uint32_t n = 0; n < calls; ++n)
      hspi.writeBytes(buf, chunk);
    const uint32_t dt = micros() - t0;
    digitalWrite(PIN_CS, HIGH);
    hspi.endTransaction();

    const double upb = (double)dt / (double)written;
    const double mbs = (double)written / (double)dt;
    const char* note = "";
    if (chunk == 120)
      note = "riga immagine _writeImage";
    else if (chunk == 256)
      note = "chunk _writeScreenBuffer";
    Serial.printf("  %6u %9lu %7lu %9.3f %8.2f %8.0f%%  %s\n",
                  (unsigned)chunk, (unsigned long)written / chunk, (unsigned long)dt,
                  upb, mbs, mbs / theoretical * 100.0, note);
    if (chunk == 120)
      rowUsPerByte = upb;
    if (upb > 0.0 && (bulkUsPerByte == 0.0 || upb < bulkUsPerByte))
      bulkUsPerByte = upb;
  }

  /**
   * Costo di una transazione per singolo byte: è la via di writeData e
   * writeCommand, quella che il driver usa per comandi e parametri e che
   * evita nei hot path proprio per questo scarto.
   */
  const uint32_t SINGLE = 512;
  setRamWindow(0, 0, SRC, 1);
  writeCommand(0x26);
  const uint32_t tData = micros();
  for (uint32_t n = 0; n < SINGLE; ++n)
    writeData(0x00);
  const uint32_t usData = micros() - tData;

  /**
   * Costo della transazione di comando. Si ripete 0x7F, che il datasheet
   * SSD1677 definisce NOP: comando vuoto senza alcun effetto sul modulo,
   * utilizzabile anche per terminare una scrittura o lettura di frame memory.
   * È quindi la ripetizione che non può alterare lo stato del controller.
   */
  const uint32_t tCmd = micros();
  for (uint32_t n = 0; n < SINGLE; ++n)
    writeCommand(0x7F);
  const uint32_t usCmd = micros() - tCmd;

  const double singleUsPerByte = (double)usData / (double)SINGLE;
  Serial.printf("  writeData()    %.2f us/byte, %.0f volte il blocco grande\n",
                singleUsPerByte, singleUsPerByte / usPerByteReference());
  Serial.printf("  writeCommand() %.2f us/comando\n",
                (double)usCmd / (double)SINGLE);
  Serial.printf("  un piano da %lu byte: %.0f ms a blocchi da 120, %.0f ms al limite del bus,\n",
                (unsigned long)PLANE_BYTES,
                (rowUsPerByte > 0.0 ? rowUsPerByte : usPerByteReference()) * PLANE_BYTES / 1000.0,
                usPerByteReference() * PLANE_BYTES / 1000.0);
  Serial.printf("                       %.0f ms se passasse da writeData()\n",
                singleUsPerByte * PLANE_BYTES / 1000.0);

  totalCommands = cmdSnapshot;
  totalParamBytes = paramSnapshot;
}

/**
 * Legge n byte da un registro del controller. Sulla Waveshare E-Paper ESP32
 * Driver Board la linea di uscita dati del pannello non è cablata al MISO,
 * quindi l'esito è credibile solo su hardware che la porta fuori: una lettura
 * tutta 0x00 o tutta 0xFF va interpretata come "non cablato".
 */
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
 * Legge lo Status Bit Read (0x2F) e lo usa come prova di validità del
 * percorso di lettura: il datasheet dà POR 0x01, con A[1:0] = chip ID = 01 e
 * A[2] = busy flag. Se il valore letto ha il chip ID a 01 e il busy flag
 * coerente col pin, allora la linea di uscita dati è cablata e anche le altre
 * letture valgono qualcosa. 0x00 o 0xFF pieni dicono il contrario.
 * Ritorna true se la lettura è credibile.
 */
static bool reportStatus()
{
  uint8_t st = 0;
  readRegister(0x2F, &st, 1);
  const uint8_t chipId = st & 0x03;
  const bool busyFlag = (st & 0x04) != 0;
  const bool pinBusy = digitalRead(PIN_BUSY) == BUSY_ACTIVE;
  const bool credible = (st != 0x00 && st != 0xFF && chipId == 0x01);
  /**
   * A[5] e A[4] sono attivi bassi: il datasheet dà 0 = Ready / Normal e 1 =
   * Not Ready / VCI sotto soglia. E avverte che dopo il reset non sono validi
   * finchè non li si inizializza con 0x14 e 0x15, che questo test non invia:
   * vanno letti come indeterminati.
   */
  Serial.printf("status 0x2F        0x%02X  chip ID %u, busy flag %d (pin BUSY %d), HV %s, VCI %s\n",
                st, (unsigned)chipId, (int)busyFlag, (int)pinBusy,
                (st & 0x20) ? "NON pronta" : "pronta",
                (st & 0x10) ? "sotto soglia" : "normale");
  Serial.println(F("                   HV e VCI non inizializzati (0x14 / 0x15 non inviati): indeterminati"));
  if (credible)
    Serial.println(F("                   chip ID atteso: la linea di lettura è cablata, letture valide"));
  else
    Serial.println(F("                   chip ID non atteso: linea di lettura non cablata, letture da ignorare"));
  return credible;
}

/**
 * Legge i 10 byte di User ID da OTP (0x2E). Su un pannello recuperato da ESL
 * è l'unico modo di farsi dire dal controller quale modulo sta pilotando,
 * quindi vale il tentativo anche se serve la linea di lettura cablata.
 */
static void reportUserId()
{
  uint8_t id[10];
  memset(id, 0, sizeof(id));
  readRegister(0x2E, id, sizeof(id));
  Serial.print(F("user ID 0x2E       "));
  for (uint8_t i = 0; i < sizeof(id); ++i)
    Serial.printf("%02X ", id[i]);
  Serial.print(F(" ascii \""));
  for (uint8_t i = 0; i < sizeof(id); ++i)
    Serial.print((char)(id[i] >= 0x20 && id[i] < 0x7F ? id[i] : '.'));
  Serial.println(F("\""));
}

/**
 * Legge il registro di temperatura (0x1B) e prova a decodificarlo: 12 bit in
 * complemento a due, 1 LSB = 1/16 di grado. Il valore è significativo solo
 * dopo che un update ha caricato la temperatura, cioè dopo il bit load-temp
 * del comando 0x22: per questo viene letto anche a refresh concluso. È la
 * temperatura che seleziona la LUT, quindi determina la durata del refresh.
 */
static void reportTemperature(const char* when)
{
  uint8_t raw[2] = { 0, 0 };
  readRegister(0x1B, raw, 2);
  const uint16_t reg = ((uint16_t)raw[0] << 8) | raw[1];
  int16_t value = (int16_t)(reg >> 4);
  if (value & 0x0800)
    value -= 4096;
  Serial.printf("temperatura %-18s raw 0x%02X%02X", when, raw[0], raw[1]);
  if (reg == 0x0000 || reg == 0xFFFF)
    Serial.println(F("  -> linea di lettura non cablata o sensore non campionato"));
  else
    Serial.printf("  -> %.1f gradi\n", (double)value / 16.0);
}

/**
 * Righe di testo precomposte più la riga di fondo. Statiche e non sullo stack:
 * sono 21 x 120 byte, e servono a tenere la composizione FUORI dalla regione
 * cronometrata (vedi writeRowsWithText).
 */
static uint8_t textRows[TEXT_MAX_LINES * FONT_H][ROW_BYTES];
static uint8_t textBgRow[ROW_BYTES];

/**
 * Accende n pixel consecutivi a partire da x nel valore fg, dentro una riga
 * RAM. Il bit più significativo di un byte è il pixel più a sinistra, e il
 * corpo del font non è più multiplo di 8: serve la maschera, la memset di byte
 * interi non basta più.
 */
static void setSpan(uint8_t* row, uint16_t x, uint8_t n, uint8_t fg)
{
  for (uint8_t i = 0; i < n; ++i)
  {
    const uint16_t px = x + i;
    if (px >= SRC) return;
    const uint8_t m = (uint8_t)(0x80 >> (px % 8));
    uint8_t& b = row[px / 8];
    b = (uint8_t)((b & ~m) | (fg & m));
  }
}

/**
 * Riempie la finestra RAM corrente riga per riga sovraimprimendo la frase
 * della passata: fondo a bg, pixel del testo a fg. È il percorso di
 * _writeImage del driver, blocchi da ROW_BYTES, quindi misura l'altro dei due
 * modi in cui il driver spinge i dati. La finestra resta a larghezza piena: il
 * testo si ottiene componendo le righe, non restringendo la finestra.
 *
 * Le righe di testo si compongono PRIMA di micros(), una volta sola: sono
 * lay.lines * FONT_H righe distinte e ognuna va ripetuta lay.scale volte,
 * quindi il ciclo cronometrato si limita a spingere la riga giusta e il costo
 * della maschera di bit non entra nei us/byte, che sono una misura del test.
 * Ritorna i microsecondi del solo transfer.
 */
static uint32_t writeRowsWithText(uint8_t bg, uint16_t h, uint8_t fg,
                                  const TextLayout& lay)
{
  memset(textBgRow, bg, sizeof(textBgRow));
  for (uint8_t l = 0; l < lay.lines; ++l)
  {
    for (uint8_t fr = 0; fr < FONT_H; ++fr)
    {
      uint8_t* row = textRows[l * FONT_H + fr];
      memset(row, bg, ROW_BYTES);
      for (uint8_t k = 0; k < lay.len[l]; ++k)
      {
        const uint8_t bits = glyphFor(lay.start[l][k])[fr];
        for (uint8_t c = 0; c < FONT_W; ++c)
        {
          if (bits & (0x10 >> c))
            setSpan(row, (uint16_t)(TEXT_MARGIN_X
                                    + ((uint16_t)k * FONT_PITCH + c) * lay.scale),
                    lay.scale, fg);
        }
      }
    }
  }

  const int16_t lineH = (int16_t)FONT_H * lay.scale;
  const int16_t pitch = lineH + (int16_t)TEXT_LINE_GAP * lay.scale;

  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  const uint32_t t0 = micros();
  for (uint16_t r = 0; r < h; ++r)
  {
    const uint8_t* src = textBgRow;
    const int16_t dy = (int16_t)r - lay.top;
    if (dy >= 0)
    {
      const int16_t line   = dy / pitch;
      const int16_t within = dy % pitch;
      if (line < (int16_t)lay.lines && within < lineH)
        src = textRows[line * FONT_H + within / lay.scale];
    }
    hspi.writeBytes(src, ROW_BYTES);
  }
  const uint32_t dt = micros() - t0;
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
  totalSpiBytes += (uint32_t)ROW_BYTES * h;
  totalSpiMicros += dt;
  return dt;
}

/**
 * Scrive una fascia a larghezza piena su un piano, con valore costante.
 * Separa il costo dell'indirizzamento da quello dei dati: la finestra sono 4
 * comandi più 12 parametri a singolo byte, e su fasce piccole pesa.
 *
 * Con un testo la frase viene sovraimpressa al valore fg e le righe passano per
 * il percorso _writeImage; senza testo si usa il percorso a blocchi da 256 di
 * _writeScreenBuffer. Le due chiamate per banda, una per piano, devono passare
 * lo stesso testo: sono i due piani insieme a determinare il colore dei pixel
 * della frase, e su un piano fg può coincidere col fondo (scrittura senza
 * effetto, ma simmetrica).
 */
static void fillBand(uint8_t plane, uint16_t y, uint16_t h, uint8_t value,
                     const char* text = nullptr, uint8_t fg = 0x00)
{
  const uint32_t bytes = (uint32_t)ROW_BYTES * h;
  const TextLayout lay = layoutText(text, h);
  const bool withText = lay.scale > 0;
  const uint32_t tWin = micros();
  setRamWindow(0, y, SRC, h);
  writeCommand(plane);
  const uint32_t usWin = micros() - tWin;
  const uint32_t us = withText ? writeRowsWithText(value, h, fg, lay)
                               : writeConst(value, bytes);
  Serial.printf("   0x%02X y=%3u..%3u val=%02X %6lu B  win %4lu us  dati %6lu us  %.2f us/B  %.2f MB/s  %s\n",
                plane, (unsigned)y, (unsigned)(y + h - 1), value,
                (unsigned long)bytes, (unsigned long)usWin, (unsigned long)us,
                (double)us / (double)bytes, (double)bytes / (double)us,
                withText ? "righe 120 + testo"
                         : (text ? "blocchi 256, testo non entra nella fascia"
                                 : "blocchi 256"));
}

/**
 * Riempie un rettangolo di un piano con un valore costante, finestra
 * ristretta anche lungo X. x e w devono essere multipli di 8: sull'asse
 * source la finestra lavora per byte e un bordo a metà byte non è
 * rappresentabile. Con l'entry mode 0x03 l'indirizzo avanza dentro la
 * finestra e va a capo da solo, quindi basta spingere (w/8)*h byte di seguito.
 *
 * Serve alla sonda del partial d'area, l'unica parte del test che restringe
 * la finestra su entrambi gli assi: le bande della misura dei colori sono a
 * larghezza piena per definizione e continuano a passare da fillBand.
 * Ritorna i microsecondi del solo transfer.
 */
static uint32_t fillRect(uint8_t plane, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint8_t value)
{
  setRamWindow(x, y, w, h);
  writeCommand(plane);
  return writeConst(value, (uint32_t)(w / 8) * h);
}

// ---------------------------------------------------------------------------
// SCHERMATE NUMERATE
//
// Il numero della schermata lega il log al vetro: con una trentina di refresh
// e nove sonde che riusano le stesse fasce, è l'unico modo di sapere quale
// blocco di log commenta quello che si sta guardando.
// ---------------------------------------------------------------------------

/**
 * Le schermate da guardare, nell'ordine in cui il test le produce. Il numero
 * stampato lo assegna apriSchermata() al volo, così una sonda che si salta da
 * sè non consuma un numero e nella sequenza non restano buchi.
 */
enum Schermata : uint8_t
{
  SCH_BANDE,       // le 4 combinazioni dei due piani, Mode 1
  SCH_MODE2,       // le stesse 4 combinazioni, Mode 2
  SCH_LIVELLI,     // probe del quarto colore per livello di sorgente
  SCH_DIFF,        // sonda differenziale, le due passate 0xFC
  SCH_DIFF_DEEP,   // sonda differenziale approfondita, ultima passata
  SCH_LUT,         // partial con LUT caricata via 0x32
  SCH_AREA,        // partial d'area: fasce, trappola e riquadro
  SCH_TEMP,        // banchi di waveform per temperatura
  SCH_MUX,         // MUX ridotto
  SCH_SLEEP,       // refresh di prova dopo il risveglio dal deep sleep
  SCH_COUNT
};

/** Numero di ogni schermata, 0 se la sonda non è arrivata a produrla: lo cita
 *  la scheda finale. */
static uint8_t schermate[SCH_COUNT] = { 0 };

static uint8_t schermataCorrente = 0;   // schermata aperta, 0 = nessuna
static uint8_t numeroSulVetro    = 0;   // ultimo riquadro disegnato

/**
 * Apre una schermata: assegna il numero successivo, che fino a
 * chiudiSchermata() ogni refresh disegna sul vetro e ogni riga di osservazione
 * stampa in testa.
 */
static void apriSchermata(Schermata quale)
{
  static uint8_t assegnati = 0;
  schermataCorrente = ++assegnati;
  schermate[quale] = schermataCorrente;
}

/** Chiude la schermata: i refresh successivi non portano più il riquadro. */
static void chiudiSchermata()
{
  schermataCorrente = 0;
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
static const uint8_t BADGE_MAX_H     = FONT_H * BADGE_SCALA_MAX
                                       + 2 * (BADGE_BORDO + BADGE_ARIA);
static const uint8_t BADGE_MAX_BYTES = 16;

static uint8_t badgeRows[BADGE_MAX_H][BADGE_MAX_BYTES];

/**
 * Spinge il riquadro su un piano: le righe di badgeRows sul piano B/N, zeri sul
 * piano accent. Non alimenta i contatori del bus, perchè è strumentazione del
 * test e non lavoro che il driver pagherebbe.
 */
static void pushBadgePiano(uint8_t plane, uint16_t bx, uint16_t by,
                           uint8_t bw, uint8_t bh, bool cifre)
{
  static const uint8_t zeri[BADGE_MAX_BYTES] = { 0 };
  const uint8_t nbyte = (uint8_t)(bw / 8);

  setRamWindow(bx, by, bw, bh);
  writeCommand(plane);
  digitalWrite(PIN_DC, HIGH);
  hspi.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  for (uint8_t r = 0; r < bh; ++r)
    hspi.writeBytes(cifre ? badgeRows[r] : zeri, nbyte);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

/**
 * Disegna il numero della schermata in alto a destra della finestra RAM
 * corrente: cifre nere su fondo bianco con cornice, accent spento sotto tutto
 * il riquadro, così si legge sopra qualunque colore la passata dipinga. Stando
 * dentro la finestra, il refresh lo ridipinge anche quando è ristretta.
 *
 * Il corpo è il più grande che ci entra, da 30x42 px a 10x14; sotto quello il
 * riquadro si salta e lo dichiara. La finestra viene salvata e rimessa: è
 * quella a decidere l'area ridipinta. Chiamata da runRefresh().
 */
static void disegnaBadge(uint8_t numero)
{
  char testo[4];
  const int len = snprintf(testo, sizeof(testo), "%u", (unsigned)numero);

  uint8_t scala = 0;
  uint8_t bw = 0, bh = 0;
  for (uint8_t sc = BADGE_SCALA_MAX; sc >= BADGE_SCALA_MIN; --sc)
  {
    const uint16_t testoW = (uint16_t)(len * FONT_PITCH - 1) * sc;
    const uint16_t larga  = (uint16_t)((testoW + 2 * (BADGE_BORDO + BADGE_ARIA) + 7) & ~7);
    const uint16_t alta   = (uint16_t)(FONT_H * sc + 2 * (BADGE_BORDO + BADGE_ARIA));
    if (larga + BADGE_MARGINE > ramWinW) continue;
    if (alta > ramWinH) continue;
    scala = sc;
    bw = (uint8_t)larga;
    bh = (uint8_t)alta;
    break;
  }
  if (scala == 0)
  {
    Serial.printf("   finestra %ux%u troppo piccola per il riquadro della schermata %u:\n"
                  "   questa passata la riconosci dalla frase sulla fascia\n",
                  (unsigned)ramWinW, (unsigned)ramWinH, (unsigned)numero);
    return;
  }

  // fondo bianco, cornice nera sui quattro lati
  const uint8_t nbyte = (uint8_t)(bw / 8);
  for (uint8_t r = 0; r < bh; ++r)
  {
    if (r < BADGE_BORDO || r >= bh - BADGE_BORDO)
      memset(badgeRows[r], 0x00, nbyte);
    else
    {
      memset(badgeRows[r], 0xFF, nbyte);
      setSpan(badgeRows[r], 0, BADGE_BORDO, 0x00);
      setSpan(badgeRows[r], (uint16_t)(bw - BADGE_BORDO), BADGE_BORDO, 0x00);
    }
  }

  // cifre centrate nel riquadro, stesso font 5x7 delle frasi sulle fasce
  const uint16_t testoW = (uint16_t)(len * FONT_PITCH - 1) * scala;
  const uint16_t x0 = (uint16_t)((bw - testoW) / 2);
  const uint8_t  y0 = (uint8_t)((bh - FONT_H * scala) / 2);
  for (int k = 0; k < len; ++k)
  {
    const uint8_t* g = glyphFor(testo[k]);
    for (uint8_t fr = 0; fr < FONT_H; ++fr)
      for (uint8_t sy = 0; sy < scala; ++sy)
      {
        uint8_t* row = badgeRows[y0 + fr * scala + sy];
        for (uint8_t c = 0; c < FONT_W; ++c)
          if (g[fr] & (0x10 >> c))
            setSpan(row, (uint16_t)(x0 + ((uint16_t)k * FONT_PITCH + c) * scala),
                    scala, 0x00);
      }
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
  Serial.printf("[%u] GUARDA IL PANNELLO: SCHERMATA %u, il numero sta nel riquadro in\n", n, n);
  Serial.printf("[%u] alto a destra dell'area appena ridipinta\n", n);
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
  "le 4 bande, Display Mode 1",
  "le stesse 4 bande, Display Mode 2",
  "probe dei livelli di sorgente",
  "sonda differenziale",
  "sonda differenziale approfondita",
  "partial con LUT custom",
  "partial d'area",
  "banchi di waveform per temperatura",
  "MUX ridotto",
  "deep sleep, refresh di prova dopo il risveglio",
};

/**
 * Voce della scheda finale: titolo col numero della schermata a cui si
 * riferisce e righe da compilare. Una sonda che non è arrivata a produrre la
 * schermata stampa [--].
 */
static void voceScheda(Schermata quale, const char* titolo,
                       const char* const* righe, uint8_t quante)
{
  char pre[8];
  if (schermate[quale])
    snprintf(pre, sizeof(pre), "[%u]", (unsigned)schermate[quale]);
  else
    snprintf(pre, sizeof(pre), "[--]");

  Serial.println();
  Serial.printf("%s %s%s\n", pre, titolo,
                schermate[quale] ? "" : "   (schermata non prodotta)");
  for (uint8_t i = 0; i < quante; ++i)
    Serial.printf("%s   %s\n", pre, righe[i]);
}

/**
 * Riempie un piano immagine col generatore di pattern del controller: 0x47
 * per il piano 0x24, 0x46 per il piano 0x26. Il parametro codifica A[7] =
 * valore del primo step, A[6:4]=111 step height 680, A[2:0]=111 step width
 * 960, cioè un unico step su tutta la RAM nativa. La finestra piena viene
 * impostata prima come fa _fillPlaneByPattern, anche se il pattern la ignora:
 * serve a lasciare area e cursore nello stesso stato del percorso SPI. Se
 * BUSY non sale il comando non è supportato e il driver deve restare sul
 * transfer SPI. Ritorna i ms del riempimento, -1 se non praticabile.
 */
static int32_t fillByPattern(uint8_t pattern_command, uint8_t value)
{
  const uint8_t param = value ? 0xF7 : 0x77;
  ensureBusyLow("fillByPattern");
  setRamWindow(0, 0, SRC, GATE);
  writeCommand(pattern_command);
  writeData(param);
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - t0) < 200)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    Serial.printf("  0x%02X  param 0x%02X  BUSY MAI SALITO: pattern non supportato\n",
                  pattern_command, param);
    return -1;
  }
  const uint32_t riseMs = millis() - t0;
  const int32_t ms = waitBusy(2000);
  if (ms < 0)
  {
    Serial.printf("  0x%02X  param 0x%02X  timeout BUSY a 2000 ms\n", pattern_command, param);
    return -1;
  }
  // confronto col push SPI che il pattern sostituisce: 80.640 byte sul bus
  const double spiMs = usPerByteReference() * PLANE_BYTES / 1000.0;
  Serial.printf("  0x%02X  param 0x%02X  BUSY su dopo %lu ms, riempito in %ld ms  (push SPI equivalente %.0f ms, %.1fx)\n",
                pattern_command, param, (unsigned long)riseMs, (long)ms,
                spiMs, spiMs / (ms > 0 ? (double)ms : 1.0));
  return ms;
}

/**
 * Attende la fine del refresh e rileva eventuali passate successive: un
 * pannello multi-fase riabbassa e rialza il BUSY, e saperlo cambia il timeout
 * che il driver deve tenere.
 *
 * Avanzamento su UNA riga, che il chiamante ha già aperto: un punto ogni
 * 2500 ms e una barra a ogni confine di fase, poi i numeri che contano. Prima
 * era una riga di log ogni 2500 ms, cioè dodici righe per refresh e oltre
 * trecento per run, per dire tre numeri.
 *
 * Ritorna i ms dalla master activation all'ULTIMA discesa del BUSY. Gli 800 ms
 * della finestra con cui si guarda se il BUSY risale NON ci sono dentro: sono
 * tempo di misura e non di refresh, e includerli gonfiava di 800 ms ogni durata
 * stampata, il totale delle attese e le costanti ricavate per il driver. Le
 * pause fra una fase e l'altra di una passata multi-fase invece ci sono,
 * perchè quelle sono refresh. -1 al timeout.
 */
static int32_t waitRefresh(uint32_t timeout_ms)
{
  const uint32_t t0 = millis();
  int phase = 0;
  uint32_t tFall = t0;   // istante dell'ultima discesa del BUSY
  bool timeout = false;
  while (true)
  {
    ++phase;
    const uint32_t tPhase = millis();
    uint32_t nextDot = 2500;
    while (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    {
      if ((millis() - t0) > timeout_ms)
      {
        timeout = true;
        break;
      }
      if ((millis() - tPhase) >= nextDot)
      {
        Serial.print('.');
        nextDot += 2500;
      }
      delay(1);
    }
    if (timeout) break;
    tFall = millis();
    // se il BUSY si rialza entro 800 ms il refresh procede in più passate
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
    // confine fra due fasi: la durata della fase che si chiude qui è un dato
    // che il driver deve conoscere, quindi va stampata e non solo segnata
    Serial.printf("|%lu ms|", (unsigned long)(tFall - tPhase));
  }
  if (timeout)
  {
    const uint32_t dt = millis() - t0;
    totalBusyMillis += dt;
    Serial.printf("  TIMEOUT a %lu ms, refresh NON concluso\n", (unsigned long)timeout_ms);
    return -1;
  }
  const uint32_t dt = tFall - t0;
  totalBusyMillis += dt;
  Serial.printf("  %lu ms di BUSY in %d fas%s\n",
                (unsigned long)dt, phase, phase == 1 ? "e" : "i");
  return (int32_t)dt;
}

/**
 * Esegue una passata di refresh con la sequenza di update indicata e ne
 * misura i tempi. 0xF7 e 0xFF differiscono solo per il Display Mode, 1 e 2:
 * stessa RAM, waveform diversa presa da OTP; 0xFC è Mode 2 senza il power
 * down finale, la sequenza del refresh differenziale. Verifica anche che il
 * BUSY salga, cioè che il controller abbia davvero preso il comando.
 *
 * frase è LA STESSA STRINGA che la passata ha scritto sulla propria fascia: chi
 * legge il pannello ritrova nel log la riga di quella passata, e viceversa. È
 * anche l'etichetta con cui la passata compare nelle tabelle di riepilogo, così
 * di ogni passata esiste un solo nome.
 *
 * Se una schermata è aperta, la riga parte col suo numero fra parentesi quadre
 * e lo stesso numero viene disegnato sul vetro prima della master activation.
 *
 * timeout_ms è un parametro perchè una passata con LUT custom, se la waveform
 * viene applicata, dura tre ordini di grandezza meno di una piena, e una
 * passata a temperatura forzata può durare il doppio.
 *
 * Ritorna i ms del BUSY, -1 se la passata non si è conclusa, REFRESH_NOT_RUN
 * se non è stata nemmeno avviata perchè il controller era occupato.
 */
static const int32_t REFRESH_NOT_RUN = -2;

static int32_t runRefresh(uint8_t updateSequence, const char* frase,
                          uint32_t timeout_ms = 0)   // 0 = cfg.timeoutMs
{
  if (timeout_ms == 0) timeout_ms = cfg.timeoutMs;
  Serial.println();
  if (schermataCorrente)
    Serial.printf("[%u] >> %s   [0x22=0x%02X]\n",
                  (unsigned)schermataCorrente, frase, updateSequence);
  else
    Serial.printf(">> %s   [0x22=0x%02X]\n", frase, updateSequence);
  /**
   * Guardia vincolante: con il BUSY alto il controller ignora 0x22 e 0x20, e
   * quello che si misurerebbe è la coda dell'operazione precedente. Meglio
   * nessun numero che un numero falso.
   */
  if (!ensureBusyLow(frase))
  {
    Serial.println(F("   NON ESEGUITA: il controller era occupato"));
    return REFRESH_NOT_RUN;
  }

  // il riquadro va scritto in RAM prima della master activation, o non si vede
  if (schermataCorrente)
    disegnaBadge(schermataCorrente);

  writeCommand(0x22);   // clock, analog, load temp, load LUT, display, power off
  writeData(updateSequence);
  writeCommand(0x20);   // master activation

  /**
   * Il BUSY deve salire quasi subito. Se non sale, il controller non ha preso
   * il comando: cablaggio o alimentazione, e tutto il resto non ha valore.
   */
  const uint32_t tAct = millis();
  while (digitalRead(PIN_BUSY) != BUSY_ACTIVE && (millis() - tAct) < 1000)
    delay(1);
  if (digitalRead(PIN_BUSY) != BUSY_ACTIVE)
  {
    Serial.println(F("   ATTENZIONE: BUSY non è mai salito, il controller non risponde"));
    return -1;
  }
  Serial.printf("   BUSY su dopo %lu ms, attesa ", (unsigned long)(millis() - tAct));
  return waitRefresh(timeout_ms);
}

/**
 * Pausa di osservazione: il pannello mostra un risultato che la fase successiva
 * sovrascrive. Aspetta INVIO, oppure cfg.pausaMs se nessuno risponde — così una
 * lettura lenta non costa più un run intero, e un run non presidiato scorre da
 * solo come prima. Le righe portano il numero della schermata sul vetro.
 */
static void observePause(const char* cosaSuccede)
{
  if (cfg.pausaMs == 0) return;
  const unsigned n = numeroDiRiga();
  scartaInputPendente();
  Serial.println();
  Serial.printf("[%u] GUARDA IL PANNELLO E ANNOTA: %s\n", n, cosaSuccede);
  Serial.printf("[%u] premi INVIO quando hai finito, o aspetta %lu s\n",
                n, (unsigned long)(cfg.pausaMs / 1000));

  const uint32_t t0 = millis();
  uint32_t prossimo = 5000;
  bool anticipata = false;
  while ((millis() - t0) < cfg.pausaMs)
  {
    if (Serial.available()) { anticipata = true; break; }
    if ((millis() - t0) >= prossimo)
    {
      Serial.printf("[%u]   %lu s\n", n, (unsigned long)((cfg.pausaMs - prossimo) / 1000));
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

// ---------------------------------------------------------------------------
// Waveform LUT per le sonde del partial e del quarto colore.
//
// Layout, dal datasheet SSD1677 §6.7 Figure 6-6. Il comando 0x32 scrive i byte
// 0..104, cioè tutto tranne le tensioni:
//
//   byte   0.. 9   LUT0, dieci gruppi da quattro fasi, 2 bit per fase
//   byte  10..19   LUT1
//   byte  20..29   LUT2
//   byte  30..39   LUT3
//   byte  40..49   LUT4
//   byte  50..99   dieci gruppi da { TP[nA], TP[nB], TP[nC], TP[nD], RP[n] }
//   byte 100..104  frame rate
//   byte 105..109  VGH, VSH1, VSH2, VSL, VCOM: NON scritti da 0x32, arrivano
//                  da 0x03 / 0x04 / 0x2C e restano quelli dell'OTP.
//
// È il motivo per cui queste sonde sono a rischio contenuto: cambiano la
// sequenza delle fasi, non le tensioni con cui il film viene pilotato.
//
// I 2 bit di VS[nX-LUTm], Table 6-6: 00 = VSS, 01 = VSH1, 10 = VSL, 11 = VSH2.
// TP[nX] = durata della fase in frame, 0 = fase saltata. RP[n] = ripetizioni
// del gruppo meno una.
// ---------------------------------------------------------------------------
static const uint16_t LUT_BYTES = 105;

/**
 * LUT di partial update del GDEH116T91, copiata da GxEPD2 1.6.9,
 * src/epd/GxEPD2_1160_T91.cpp (GPL-3.0, come questa libreria).
 *
 * Perchè proprio questa: quel pannello è 960x640 sullo stesso SSD1677, cioè
 * stesso silicio e stessi 960 source, e con essa dichiara e ottiene
 * partial_refresh_time = 700 ms contro i 6200 del suo refresh pieno.
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
 * frame nuovo): LUT1 e LUT2 sono quindi le due transizioni, una per verso, e
 * si vede che sono simmetriche. LUT0 e LUT3 a zero sono i pixel che non
 * cambiano, e non venendo pilotati non consumano tempo.
 *
 * La verifica che il layout è interpretato bene: 10 + 18 = 28 frame in tutto,
 * che a un frame rate di ~50 Hz fanno ~560 ms. Il driver di quel pannello
 * dichiara partial_refresh_time = 700 ms, cioè lo stesso ordine di grandezza
 * con il margine che ci si aspetta.
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
 * Serve perchè su questo pannello le due RAM non sono (precedente, nuovo) ma
 * (accent, bianco/nero), e l'indice di LUT esce dalla Table 6-4: (0,0) nero =
 * LUT0, (0,1) bianco = LUT1, (1,x) accent = LUT2 e LUT3. La LUT del 1160
 * lascia LUT0 a zero, quindi con essa i pixel neri non verrebbero pilotati
 * affatto: qui LUT0 prende la waveform che nel 1160 stava in LUT2, cioè quella
 * che spinge nel verso opposto a LUT1, e LUT2 / LUT3 restano a zero perchè un
 * frame aggiornato in partial è per forza senza accent.
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
 * due bande stampate è quale delle due tensioni positive tocca il film.
 *
 * È l'ultimo test possibile sul quarto colore, e l'unico ancora sensato:
 *   - un film a tre pigmenti risponde a VSH1 e VSH2 con la stessa migrazione,
 *     più o meno intensa, quindi le due bande escono dello stesso colore;
 *   - un film a quattro pigmenti separa i due per soglia, ed è esattamente
 *     così che un BWRY distingue rosso e giallo.
 * Le tensioni non le decide questa LUT: VSH1 e VSH2 stanno ai byte 106 e 107,
 * fuori dalla portata di 0x32, e restano quelle di fabbrica.
 *
 * Tempi: quattro fasi da 50 frame, gruppo non ripetuto, cioè 200 frame in
 * tutto. Un ordine di grandezza sotto la waveform di produzione, che a occhio
 * ne usa qualche migliaio: abbastanza per muovere il pigmento, non abbastanza
 * per sovra-pilotarlo.
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
/** Durate del BUSY delle varianti di partial, -1 se non misurate. */
static int32_t lutPartialMs[VARIANTI_LUT] = { -1, -1 };

/**
 * Carica una waveform LUT via 0x32 e imposta il border.
 *
 * Il border va messo a 0xC0, HiZ, e non al valore di produzione 0x01, che
 * aggancia la cornice a LUT1: durante un partial quella cornice verrebbe
 * pilotata a ogni passata e lampeggerebbe. È quello che fa _Init_Part() del
 * GDEH116T91.
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
 * Prepara i due piani per una passata di partial: 0x26 riceve il frame
 * precedente e 0x24 quello nuovo, con una fascia nera e la sua frase.
 *
 * Attenzione alla convenzione: in Mode 2 la RAM 0x26 non è l'accent ma il
 * frame precedente, quindi va scritta con la polarità del piano BW, cioè
 * bit=1 = bianco. È come lo fa writeImageAgain() del GDEH116T91, che scrive
 * la stessa bitmap su 0x26 e 0x24.
 */
static void stagePartialFrame(const char* const* frasi, uint8_t upTo,
                              uint16_t h, bool mode2)
{
  /**
   * 0x26 secondo il Display Mode della passata, e non è un dettaglio.
   *
   * In Mode 2 quella RAM è il frame precedente e va scritta con la polarità del
   * piano BW, cioè a 1 dove il vetro è bianco. In Mode 1 invece è ancora
   * l'accent, e lasciarla a 1 accenderebbe l'accent su tutto lo schermo: la
   * Table 6-4 manderebbe ogni pixel su LUT2 o LUT3, che nella LUT riassegnata
   * sono a zero, e quelle due varianti non dipingerebbero niente. Con l'accent
   * spento i pixel usano LUT0 e LUT1 secondo il piano BW, che è quello per cui
   * la LUT riassegnata è costruita.
   */
  fillByPattern(0x46, mode2 ? 0xFF : 0x00);
  fillByPattern(0x47, 0xFF);   // 0x24 = frame nuovo, parte da bianco

  /**
   * Le fasce si accumulano: il refresh ridipinge tutto lo schermo secondo la
   * RAM, quindi scrivere solo la fascia corrente cancellerebbe quelle delle
   * passate precedenti, e la scheda chiede proprio di confrontarle. Così una
   * passata che non dipinge lascia sul vetro le fasce dell'ultima riuscita, e
   * una che dipinge le mostra tutte fino alla propria.
   */
  for (uint8_t d = 1; d <= upTo; ++d)
    fillBand(0x24, (uint16_t)(h * (d - 1)), h, 0x00, frasi[d - 1], 0xFF);

  /**
   * Finestra piena prima di uscire: fillBand la lascia sulla propria fascia, e
   * la si rimette a schermo intero perchè le fasce accumulate vanno ridipinte
   * tutte e la durata misurata non deve dipendere anche dall'indirizzamento,
   * che qui non è la variabile in esame.
   * Che la finestra confini l'area ridipinta NON è vero su questo pannello: la
   * sonda d'area lo ha misurato con la fascia di trappola, comparsa nera pur
   * non essendo compresa in nessuna finestra. È comunque la configurazione con
   * cui i 641 ms del partial sono stati cronometrati, e va tenuta.
   */
  setRamWindow(0, 0, SRC, GATE);
}

/**
 * Sonda del partial via LUT caricata dall'MCU: la strada che il log del run
 * precedente NON aveva provato.
 *
 * Cosa era già stato misurato, e perchè non chiudeva la questione. Tutte le
 * passate cronometrate finora — 0xFC, 0xFF, 0xCF, 0xC7 — hanno il bit 4 di
 * 0x22 attivo, cioè ricaricano la LUT dall'OTP, oppure girano su una LUT che
 * dall'OTP era già stata caricata. Misuravano quindi sempre la stessa
 * waveform, e i 24,6-24,8 s uniformi dicono che in OTP ce n'è una sola: non
 * dicono che il partial sia impossibile.
 *
 * La sequenza mancante è quella del GDEH116T91, stesso SSD1677 con gli stessi
 * 960 source, che così fa 700 ms:
 *
 *   0x3C = 0xC0        border HiZ
 *   0x32 + 105 byte    waveform breve, scritta dall'MCU
 *   0x26 <- precedente, 0x24 <- nuovo
 *   0x22 = 0xCC        bit4 SPENTO: non ricarica l'OTP sopra la LUT custom
 *   0x20
 *
 * Due varianti, ognuna con la propria fascia e la propria frase, così sul vetro
 * si legge quale ha davvero dipinto:
 *
 *   fascia 1  PARTIAL 1  LUT del 1160, 0x22 = 0xCC (Mode 2)
 *   fascia 2  PARTIAL 2  LUT riassegnata alla Table 6-4, 0x22 = 0xC4 (Mode 1)
 *
 * Sono le due DIAGONALI della matrice LUT per Display Mode. Le due celle
 * incrociate — LUT del 1160 in Mode 1 e riassegnata in Mode 2 — sono mute per
 * costruzione e non vengono provate: sarebbero due refresh da venticinque
 * secondi che non possono dipingere.
 *
 * Mode 1 contro Mode 2 conta perchè cambia il significato delle due RAM: in
 * Mode 2 sono (precedente, nuovo), in Mode 1 la Table 6-4 le legge come
 * (accent, BW). Le due LUT sono costruite per le due letture, e la coppia di
 * varianti dice quale delle due il silicio applica davvero.
 *
 * QUALI DELLE QUATTRO POSSONO DIPINGERE, e perchè due no. La matrice LUT per
 * Display Mode ha due celle mute per costruzione, e sono un controllo:
 *
 *   Mode 2  le due RAM sono (precedente, nuovo), e sulla fascia nera la coppia
 *           è (1, 0) -> LUT2. Popolata nella LUT del 1160, a zero nella
 *           riassegnata.
 *   Mode 1  la Table 6-4 legge (accent, BW), e con l'accent spento la fascia
 *           nera è (0, 0) -> LUT0. A zero nella LUT del 1160, popolata nella
 *           riassegnata.
 *
 * Dipingono quindi la variante 1 (1160 in Mode 2) e la 4 (riassegnata in Mode
 * 1); la 2 e la 3 sono le celle incrociate, e il loro silenzio è atteso. Se
 * dipingessero, vorrebbe dire che il silicio non legge le due RAM come il
 * datasheet dichiara, e sarebbe un'informazione a sua volta.
 *
 * Come si legge l'esito, in ordine di importanza:
 *   1. una durata sotto i 3 s CON la sua frase sul vetro = partial trovato;
 *   2. una durata sotto i 3 s SENZA niente sul vetro = la waveform non
 *      pilota, la LUT va aggiustata ma la strada è quella;
 *   3. 24 s = il controller ha ricaricato l'OTP e la LUT custom è stata
 *      ignorata, quindi su questo silicio 0x32 non è utilizzabile così.
 *
 * Ogni variante riparte da reset più init, perchè il SWRESET rimette le LUT
 * dell'OTP e nessuna variante deve trovare i registri sporchi dalla
 * precedente. Il prezzo è che dopo la sonda i registri sono quelli di init.
 */
static void probePartialLut(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del partial con LUT caricata via 0x32 ==="));
  Serial.println(F("la strada mai provata: le passate cronometrate finora ricaricavano"));
  Serial.println(F("tutte la LUT dall'OTP (bit 4 di 0x22), quindi misuravano sempre la"));
  Serial.println(F("stessa waveform lunga. Qui la waveform la scrive l'MCU e 0x22 ha il"));
  Serial.println(F("bit 4 spento, come fa il GDEH116T91 sullo stesso silicio a 700 ms."));

  /**
   * Schermata aperta prima delle varianti: entrambe la ridipingono, quindi il
   * riquadro esce con il numero della passata che ha davvero dipinto. Con una
   * LUT custom può uscire incompleto, perchè la waveform non pilota tutte le
   * LUT: è dichiarato nel blocco di osservazione.
   */
  apriSchermata(SCH_LUT);

  /**
   * Nessuna baseline, ed è un refresh risparmiato senza perdere niente: se una
   * variante dipinge, ridipinge lei tutto lo schermo secondo la RAM, che
   * stagePartialFrame porta a bianco più le fasce; se non dipinge, il vetro
   * resta come l'ha lasciato la sonda precedente, ed è l'esito da leggere.
   */
  struct Variante
  {
    const uint8_t* lut;
    const char*    frase;     // sul vetro e sul seriale, la stessa stringa
    uint8_t        sequence;
    uint8_t        banda;     // 1..N: la fascia su cui la frase viene scritta
  };
  /**
   * Le due diagonali della matrice, e solo quelle. Le celle incrociate — LUT
   * del 1160 in Mode 1 e LUT riassegnata in Mode 2 — sono MUTE PER
   * COSTRUZIONE: in Mode 1 la fascia nera cade su LUT0, che nella LUT del 1160
   * è a zero, e in Mode 2 cade su LUT2, che nella riassegnata è a zero. Non
   * potendo dipingere servivano da controllo del modello di lettura delle due
   * RAM, ma quel controllo lo dà già quale delle due diagonali dipinge.
   * La y della fascia la ricava stagePartialFrame dall'indice: BAND_H * (b-1).
   */
  static const char* const frasiVarianti[VARIANTI_LUT] =
  {
    "PARTIAL 1 LUT 1160 MODE 2 0X22=CC",
    "PARTIAL 2 LUT TABLE 6-4 MODE 1 0X22=C4",
  };
  static const Variante varianti[VARIANTI_LUT] =
  {
    { LUT_PARTIAL_1160, frasiVarianti[0], 0xCC, 1 },
    { LUT_PARTIAL_T64,  frasiVarianti[1], 0xC4, 2 },
  };

  for (int v = 0; v < VARIANTI_LUT; ++v)
  {
    const Variante& t = varianti[v];
    const uint16_t y = (uint16_t)(BAND_H * (t.banda - 1));
    Serial.printf("\n-- fascia y=%u..%u: %s\n",
                  (unsigned)y, (unsigned)(y + BAND_H - 1), t.frase);

    /**
     * Reset più init a ogni variante: il SWRESET rimette in RAM le LUT
     * dell'OTP, quindi ognuna parte dallo stesso stato e la LUT custom
     * dell'iterazione precedente non sopravvive per sbaglio.
     */
    resetPanel();
    initPanel();
    loadWaveformLut(t.lut, 0xC0);
    stagePartialFrame(frasiVarianti, t.banda, BAND_H, (t.sequence & 0x08) != 0);

    /**
     * Power on esplicito prima della passata. Le sequenze senza bit 1 e bit 0
     * non spengono nulla alla fine, ma non è detto che accendano: il
     * GDEH116T91 chiama _PowerOn() prima di ogni _Update_Part() e qui si fa
     * lo stesso, altrimenti un esito negativo non distinguerebbe "waveform
     * inefficace" da "alte tensioni mai salite".
     */
    writeCommand(0x22);
    writeData(0xC0);
    writeCommand(0x20);
    waitBusy(2000);

    lutPartialMs[v] = runRefresh(t.sequence, t.frase);
  }

  Serial.println(F("\nesito della sonda del partial con LUT custom:"));
  for (int v = 0; v < VARIANTI_LUT; ++v)
  {
    const Variante& t = varianti[v];
    if (lutPartialMs[v] == REFRESH_NOT_RUN)
      Serial.printf("  non eseguita               %s\n", t.frase);
    else if (lutPartialMs[v] < 0)
      Serial.printf("  BUSY mai salito o timeout  %s\n", t.frase);
    else
      Serial.printf("  %5ld ms                  %s\n", (long)lutPartialMs[v], t.frase);
  }
  if (fullMs > 0)
    Serial.printf("  riferimento: refresh pieno 0xF7 %ld ms\n", (long)fullMs);

  int veloci = 0;
  for (int v = 0; v < VARIANTI_LUT; ++v)
    if (lutPartialMs[v] >= 0 && lutPartialMs[v] < 3000)
      ++veloci;

  inizioOsservazione("PARTIAL CON LUT CUSTOM: quale fascia è nera");
  static const char* const OSS_LUT[] =
  {
    "fascia 1, y=0..167    PARTIAL 1 LUT 1160 MODE 2 0X22=CC",
    "fascia 2, y=168..335  PARTIAL 2 LUT TABLE 6-4 MODE 1 0X22=C4",
    "",
    "DOMANDA: quale delle due fasce è diventata nera?",
    "  una fascia NERA, con la durata sotto i 3 s che il riepilogo ha",
    "    stampato -> IL PARTIAL ESISTE, e va nel driver come _Init_Part",
    "    più _Update_Part",
    "  NESSUNA nera con durate sotto i 3 s -> la LUT viene accettata ma non",
    "    pilota: la waveform va aggiustata, la strada è giusta",
    "  NESSUNA nera con durate da 24 s -> il silicio ricarica l'OTP anche col",
    "    bit 4 spento, e hasFastPartialUpdate = false resta la scelta giusta",
    "",
    "guarda anche la CORNICE: con 0x3C=0xC0 non deve lampeggiare",
    "il riquadro col numero può uscire incompleto: la LUT custom non pilota",
    "tutte le LUT, e quello che non pilota resta come stava",
  };
  rigaOsservazioneF("passate sotto i 3 s: %d", veloci);
  righeOsservazione(OSS_LUT, sizeof(OSS_LUT) / sizeof(OSS_LUT[0]));
  fineOsservazione();
  chiudiSchermata();

  /**
   * I registri restano quelli dell'ultima variante, con una LUT custom in RAM:
   * un reset li rimette a posto per le sonde che seguono, che misurano la
   * waveform dell'OTP e sarebbero falsate.
   */
  resetPanel();
  initPanel();
  Serial.println(F("  reset hardware finale: le LUT tornano quelle dell'OTP"));
}

/**
 * Probe del quarto colore per livello di sorgente: l'ultimo test possibile, e
 * il solo ancora sensato dopo le bande.
 *
 * Le quattro combinazioni dei due piani sono già state viste tutte, e (1,0) e
 * (1,1) sono uscite entrambe rosse: sotto la waveform dell'OTP LUT2 e LUT3
 * rendono lo stesso colore, come la Table 6-4 dichiara. Ma quella tabella dice
 * che le due LUT sono aliasate dalla waveform, non che il film abbia tre soli
 * pigmenti: se le due LUT venissero pilotate a tensioni diverse, un eventuale
 * quarto pigmento si separerebbe.
 *
 * Qui LUT2 va a VSH1 e LUT3 a VSH2, stessi tempi, e le due bande in basso
 * portano la stessa coppia di bit che le distingueva prima:
 *
 *   banda 3  (accent=1, BW=1)  ->  LUT3  ->  VSH2
 *   banda 4  (accent=1, BW=0)  ->  LUT2  ->  VSH1
 *
 * Mode 1 (0x22 = 0xC4) e non Mode 2, perchè è Mode 1 che legge le due RAM
 * secondo la Table 6-4; in Mode 2 sarebbero (precedente, nuovo) e la coppia di
 * bit non selezionerebbe più la LUT che ci interessa.
 *
 * Esito:
 *   bande di colore DIVERSO  -> il film separa le due tensioni: esiste un
 *                               quarto stato, e il driver può renderlo con la
 *                               coppia di bit che già ha, più una LUT custom;
 *   bande IDENTICHE          -> tre pigmenti, ed è la prova diretta che
 *                               finora mancava: la questione si chiude;
 *   bande entrambe BIANCHE   -> 200 frame non bastano a muovere il pigmento,
 *                               o la LUT custom non viene applicata (guarda
 *                               l'esito della sonda precedente): inconcludente.
 */
static void probeFourthColorLevels()
{
  Serial.println(F("\n=== probe del quarto colore: LUT2 a VSH1 contro LUT3 a VSH2 ==="));
  Serial.println(F("le tensioni restano quelle dell'OTP: 0x32 scrive i byte 0..104 della"));
  Serial.println(F("waveform, mentre VSH1 e VSH2 stanno ai byte 106 e 107 e arrivano da"));
  Serial.println(F("0x04, che questa sonda non tocca. Cambia solo QUALE delle due tensioni"));
  Serial.println(F("già presenti nel silicio viene applicata a ciascuna LUT."));

  resetPanel();
  initPanel();

  /**
   * Schermata aperta sul fondo bianco e non sulla passata dei livelli: LUT0 e
   * LUT1 sono a zero in LUT_LEVELS, quindi lì un riquadro nero su bianco non
   * verrebbe pilotato. Disegnato adesso, con la waveform dell'OTP, resta sul
   * vetro perchè la passata dei livelli non tocca i pixel bianchi.
   */
  apriSchermata(SCH_LIVELLI);

  // Fondo bianco su entrambe le metà, così le due bande partono uguali.
  fillByPattern(0x47, 0xFF);
  fillByPattern(0x46, 0x00);
  runRefresh(0xF7, "FONDO BIANCO PRIMA DEL PROBE DEI LIVELLI");

  resetPanel();
  initPanel();
  loadWaveformLut(LUT_LEVELS, 0x01);

  /**
   * Accent acceso su tutta la metà bassa dello schermo, e il piano BW che
   * distingue le due bande: y=336..503 con BW=1 seleziona LUT3, y=504..671 con
   * BW=0 seleziona LUT2. Le due metà alte restano bianche e non vengono
   * pilotate, perchè LUT1 in questa waveform è a zero.
   */
  fillByPattern(0x47, 0xFF);   // BW tutto a 1
  fillByPattern(0x46, 0x00);   // accent spento
  fillBand(0x26, BAND_H * 2, BAND_H * 2, 0xFF);          // accent su bande 3 e 4
  fillBand(0x24, BAND_H * 3, BAND_H, 0x00);              // banda 4: BW=0 -> LUT2

  // finestra piena: le due bande devono essere ridipinte entrambe
  setRamWindow(0, 0, SRC, GATE);

  writeCommand(0x22);
  writeData(0xC0);
  writeCommand(0x20);
  waitBusy(2000);

  /**
   * Nessuna frase sulle due fasce, e non è una dimenticanza: qui l'accent è
   * acceso su entrambe e LUT2 = LUT3, quindi un testo scritto nel piano BW
   * sarebbe invisibile per costruzione. Le due fasce si distinguono per
   * posizione, come dice il testo qui sotto.
   */
  const int32_t ms = runRefresh(0xC4, "PROBE LIVELLI LUT2 VSH1 CONTRO LUT3 VSH2 0X22=C4");
  if (ms >= 0)
    Serial.printf("  BUSY %ld ms (200 frame attesi, la LUT non ripete il gruppo)\n",
                  (long)ms);

  inizioOsservazione("PROBE DEI LIVELLI: le due bande in basso");
  static const char* const OSS_LIVELLI[] =
  {
    "banda 3, sopra la linea y=504: accent=1 BW=1 -> LUT3 -> VSH2",
    "banda 4, sotto la linea y=504: accent=1 BW=0 -> LUT2 -> VSH1",
    "",
    "DOMANDA: le due bande hanno colore diverso?",
    "  DIVERSO  -> il film separa VSH1 da VSH2: esiste un quarto stato, e il",
    "    driver può arrivarci con la coppia di bit che ha già più una LUT",
    "    custom",
    "  IDENTICO -> tre pigmenti: è la prova diretta che mancava, la questione",
    "    del quarto colore si chiude qui",
    "  ENTRAMBE BIANCHE -> 200 frame non bastano, o la LUT custom non viene",
    "    applicata: se anche la sonda precedente non ha dipinto è",
    "    inconcludente",
    "",
    "il riquadro col numero è stato dipinto dal fondo bianco: la passata dei",
    "livelli non tocca i pixel bianchi e neri, quindi lì resta invariato",
  };
  righeOsservazione(OSS_LIVELLI, sizeof(OSS_LIVELLI) / sizeof(OSS_LIVELLI[0]));
  fineOsservazione();
  chiudiSchermata();

  resetPanel();
  initPanel();
  Serial.println(F("  reset hardware finale: le LUT tornano quelle dell'OTP"));
}

/**
 * Sonda del refresh differenziale: la strada per un aggiornamento da ~600 ms
 * invece dei 22 s del refresh pieno.
 *
 * Da dove viene l'ipotesi. Il fratello monocromatico dello stesso silicio,
 * GxEPD2_1330_GDEM133T91 (13.3", 960x680, SSD1677), dichiara
 * partial_refresh_time = 600 ms e lo ottiene così: scrive il frame precedente
 * nella RAM 0x26 e quello corrente nella 0x24, poi lancia 0x22=0xFC invece di
 * 0xF7. Su questo pannello 0x26 è l'accent, quindi accent e partial non
 * possono coesistere; ma se il banco di waveform differenziale esiste in OTP,
 * un frame senza accent può usarlo.
 *
 * Decodifica di 0x22 dalla tabella del datasheet: bit7 enable clock, bit6
 * enable analog, bit5 load temperature, bit4 load LUT, bit3 seleziona DISPLAY
 * Mode 2, bit2 display, bit1 disable analog, bit0 disable OSC. Quindi
 * 0xF7 = Mode 1 con power down finale, 0xFF = Mode 2 con power down,
 * 0xFC = Mode 2 lasciando clock e analogico accesi, che è il modo in cui una
 * catena di partial resta veloce.
 *
 * Perchè serve una baseline tutta nera: il differenziale calcola la
 * transizione fra 0x26 e 0x24, quindi 0x26 deve contenere quello che il
 * pannello sta davvero mostrando. Dopo le bande non lo contiene, e l'unico
 * stato coerente ottenibile al prezzo di un solo refresh è "tutto nero" su
 * entrambi i piani. Da lì le due passate fanno due inversioni piene, il caso
 * peggiore per una waveform differenziale: se le regge in 600 ms il partial su
 * questo pannello è reale.
 *
 * Riempie diffMs1 e diffMs2 e ritorna i ms della prima passata, -1 se non si
 * è conclusa.
 */
static int32_t probeDifferentialRefresh(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del refresh differenziale, candidato ~600 ms ==="));

  /**
   * Baseline coerente: entrambi i piani a 0, cioè BW nero e accent spento, col
   * pattern hardware. Dopo il refresh il pannello è nero e la RAM 0x26 vale 0
   * ovunque, che come frame precedente si legge "tutto nero": coerente con
   * quello che si vede.
   */
  Serial.println(F("baseline: entrambi i piani a nero col pattern, poi refresh pieno Mode 1"));
  fillByPattern(0x47, 0x00);   // 0x24 tutto nero
  fillByPattern(0x46, 0x00);   // 0x26 accent spento, cioè frame precedente nero
  if (runRefresh(0xF7, "BASELINE NERA DELLA SONDA DIFFERENZIALE") < 0)
  {
    Serial.println(F("baseline non riuscita: sonda differenziale abbandonata"));
    return -1;
  }

  /**
   * Prima passata: tutto bianco con la propria frase in nero, scritta SOLO su
   * 0x24. 0x26 resta la baseline nera, cioè il frame precedente.
   */
  static const char FRASE_DIFF1[] = "DIFFERENZIALE 1 DA NERO A BIANCO 0X22=FC";
  static const char FRASE_DIFF2[] = "DIFFERENZIALE 2 DA BIANCO A NERO 0X22=FC";
  // la baseline resta senza numero: la schermata da guardare è quella che le
  // due passate lasciano, non il nero da cui partono
  apriSchermata(SCH_DIFF);
  fillBand(0x24, 0, GATE, 0xFF, FRASE_DIFF1, 0x00);
  diffMs1 = runRefresh(0xFC, FRASE_DIFF1, 30000);

  if (diffMs1 > 0)
  {
    /**
     * Seconda passata: prima si allinea il frame precedente scrivendo su 0x26
     * lo stesso contenuto appena mostrato, come fa writeImageAgain del driver
     * monocromatico, poi si scrive su 0x24 l'inversione con la propria frase.
     * Una catena di due passate distingue un colpo di fortuna da un meccanismo.
     */
    fillBand(0x26, 0, GATE, 0xFF, FRASE_DIFF1, 0x00);   // precedente = ciò che si vede
    fillBand(0x24, 0, GATE, 0x00, FRASE_DIFF2, 0xFF);   // corrente = inversione
    diffMs2 = runRefresh(0xFC, FRASE_DIFF2, 30000);
  }

  // 0xFC lascia clock e analogico accesi: si spengono come fa _PowerOff del driver
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  waitBusy(2000);

  Serial.println(F("\nesito della sonda differenziale:"));
  if (diffMs1 < 0)
    Serial.println(F("  la passata non si è conclusa: 0xFC non è praticabile su questo pannello"));
  else
  {
    Serial.printf("  passata 1  %ld ms\n", (long)diffMs1);
    if (diffMs2 > 0)
      Serial.printf("  passata 2  %ld ms\n", (long)diffMs2);
    if (fullMs > 0)
      Serial.printf("  riferimenti: %ld ms il refresh pieno qui, 600 ms il partial del\n"
                    "               GDEM133T91 monocromatico sullo stesso silicio\n",
                    (long)fullMs);
    if (diffMs1 < 3000)
      Serial.println(F("  durata da partial: se il pannello mostra davvero il frame nuovo,\n"
                       "  il banco differenziale esiste in OTP"));
    else if (fullMs > 0 && diffMs1 > fullMs - 3000)
      Serial.println(F("  durata da refresh pieno: 0xFC ricade sulla waveform completa"));
    else
      Serial.println(F("  durata intermedia: annotarla, non è nessuno dei due casi noti"));
  }

  inizioOsservazione("SONDA DIFFERENZIALE: quale frase è rimasta sul vetro");
  static const char* const OSS_DIFF[] =
  {
    "le due passate scrivono, in ordine, DIFFERENZIALE 1 DA NERO A BIANCO e",
    "DIFFERENZIALE 2 DA BIANCO A NERO",
    "",
    "DOMANDA: sul vetro c'è la frase della SECONDA passata?",
    "  sì, con durate sotto i 3 s -> il banco differenziale esiste in OTP",
    "  sì, con durate da 24 s     -> 0xFC ricade sulla waveform piena",
    "  no, schermo nero o frase della prima -> la seconda non ha dipinto, e",
    "    una durata breve lì è un refresh ingoiato, non un partial",
  };
  righeOsservazione(OSS_DIFF, sizeof(OSS_DIFF) / sizeof(OSS_DIFF[0]));
  fineOsservazione();
  chiudiSchermata();

  return diffMs1;
}

/**
 * Init di fabbrica SOLUM per la 9.7", riprodotta verbatim da
 * docs/openepaperlink/oepl_display_driver_unissd.c, ramo
 * "Custom init for 9.7" Solum SSD" (960 x 672), righe 215-235.
 *
 * Serve perchè l'init dell'altra funzione, initPanel(), è quella del driver
 * custom, e il driver custom NON è una fonte di verità: è l'oggetto che questa
 * sonda deve permettere di correggere. Se una misura di questa sonda dipende
 * dall'init, misurarla con un solo init non dice niente. Da qui la variante.
 *
 * Le differenze rispetto all'init del driver custom non sono cosmetiche:
 *
 *   - NESSUNO SWRESET (0x12). Il firmware di fabbrica fa il reset hardware e
 *     passa direttamente ai pattern. Il driver custom manda 0x12 e poi aspetta
 *     200 ms.
 *   - riempie SUBITO entrambi i piani col pattern a 0xF7, con 15 ms di attesa
 *     ciascuno invece di guardare il BUSY.
 *   - entry mode 0x11 = 0x02, cioè X DECRESCENTE, con finestra X 959 -> 0. Il
 *     driver custom usa 0x03 con X 0 -> 959.
 *   - manda 0x21 = {0x08, 0x00}, cioè Red normale + BW INVERSE, che il driver
 *     custom non manda mai: resta al POR e compensa invertendo il piano rosso
 *     in software. Le due convenzioni sono equivalenti sull'immagine, ma non
 *     è detto che lo siano per il motore di update.
 *   - manda 0x22 = {0xF7} SENZA la master activation 0x20 subito dopo. Qui è
 *     riprodotto com'è, perchè lo scopo è replicare, non migliorare.
 *
 * Non tocca la finestra col nostro setRamWindow: chi la usa dopo la
 * reimposterebbe comunque, e le prove che seguono usano il generatore di
 * pattern, che la finestra la ignora.
 */
static void initPanelFactory()
{
  Serial.println(F("  init di fabbrica SOLUM/OEPL: nessuno SWRESET, entry mode 0x02,"));
  Serial.println(F("  finestra X 959->0, 0x21 = 08 00 (BW inverse)"));
  resetPanel();
  delay(10);
  writeCommand(0x46);   // pattern sul piano accent
  writeData(0xF7);
  delay(15);
  writeCommand(0x47);   // pattern sul piano BW
  writeData(0xF7);
  delay(15);
  writeCommand(0x0C);   // soft start, identico al driver custom
  writeData(0xAE);
  writeData(0xC7);
  writeData(0xC3);
  writeData(0xC0);
  writeData(0x80);
  writeCommand(0x01);   // MUX 671
  writeData(0x9F);
  writeData(0x02);
  writeData(0x00);
  writeCommand(0x11);   // entry mode: X decrescente
  writeData(0x02);
  writeCommand(0x44);   // finestra X 959 -> 0
  writeData(0xBF);
  writeData(0x03);
  writeData(0x00);
  writeData(0x00);
  writeCommand(0x45);   // finestra Y 0 -> 671
  writeData(0x00);
  writeData(0x00);
  writeData(0x9F);
  writeData(0x02);
  writeCommand(0x3C);   // border waveform
  writeData(0x01);
  writeCommand(0x18);   // sensore di temperatura interno
  writeData(0x80);
  writeCommand(0x22);   // come da fabbrica: nessun 0x20 subito dopo
  writeData(0xF7);
  writeCommand(0x21);   // Red normale + BW inverse
  writeData(0x08);
  writeData(0x00);
}

/**
 * Esito di una passata della sonda differenziale approfondita: il confronto
 * fra passate vale più del valore singolo, quindi si tabellano tutte.
 */
struct DiffPass
{
  const char* frase;      // la stessa stringa che la passata ha scritto sul vetro
  uint8_t     sequence;   // parametro di 0x22
  int32_t     ms;         // BUSY in ms, negativo se non eseguita
  bool        paints;     // false per le sequenze che per costruzione non dipingono
};

static DiffPass diffPasses[16];
static int      diffPassCount = 0;

/**
 * Registra una passata per il riepilogo. La frase è quella scritta sul vetro,
 * quindi la tabella finale è già la legenda di quello che si vede sul pannello
 * e non serve stamparne una seconda. paints a false per una sequenza che per
 * costruzione non dipinge: la sua brevità è attesa e non va contata fra le
 * passate veloci.
 */
static void recordDiffPass(const char* frase, uint8_t sequence, int32_t ms,
                           bool paints = true)
{
  const int cap = (int)(sizeof(diffPasses) / sizeof(diffPasses[0]));
  if (diffPassCount >= cap)
    return;
  diffPasses[diffPassCount].frase    = frase;
  diffPasses[diffPassCount].sequence = sequence;
  diffPasses[diffPassCount].ms       = ms;
  diffPasses[diffPassCount].paints   = paints;
  ++diffPassCount;
}

/**
 * Sonda differenziale approfondita: tutto quello che resta da provare sul
 * partial refresh, dopo che la sonda base ha misurato 0xFC come una waveform
 * piena.
 *
 * Perchè serve. La sequenza della sonda base non è un'invenzione: il driver
 * GxEPD2_1330_GDEM133T91 (13.3", 960x680, lo STESSO SSD1677, monocromatico)
 * dichiara partial_refresh_time = 600 ms e lo ottiene con esattamente
 * _setPartialRamArea + 0x22=0xFC + 0x20 — nessun 0x21, nessun 0x37, nessun
 * 0x32. Il suo frame precedente sta nella RAM 0x26, che è quella che la sonda
 * base riempie, e la sua init condivide con la nostra soft start, MUX,
 * 0x3C=0x01 e 0x18=0x80. Cioè: la sequenza di update è stata replicata bene, e
 * ha dato 24 s.
 *
 * Quello che NON si può dare per buono è l'init. La nostra è quella del driver
 * custom, e il driver custom è l'oggetto da correggere, non un riferimento:
 * l'init di fabbrica SOLUM (docs/openepaperlink/oepl_display_driver_unissd.c)
 * differisce in quattro punti non cosmetici — nessuno SWRESET, entry mode
 * 0x02 con finestra X rovesciata, 0x21 = 08 00, e 0x22 senza 0x20 — e nessuno
 * di quei punti è mai stato messo alla prova sul motore di update. Per questo
 * fra le prove qui sotto ci sono anche il 0x21 di fabbrica e l'init di
 * fabbrica intera.
 *
 * Restano quattro cose non provate, in ordine di valore diagnostico.
 *
 * 1. DIFFERENZA ZERO. È il test minimo e nessuno l'ha fatto. Un motore
 *    differenziale confronta 0x24 con 0x26: se i due piani sono identici non
 *    c'è nessun pixel da muovere, e la passata deve durare molto meno di una
 *    con differenza massima. Se invece dura uguale, il controller non sta
 *    confrontando niente e il differenziale non esiste: è la prova negativa
 *    più forte ottenibile senza read-back.
 *
 * 2. LE SEQUENZE DI 0x22 SENZA RICARICA. 0xFC e 0xFF hanno i bit 5 e 4 alti,
 *    cioè ricaricano temperatura e LUT a ogni passata. 0xCF e 0xC7 sono le
 *    stesse passate di display senza quella ricarica (datasheet: "Enable clock
 *    + Enable Analog + Display Mode 2/1 + Disable Analog + Disable OSC"). Se i
 *    24 s fossero il caricamento della LUT invece della waveform, qui si
 *    vedrebbe. Serve anche a chiarire un'anomalia del run precedente, dove una
 *    passata 0xFF si è chiusa in 224 ms di BUSY, ma in circostanze sporche.
 *
 * 3. 0x21, l'opzione del contenuto RAM. Il driver non lo scrive mai e resta al
 *    POR. Con A[7:4] = 0100, "Bypass RED RAM content as 0", la RAM 0x26 non
 *    partecipa più: se la durata di 0xFC cambia, quella RAM entra davvero nel
 *    conto differenziale; se non cambia, non ci entra affatto.
 *
 * 4. 0x37 F[6], il RAM ping-pong. È l'unico interruttore che il datasheet lega
 *    esplicitamente al differenziale ("RAM ping-pong function is not support
 *    for Display Mode 1"). ATTENZIONE, ed è scritto anche nel log: 0x37 sono
 *    10 byte che normalmente arrivano dall'OTP, e fra questi ci sono i bit di
 *    Display Mode per ognuno dei 36 stadi della waveform. Senza read-back non
 *    sappiamo cosa contengano, quindi scriverlo è alla cieca e può produrre
 *    una waveform rotta. La prova è asimmetrica: se dopo 0x37 una passata
 *    scende sotto il secondo abbiamo trovato l'interruttore, se resta a 24 s
 *    non abbiamo imparato niente, perchè non sappiamo se i valori scritti
 *    avessero senso. Per questo sta per ultima e si chiude con un reset
 *    hardware, che rimette l'OTP.
 */
static void probeDifferentialDeeper(int32_t fullMs)
{
  Serial.println(F("\n=== sonda differenziale approfondita: gli interruttori non provati ==="));
  Serial.println(F("la sequenza 0xFC della sonda base è quella del driver monocromatico di"));
  Serial.println(F("riferimento sullo stesso silicio: qui si provano gli interruttori"));

  /**
   * 1a. Differenza zero. I due piani identici bit per bit, frase compresa: come
   * coppia differenziale è "precedente identico al corrente", cioè nessun pixel
   * da muovere. Le fasce si scrivono col percorso a righe invece del pattern
   * hardware perchè così la passata lascia la propria frase sul vetro, e 130 ms
   * di push su un refresh da 24 s non cambiano la misura. Il colore che esce non
   * interessa: qui si misura solo la durata.
   */
  static const char FRASE_ZERO[] = "DIFFERENZA ZERO 0X24 E 0X26 IDENTICI 0X22=FC";
  fillBand(0x24, 0, GATE, 0xFF, FRASE_ZERO, 0x00);
  fillBand(0x26, 0, GATE, 0xFF, FRASE_ZERO, 0x00);   // identica a 0x24, bit per bit
  const int32_t msZero = runRefresh(0xFC, FRASE_ZERO);
  recordDiffPass(FRASE_ZERO, 0xFC, msZero);

  /**
   * 1b. Differenza massima subito dopo, stessa sessione: 0x24 è l'esatto inverso
   * di 0x26 su ogni pixel, frase compresa, quindi la differenza è totale e non
   * quasi totale. Il confronto fra 1a e 1b è il cuore della sonda, e va fatto
   * vicino nel tempo perchè la waveform è compensata in temperatura.
   */
  static const char FRASE_MAX[] = "DIFFERENZA MASSIMA 0X24 INVERSO DI 0X26 0X22=FC";
  fillBand(0x26, 0, GATE, 0xFF, FRASE_MAX, 0x00);
  fillBand(0x24, 0, GATE, 0x00, FRASE_MAX, 0xFF);   // l'esatto inverso di 0x26
  const int32_t msMax = runRefresh(0xFC, FRASE_MAX);
  recordDiffPass(FRASE_MAX, 0xFC, msMax);

  /**
   * Da qui in avanti l'accent si spegne. I due test di differenza sopra hanno
   * bisogno di 0x26 = 0xFF per costruire la condizione "precedente identico al
   * corrente", ma con l'accent acceso su tutto lo schermo ogni pixel è (1,1) o
   * (0,1) e le passate successive sarebbero invisibili. Con 0x26 a 0 il
   * pannello torna in bianco e nero, e ogni passata qui sotto scrive la propria
   * frase: così una passata veloce si riconosce anche a occhio, leggendo quale
   * frase è rimasta sul vetro, e non solo dal cronometro.
   */
  Serial.println(F("\naccent spento: da qui ogni passata scrive la propria frase, e quella"));
  Serial.println(F("che resta sul vetro dice chi ha davvero dipinto"));
  fillByPattern(0x46, 0x00);

  /**
   * 2. Il carico della LUT, misurato per primo perchè è il TETTO al guadagno
   * che le passate di solo display possono cercare.
   *
   * 0x99 è "Enable clock + Load LUT with DISPLAY Mode 2 + Disable clock": non
   * dipinge, carica. Le passate 2a e seguenti dipingono SENZA ricaricare la
   * LUT, quindi al massimo risparmiano quello che il carico costa: se 0x99
   * chiude in un secondo, non c'è niente da risparmiare e quelle passate sono
   * tre refresh da venticinque secondi che non possono dire niente. Costa un
   * secondo saperlo, e le rende condizionali.
   */
  static const char FRASE_LOADLUT[] = "CARICO LUT MODE 2 0X22=99 NON DIPINGE";
  fillBand(0x24, 0, GATE, 0xFF, FRASE_LOADLUT, 0x00);
  const int32_t msLoadLut = runRefresh(0x99, FRASE_LOADLUT, 10000);
  recordDiffPass(FRASE_LOADLUT, 0x99, msLoadLut, false);

  /**
   * 2a-2c. Solo display, e solo se il carico pesa. La LUT in uso è quella che
   * 0x99 ha appena caricato, quindi qui si misura la sola waveform: 0xCF è
   * Mode 2, 0xC7 il controllo su Mode 1, 0xFF Mode 2 completo col power down.
   */
  if (cfg.esaustivo || msLoadLut > 3000)
  {
    /**
     * Ogni passata scrive la PROPRIA frase, anche la 2a: scrivere la RAM non
     * tocca la LUT che 0x99 ha caricato, quindi il confronto resta quello
     * voluto, e sul vetro non resta la frase di 0x99, che per costruzione non
     * dipinge e sarebbe una falsa attribuzione.
     */
    static const char FRASE_CF[] = "DISPLAY DOPO CARICO LUT 0X22=CF";
    fillBand(0x24, 0, GATE, 0xFF, FRASE_CF, 0x00);   // bianco con frase nera
    const int32_t msCF = runRefresh(0xCF, FRASE_CF);
    recordDiffPass(FRASE_CF, 0xCF, msCF);

    static const char FRASE_C7[] = "DISPLAY MODE 1 SENZA RICARICA 0X22=C7";
    fillBand(0x24, 0, GATE, 0x00, FRASE_C7, 0xFF);   // nero con frase bianca
    const int32_t msC7 = runRefresh(0xC7, FRASE_C7);
    recordDiffPass(FRASE_C7, 0xC7, msC7);

    static const char FRASE_FF[] = "MODE 2 COMPLETO CON POWER DOWN 0X22=FF";
    fillBand(0x24, 0, GATE, 0xFF, FRASE_FF, 0x00);   // bianco con frase nera
    const int32_t msFF = runRefresh(0xFF, FRASE_FF);
    recordDiffPass(FRASE_FF, 0xFF, msFF);
  }
  else
  {
    Serial.printf("\n-- 2a-2c saltate: il carico della LUT costa %ld ms, quindi\n"
                  "   dipingere senza ricaricarla non può far risparmiare di più.\n"
                  "   Sono tre refresh da ~25 s che non avrebbero niente da trovare.\n",
                  (long)msLoadLut);
  }

  /**
   * 3. 0x21 con la RAM rosso bypassata a 0: se 0x26 partecipa al conto
   * differenziale la durata deve cambiare rispetto alla 1b. Due byte come
   * l'init di fabbrica, anche se la Rev 1.0 del datasheet definisce 0x21 con
   * un solo parametro.
   */
  /**
   * Le passate 3, 3b e 4 rifanno lo stesso confronto zero/massima in tre
   * condizioni di registro diverse: 0x26 bypassata, 0x21 come lo scrive il
   * firmware di fabbrica, e l'init di fabbrica intera. Sono cinque refresh, e
   * hanno senso solo se il confronto base ha lasciato una domanda aperta: se
   * differenza zero e differenza massima coincidono, il controller non guarda
   * il contenuto dei piani, e nessun valore di 0x21 nè nessun init possono
   * fargli guardare quello che non guarda. In quel caso si saltano tutte e tre.
   */
  const bool confrontoAmbiguo = (msZero <= 0) || (msMax <= 0) ||
                                (labs((long)(msMax - msZero)) >= 2000);

  /**
   * Dichiarate qui e non dentro il blocco: il riepilogo le legge, e a -1 salta
   * da sè la riga corrispondente, che è quello che serve quando la passata non
   * è stata eseguita.
   */
  int32_t msBypass  = -1;
  int32_t ms21Zero  = -1;
  int32_t ms21Max   = -1;
  int32_t msFacZero = -1;
  int32_t msFacMax  = -1;
  if (!confrontoAmbiguo && !cfg.esaustivo)
  {
    Serial.println(F("\n-- 3, 3b e 4 saltate: differenza zero e differenza massima"));
    Serial.println(F("   coincidono, quindi il controller non confronta i due piani."));
    Serial.println(F("   Rifare la stessa misura con 0x26 bypassata, col 0x21 di fabbrica"));
    Serial.println(F("   o con l'init di fabbrica non può cambiare un esito che non"));
    Serial.println(F("   dipende dal contenuto delle RAM: sono cinque refresh risparmiati."));
  }

  if (confrontoAmbiguo || cfg.esaustivo)
  {
  static const char FRASE_BYPASS[] = "RAM ROSSO BYPASSATA 0X21=40 0X22=FC";
  writeCommand(0x21);
  writeData(0x40);
  writeData(0x00);
  fillBand(0x24, 0, GATE, 0x00, FRASE_BYPASS, 0xFF);   // nero con frase bianca
  msBypass = runRefresh(0xFC, FRASE_BYPASS);
  recordDiffPass(FRASE_BYPASS, 0xFC, msBypass);
  // ripristino: entrambe le RAM in modalità normale
  writeCommand(0x21);
  writeData(0x00);
  writeData(0x00);

  /**
   * 3b. 0x21 come lo scrive il firmware di fabbrica: {0x08, 0x00}, Red normale
   * + BW inverse. Il driver custom non lo manda mai e resta al POR, e questa
   * differenza non è mai stata messa alla prova sul motore di update. Si
   * ripete il confronto differenza zero / differenza massima, perchè è quello
   * che risponde, non il valore singolo.
   */
  static const char FRASE_21_ZERO[] = "0X21 DI FABBRICA 08 DIFFERENZA ZERO 0X22=FC";
  static const char FRASE_21_MAX[]  = "0X21 DI FABBRICA 08 DIFFERENZA MASSIMA 0X22=FC";
  writeCommand(0x21);
  writeData(0x08);
  writeData(0x00);
  fillBand(0x24, 0, GATE, 0xFF, FRASE_21_ZERO, 0x00);
  fillBand(0x26, 0, GATE, 0xFF, FRASE_21_ZERO, 0x00);
  ms21Zero = runRefresh(0xFC, FRASE_21_ZERO);
  recordDiffPass(FRASE_21_ZERO, 0xFC, ms21Zero);
  fillBand(0x26, 0, GATE, 0xFF, FRASE_21_MAX, 0x00);
  fillBand(0x24, 0, GATE, 0x00, FRASE_21_MAX, 0xFF);
  ms21Max = runRefresh(0xFC, FRASE_21_MAX);
  recordDiffPass(FRASE_21_MAX, 0xFC, ms21Max);
  writeCommand(0x21);
  writeData(0x00);
  writeData(0x00);

  /**
   * 4. L'INIT INTERA di fabbrica. È il test che mette in dubbio la premessa di
   * tutta la sonda: initPanel() replica l'init del driver custom, e il driver
   * custom è l'oggetto da correggere, non la misura di riferimento. Se il
   * differenziale dipendesse da un dettaglio dell'init — l'assenza di SWRESET,
   * l'entry mode, 0x21 — con un solo init non lo vedremmo mai. Dopo l'init di
   * fabbrica si ripete la stessa coppia zero/massima, così il confronto è fra
   * quantità omogenee.
   */
  static const char FRASE_FAC_ZERO[] = "INIT DI FABBRICA DIFFERENZA ZERO 0X22=FC";
  static const char FRASE_FAC_MAX[]  = "INIT DI FABBRICA DIFFERENZA MASSIMA 0X22=FC";
  initPanelFactory();
  fillBand(0x24, 0, GATE, 0xFF, FRASE_FAC_ZERO, 0x00);
  fillBand(0x26, 0, GATE, 0xFF, FRASE_FAC_ZERO, 0x00);
  msFacZero = runRefresh(0xFC, FRASE_FAC_ZERO);
  recordDiffPass(FRASE_FAC_ZERO, 0xFC, msFacZero);
  fillBand(0x26, 0, GATE, 0xFF, FRASE_FAC_MAX, 0x00);
  fillBand(0x24, 0, GATE, 0x00, FRASE_FAC_MAX, 0xFF);
  msFacMax = runRefresh(0xFC, FRASE_FAC_MAX);
  recordDiffPass(FRASE_FAC_MAX, 0xFC, msFacMax);
  }   // fine del blocco condizionale delle passate 3, 3b e 4

  /**
   * 5. 0x37 F[6], il RAM ping-pong. Tentativo alla cieca, dichiarato tale.
   * A..E a 0xFF mettono tutti gli stadi della waveform su Display Mode 2, F a
   * 0x40 accende il ping-pong, G..J restano a 0 (sono module ID e versione
   * della waveform, non pilotano la resa).
   */
  static const char FRASE_PINGPONG[] = "RAM PING PONG VIA 0X37 0X22=FC";
  Serial.println(F("\n0x37 sono 10 byte che di norma vengono dall'OTP e contengono i bit di"));
  Serial.println(F("Display Mode dei 36 stadi della waveform: senza read-back è un tentativo"));
  Serial.println(F("alla cieca, e solo un esito POSITIVO vale."));
  /**
   * Solo l'ultima passata porta il numero. Le due prima di essa confrontano
   * differenza zero e differenza massima, e per farlo i due piani devono essere
   * identici bit per bit: il riquadro li renderebbe diversi e falserebbe la
   * misura centrale della sonda.
   */
  apriSchermata(SCH_DIFF_DEEP);
  writeCommand(0x37);
  writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF); writeData(0xFF);
  writeData(0x40);                                  // F[6] = RAM ping-pong
  writeData(0x00); writeData(0x00); writeData(0x00); writeData(0x00);
  // se sul vetro resta la frase di una passata precedente, questa non ha dipinto
  fillBand(0x24, 0, GATE, 0xFF, FRASE_PINGPONG, 0x00);
  const int32_t msPingPong = runRefresh(0xFC, FRASE_PINGPONG);
  recordDiffPass(FRASE_PINGPONG, 0xFC, msPingPong);

  /**
   * Reset hardware: rimette i valori OTP di 0x37 e cancella qualunque
   * configurazione azzardata qui sopra. La sonda successiva riparte da una
   * baseline propria, quindi la RAM azzerata dal reset non le crea problemi.
   */
  Serial.println(F("\nreset hardware: rimetto i valori OTP dopo il tentativo su 0x37"));
  resetPanel();
  initPanel();

  // ------------------------------------------------------------ riepilogo
  /**
   * La tabella usa come etichetta la frase scritta sul vetro, quindi è già la
   * legenda di quello che si vede sul pannello: non ne serve una seconda.
   */
  Serial.println(F("\nesito della sonda differenziale approfondita:"));
  for (int i = 0; i < diffPassCount; ++i)
  {
    if (diffPasses[i].ms < 0)
      Serial.printf("  non eseguita  %s\n", diffPasses[i].frase);
    else
      Serial.printf("  %6ld ms      %s\n", (long)diffPasses[i].ms, diffPasses[i].frase);
  }
  if (fullMs > 0)
    Serial.printf("  riferimento: refresh pieno 0xF7 %ld ms\n", (long)fullMs);

  /**
   * La conclusione che questa sonda può tirare da sola, senza guardare il
   * pannello: se la durata dipende dal contenuto dei piani, il controller li
   * confronta.
   */
  if (msZero > 0 && msMax > 0)
  {
    const long delta = (long)msMax - (long)msZero;
    const bool depends = (delta > 2000) || (delta < -2000);
    Serial.printf("  differenza zero %ld ms contro differenza massima %ld ms: scarto %ld ms\n",
                  (long)msZero, (long)msMax, delta);
    if (depends)
      Serial.println(F("  la durata DIPENDE dal contenuto dei piani: il controller li confronta,\n"
                       "  quindi un meccanismo differenziale esiste e va perseguito"));
    else
      Serial.println(F("  la durata NON dipende dal contenuto dei piani: il controller non li\n"
                       "  confronta, quindi su questo pannello il refresh differenziale non\n"
                       "  esiste, e hasFastPartialUpdate = false non è prudenza ma un fatto"));
  }

  if (msFacZero > 0 && msFacMax > 0)
  {
    const long deltaFac = (long)msFacMax - (long)msFacZero;
    Serial.printf("  con l'init di fabbrica: zero %ld ms, massima %ld ms, scarto %ld ms\n",
                  (long)msFacZero, (long)msFacMax, deltaFac);
    if ((deltaFac > 2000) || (deltaFac < -2000))
      Serial.println(F("  sotto l'init di fabbrica la durata dipende dai piani: l'init del\n"
                       "  driver custom è il problema, e va corretta prima di concludere"));
    else
      Serial.println(F("  nemmeno l'init di fabbrica cambia il quadro: l'esito non dipende\n"
                       "  dall'init, quindi non è un difetto del driver custom"));
  }

  /**
   * Passate veloci, e solo fra quelle che possono dipingere. 0x99 carica la LUT
   * e non dipinge: è breve per costruzione, e contarla fra le veloci mandava a
   * cercare sul vetro un frame che quella sequenza non ha mai disegnato.
   */
  int fast = 0;
  for (int i = 0; i < diffPassCount; ++i)
    if (diffPasses[i].paints && diffPasses[i].ms > 0 && diffPasses[i].ms < 3000)
      ++fast;
  for (int i = 0; i < diffPassCount; ++i)
    if (!diffPasses[i].paints && diffPasses[i].ms > 0)
      Serial.printf("  fuori dal conteggio, 0x%02X per costruzione non dipinge: %s\n",
                    diffPasses[i].sequence, diffPasses[i].frase);
  if (fast == 0)
    Serial.println(F("  nessuna passata che dipinge è scesa sotto i 3 s: nessuna combinazione\n"
                     "  di 0x22, 0x21 e 0x37 provata qui apre un partial su questo pannello"));

  inizioOsservazione("SONDA APPROFONDITA: quale frase è rimasta sul vetro");
  static const char* const OSS_DEEP[] =
  {
    "ogni passata scrive la propria frase, e la tabella qui sopra le elenca",
    "tutte con la loro durata",
    "",
    "DOMANDA: quale frase c'è sul vetro?",
    "  quella di una passata sotto i 3 s -> quella sequenza è il partial di",
    "    questo pannello",
    "  quella di una passata da 24 s -> le veloci non hanno dipinto, e la loro",
    "    durata è un refresh ingoiato",
    "",
    "il riquadro col numero lo porta solo l'ultima passata, RAM PING PONG: le",
    "altre confrontano i due piani e un riquadro li renderebbe diversi",
  };
  rigaOsservazioneF("passate che dipingono sotto i 3 s: %d", fast);
  righeOsservazione(OSS_DEEP, sizeof(OSS_DEEP) / sizeof(OSS_DEEP[0]));
  fineOsservazione();
  chiudiSchermata();
}

/**
 * Esito di una passata della sonda d'area, tenuto per il riepilogo finale:
 * il confronto fra passate serve più del valore singolo.
 */
struct AreaPass
{
  const char* frase;   // la stessa stringa che la passata scrive sulla fascia
  uint16_t x, y, w, h;
  uint32_t pushUs;     // microsecondi del solo transfer
  int32_t  ms;         // millisecondi di BUSY, -1 se non conclusa
};

static AreaPass areaPasses[6];
static uint8_t  areaPassCount = 0;

/**
 * Una passata della sonda d'area: scrive la finestra su 0x24, aggiorna con la
 * sequenza indicata e poi riallinea la stessa finestra su 0x26, perchè la
 * passata successiva trovi come frame precedente quello che il pannello sta
 * davvero mostrando. È lo stesso schema di writeImagePartToPrevious del
 * driver monocromatico GxEPD2_1330_GDEM133T91.
 *
 * La finestra viene reimpostata subito prima della master activation: è
 * quella presente in quel momento nei registri 0x44/0x45 a definire l'area
 * che il refresh percorre, ed è il punto in prova.
 *
 * La frase viene scritta sulla fascia solo a larghezza piena, perchè il
 * percorso a righe lavora così: sul riquadro ristretto in X la fascia esce
 * uniforme, ed è dichiarato nel log. Ritorna i ms del refresh, -1 se non si è
 * concluso.
 */
static int32_t areaPass(const char* frase, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint8_t value, uint8_t fg,
                        uint8_t updateSequence, uint32_t timeout_ms)
{
  const TextLayout lay = (w == SRC) ? layoutText(frase, h) : TextLayout();
  const bool withText = lay.scale > 0;
  const uint32_t bytes = (uint32_t)(w / 8) * h;

  Serial.printf("\n-- finestra x=%u..%u y=%u..%u, %u righe, %lu byte%s\n",
                (unsigned)x, (unsigned)(x + w - 1),
                (unsigned)y, (unsigned)(y + h - 1), (unsigned)h, (unsigned long)bytes,
                withText ? "" : ", fascia uniforme: la frase sta solo a larghezza piena");

  uint32_t pushUs;
  if (withText)
  {
    setRamWindow(x, y, w, h);
    writeCommand(0x24);
    pushUs = writeRowsWithText(value, h, fg, lay);
  }
  else
    pushUs = fillRect(0x24, x, y, w, h, value);
  Serial.printf("   0x24 scritto in %lu us (%.2f us/B)\n",
                (unsigned long)pushUs, (double)pushUs / (double)bytes);

  setRamWindow(x, y, w, h);
  const int32_t ms = runRefresh(updateSequence, frase, timeout_ms);

  // frame precedente allineato a quello che si vede adesso, nella stessa finestra
  if (withText)
  {
    setRamWindow(x, y, w, h);
    writeCommand(0x26);
    writeRowsWithText(value, h, fg, lay);
  }
  else
    fillRect(0x26, x, y, w, h, value);

  if (areaPassCount < (uint8_t)(sizeof(areaPasses) / sizeof(areaPasses[0])))
  {
    AreaPass& p = areaPasses[areaPassCount++];
    p.frase = frase;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.pushUs = pushUs;
    p.ms = ms;
  }
  return ms;
}

/**
 * Riepilogo della sonda d'area: la tabella delle passate e le letture che
 * contano, cioè se la durata scala con l'altezza della finestra e quanto
 * costerebbe un aggiornamento B/N fatto per finestre.
 */
static void reportAreaPasses(int32_t fullMs)
{
  // stesso costo per byte del riepilogo finale: righe da 120, il percorso _writeImage
  const double usbRow = rowUsPerByte > 0.0 ? rowUsPerByte : usPerByteReference();
  Serial.println(F("\nesito della sonda del partial d'area:"));
  if (areaPassCount == 0)
  {
    Serial.println(F("  nessuna passata eseguita"));
    return;
  }

  Serial.println(F("  finestra              righe  push us  refresh   frase sulla fascia"));
  for (uint8_t k = 0; k < areaPassCount; ++k)
  {
    const AreaPass& p = areaPasses[k];
    Serial.printf("  x=%3u..%3u y=%3u..%3u  %5u  %7lu  ",
                  (unsigned)p.x, (unsigned)(p.x + p.w - 1),
                  (unsigned)p.y, (unsigned)(p.y + p.h - 1), (unsigned)p.h,
                  (unsigned long)p.pushUs);
    if (p.ms < 0)
      Serial.printf("non concl  %s\n", p.frase);
    else
      Serial.printf("%6ld ms  %s\n", (long)p.ms, p.frase);
  }

  /**
   * Scala con l'altezza. Se la durata è proporzionale alle gate line, al
   * driver conviene passare la finestra minima; se è costante, il costo è
   * tutto della waveform e restringere la finestra fa risparmiare solo bus.
   */
  if (areaMsFirst > 0 && areaMsThin > 0)
  {
    const double ratioTempi = (double)areaMsFirst / (double)areaMsThin;
    const double ratioRighe = (double)AREA_P1_H / (double)AREA_THIN_H;
    Serial.printf("  %u righe %ld ms contro %u righe %ld ms: tempi 1:%.2f, righe 1:%.2f\n",
                  (unsigned)AREA_P1_H, (long)areaMsFirst,
                  (unsigned)AREA_THIN_H, (long)areaMsThin, ratioTempi, ratioRighe);
    if (ratioTempi > 1.5)
      Serial.println(F("  la durata scala con le gate line coinvolte: al driver conviene\n"
                       "  passare la finestra minima che contiene il disegno cambiato"));
    else
      Serial.println(F("  la durata non dipende dall'altezza: il costo è della waveform e\n"
                       "  restringere la finestra fa guadagnare solo sul push SPI"));
  }

  if (areaMsFirst > 0)
  {
    if (fullMs > 0)
      Serial.printf("  passata d'area %ld ms contro %ld ms del refresh pieno: %.1fx\n",
                    (long)areaMsFirst, (long)fullMs, (double)fullMs / (double)areaMsFirst);
    const double pushMs = usbRow * (double)ROW_BYTES * AREA_P1_H / 1000.0;
    Serial.printf("  ciclo su una fascia di %u righe: %.0f ms di push + %ld ms di refresh = %.2f s\n",
                  (unsigned)AREA_P1_H, pushMs, (long)areaMsFirst,
                  (pushMs + (double)areaMsFirst) / 1000.0);
    /**
     * La costante per il driver si propone solo se la passata d'area è davvero
     * più corta di un refresh pieno. Prima veniva stampata sempre, e su una
     * passata che costa come il pieno proponeva un partial_refresh_time per un
     * partial che non esiste.
     */
    if (fullMs > 0 && areaMsFirst * 2 < fullMs)
      Serial.printf("  partial_refresh_time da mettere nel driver: %ld ms con margine\n",
                    (long)(areaMsFirst * 2 + 200));
    else if (fullMs > 0)
      Serial.println(F("  la passata d'area costa come un refresh pieno: non c'è nessun\n"
                       "  partial_refresh_time da mettere nel driver"));
  }

  if (areaMsMode1 > 0)
  {
    Serial.printf("  Mode 1 su finestra (0x22=0xF4) %ld ms su %u righe",
                  (long)areaMsMode1, (unsigned)AREA_M1_H);
    if (fullMs > 0)
      Serial.printf(", contro %ld ms del pieno", (long)fullMs);
    Serial.println();
    if (fullMs > 0 && areaMsMode1 * 2 < fullMs)
      Serial.println(F("  Mode 1 si accorcia con la finestra: anche senza banco differenziale\n"
                       "  un refresh d'area costa meno di uno pieno, e non consuma 0x26,\n"
                       "  quindi resterebbe compatibile con l'accent"));
    else if (fullMs > 0)
      Serial.println(F("  Mode 1 dura come il pieno: la finestra non accorcia la sua waveform"));
  }

  Serial.println(F("  le passate 0xFC valgono per frame in bianco e nero: 0x26 lì fa da\n"
                   "  frame precedente, quindi in quella modalità l'accent non esiste"));
}

/**
 * Sonda del partial d'area: verifica se restringendo la finestra RAM il
 * pannello aggiorna davvero solo quella porzione, e a che prezzo in tempo.
 *
 * È la domanda che resta aperta dopo la sonda differenziale. Quella misura la
 * waveform, a schermo pieno; questa misura l'indirizzamento. Sul fratello
 * monocromatico dello stesso silicio, GxEPD2_1330_GDEM133T91, refresh(x,y,w,h)
 * fa esattamente questo: _setPartialRamArea seguito da 0x22=0xFC, oppure 0xF4
 * quando il banco differenziale non c'è. Il driver 097c invece manda sempre
 * _Update_Full a schermo pieno da entrambi gli overload di refresh, quindi qui
 * non c'è niente di già verificato sul campo.
 *
 * COME SI DISTINGUE UNA FINESTRA RISPETTATA DA UNA IGNORATA. Il contenuto
 * finale non basta: la RAM accumula le scritture, quindi un refresh che
 * ripassa tutto il pannello mostra la stessa identica immagine di uno che
 * ripassa solo la finestra. Serve una discordanza voluta fra RAM e schermo,
 * ed è la fascia di trappola: nera in 0x24 a y=176..215, e nessuna finestra
 * di refresh la comprende. Se resta bianca le finestre sono rispettate, se
 * compare il refresh ha percorso il pannello intero.
 *
 * Le passate sono disgiunte lungo Y, così a sonda finita si leggono tutte
 * insieme sullo stesso schermo:
 *     0..167   passata AREA 1, nera, poi la AREA 4 la riporta a bianca: due
 *              scritture sulla stessa area dicono se una catena di partial
 *              regge, e la frase che resta dice quale delle due ha dipinto
 *   176..215   fascia di trappola, scritta in RAM e mai refreshata
 *   224..247   finestra sottile di 24 righe: dice se la durata scala con
 *              l'altezza o è tutta della waveform
 *   264..311   passata Mode 1 (0x22=0xF4), la strada che resta se 0xFC non va
 *   336..503   passata AREA 2, nera, con bordo 0x3C=0x80
 *   504..671   riquadro ristretto anche in X, x=256..511: l'unico posto del
 *              test dove la finestra non è a larghezza piena
 *
 * Il prezzo è già accettato: nelle passate 0xFC la 0x26 fa da frame
 * precedente e non da accent, quindi quello che si misura lì vale per un
 * pannello bianco e nero.
 */
static void probePartialWindowRefresh(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del partial d'area: la finestra RAM limita il refresh? ==="));
  Serial.println(F("questa sonda misura l'INDIRIZZAMENTO, non la waveform: la domanda è se"));
  Serial.println(F("la finestra RAM limita l'area ridipinta. Il discriminante è la fascia"));
  Serial.println(F("di trappola, che nessuna finestra comprende."));
  Serial.println(F("0x26 resta a 0 per tutta la sonda, cioè accent spento: serve a tenere il"));
  Serial.println(F("pannello in bianco e nero, altrimenti le strisce non si vedono."));

  // baseline bianca, col pattern hardware: fondo su cui il nero si legge subito
  Serial.println(F("\nbaseline: schermo bianco col pattern, poi refresh pieno Mode 1"));
  fillByPattern(0x47, 0xFF);   // 0x24 tutto bianco
  fillByPattern(0x46, 0x00);   // 0x26 accent spento: il refresh pieno lo legge come accent
  if (runRefresh(0xF7, "BASELINE BIANCA DELLA SONDA D AREA") < 0)
  {
    Serial.println(F("baseline non riuscita: sonda del partial d'area abbandonata"));
    return;
  }

  // una sola schermata per tutte le passate: si leggono insieme alla fine, e
  // il riquadro finisce dentro la finestra di quella che dipinge per ultima
  apriSchermata(SCH_AREA);

  /**
   * 0x26 NON viene portato a 0xFF, e qui c'è un cambio rispetto a prima che
   * vale la pena spiegare, perchè era il difetto che rendeva questa sonda
   * illeggibile.
   *
   * L'idea precedente era: "il pannello ora è bianco, quindi 0x26 deve
   * contenere il frame precedente, cioè bianco" — corretta SE il differenziale
   * esiste, perchè in quel caso 0x26 non è più l'accent. Ma se il
   * differenziale non esiste, 0x26 resta il piano accent, e 0x26 = 0xFF
   * significa accent acceso su TUTTI i 960 x 672: da quel momento ogni pixel
   * è (1,1) o (0,1), cioè accent in entrambi i casi, e le strisce scritte in
   * 0x24 non si distinguono da niente. È esattamente quello che è successo nel
   * run precedente, dove lo schermo era uniforme e la fascia di trappola non
   * era leggibile.
   *
   * Con 0x26 a 0 il pannello resta in bianco e nero: le strisce, le frasi e la
   * fascia di trappola si leggono, e la domanda di questa sonda — la finestra
   * limita l'area ridipinta? — trova risposta. Il prezzo è che, se un
   * differenziale esistesse, il frame precedente sarebbe tutto nero invece che
   * bianco e la passata ridipingerebbe tutto: ma il differenziale ha ormai una
   * sonda dedicata tutta sua (probeDifferentialDeeper), quindi qui conviene
   * ottimizzare la leggibilità.
   */

  Serial.printf("\ntrappola: fascia nera scritta in 0x24 a y=%u..%u, che nessuna finestra di\n"
                "refresh comprende. Se compare sul pannello, la finestra non è rispettata\n",
                (unsigned)AREA_TRAP_Y, (unsigned)(AREA_TRAP_Y + AREA_TRAP_H - 1));
  fillRect(0x24, 0, AREA_TRAP_Y, SRC, AREA_TRAP_H, 0x00);

  const bool differenzialeVivo = (diffMs1 > 0);
  if (!differenzialeVivo)
    Serial.println(F("\n0xFC non si è concluso nella sonda precedente: le passate 0xFC qui\n"
                     "sarebbero solo cinque timeout, si va diritti a Mode 1 su finestra\n"
                     "(0x22=0xF4), che è la strada che resta"));

  if (differenzialeVivo)
  {
    // bordo come lo lascia l'init, cioè come lo tiene il driver oggi
    writeCommand(0x3C);
    writeData(0x01);
    areaMsFirst = areaPass("AREA 1 STRISCIA ALTA NERA BORDO 0X3C=01 0X22=FC",
                           0, AREA_P1_Y, SRC, AREA_P1_H, 0x00, 0xFF, 0xFC, 30000);

    /**
     * Bordo su VCOM invece che sulla LUT: su questa famiglia di controller è
     * il valore che tiene ferma la cornice durante un partial. Le due passate
     * differiscono solo per questo, quindi il confronto è pulito.
     */
    writeCommand(0x3C);
    writeData(0x80);
    areaPass("AREA 2 STRISCIA CENTRALE NERA BORDO 0X3C=80 0X22=FC",
             0, AREA_P2_Y, SRC, AREA_P2_H, 0x00, 0xFF, 0xFC, 30000);

    areaPass("AREA 3 RIQUADRO RISTRETTO ANCHE IN X 0X22=FC",
             AREA_BOX_X, AREA_BOX_Y, AREA_BOX_W, AREA_BOX_H, 0x00, 0x00, 0xFC, 30000);

    // seconda scrittura sulla stessa area della passata 1: la catena regge?
    /**
     * Le due passate che seguono si fanno solo se il partial guadagna tempo. La
     * 4 chiede se una catena di partial sulla stessa area regge, la 5 se la
     * durata scala con l'altezza della finestra: se la passata 1 è durata come
     * un refresh pieno, la catena non interessa a nessuno e il tempo non scala
     * per definizione, quindi sono cinquanta secondi che non dicono niente.
     */
    const bool partialGuadagna = (fullMs <= 0) || (areaMsFirst * 2 < fullMs);
    if (!partialGuadagna && !cfg.esaustivo)
    {
      Serial.println(F("\n-- passate 4 e 5 saltate: la passata 1 è durata come un refresh"));
      Serial.println(F("   pieno, quindi non c'è nessun tempo da far scalare e nessuna"));
      Serial.println(F("   catena di partial che valga la pena provare"));
    }

    if (partialGuadagna || cfg.esaustivo)
      areaPass("AREA 4 STRISCIA ALTA DI NUOVO BIANCA 0X22=FC",
               0, AREA_P1_Y, SRC, AREA_P1_H, 0xFF, 0x00, 0xFC, 30000);

    if (partialGuadagna || cfg.esaustivo)
      areaMsThin = areaPass("AREA 5 FINESTRA SOTTILE DI 24 RIGHE 0X22=FC",
                            0, AREA_THIN_Y, SRC, AREA_THIN_H, 0x00, 0x00, 0xFC, 30000);

    inizioOsservazione("PARTIAL D'AREA, prima della passata Mode 1");
    static const char* const OSS_AREA_MID[] =
    {
      "la fascia di trappola y=176..215 è nera in RAM e nessuna finestra la",
      "comprende: se è ancora bianca, le finestre sono rispettate",
      "",
      "guardala ADESSO: la passata Mode 1 che sta per partire potrebbe farla",
      "comparire, e a quel punto non si saprebbe più chi l'ha fatta comparire",
    };
    righeOsservazione(OSS_AREA_MID, sizeof(OSS_AREA_MID) / sizeof(OSS_AREA_MID[0]));
    observePause("parte la passata Mode 1 su finestra");
    fineOsservazione();
  }

  /**
   * Mode 1 su finestra. 0xF4 è 0xF7 senza il power down finale, ed è quello
   * che il driver monocromatico manda quando hasFastPartialUpdate è false:
   * waveform piena, ma sempre delimitata dalla finestra. Vale la misura in
   * ogni caso: se 0xFC non funziona è l'unica strada che resta, e a
   * differenza di 0xFC non consuma 0x26, quindi resterebbe compatibile con
   * l'accent.
   */
  writeCommand(0x3C);
  writeData(0x01);
  areaMsMode1 = areaPass("AREA MODE 1 SU FINESTRA 0X22=F4",
                         0, AREA_M1_Y, SRC, AREA_M1_H, 0x00, 0x00, 0xF4, 40000);

  // 0xFC e 0xF4 lasciano clock e analogico accesi: si spengono come fa _PowerOff
  writeCommand(0x22);
  writeData(0xC3);
  writeCommand(0x20);
  waitBusy(2000);

  reportAreaPasses(fullMs);

  inizioOsservazione("PARTIAL D'AREA: fasce, fascia di trappola e riquadro");
  static const char* const OSS_AREA[] =
  {
    "  0..167    AREA 1 nera, poi AREA 4 di nuovo bianca",
    "176..215    fascia di trappola, mai compresa in una finestra",
    "224..247    AREA 5, finestra sottile di 24 righe",
    "264..311    AREA MODE 1 su finestra",
    "336..503    AREA 2 nera, bordo 0x3C=0x80",
    "504..671    AREA 3, riquadro ristretto anche in X, x=256..511",
    "",
    "il numero compare in alto a destra di OGNI fascia ridipinta, non una",
    "volta sola: le passate hanno finestre diverse",
    "",
    "la fascia di trappola è BIANCA o NERA?",
    "  bianca -> la finestra RAM limita davvero l'area ridipinta",
    "  nera   -> il refresh ripassa tutto il pannello, qualunque finestra sia",
    "    impostata",
    "è comparsa solo DOPO la passata Mode 1? allora solo 0xFC rispetta la",
    "  finestra, e un partial d'area costa l'accent",
    "",
    "i bordi verticali del riquadro x=256..511 sono netti? se no, lungo X la",
    "  finestra non vale e nel driver il partial va allargato a tutta la riga",
    "la striscia in cima porta la frase AREA 4 su fondo bianco? se no, la",
    "  catena di partial sulla stessa area non passa pulita e serve un refresh",
    "  pieno periodico",
    "ha lampeggiato solo la cornice di AREA 1 (0x3C=0x01)? allora il driver",
    "  deve mandare 0x3C=0x80 prima di un partial e rimettere 0x01 dopo",
  };
  righeOsservazione(OSS_AREA, sizeof(OSS_AREA) / sizeof(OSS_AREA[0]));
  fineOsservazione();
  chiudiSchermata();
}

/**
 * Prova decisiva di sordità: si manda al controller un'operazione che da
 * sveglio si vedrebbe di sicuro sul BUSY — riempimento della RAM col pattern
 * più la master activation di un refresh — e si guarda se il BUSY fa quello
 * che farebbe se il comando fosse stato eseguito.
 *
 * Serve perchè il livello del BUSY da solo non prova niente: il datasheet lo
 * dà alto durante il deep sleep, ma un pin flottante sta alto lo stesso. Il
 * criterio quindi dipende da come il BUSY sta mentre il controller dorme:
 *
 *   BUSY alto  -> da sveglio il refresh finirebbe e il BUSY scenderebbe. Se
 *                 non scende entro la finestra, il comando non è stato eseguito.
 *   BUSY basso -> da sveglio il refresh alzerebbe il BUSY entro un secondo. Se
 *                 non si alza, il comando non è stato eseguito.
 *
 * Se il controller esegue, il pannello diventa nero e si vede: è comunque un
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

// --- banchi di waveform per temperatura ------------------------------

/**
 * Temperature dello sweep, in gradi Celsius: tre punti dentro il range
 * operativo dichiarato del pannello (0..40 °C sul datasheet SOLUM) più uno
 * fuori. Le tre dentro il range portano una fascia numerata sul vetro, quella
 * fuori no e serve a un'altra domanda, vedi probeTemperatureBanks().
 *
 * Tre punti bastano alla domanda, che è "l'OTP ha più di un banco?": se gli
 * estremi e il centro danno la stessa durata, un quarto punto in mezzo non
 * cambierebbe la conclusione e costerebbe venticinque secondi.
 */
/**
 * Frase di ogni passata, composta dai valori scelti nel menu invece che
 * scritta a mano: il valore di 0x1A è gradi per sedici, e tenerlo accoppiato a
 * mano era il modo più facile di stampare sul vetro un numero diverso da
 * quello davvero programmato.
 *
 * L'ultima passata è quella fuori range: non scrive una fascia propria, ma
 * cancella quella dell'ultima riuscita, vedi probeTemperatureBanks().
 */
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

static char tempFrasi[TEMP_PASSES][48];

static void componiFrasiTemperatura()
{
  for (uint8_t t = 0; t < TEMP_PASSES; ++t)
    snprintf(tempFrasi[t], sizeof(tempFrasi[t]), "TEMPERATURA %d GRADI%s 0X1A=%03X",
             (int)cfg.temp[t],
             tempFuoriRange(cfg.temp[t]) ? " FUORI RANGE" : "",
             (unsigned)((cfg.temp[t] * 16) & 0xFFF));
}

/** Frase di una passata: nullptr per l'ultima, che non dipinge una fascia. */
static const char* tempFrase(uint8_t t)
{
  return (t == TEMP_PASSES - 1) ? nullptr : tempFrasi[t];
}
static int32_t tempSweepMs[TEMP_PASSES] = { -1, -1, -1, -1 };

/**
 * Pavimento del timeout per le passate di questa sonda, l'unica che ne chiede
 * uno più lungo. Un banco per temperatura può essere più corto di quello
 * ambiente, che è la ragione della sonda, ma può anche essere più LUNGO: al
 * freddo il pigmento migra più lentamente e le waveform dei range bassi sono
 * più lunghe. Con i 40 s del default la passata a 0 °C scadeva prima di
 * concludersi, e la misura andava persa proprio dove il banco è diverso da
 * tutti gli altri.
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
 * Forza la temperatura che il controller usa per scegliere la waveform.
 *
 * 0x18 = 0x48 mette il sensore su "esterno": da quel momento il controller non
 * campiona il proprio sensore ma legge il registro scritto con 0x1A. Il formato
 * è quello del §6.8.3, 12 bit in complemento a due dove il valore è i gradi per
 * sedici: 25 °C = 0x190, 50 °C = 0x320, 100 °C = 0x640. Sul bus i 12 bit vanno
 * come li restituisce 0x1B in lettura, cioè A[11:4] nel primo byte e A[3:0] nei
 * bit alti del secondo.
 */
static void setForcedTemperature(int16_t degC)
{
  const int16_t raw = (int16_t)(degC * 16);
  writeCommand(0x18);
  writeData(0x48);   // sensore esterno: usa il registro, non il silicio
  writeCommand(0x1A);
  writeData((uint8_t)((raw >> 4) & 0xFF));
  writeData((uint8_t)((raw << 4) & 0xF0));
}

/** Rimette il sensore interno, che è quello che il driver usa in produzione. */
static void restoreInternalTemperature()
{
  writeCommand(0x18);
  writeData(0x80);
}

/**
 * Sonda dei banchi di waveform per temperatura: la strada che nessuna passata
 * di questo test aveva imboccato, e la sola a costo zero che restava.
 *
 * Da dove viene. Il §6.9 del datasheet, "Waveform LUT Searching Mechanism",
 * dice che l'OTP può contenere fino a **34 set di waveform**, WS0..WS33, uno
 * per ogni range di temperatura TR0..TR33, e che il controller li seleziona
 * così: legge la temperatura, scorre i range da TR0 a TR33 e carica il set
 * dell'**ultimo** che corrisponde. Ogni passata di questo test ha sempre girato
 * col sensore interno (0x18 = 0x80) a temperatura ambiente, quindi ha
 * esercitato **un solo** set: quello del range in cui cade la stanza.
 *
 * Perchè conta per il partial. Le altre sonde cercano una waveform breve dove
 * potrebbe non essercene: quella dell'OTP è unica per temperatura, e una LUT
 * scritta via 0x32 è una waveform che il produttore non ha mai qualificato su
 * questo film. Qui invece si cercano waveform **di fabbrica**, già tarate su
 * questo pannello, che il silicio carica da sè: se una di esse è più corta, il
 * partial esiste e non costa nè rischio nè LUT inventate. Sui pannelli a
 * inchiostro le waveform calde SONO più corte, perchè il pigmento a temperatura
 * più alta migra prima: è la ragione per cui questa sonda ha senso.
 *
 * Cosa si misura, e come si legge:
 *   - le tre temperature dentro il range stampano una fascia numerata. Una
 *     durata molto sotto i 24 s **con** la sua frase sul vetro è il partial
 *     trovato, e la si mette nel driver forzando quella temperatura via
 *     0x18/0x1A prima del refresh;
 *   - una durata molto SOPRA i 24 s è l'esito opposto e conta comunque: dice
 *     che i banchi esistono e che la temperatura li seleziona davvero, ma che
 *     verso il freddo la waveform si allunga. Non è un partial, però obbliga il
 *     driver a un timeout capace di coprire il banco più lungo, ed è la ragione
 *     per cui qui il timeout è tempTimeoutMs() e non quello di default;
 *   - l'ultima è la passata di controllo, e va messa fuori dal range dichiarato
 *     0..40 °C. Serve alla domanda opposta: il datasheet avverte che se nessun
 *     range corrisponde "display will not be updated", quindi un BUSY
 *     brevissimo lì è un rifiuto e non una waveform veloce. È il controllo che
 *     tiene onesta la lettura delle altre tre: senza di esso, una durata breve
 *     resterebbe ambigua. Con un valore dentro 0..40 il controllo non
 *     discrimina, e la sonda lo dice invece di darlo per rifiutato.
 *
 * Rischio: nessuno. Le waveform sono quelle programmate in fabbrica, e la
 * temperatura forzata è un registro che il reset riporta al sensore interno.
 */
static void probeTemperatureBanks(int32_t fullMs)
{
  Serial.println(F("\n=== sonda dei banchi di waveform per temperatura ==="));
  Serial.println(F("il §6.9 del datasheet dà l'OTP per capace di 34 set di waveform,"));
  Serial.println(F("WS0..WS33, uno per range di temperatura TR0..TR33, scelti dal"));
  Serial.println(F("controller in base alla temperatura letta. Tutte le passate di questo"));
  Serial.println(F("test hanno girato col sensore interno a temperatura ambiente, quindi"));
  Serial.println(F("hanno esercitato un solo set: qui si prova a chiederne altri."));
  Serial.println(F("Sono waveform di fabbrica, tarate su questo film: se una è più corta,"));
  Serial.println(F("il partial esiste senza LUT inventate e senza rischio. Una più LUNGA non"));
  Serial.println(F("è un fallimento della sonda: dice che i banchi esistono e che verso il"));
  Serial.printf ("freddo la waveform si allunga, e per questo il timeout qui è %lu ms.\n",
                 (unsigned long)tempTimeoutMs());

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
    const int16_t degC = cfg.temp[t];
    const char*   frase = tempFrase(t);

    if (!frase)
    {
      if (tempFuoriRange(degC))
        Serial.printf("\n-- %d °C, fuori dal range dichiarato: qui un BUSY corto è un"
                      " rifiuto\n", (int)degC);
      else
        Serial.printf("\n-- %d °C: è la passata di controllo, ma cade DENTRO il range"
                      " dichiarato\n   %d..%d °C, quindi il rifiuto non è atteso e il"
                      " controllo non discrimina\n",
                      (int)degC, (int)TEMP_RANGE_MIN, (int)TEMP_RANGE_MAX);
    }

    /**
     * Reset e init a ogni passata: il registro della temperatura e la LUT in
     * RAM tornano allo stato noto, e la fascia precedente resta sul vetro.
     */
    resetPanel();
    initPanel();
    setForcedTemperature(degC);
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);

    if (frase)
    {
      /**
       * Le fasce si accumulano, per la stessa ragione della sonda con LUT
       * custom: 0xF7 è un refresh pieno e ridipinge tutto secondo la RAM,
       * quindi la sola fascia corrente cancellerebbe le precedenti. Con
       * l'accumulo una passata rifiutata lascia sul vetro le fasce
       * dell'ultima riuscita, ed è esattamente il confronto che serve.
       */
      for (int d = 0; d <= t; ++d)
        fillBand(0x24, (uint16_t)(BAND_H * d), BAND_H, 0x00, tempFrasi[d], 0xFF);
    }
    else
    {
      /**
       * La passata fuori range riscrive a BIANCO la fascia dell'ultima passata
       * riuscita, invece di dipingerne una nuova: se il refresh viene eseguito
       * quella frase sparisce dal vetro, se viene rifiutato resta. Il
       * discriminante è quindi la presenza di quella frase, e non si perde
       * nessuna delle fasce precedenti, che servono ancora.
       */
      for (int d = 0; d <= t - 2; ++d)
        fillBand(0x24, (uint16_t)(BAND_H * d), BAND_H, 0x00, tempFrasi[d], 0xFF);
      fillBand(0x24, (uint16_t)(BAND_H * (t - 1)), BAND_H, 0xFF);
    }

    // finestra piena: la variabile di questa sonda è la waveform, non l'area
    setRamWindow(0, 0, SRC, GATE);

    /**
     * 0xF7 e non una sequenza ridotta: il bit 5 è "load temperature" e il bit 4
     * "load LUT", e servono entrambi, perchè è proprio la ricarica che deve
     * andare a cercare il set del range forzato.
     */
    tempSweepMs[t] = runRefresh(0xF7, tempFrasi[t], tempTimeoutMs());

    /**
     * Timeout non vuol dire misura persa: il refresh è ancora in corso, e un
     * reset hardware a metà transizione lascerebbe il pigmento in uno stato non
     * noto e la passata successiva senza baseline. Si attende la discesa e si
     * stampa la durata vera, che è il dato interessante proprio perchè esce dal
     * quadro delle altre.
     */
    if (tempSweepMs[t] == -1)
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
  for (int t = 0; t < TEMP_PASSES; ++t)
  {
    const unsigned raw = (unsigned)((cfg.temp[t] * 16) & 0xFFF);
    const char* etichetta = tempFrasi[t];
    if (tempSweepMs[t] < 0)
      Serial.printf("  0x%03X  non conclusa  %s\n", raw, etichetta);
    else
      Serial.printf("  0x%03X  %6ld ms    %s\n", raw, (long)tempSweepMs[t], etichetta);
  }
  if (fullMs > 0)
    Serial.printf("  riferimento: refresh pieno a temperatura ambiente %ld ms\n",
                  (long)fullMs);

  /**
   * Il confronto che conta non è con lo zero ma fra le passate: se l'OTP ha un
   * solo set, tutte le durate coincidono a meno del rumore di misura.
   */
  int32_t minMs = -1, maxMs = -1;
  int     nonConcluse = 0;
  for (int t = 0; t < TEMP_PASSES - 1; ++t)   // le passate che dipingono la propria fascia
  {
    if (tempSweepMs[t] == -1) { ++nonConcluse; continue; }   // oltre il timeout
    if (tempSweepMs[t] < 0) continue;                        // mai avviata
    if (minMs < 0 || tempSweepMs[t] < minMs) minMs = tempSweepMs[t];
    if (maxMs < 0 || tempSweepMs[t] > maxMs) maxMs = tempSweepMs[t];
  }

  /**
   * Una passata che non si conclude NON è un dato mancante: è una waveform più
   * lunga del timeout, quindi la prova che l'OTP ha più di un set e che la
   * temperatura lo seleziona. Contarla come assente e leggere lo scarto fra le
   * sole passate riuscite porta alla conclusione opposta a quella dei dati,
   * cioè "una sola waveform".
   */
  if (nonConcluse > 0)
    Serial.printf("  %d passate dentro il range non si sono concluse entro %lu ms: quel\n"
                  "  range ha una waveform PIÙ LUNGA, quindi l'OTP ha più di un set e la\n"
                  "  temperatura lo seleziona davvero\n",
                  nonConcluse, (unsigned long)tempTimeoutMs());

  if (minMs > 0 && maxMs > 0)
  {
    Serial.printf("  scarto fra le passate concluse: %ld ms\n", (long)(maxMs - minMs));
    if ((maxMs - minMs) > 2000)
      Serial.println(F("  fra le concluse ce n'è una molto più corta: se la sua fascia è"
                       "\n  nera il driver può forzare quella temperatura, vedi il blocco"
                       " qui sotto"));
    else if (nonConcluse > 0)
      Serial.println(F("  le concluse coincidono fra loro: i banchi si distinguono solo verso"
                       "\n  il freddo, dove la waveform si ALLUNGA. La temperatura è quindi"
                       " una\n  leva reale ma nel verso sbagliato: da qui non esce nessun"
                       " partial, ed è\n  il timeout del driver che deve coprire il banco"
                       " più lungo"));
    else
      Serial.println(F("  scarto trascurabile: su questo pannello l'OTP ha una sola"
                       " waveform,\n  o comunque una sola per tutto il range utile"));
  }
  if (tempSweepMs[TEMP_PASSES - 1] >= 0 && minMs > 0
      && tempSweepMs[TEMP_PASSES - 1] < minMs / 4)
    Serial.println(F("  la passata fuori range è molto più corta delle altre: leggi il"
                     "\n  pannello prima di chiamarla veloce, vedi il blocco qui sotto"));

  inizioOsservazione("BANCHI PER TEMPERATURA: quali fasce sono nere");
  static const char* const OSS_TEMP[] =
  {
    "  0..167    TEMPERATURA 0 GRADI",
    "168..335    TEMPERATURA 20 GRADI",
    "336..503    TEMPERATURA 40 GRADI",
    "",
    "quali fasce sono nere? una fascia nera con una durata molto sotto le",
    "altre è un banco più veloce, e il driver può forzarne la temperatura",
    "via 0x18/0x1A prima del refresh",
    "",
    "la fascia TEMPERATURA 40 GRADI c'è ancora dopo la passata a 70 °C?",
    "  sì -> la passata fuori range è stata rifiutata, che è quello che il",
    "    datasheet descrive quando nessun range corrisponde: la sua durata",
    "    breve non è una waveform veloce",
    "  no -> quella waveform ha dipinto in una frazione del tempo, ed è una",
    "    scoperta",
  };
  righeOsservazione(OSS_TEMP, sizeof(OSS_TEMP) / sizeof(OSS_TEMP[0]));
  fineOsservazione();
  chiudiSchermata();
}

// --- MUX ridotto: meno gate line da scandire -------------------------

static int32_t muxScalingMs[3] = { -1, -1, -1 };
/**
 * Frase di ogni passata, scritta sulla fascia nera in cima e composta dal
 * valore scelto: a MUX ridotto la fascia è più bassa e layoutText() sceglie il
 * corpo che ci sta.
 */
static char muxFrasi[MUX_PASSES][32];

static void componiFrasiMux()
{
  for (uint8_t m = 0; m < MUX_PASSES; ++m)
    snprintf(muxFrasi[m], sizeof(muxFrasi[m]), "MUX %u GATE LINE", (unsigned)cfg.mux[m]);
}

/**
 * Sonda del MUX: il refresh costa in proporzione alle gate line scandite?
 *
 * È l'ultima leva che resta, e non ha niente a che vedere con le waveform. Il
 * comando 0x01 programma il MUX, cioè **quante gate line il driver scandisce**;
 * la finestra RAM di 0x44/0x45 invece dice soltanto dove finiscono i byte. Le
 * passate della sonda d'area hanno mostrato che restringere la finestra non
 * accorcia il refresh — 168, 48 e 24 righe misurano tutte lo stesso tempo — ma
 * quelle passate scandivano comunque tutte e 672 le gate. Qui si riduce il MUX,
 * quindi il pannello ne scandisce davvero meno.
 *
 * Cosa decide l'esito:
 *   - se il tempo scala col MUX, il costo del refresh è per gate line e non per
 *     frame: si può aggiornare una fascia alta N righe in tempo proporzionale,
 *     ed è un partial vero, per bande che partono dalla prima gate;
 *   - se il tempo resta 24 s, il frame rate della waveform è fisso e il
 *     controller aspetta il tempo di frame anche con meno linee da pilotare.
 *     Allora nulla di ciò che riguarda la geometria può accorciare il refresh,
 *     e restano solo le waveform.
 *
 * Il prezzo, se funziona: si scandiscono le prime N gate line, quindi la fascia
 * aggiornabile parte dalla riga 0. Il bit GD di 0x01 sposta il primo gate
 * output e potrebbe spostare anche la fascia, ma è un'altra misura.
 *
 * Rischio: nessuno, il MUX è un registro come gli altri e il reset lo riporta
 * al valore dell'init.
 */
static void probeMuxScaling(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del MUX: il refresh scala con le gate line scandite? ==="));
  Serial.println(F("la sonda d'area ha mostrato che restringere la FINESTRA RAM non"));
  Serial.println(F("accorcia il refresh, ma quelle passate scandivano comunque tutte le"));
  Serial.println(F("672 gate. Qui si riduce il MUX di 0x01, cioè quante gate il driver"));
  Serial.println(F("scandisce davvero: è l'unica leva geometrica non ancora provata."));

  apriSchermata(SCH_MUX);

  for (int m = 0; m < 3; ++m)
  {
    const uint16_t gate = cfg.mux[m];
    const uint16_t mux  = (uint16_t)(gate - 1);

    resetPanel();
    initPanel();
    /**
     * initPanel ha già scritto 0x01 con il MUX pieno: qui lo si riscrive, e
     * dopo va rifatta la finestra RAM, che l'init aveva impostata sull'altezza
     * intera.
     */
    writeCommand(0x01);
    writeData((uint8_t)(mux & 0xFF));
    writeData((uint8_t)((mux >> 8) & 0x03));
    writeData(0x00);
    setRamWindow(0, 0, SRC, gate);

    // bianco su tutta la RAM, poi una fascia nera in cima che deve comparire
    fillByPattern(0x47, 0xFF);
    fillByPattern(0x46, 0x00);
    const uint16_t stripeH = (uint16_t)(gate / 4);
    fillBand(0x24, 0, stripeH, 0x00, muxFrasi[m], 0xFF);

    /**
     * Finestra sulle gate line che il MUX programma, non sulla fascia: la
     * variabile di questa sonda è il MUX, e la finestra dev'essere la più
     * ampia compatibile con esso perchè il confronto fra le tre passate resti
     * pulito.
     */
    setRamWindow(0, 0, SRC, gate);

    muxScalingMs[m] = runRefresh(0xF7, muxFrasi[m]);
  }

  // Ripristino: il MUX pieno è quello che il driver programma in produzione.
  resetPanel();
  initPanel();

  Serial.println(F("\nesito della sonda del MUX:"));
  Serial.println(F("  MUX    BUSY      ms per gate line   frase sulla fascia"));
  for (int m = 0; m < 3; ++m)
  {
    const uint16_t gate = cfg.mux[m];
    if (muxScalingMs[m] < 0)
      Serial.printf("  %4u   non conclusa                  %s\n",
                    (unsigned)(gate - 1), muxFrasi[m]);
    else
      Serial.printf("  %4u   %6ld ms   %14.3f   %s\n",
                    (unsigned)(gate - 1), (long)muxScalingMs[m],
                    (double)muxScalingMs[m] / (double)gate, muxFrasi[m]);
  }
  if (fullMs > 0)
    Serial.printf("  riferimento: refresh pieno %ld ms\n", (long)fullMs);

  if (muxScalingMs[0] > 0 && muxScalingMs[2] > 0)
  {
    const double ratio = (double)muxScalingMs[0] / (double)muxScalingMs[2];
    Serial.printf("  MUX pieno contro un quarto: %.2fx (4.00 se il costo è per gate line,\n"
                  "  1.00 se il frame rate è fisso)\n", ratio);
    if (ratio > 2.0)
      Serial.println(F("  il refresh SCALA con le gate line: esiste un partial per bande,"
                       "\n  e si ottiene riducendo il MUX invece della finestra RAM"));
    else
      Serial.println(F("  il refresh NON scala: il frame rate della waveform è fisso e il"
                       "\n  controller aspetta il tempo di frame anche con meno linee."
                       "\n  Nessuna leva geometrica può accorciare il refresh"));
  }

  inizioOsservazione("MUX RIDOTTO: la fascia nera in cima");
  static const char* const OSS_MUX[] =
  {
    "le tre passate scrivono in cima una fascia nera alta un quarto delle",
    "gate scandite, con la frase MUX 672 / 336 / 168 GATE LINE",
    "",
    "sul vetro c'è la fascia dell'ULTIMA passata, MUX 168?",
    "  sì -> a tutti e tre i valori il pannello scandisce la fascia",
    "  no -> a quel MUX non scandisce: o il valore è troppo basso, o la",
    "    finestra RAM non è stata riadattata",
  };
  righeOsservazione(OSS_MUX, sizeof(OSS_MUX) / sizeof(OSS_MUX[0]));
  fineOsservazione();
  chiudiSchermata();
}

/**
 * Sonda del deep sleep: non "quale parametro alza il BUSY", che è solo un
 * livello su un pin, ma se il pannello si può davvero addormentare e — quello
 * che conta di più — se poi si risveglia e torna a stampare.
 *
 * Un deep sleep da cui non si torna non è un risparmio, è un pannello morto
 * fino al power cycle: per il firmware la domanda utile è il ciclo completo
 * dormi / svegliati / stampa, non il comando di andata.
 *
 * COSA FA, in ordine
 *   1. prova i due parametri di 0x10, uno alla volta, ognuno partendo da un
 *      reset hardware più init così la misura non eredita niente. Dove il BUSY
 *      resta basso la sordità si prova subito col pattern; dove resta alto
 *      serve la prova lunga del punto 2, perchè un BUSY alto lo dà anche un
 *      pin scollegato
 *   2. sul parametro scelto, la prova decisiva: da addormentato gli si manda
 *      un refresh intero e si guarda il BUSY per più del tempo di un refresh
 *   3. finestra ferma per la misura di corrente, che è l'unica prova del
 *      risparmio e in firmware non si può fare: la fa il multimetro
 *   4. risveglio cronometrato con reset hardware più init, e refresh di prova
 *      a schermo nero: se il nero arriva, il ciclo completo funziona
 *   5. riaddormentamento, così il test finisce con il pannello nello stato in
 *      cui lo lascerebbe hibernate()
 *
 * Durante i punti 2 e 3 il pannello mostra ancora l'immagine precedente: è
 * anche la verifica che entrare in deep sleep non la sporchi.
 */
static void probeDeepSleep(int32_t fullMs)
{
  Serial.println(F("\n=== sonda del deep sleep: si addormenta, e soprattutto si sveglia? ==="));

  // finestra di attesa più lunga di un refresh pieno, che è il metro della prova di sordità
  const uint32_t finestra = (uint32_t)(fullMs > 0 ? fullMs : 22000) + 8000;

  static const uint8_t PARAM[2] = { 0x03, 0x11 };
  static const char* PARAM_DESC[2] =
  {
    "A[1:0]=11, l'unico valore che il datasheet definisce",
    "A[1:0]=01, fuori tabella, ed è quello che il driver manda oggi"
  };
  bool candidato[2] = { false, false };

  for (uint8_t k = 0; k < 2; ++k)
  {
    Serial.printf("\n-- 0x10 = 0x%02X  (%s)\n", PARAM[k], PARAM_DESC[k]);
    // reset hardware più init: si parte sveglio e da uno stato noto
    initPanel();
    writeCommand(0x10);
    writeData(PARAM[k]);
    delay(50);

    const bool busyAlto = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
    Serial.printf("   BUSY dopo il comando: %s\n",
                  busyAlto ? "ALTO, come il datasheet descrive il deep sleep"
                           : "basso, che il datasheet non prevede per il deep sleep");

    if (busyAlto)
    {
      candidato[k] = true;
      Serial.println(F("   livello compatibile col deep sleep, ma un pin flottante fa"
                       " lo stesso:\n   la conferma arriva dalla prova lunga più sotto"));
      continue;
    }

    /**
     * Col BUSY basso la prova costa poco: da sveglio il pattern hardware lo
     * alza entro qualche decina di ms, e se non lo alza i comandi non arrivano.
     *
     * Vale però solo dove il pattern è supportato, e la validazione fatta a
     * inizio test lo dice: senza quel comando un BUSY fermo non distingue
     * "dorme" da "il comando non esiste", e prenderlo per sonno sarebbe un
     * falso positivo. In quel caso si rimanda alla prova decisiva, che usa la
     * master activation e quella un controller vivo la esegue sempre.
     */
    if (patternMs24 < 0)
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
  initPanel();
  writeCommand(0x10);
  writeData(sleepParamOk);
  delay(50);
  sleepBusyHigh   = (digitalRead(PIN_BUSY) == BUSY_ACTIVE);
  sleepIgnoresCmd = deepSleepIgnoresCommands(finestra);

  if (!sleepIgnoresCmd)
  {
    Serial.println(F("\nil controller esegue anche dopo 0x10: quello che si vede sul BUSY"
                     "\nnon è deep sleep. Il pannello adesso è nero perchè il refresh di"
                     "\nprova è passato davvero."));
    return;
  }

  /**
   * Finestra ferma per il multimetro. È l'unico modo di sapere se il deep
   * sleep serve a qualcosa: il firmware può dire che il controller ignora i
   * comandi, non quanto assorbe. Si misura sul 3V3 che alimenta il pannello,
   * confrontando questo valore con quello a pannello sveglio e a riposo.
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
  rigaOsservazioneF("il pannello resta addormentato per %lu s",
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
  initPanel();
  wakeInitMs = (int32_t)(millis() - tWake);
  Serial.printf("   reset più init in %ld ms\n", (long)wakeInitMs);

  /**
   * Refresh di prova a schermo nero. Il nero è distinguibile da qualunque cosa
   * ci fosse prima, quindi se arriva il ciclo dormi / svegliati / stampa
   * funziona per intero, che è la sola cosa che il firmware deve sapere.
   */
  apriSchermata(SCH_SLEEP);
  fillByPattern(0x47, 0x00);   // 0x24 tutto nero
  fillByPattern(0x46, 0x00);   // accent spento
  wakeRefreshMs = runRefresh(0xF7, "REFRESH DI PROVA DOPO IL RISVEGLIO DAL DEEP SLEEP");

  inizioOsservazione("DEEP SLEEP: il refresh di prova dopo il risveglio");
  static const char* const OSS_SLEEP2[] =
  {
    "lo schermo è diventato NERO?",
    "  sì -> il ciclo dormi / svegliati / stampa funziona e hibernate() è",
    "    utilizzabile: dopo il risveglio il driver rifà init e riscrive i",
    "    piani, perchè il deep sleep non conserva la RAM",
    "  no -> da lì non si torna, e hibernate() lascerebbe il pannello morto",
    "    fino al power cycle",
  };
  righeOsservazione(OSS_SLEEP2, sizeof(OSS_SLEEP2) / sizeof(OSS_SLEEP2[0]));
  fineOsservazione();
  chiudiSchermata();

  // stato finale come lo lascerebbe hibernate()
  writeCommand(0x10);
  writeData(sleepParamOk);
  delay(50);
  Serial.printf("\npannello riaddormentato con 0x10 = 0x%02X, BUSY %s\n",
                sleepParamOk,
                digitalRead(PIN_BUSY) == BUSY_ACTIVE ? "alto" : "basso");
}

void setup()
{
  Serial.begin(115200);
  delay(1500);
  caricaConfig();
  menuIniziale();
  spiSettings = SPISettings(cfg.spiHz, MSBFIRST, SPI_MODE0);
  componiFrasiTemperatura();
  componiFrasiMux();
  const uint32_t tSetup = millis();
  const uint32_t heapStart = ESP.getFreeHeap();

  Serial.println();
  Serial.println(F("=== panel_diagnostic: colori e combinazioni, SOLUM ESL 9.7 pollici ==="));
  Serial.printf("chip  %s rev %d, %d core, CPU %lu MHz, APB %lu Hz\n",
                ESP.getChipModel(), (int)ESP.getChipRevision(), (int)ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)getApbFrequency());
  Serial.printf("heap  %lu byte liberi, blocco max %lu byte\n",
                (unsigned long)heapStart, (unsigned long)ESP.getMaxAllocHeap());
  Serial.printf("pin   CS=%d DC=%d RST=%d BUSY=%d (attivo %s)\n",
                PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, BUSY_ACTIVE == HIGH ? "alto" : "basso");
  Serial.printf("HSPI  SCK=%d MISO=%d MOSI=%d\n", PIN_SCK, PIN_MISO, PIN_MOSI);

  /**
   * Il clock SPI dell'ESP32 nasce dividendo l'APB per un intero: il valore
   * richiesto non sempre è ottenibile e il divisore effettivo determina il
   * limite superiore del throughput misurato più sotto.
   */
  const uint32_t apb = getApbFrequency();
  const uint32_t div = (apb + cfg.spiHz / 2) / cfg.spiHz;
  Serial.printf("bus   %lu Hz richiesti, MSBFIRST, SPI_MODE0 -> stimati %lu Hz (APB/%lu)\n",
                (unsigned long)cfg.spiHz, (unsigned long)(apb / (div ? div : 1)), (unsigned long)div);
  Serial.printf("RAM   %u source (X) x %u gate (Y), %u byte per riga, %lu byte per piano (%.1f KiB)\n",
                (unsigned)SRC, (unsigned)GATE, (unsigned)ROW_BYTES,
                (unsigned long)PLANE_BYTES, (double)PLANE_BYTES / 1024.0);
  Serial.printf("piani 0x24 (BW) e 0x26 (accent), gli unici due -> %lu byte per frame\n",
                (unsigned long)(2UL * PLANE_BYTES));
  Serial.printf("parametri  temperature %d/%d/%d/%d gradi, MUX %u/%u/%u gate,\n"
                "           clock %lu Hz, timeout %lu ms, multimetro %lu s\n",
                (int)cfg.temp[0], (int)cfg.temp[1], (int)cfg.temp[2], (int)cfg.temp[3],
                (unsigned)cfg.mux[0], (unsigned)cfg.mux[1], (unsigned)cfg.mux[2],
                (unsigned long)cfg.spiHz, (unsigned long)cfg.timeoutMs,
                (unsigned long)(cfg.sleepMs / 1000));
  Serial.printf("bande alte %u px; le sonde con LUT custom scrivono %u byte via 0x32\n",
                (unsigned)BAND_H, (unsigned)LUT_BYTES);
  /**
   * Durata: il test fa una trentina di refresh, e su questo pannello uno costa
   * 24 s a temperatura ambiente. Vale la pena saperlo prima di cominciare,
   * perchè metà delle misure non ha senso se il test viene interrotto a metà.
   * La sonda dei banchi per temperatura può sforare: al freddo la waveform si
   * allunga, e quella passata ha un timeout suo.
   */
  int16_t tempMin = cfg.temp[0];
  for (uint8_t t = 1; t < TEMP_PASSES; ++t)
    if (cfg.temp[t] < tempMin) tempMin = cfg.temp[t];
  Serial.printf ("durata  ~%d refresh da 24 s, cioè circa %d minuti più le pause,\n"
                 "        e la passata a %d gradi può durare di più\n",
                 refreshPrevisti(), (refreshPrevisti() * 25) / 60 + 1, (int)tempMin);
  if (cfg.esaustivo)
    Serial.println(F("        esaustivo attivo: nessuna passata viene saltata"));
  Serial.println(F("        Ogni refresh risponde a una domanda che nient'altro poteva"));
  Serial.println(F("        rispondere: le passate deducibili da un'altra misura sono"));
  Serial.println(F("        condizionali e si saltano da sè, dicendo perchè."));
  Serial.println(F("        Non interrompere: i riepiloghi finali confrontano fra loro"));
  Serial.println(F("        passate che stanno in sonde diverse"));
  Serial.println(F("\ncome leggere il log insieme al pannello:"));
  Serial.println(F("  ogni schermata da guardare porta un numero in un riquadro in alto a"));
  Serial.println(F("  destra, e le righe che la riguardano iniziano con lo stesso numero"));
  Serial.println(F("  fra parentesi quadre. Dove il test chiede di guardare il vetro, il"));
  Serial.println(F("  blocco sta fra una barra di v e una barra di ^"));

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_BUSY, INPUT);
  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  /**
   * Livello del BUSY a controller non ancora inizializzato. Alto è normale su
   * questo pannello: è lo stesso livello che il datasheet dà per il deep sleep
   * ed è anche quello che darebbe un pin non pilotato, quindi qui non c'è
   * niente da attendersi. Il livello che significa qualcosa è quello a init
   * finita, ed è là che sta la verifica.
   */
  Serial.printf("\nBUSY prima del reset: %d (indifferente: il controller non è ancora"
                " inizializzato)\n", digitalRead(PIN_BUSY));

  Serial.println(F("\ninit del controller, spacchettata:"));
  const uint32_t tInit = millis();
  initPanel();
  Serial.printf("  totale              %6lu ms, BUSY=%d\n",
                (unsigned long)(millis() - tInit), digitalRead(PIN_BUSY));
  if (digitalRead(PIN_BUSY) == BUSY_ACTIVE)
    Serial.println(F("  ATTENZIONE: BUSY ancora attivo a init finita, e qui deve essere a"
                     " riposo:\n  init o cablaggio non funzionano, e nessuna misura che"
                     " segue vale"));

  /**
   * Letture di registro. Lo status 0x2F va per primo perchè ha un POR noto e
   * quindi dice se il percorso di lettura vale qualcosa: senza quello, User
   * ID e temperatura sono numeri senza significato.
   */
  if (sondaAttiva(S_BENCH, "registri in lettura e benchmark del bus"))
  {
    Serial.println(F("\nregistri in lettura:"));
    const bool readsValid = reportStatus();
    reportUserId();
    // prima del refresh la temperatura non è ancora stata caricata: baseline
    reportTemperature("prima del refresh");
    if (!readsValid)
      Serial.println(F("  (le tre letture sopra non sono attendibili: MISO del pannello non cablato)"));

    benchmarkBus();
  }

  /**
   * Validazione dei comandi Auto Write Pattern, quelli con cui il driver
   * riempie un piano senza passare dal bus: un byte invece di 80.640. Sta
   * prima di tutto il resto di proposito, così i piani vengono comunque
   * riscritti per intero dopo e la misura delle combinazioni resta valida
   * anche se il controller non accettasse il comando.
   */
  Serial.println(F("\nvalido il pattern hardware sui due piani immagine"));
  patternMs24 = fillByPattern(0x47, 0xFF);   // 0x24 tutto bianco
  patternMs26 = fillByPattern(0x46, 0x00);   // 0x26 accent spento
  /**
   * Nessun comando analogico prima delle bande, e non è una dimenticanza. Un
   * run precedente azzerava 0x28 qui: quel comando alza il BUSY, il BUSY è
   * rimasto alto oltre i 3 s, le finestre delle bande sono state scritte su un
   * controller occupato e il refresh successivo è stato ingoiato — 1662 ms di
   * BUSY invece di 24 s. Le quattro bande non sono mai arrivate sul vetro, e la
   * misura principale del test è andata persa per proteggersi da una
   * contaminazione ipotetica. Da allora le bande vengono per prime, su un
   * controller che nessuna misura analogica ha toccato.
   */

  int32_t busy_ms = -1;
  if (sondaAttiva(S_BANDE, "le 4 bande in Display Mode 1"))
  {
    // Le 4 combinazioni dei due piani, dall'alto verso il basso
    static const uint8_t v24[4] = { 0xFF, 0x00, 0xFF, 0x00 };
    static const uint8_t v26[4] = { 0x00, 0x00, 0xFF, 0xFF };
    /**
     * Frase di ogni banda: dice la coppia di bit, la LUT che ne segue secondo la
     * Table 6-4 e il colore atteso. Il colore che esce lo dice il pannello, non
     * questa tabella: per le prime tre è quello che il firmware produce già oggi.
     * La stessa stringa va sul vetro e sul seriale.
     */
    static const char* const frasiBande[4] =
    {
      "BANDA 1 BW=1 RED=0 LUT1 ATTESO BIANCO",
      "BANDA 2 BW=0 RED=0 LUT0 ATTESO NERO",
      "BANDA 3 BW=1 RED=1 LUT3 IL ROSSO IN USO OGGI",
      "BANDA 4 BW=0 RED=1 LUT2 MAI USATA DAL FIRMWARE",
    };

    /**
     * Valore dei due piani sui pixel del testo: combinazione (BW=1, accent=0) su
     * tutte le bande tranne la prima, che quella combinazione la ha già come
     * fondo e per il testo usa (0,0). Così la frase stacca dal fondo qualunque
     * colore le due combinazioni rendano.
     */
    static const uint8_t d24[4] = { 0x00, 0xFF, 0xFF, 0xFF };
    static const uint8_t d26[4] = { 0x00, 0x00, 0x00, 0x00 };

    Serial.println(F("\nscrivo le 4 bande, ognuna con la propria frase in basso a sinistra:"));
    const uint32_t tBands = millis();
    for (int k = 0; k < 4; ++k)
    {
      const uint16_t y = (uint16_t)k * BAND_H;
      Serial.printf(" %s  [0x24=0x%02X 0x26=0x%02X, testo %s]\n",
                    frasiBande[k], v24[k], v26[k], d24[k] ? "bianco" : "nero");
      fillBand(0x24, y, BAND_H, v24[k], frasiBande[k], d24[k]);
      fillBand(0x26, y, BAND_H, v26[k], frasiBande[k], d26[k]);
    }
    Serial.printf("  8 fasce scritte in %lu ms\n", (unsigned long)(millis() - tBands));

    /**
     * Refresh delle bande, primo del test. Mode 1 (0x22 = 0xF7) e non Mode 2: è
     * la waveform che il driver usa in produzione, ed è sotto quella waveform
     * che la banda 4 esercita LUT2, l'unica delle quattro combinazioni che il
     * firmware non genera mai.
     *
     * Finestra piena prima di partire: le fillBand l'hanno lasciata sull'ultima
     * banda, e il riquadro col numero va in alto a destra dello schermo, non in
     * alto a destra della banda 4.
     */
    setRamWindow(0, 0, SRC, GATE);
    apriSchermata(SCH_BANDE);
    busy_ms = runRefresh(0xF7, "LE 4 BANDE IN DISPLAY MODE 1 0X22=F7");

    // ora il registro è stato caricato dal bit load-temp del comando 0x22
    reportTemperature("dopo Mode 1");

    /**
     * La lettura che decide se un quarto colore esiste. Banda 3 e banda 4 sono
     * adiacenti, separate solo dalla linea a y=504: LUT3 sopra, LUT2 sotto,
     * stesso accent acceso su entrambe, stessa altezza. Il confronto va fatto a
     * contatto, non a memoria fra due refresh.
     */
    inizioOsservazione("LE 4 BANDE IN MODE 1: banda 3 contro banda 4");
    static const char* const OSS_BANDE[] =
    {
      "  0..167    BANDA 1 (BW=1 RED=0) LUT1, attesa bianca",
      "168..335    BANDA 2 (BW=0 RED=0) LUT0, attesa nera",
      "336..503    BANDA 3 (BW=1 RED=1) LUT3, il rosso che il firmware scrive",
      "504..671    BANDA 4 (BW=0 RED=1) LUT2, mai generata dal firmware",
      "",
      "banda 3 e banda 4 sono a contatto sulla linea y=504, e il confronto va",
      "fatto lì: la linea si vede, o sono un unico blocco uniforme?",
      "  uniforme -> LUT2 = LUT3, tre colori, nessun quarto colore",
      "  diverse  -> esiste un quarto stato di pixel, e vive su una coppia di",
      "    bit dei due piani esistenti",
      "",
      "se banda 1 non è bianca o banda 2 non è nera, init o cablaggio non",
      "funzionano e nessun'altra riga di questo test ha valore",
    };
    righeOsservazione(OSS_BANDE, sizeof(OSS_BANDE) / sizeof(OSS_BANDE[0]));
    fineOsservazione();
    chiudiSchermata();
  }

  /**
   * Il canale 0x28 non viene più sondato: un run precedente ha risposto. Alla
   * scrittura alza il BUSY per circa 10 s e non dipinge niente, quindi è VCOM
   * Sense come dice il datasheet e non un terzo piano immagine. Le quattro
   * forme di quella sonda costavano ~50 s di test per una domanda chiusa.
   */
  Serial.println(F("\nil canale 0x28 non viene sondato: già misurato come VCOM Sense"));
  Serial.println(F("(BUSY alto ~10 s alla scrittura, nessuna forma sul vetro)"));

  /**
   * Passata Mode 2 sulla stessa RAM: 0x22 = 0xFF, identica a 0xF7 tranne il
   * Display Mode. Mode 2 è il banco differenziale e tratta la seconda RAM come
   * frame precedente, quindi una differenza va interpretata prima di chiamarla
   * colore. Serve a documentare cosa fa Mode 2 su questo pannello.
   */
  int32_t busy_ms2 = -1;
  if (sondaAttiva(S_MODE2, "le stesse bande in Display Mode 2"))
  {
    observePause("parte la passata DISPLAY Mode 2, che sovrascrive il risultato");
    apriSchermata(SCH_MODE2);
    busy_ms2 = runRefresh(0xFF, "LE 4 BANDE IN DISPLAY MODE 2 0X22=FF");
    reportTemperature("dopo Mode 2");

    inizioOsservazione("LE STESSE 4 BANDE IN MODE 2: è cambiato qualcosa?");
    static const char* const OSS_MODE2[] =
    {
      "stessa RAM della schermata precedente, solo il Display Mode è diverso",
      "",
      "i colori delle quattro bande sono gli stessi?",
      "  sì -> Mode 2 su questo pannello legge le due RAM come Mode 1",
      "  no -> Mode 2 le legge come (frame precedente, frame nuovo), e la",
      "    differenza va interpretata prima di chiamarla colore",
    };
    righeOsservazione(OSS_MODE2, sizeof(OSS_MODE2) / sizeof(OSS_MODE2[0]));
    fineOsservazione();
    chiudiSchermata();
  }

  /**
   * Probe del quarto colore per livello di sorgente. Chiude la domanda che le
   * bande hanno lasciato aperta: quelle hanno mostrato che sotto la waveform
   * dell'OTP LUT2 e LUT3 rendono lo stesso rosso, qui si prova a pilotarle a
   * due tensioni diverse, che è il modo in cui un film a quattro pigmenti
   * separa rosso e giallo. Sta dopo la lettura delle bande perchè riscrive la
   * metà bassa dello schermo.
   */
  if (sondaAttiva(S_LIVELLI, "quarto colore per livello di sorgente"))
  {
    observePause("parte il probe del quarto colore, che pilota LUT2 e LUT3 a tensioni diverse");
    probeFourthColorLevels();
    observePause("riparte il test");
  }


  /**
   * Sonda del partial. Sta per ultima perchè azzera i piani e riscrive lo
   * schermo da capo: tutto quello che le due passate precedenti hanno prodotto
   * va guardato prima.
   */
  if (sondaAttiva(S_DIFF, "sonda differenziale"))
  {
    observePause("parte la sonda differenziale, che azzera lo schermo e lo riscrive");
    probeDifferentialRefresh(busy_ms);
  }

  /**
   * Sonda differenziale approfondita: gli interruttori che la sonda base non
   * tocca. Non ha pause di osservazione perchè misura durate, non colori — il
   * pannello si guarda alla fine, e solo se qualche passata è stata veloce.
   */
  if (sondaAttiva(S_DIFFDEEP, "differenziale approfondita"))
    probeDifferentialDeeper(busy_ms);

  /**
   * Sonda del partial con LUT scritta dall'MCU. Viene dopo la differenziale
   * approfondita perchè quella stabilisce che in OTP c'è una sola waveform:
   * solo a quel punto ha senso provare a scriverne una breve via 0x32, che è
   * l'unica strada rimasta e la sola che il run precedente non aveva provato.
   */
  if (sondaAttiva(S_LUT, "partial con LUT custom"))
  {
    probePartialLut(busy_ms);
    observePause("riparte il test");
  }

  /**
   * Sonda del partial d'area. Viene dopo la differenziale perchè si appoggia
   * al suo esito: se 0xFC non si è concluso lì, qui le passate 0xFC vengono
   * saltate. Riparte da una baseline bianca, quindi quello che la
   * differenziale ha lasciato a schermo va guardato prima.
   */
  if (sondaAttiva(S_AREA, "partial d'area"))
  {
    observePause("parte la sonda del partial d'area, che riporta lo schermo a bianco");
    probePartialWindowRefresh(busy_ms);
  }

  /**
   * Banchi di waveform per temperatura. Viene dopo tutte le altre sonde del
   * partial perchè è l'unica che non cerca una waveform breve dove potrebbe non
   * esserci: chiede al silicio di caricarne una di fabbrica, fra le trentaquattro
   * che l'OTP può contenere secondo il §6.9 del datasheet. Se una è più corta il
   * partial esiste senza LUT inventate.
   */
  if (sondaAttiva(S_TEMP, "banchi per temperatura"))
  {
    observePause("parte la sonda dei banchi per temperatura, che riscrive lo schermo");
    probeTemperatureBanks(busy_ms);
    observePause("riparte il test");
  }

  /**
   * MUX ridotto: l'ultima leva, e non riguarda le waveform. Le passate d'area
   * hanno mostrato che restringere la finestra RAM non accorcia il refresh, ma
   * scandivano comunque tutte le gate line: qui se ne scandiscono meno davvero.
   */
  if (sondaAttiva(S_MUX, "MUX ridotto"))
    probeMuxScaling(busy_ms);

  /**
   * Sonda del deep sleep. Sta per ultima perchè si chiude riscrivendo lo
   * schermo a nero: è il refresh di prova che dimostra il risveglio, e
   * cancella quello che la sonda d'area ha lasciato.
   */
  if (sondaAttiva(S_SLEEP, "deep sleep"))
  {
    observePause("parte la sonda del deep sleep, che si chiude con lo schermo nero");
    probeDeepSleep(busy_ms);
  }

  /**
   * Riepilogo: i totali dicono quanto di un aggiornamento è bus, quanto è
   * attesa del pannello e quanto costerebbe un frame pieno.
   */
  const double usbBulk = usPerByteReference();
  const double usbRow = rowUsPerByte > 0.0 ? rowUsPerByte : usbBulk;
  Serial.println(F("\n--- riepilogo tempi ---"));
  if (totalSpiBytes > 0 && totalSpiMicros > 0)
    Serial.printf("dati sul bus     %lu byte in %lu us -> %.2f us/byte, %.2f MB/s\n",
                  (unsigned long)totalSpiBytes, (unsigned long)totalSpiMicros,
                  (double)totalSpiMicros / (double)totalSpiBytes,
                  (double)totalSpiBytes / (double)totalSpiMicros);
  Serial.printf("transazioni      %lu comandi, %lu parametri a singolo byte\n",
                (unsigned long)totalCommands, (unsigned long)totalParamBytes);
  Serial.printf("attese BUSY      %lu ms in totale\n", (unsigned long)totalBusyMillis);
  Serial.printf("setup completo   %lu ms\n", (unsigned long)(millis() - tSetup));
  Serial.printf("heap             %lu byte liberi (%ld byte consumati dal test)\n",
                (unsigned long)ESP.getFreeHeap(),
                (long)heapStart - (long)ESP.getFreeHeap());

  Serial.println(F("\n--- stima di un aggiornamento a schermo pieno ---"));
  Serial.printf("push 2 piani (BW+RED) %lu byte -> %.0f ms a blocchi da 120 byte\n",
                (unsigned long)(2UL * PLANE_BYTES), usbRow * 2.0 * PLANE_BYTES / 1000.0);
  if (patternMs24 >= 0 && patternMs26 >= 0)
    Serial.printf("clear dei 2 piani col pattern hardware: %ld ms invece di %.0f ms\n",
                  (long)(patternMs24 + patternMs26), usbBulk * 2.0 * PLANE_BYTES / 1000.0);
  if (busy_ms > 0)
    Serial.printf("ciclo completo        %.1f s: %.0f ms di push + %ld ms di refresh\n",
                  (usbRow * 2.0 * PLANE_BYTES / 1000.0 + (double)busy_ms) / 1000.0,
                  usbRow * 2.0 * PLANE_BYTES / 1000.0, (long)busy_ms);
  if (busy_ms > 0 && busy_ms2 > 0)
    Serial.printf("refresh Mode 1 %ld ms, Mode 2 %ld ms: %s\n", (long)busy_ms, (long)busy_ms2,
                  busy_ms2 > busy_ms + 500 || busy_ms2 < busy_ms - 500
                    ? "durate diverse, quindi Mode 2 usa un altro banco di waveform"
                    : "durate uguali, probabilmente lo stesso banco");
  if (diffMs1 > 0)
  {
    Serial.printf("refresh differenziale (0x22=0xFC) %ld ms", (long)diffMs1);
    if (diffMs2 > 0)
      Serial.printf(", seconda passata %ld ms", (long)diffMs2);
    /**
     * Il rapporto si annuncia come guadagno solo se è un guadagno: con 1,0x la
     * riga "1.0x più veloce del pieno" diceva il contrario di quello che i
     * numeri dicevano.
     */
    if (busy_ms > 0)
    {
      const double ratio = (double)busy_ms / (double)diffMs1;
      if (ratio >= 1.1)
        Serial.printf(" -> %.1fx più veloce del pieno", ratio);
      else
        Serial.printf(" -> %.2fx, cioè come il refresh pieno: nessun guadagno", ratio);
    }
    Serial.println();
    if (busy_ms > 0 && diffMs1 * 4 < busy_ms)
    {
      const double pushMs = usbRow * PLANE_BYTES / 1000.0;
      Serial.printf("se il frame nuovo si vede, un aggiornamento B/N costerebbe %.1f s\n"
                    "  (%.0f ms per il solo piano 0x24 + %ld ms di refresh) invece di %.1f s\n",
                    (pushMs + (double)diffMs1) / 1000.0, pushMs, (long)diffMs1,
                    (usbRow * 2.0 * PLANE_BYTES / 1000.0 + (double)busy_ms) / 1000.0);
    }
  }
  if (areaMsFirst > 0 || areaMsMode1 > 0)
  {
    Serial.println(F("partial d'area, valido solo se la fascia di trappola è rimasta bianca:"));
    if (areaMsFirst > 0)
    {
      const double pushBand = usbRow * (double)ROW_BYTES * AREA_P1_H / 1000.0;
      Serial.printf("  0xFC su %u righe: %.0f ms di push + %ld ms di refresh = %.2f s\n",
                    (unsigned)AREA_P1_H, pushBand, (long)areaMsFirst,
                    (pushBand + (double)areaMsFirst) / 1000.0);
    }
    if (areaMsThin > 0)
      Serial.printf("  0xFC su %u righe: %ld ms di refresh\n",
                    (unsigned)AREA_THIN_H, (long)areaMsThin);
    if (areaMsMode1 > 0)
      Serial.printf("  0xF4 su %u righe: %ld ms di refresh\n",
                    (unsigned)AREA_M1_H, (long)areaMsMode1);
  }

  /**
   * Scheda di osservazione. Il test non può vedere il pannello, quindi si
   * ferma qui e lascia le domande e la mappa esito -> conseguenza. Le
   * combinazioni misurate sono tutte quelle che due piani a 1 bit possono
   * esprimere, quindi le risposte a questi punti chiudono la questione.
   */
  Serial.println(F("\n--- deep sleep ---"));
  if (sleepParamOk == 0x00)
    Serial.println(F("nessun parametro di 0x10 addormenta il controller"));
  else
  {
    Serial.printf("parametro         0x10 = 0x%02X, BUSY %s mentre dorme\n",
                  sleepParamOk, sleepBusyHigh ? "alto" : "basso");
    Serial.printf("sordità           %s\n",
                  sleepIgnoresCmd ? "ignora anche un refresh intero: dorme davvero"
                                  : "esegue ancora i comandi: non è deep sleep");
    if (wakeInitMs >= 0)
      Serial.printf("risveglio         %ld ms di reset più init\n", (long)wakeInitMs);
    if (wakeRefreshMs > 0)
      Serial.printf("primo frame dopo  %ld ms di refresh -> ciclo completo %.1f s\n",
                    (long)wakeRefreshMs,
                    ((double)(wakeInitMs > 0 ? wakeInitMs : 0) + (double)wakeRefreshMs) / 1000.0);
    else if (sleepIgnoresCmd)
      Serial.println(F("primo frame dopo  NON arrivato: dal deep sleep non si torna"));
  }
  Serial.println(F("il risparmio in corrente non è misurabile da qui: vedi la scheda"));

  Serial.println(F("\n============ SCHEDA DI OSSERVAZIONE ============"));
  Serial.println(F("solo le domande dell'occhio: quello che si deduce da una durata lo hanno"));
  Serial.println(F("già scritto i verdetti delle sonde. Il numero di ogni voce è quello del"));
  Serial.println(F("riquadro sul vetro e delle righe di log della schermata."));

  Serial.println(F("\nschermate prodotte:"));
  for (int i = 0; i < SCH_COUNT; ++i)
  {
    if (schermate[i])
      Serial.printf("  [%u] %s\n", (unsigned)schermate[i], NOME_SCHERMATA[i]);
    else
      Serial.printf("  [--] %s   non prodotta\n", NOME_SCHERMATA[i]);
  }

  static const char* const SCHEDA_BANDE[] =
  {
    "BANDA 1 (BW=1 RED=0) LUT1  colore ................",
    "BANDA 2 (BW=0 RED=0) LUT0  colore ................",
    "BANDA 3 (BW=1 RED=1) LUT3  colore ................",
    "BANDA 4 (BW=0 RED=1) LUT2  colore ................",
    "la linea y=504 si vede?    SI / NO",
    "atteso, già misurato      -> 1 bianca, 2 nera, 3 e 4 entrambe rosse:",
    "                             LUT2 = LUT3, nessun quarto colore",
    "banda 1 o 2 sbagliate     -> init o cablaggio: nient'altro vale",
  };
  voceScheda(SCH_BANDE, "LE 4 BANDE, 0x22 = 0xF7",
             SCHEDA_BANDE, sizeof(SCHEDA_BANDE) / sizeof(SCHEDA_BANDE[0]));

  static const char* const SCHEDA_MODE2[] =
  {
    "è cambiato qualcosa rispetto alla schermata precedente?  ................",
  };
  voceScheda(SCH_MODE2, "LE STESSE BANDE IN DISPLAY MODE 2, 0x22 = 0xFF",
             SCHEDA_MODE2, sizeof(SCHEDA_MODE2) / sizeof(SCHEDA_MODE2[0]));

  static const char* const SCHEDA_LIVELLI[] =
  {
    "sopra y=504 (LUT3 a VSH2)  colore ................",
    "sotto y=504 (LUT2 a VSH1)  colore ................",
    "colori DIVERSI            -> esiste un quarto stato: coppia di bit più",
    "                             una LUT custom via 0x32",
    "colori IDENTICI           -> tre pigmenti, la questione si chiude",
    "entrambe bianche          -> inconcludente: leggilo insieme alla sonda",
    "                             del partial con LUT",
  };
  voceScheda(SCH_LIVELLI, "PROBE DEI LIVELLI, le due fasce in basso",
             SCHEDA_LIVELLI, sizeof(SCHEDA_LIVELLI) / sizeof(SCHEDA_LIVELLI[0]));

  static const char* const SCHEDA_LUT[] =
  {
    "quale fascia è NERA:  PARTIAL 1 ....  PARTIAL 2 ....  nessuna ....",
    "fascia nera sotto 1 s     -> IL PARTIAL ESISTE: il lavoro da fare sul",
    "                             driver è elencato in README.md",
    "nessuna nera              -> la LUT è accettata ma non pilota: la",
    "                             waveform va aggiustata",
    "vince PARTIAL 1           -> il silicio legge (precedente, nuovo), e in",
    "                             Mode 2 un frame in partial è senza accent",
    "vince PARTIAL 2           -> legge la Table 6-4, cioè (accent, BW)",
    "la cornice ha lampeggiato? con 0x3C = 0xC0 non deve   SI / NO",
  };
  voceScheda(SCH_LUT, "PARTIAL CON LUT CUSTOM, la domanda più importante",
             SCHEDA_LUT, sizeof(SCHEDA_LUT) / sizeof(SCHEDA_LUT[0]));

  static const char* const SCHEDA_AREA[] =
  {
    "trappola y=176..215       -> era   BIANCA / NERA",
    "bianca                    -> la finestra RAM limita l'area ridipinta",
    "nera                      -> il refresh ripassa tutto il pannello",
    "nera solo dopo AREA MODE 1-> solo 0xFC rispetta la finestra, e un",
    "                             partial d'area costa l'accent",
    "mai comparsa              -> anche Mode 1 la rispetta: il partial d'area",
    "                             resta compatibile con l'accent",
    "bordi x=256..511 non netti-> lungo X la finestra non vale: nel driver il",
    "                             partial va a x=0 e w=WIDTH",
    "in cima non c'è AREA 4    -> la catena di partial non passa pulita:",
    "                             serve un contatore che forzi _Update_Full",
    "                             ogni N",
    "lampeggia solo AREA 1     -> 0x3C = 0x80 prima di un partial, 0x01 prima",
    "                             di un refresh pieno",
  };
  voceScheda(SCH_AREA, "PARTIAL D'AREA",
             SCHEDA_AREA, sizeof(SCHEDA_AREA) / sizeof(SCHEDA_AREA[0]));

  static const char* const SCHEDA_TEMP[] =
  {
    "fasce nere:  0 GRADI ....   20 GRADI ....   40 GRADI ....",
    "40 GRADI ancora sul vetro -> la passata a 70 °C è stata rifiutata, come",
    "                             il datasheet descrive quando nessun range",
    "                             corrisponde: la sua durata breve non è una",
    "                             waveform veloce",
  };
  voceScheda(SCH_TEMP, "BANCHI PER TEMPERATURA",
             SCHEDA_TEMP, sizeof(SCHEDA_TEMP) / sizeof(SCHEDA_TEMP[0]));

  static const char* const SCHEDA_MUX[] =
  {
    "fascia nera in cima:  672 ....   336 ....   168 ....",
    "manca a un valore         -> a quel MUX il pannello non scandisce la",
    "                             fascia: valore troppo basso, o finestra RAM",
    "                             non riadattata",
  };
  voceScheda(SCH_MUX, "MUX RIDOTTO",
             SCHEDA_MUX, sizeof(SCHEDA_MUX) / sizeof(SCHEDA_MUX[0]));

  static const char* const SCHEDA_SLEEP[] =
  {
    "schermo NERO a fine test  -> il ciclo funziona e hibernate() è",
    "                             utilizzabile: dopo il risveglio il driver",
    "                             rifà init e riscrive i piani",
    "schermo non tornato       -> da lì non si torna: hibernate() lascerebbe",
    "                             il pannello morto fino al power cycle",
    "immagine sporcata         -> dormi solo dopo un refresh completo, mai a",
    "                             metà di una sequenza",
    "CORRENTE sul 3V3, l'unica cosa che il firmware non può misurare:",
    "  addormentato ....... mA      sveglio e fermo ....... mA",
    "i due valori coincidono   -> nessun risparmio: il guadagno vero sarebbe",
    "                             togliere alimentazione al pannello",
  };
  voceScheda(SCH_SLEEP, "DEEP SLEEP",
             SCHEDA_SLEEP, sizeof(SCHEDA_SLEEP) / sizeof(SCHEDA_SLEEP[0]));

  Serial.println();
  Serial.println(F("================================================"));
}

void loop()
{
  delay(60000);
}
