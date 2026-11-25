// =============================================================================
// Config.h — configurazione della sessione, limiti del silicio, input seriale.
//
// NIENTE QUI RICHIEDE UNA RICOMPILAZIONE. Una compilazione copre un'intera
// sessione di test: init, soft start, entry mode, MUX, 0x21, 0x22, LUT,
// tensioni, border, temperature, clock, timeout e modo bus si scelgono dal
// menu, a cui il test torna dopo ogni sonda. Un parametro che stesse in un
// #define costerebbe un riflash a metà sessione, ed è esattamente ciò che
// questo file esiste per evitare.
//
// Persistenza in NVS (namespace "dpf122", byte di versione in testa ai blob): la
// configurazione e gli esiti delle caratteristiche si salvano a ogni cambio e si
// rileggono al boot, così un riavvio a metà di un cablaggio non fa perdere il
// filo. Gli esiti riletti restano marcati come dichiarati e non come misurati,
// perchè non li ha prodotti questa esecuzione, e valgono come una misura ai fini
// dei prerequisiti. Il tasto z azzera anche la NVS; la voce "caratteristiche
// note" del menu resta il modo di ridichiarare a mano quello che si sa già.
//
// L'unica cosa che resta fisica è quale coda del pannello è infilata in quale
// connettore: quella si cambia con le dita, non col compilatore.
//
// I LIMITI SONO DEL SILICIO, non preferenze. Stanno qui accanto ai parametri
// che vincolano, con la citazione del datasheet, e li applicano i sottomenu più
// waveformGuard(): da un campo numerico libero si esce facilmente con una
// configurazione che non misura niente, o che sull'unica leva pericolosa —
// alzare VSH1 — rovina il film.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_CONFIG_H
#define DUAL_PANEL_FINDER_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <stdarg.h>

static const uint8_t TAIL_LUNGA = 1;
static const uint8_t TAIL_CORTA = 2;

/**
 * Connettore su cui parlano le primitive di bus: il primo (J1, CS 15 e BUSY 25) o
 * il secondo (CS 32 e BUSY 22). È una coppia (CS, BUSY) letta da csAssert(),
 * waitBusy() e da ogni sonda, così le stesse misure valgono per la coda lunga in
 * J1 e per la corta sulla breakout con la lunga ferma.
 */
static const uint8_t TARGET_J1     = 0;
static const uint8_t TARGET_SECOND = 1;

/**
 * Stato del cablaggio che la procedura guidata (tasto t) chiede all'operatore e
 * che il menu mostra: decide quali sonde hanno senso e se il secondo connettore
 * lavora in sola logica.
 */
static const uint8_t WIRING_UNKNOWN      = 0;   // non dichiarato
static const uint8_t WIRING_J1_ONLY      = 1;   // sola coda lunga in J1
static const uint8_t WIRING_SECOND_LOGIC = 2;   // coda corta sulla breakout, sola logica
static const uint8_t WIRING_CASCADE      = 3;   // ponte cascade fatto, CS unico
static const uint8_t WIRING_DUAL_CS      = 4;   // ponte a due CS fatto

/** Polarità del piano 0x24: la misura probeBwPolarity(), non si deduce. */
static const int8_t BW_UNKNOWN   = -1;   // non ancora osservata
static const int8_t BW_DATASHEET =  0;   // bit = 1 -> pixel BIANCO, Table 6-4
static const int8_t BW_INVERSE   =  1;   // bit = 1 -> pixel NERO

/**
 * Modo del bus MCU, scelto dal pin BS1 del controller (Table 5-2 del datasheet
 * SSD1677, per analogia: il die è sconosciuto): BS1 basso = 4 fili, BS1 alto = 3
 * fili a 9 bit. Sulla board Waveshare V3 BS1 è fisso a GND tramite R11 da 0 ohm,
 * quindi il chip in J1 resta a 4 fili qualunque cosa mandi l'ESP32 e la prova a 3
 * fili non discrimina niente; su una breakout invece BS1 è un pad libero che può
 * flottare, ed è lì che il modo bus torna a essere una variabile. Da qui il modo
 * come parametro invece che come assunzione.
 *
 * In 3 fili (§6.1.3) il D/C# "is not used and it must be tied to LOW" e il
 * frame è di 9 bit, con sequenza D/C# bit, D7, D6 ... D0: il primo bit dice se
 * il byte che segue è comando (0) o dato (1).
 */
enum BusMode : uint8_t
{
  BUS_4WIRE = 0,
  BUS_3WIRE = 1,
};

/**
 * Le sonde, una per bit: la maschera in Config dice quali eseguire, e il menu
 * la costruisce. Poterne eseguire una sola è quello che evita di rifare undici
 * minuti per rileggere una schermata.
 */
enum SondaBit : uint32_t
{
  S_CANDIDATE  = 1UL << 0,    // sweep elettrico delle candidate di init
  S_BIT7       = 1UL << 1,    // sonda elettrica del bit 7 dell'opcode
  S_MUX        = 1UL << 2,    // sweep del MUX, elettrico
  S_CLOCK      = 1UL << 3,    // costo del push ai tre clock
  S_IDENT      = 1UL << 4,    // frame di identificazione, uno per candidata
  S_POLARITA   = 1UL << 5,    // polarità del piano BW e opzioni RAM di 0x21
  S_BANDE      = 1UL << 6,    // 4 bande dei piani più i box a finestra parziale
  S_AREA       = 1UL << 7,    // partial d'area
  S_DIFF       = 1UL << 8,    // differenziale
  S_LUT        = 1UL << 9,    // partial con LUT custom e tensioni
  S_LIVELLI    = 1UL << 10,   // quarto colore per livello di sorgente
  S_SLAVE      = 1UL << 11,   // secondo controller a opcode|0x80
  S_TEMP       = 1UL << 12,   // banchi di waveform per temperatura
  S_PINGPONG   = 1UL << 13,   // RAM ping-pong via 0x37, in Display Mode 2
  S_SLEEP      = 1UL << 14,   // deep sleep
  S_DRIVER     = 1UL << 15,   // fase driver
  S_IMPRONTA   = 1UL << 16,   // impronta elettrica della coda, e confronto
  S_TREFILI    = 1UL << 17,   // SPI a 3 fili, inconcludente con BS1 a GND
  S_RESET      = 1UL << 18,   // reset a ritentativi
  S_DIE        = 1UL << 19,   // identità del die: MUX e finestra fuori specifica
  S_TB         = 1UL << 20,   // TB = 1, reverse scan gate in hardware
  S_ENTRY      = 1UL << 21,   // specchiatura via entry mode 0x11
  S_SEQ22      = 1UL << 22,   // bitmask degli stadi di 0x22
  S_TUTTE      = 0x007FFFFFUL,
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
static const uint8_t MUX_PASSES = 4;

// ===== Limiti del silicio, dal datasheet ==============================
//
// SSD1677 Rev 1.0, usato per analogia: il controller del 12.2" è sconosciuto e
// non è dimostrato che sia quel chip. Ogni riga è un vincolo del datasheet, non
// una preferenza, e il menu li applica come clamp.

/**
 * MUX, §8.1: "Multiplex ratio (MUX ratio) from 300 MUX to 680 MUX", A[9:0]+1
 * linee. Il tetto del MENU è però il massimo del REGISTRO, 1024, e non i 680
 * del chip: la sonda dell'identità del die deve poter chiedere 768 linee, cioè
 * uscire di proposito dalla specifica, perchè è così che si scopre se il die è
 * davvero una Rev 1.0. Non si tocca nessuna tensione facendolo, e chiedere più
 * linee di quante ne esistano è un overflow del contatore di scansione: il 9.7"
 * ha già girato a MUX 167 e 335, cioè SOTTO il pavimento dichiarato, senza
 * danni.
 */
static const uint16_t MUX_SPEC_MIN = 300;
static const uint16_t MUX_SPEC_MAX = 680;
static const uint16_t MUX_REG_MAX  = 1024;

/**
 * Finestra RAM in Y, §8.4: "The settings follow the condition on 00h <=
 * YSA[9:0], YEA[9:0] <= 2A7h". La riga 679 è l'ultima indirizzabile, e con
 * l'asse gate del 12.2" a 768 px è il vincolo che rende i due controller
 * obbligatori. La sonda del die prova anche oltre, per lo stesso motivo del MUX.
 */
static const uint16_t YEA_SPEC_MAX = 679;

/**
 * Tensioni di sorgente, tabelle di 0x04. Sono i POR, e sono anche il tetto:
 * alzare VSH1 sopra i 15 V è la sola leva che può danneggiare il film in modo
 * permanente, e senza la waveform del produttore non c'è modo di sapere quanto
 * margine ci sia. Riportare una tensione al POR non è invece uscire dai default
 * del chip: sono i valori a cui questo pannello ha già girato.
 */
static const uint8_t VSH1_POR = 0x41;   // 15 V, POR e massimo consentito
static const uint8_t VSH2_POR = 0xA8;   //  5 V
static const uint8_t VSL_POR  = 0x32;   // -15 V

/**
 * Temperatura di esercizio dichiarata dal produttore per il pannello BWRY:
 * 0 ~ 40 °C (datasheet SOLUM Newton PRO §3.1). Lo zero è il PAVIMENTO DI
 * SPECIFICA, non un estremo, e allo zero il 9.7" sullo stesso silicio ha
 * misurato 59067 ms di refresh: è il caso peggiore in specifica che
 * full_refresh_time e busy_timeout del driver devono coprire.
 */
static const int16_t PANEL_TEMP_MIN = 0;
static const int16_t PANEL_TEMP_MAX = 40;

/** Clock SPI: "MCU interface: SPI serial peripheral, Maximum 20MHz for write". */
static const uint32_t SPI_HZ_MIN = 100000;
static const uint32_t SPI_HZ_MAX = 20000000;

struct Config
{
  uint8_t  coda;        // TAIL_LUNGA o TAIL_CORTA, la coda infilata in J1
  bool     secondFfc;   // il secondo connettore FFC è cablato
  bool     esaustivo;   // nessuna passata condizionale viene saltata
  bool     verbose;     // righe di dettaglio sul seriale
  int8_t   bwPolarity;  // BW_UNKNOWN finché la sonda non la determina
  uint32_t sonde;       // maschera di SondaBit

  /**
   * Parametri di misura: i numeri che le sonde spazzano. Stanno qui perchè
   * cambiarli non deve costare una ricompilazione.
   */
  int16_t  temp[TEMP_PASSES];   // gradi forzati via 0x18 / 0x1A
  uint16_t mux[MUX_PASSES];     // gate line programmate in 0x01, 0 = non scritto
  uint16_t gate;                // gate line per controller: l'altezza di banda in prova
  uint32_t spiBase;             // clock delle sonde
  uint32_t spiFast;             // clock della metà bassa nel frame di verifica
  uint32_t spiDriver;           // clock della fase driver
  uint32_t timeoutMs;           // timeout di default di un refresh
  uint32_t sleepMs;             // finestra ferma per il multimetro

  /**
   * Registri che le sonde nuove spazzano, e che prima erano cablati nel
   * sorgente di una sequenza.
   */
  uint8_t  busMode;       // BUS_4WIRE o BUS_3WIRE
  uint8_t  softLevel;     // 5° byte di 0x0C: 0x40 Level 1, 0x80 Level 2
  uint8_t  entryMode;     // parametro di 0x11
  bool     tbReverse;     // 0x01 B[0]: reverse scan gate, Reserved sul 1677
  uint8_t  reg21A;        // 1° byte di 0x21: opzioni RAM Red e BW
  bool     reg21Due;      // manda anche il 2° byte (esiste solo dal 1683)
  uint8_t  reg21B;        // 2° byte: B[4] ckouten, cascade selection
  uint8_t  reg21Dove;     // 0 mai, 1 nell'init, 2 prima di 0x22
  uint8_t  resetLowMs;    // durata dell'impulso di reset
  uint8_t  resetTentativi;// quanti reset a durata crescente prima di rinunciare
  uint8_t  lutBorder;     // parametro di 0x3C sotto LUT custom, POR 0xC0 = HiZ
  bool     lutTensioni;   // manda 0x04 con i POR dopo 0x32
  bool     lutSwreset;    // fa uno SWRESET prima di 0x32, invece di 0x04
  uint8_t  lutVsh1;       // tensioni della LUT, clampate ai POR da waveformGuard
  uint8_t  lutVsh2;
  uint8_t  lutVsl;

  /**
   * Cablaggio e bersaglio del bus, in coda alla struct perchè il default
   * posizionale sotto li elenca nello stesso ordine.
   */
  uint8_t  wiring;        // WIRING_*, lo stato dichiarato dalla procedura guidata
  uint8_t  busTarget;     // TARGET_J1 o TARGET_SECOND
};

/**
 * Valori di partenza della sessione, da cui il menu parte. La fase driver sta
 * nella maschera come le altre sonde, e la maschera parte con TUTTE selezionate:
 * il default esegue la suite intera.
 */
static const Config CFG_DEFAULT =
{
  TAIL_CORTA,     // coda
  false,          // secondo FFC non cablato
  true,           // esaustivo
  false,          // dettagli spenti
  BW_UNKNOWN,     // polarità da misurare
  S_TUTTE,        // tutte le sonde
  { 0, 20, 40, 70 },          // temperature forzate: 0 è il pavimento di specifica
  { 384, 679, 767, MUX_NOT_WRITTEN },  // MUX: banda, tetto del chip, fuori specifica, OTP
  384,            // gate per controller in prova
  4000000, 20000000, 10000000,
  40000,          // timeout di un refresh
  20000,          // finestra del multimetro
  BUS_4WIRE,      // modo bus
  0x80,           // soft start Level 2, come l'init di fabbrica SOLUM
  0x03,           // entry mode POR: Y e X incrementanti
  false,          // TB al POR
  0x00,           // 0x21 A: entrambi i piani Normal, che è il POR
  false,          // un solo byte, come la Rev 1.0 dichiara
  0x10,           // 2° byte: B[4] ckouten = 1, se lo si manda
  0,              // e per default non lo si manda affatto
  10,             // reset basso per 10 ms
  1,              // un tentativo solo
  0xC0,           // border HiZ, il POR
  true,           // manda 0x04 coi POR dopo la LUT
  false,          // senza passare da uno SWRESET
  VSH1_POR, VSH2_POR, VSL_POR,
  WIRING_UNKNOWN, // cablaggio non dichiarato
  TARGET_J1,      // le primitive di bus parlano al connettore J1
};

static Config cfg = CFG_DEFAULT;

/**
 * Modo "sola logica": vale quando le primitive parlano al secondo connettore e il
 * cablaggio dichiarato è la coda corta con la sola logica. Con i rail ad alta
 * tensione non ponticellati i regolatori dello slave non hanno condensatori, quindi
 * niente 0x22 con bit analogici seguito da 0x20: restano SWRESET, 0x34 e 0x47/0x46.
 */
static inline bool logicOnly()
{
  return (cfg.wiring == WIRING_SECOND_LOGIC) && (cfg.busTarget == TARGET_SECOND);
}

/**
 * Riporta a un valore sicuro qualunque tensione oltre il POR. Chiamata prima di
 * ogni carico di waveform: il menu clampa già, ma una sonda che compone una
 * LUT byte per byte potrebbe arrivarci per un'altra strada, e questa è l'unica
 * leva del test che può fare un danno permanente. Ritorna false se ha corretto
 * qualcosa, così il chiamante lo mette nel log.
 */
static bool waveformGuard()
{
  bool ok = true;
  if (cfg.lutVsh1 > VSH1_POR) { cfg.lutVsh1 = VSH1_POR; ok = false; }
  if (cfg.lutVsh2 > VSH2_POR) { cfg.lutVsh2 = VSH2_POR; ok = false; }
  if (cfg.lutVsl  > VSL_POR)  { cfg.lutVsl  = VSL_POR;  ok = false; }
  return ok;
}

// ---------------------------------------------------------------------------
// PERSISTENZA IN NVS
//
// Un solo namespace e due blob, uno per la configurazione (qui) e uno per gli
// esiti delle caratteristiche (Report.h). Il byte di versione in testa serve a
// scartare un profilo scritto da una build con una struct diversa invece di
// rileggerlo come se fosse compatibile: va incrementato ogni volta che cambia
// Config, Esito o l'enum Caratteristica.
// ---------------------------------------------------------------------------
static const char*   NVS_NAMESPACE    = "dpf122";
static const uint8_t NVS_BLOB_VERSION = 1;

static Preferences prefs;

/**
 * Controlla che un profilo riletto dalla NVS abbia valori nei range che il resto
 * dello sketch assume, perchè un indice di enum fuori tabella finirebbe in un
 * accesso fuori array. Ritorna false se anche un solo campo è fuori range.
 */
static bool configLooksValid(const Config& c)
{
  return (c.coda == TAIL_LUNGA || c.coda == TAIL_CORTA)
         && c.wiring <= WIRING_DUAL_CS
         && c.busTarget <= TARGET_SECOND
         && c.busMode <= BUS_3WIRE
         && c.gate >= 8 && c.gate <= MUX_REG_MAX
         && c.reg21Dove <= 2
         && c.resetTentativi >= 1 && c.resetLowMs >= 1
         && c.spiBase >= SPI_HZ_MIN && c.spiBase <= SPI_HZ_MAX
         && c.spiFast >= SPI_HZ_MIN && c.spiFast <= SPI_HZ_MAX
         && c.spiDriver >= SPI_HZ_MIN && c.spiDriver <= SPI_HZ_MAX;
}

/**
 * Rilegge la configurazione salvata. Un profilo assente, di un'altra versione, di
 * un'altra taglia o fuori range lascia i valori di fabbrica già in cfg.
 */
static void loadConfig()
{
  uint8_t blob[1 + sizeof(Config)];
  prefs.begin(NVS_NAMESPACE, false);
  const size_t n = prefs.getBytes("cfg", blob, sizeof(blob));
  prefs.end();
  if (n != sizeof(blob) || blob[0] != NVS_BLOB_VERSION) return;

  Config letta;
  memcpy(&letta, blob + 1, sizeof(Config));
  if (configLooksValid(letta)) cfg = letta;
}

/**
 * Salva la configurazione corrente, e solo se è cambiata: si chiama dopo ogni
 * tasto del menu, e riscrivere a ogni pressione consumerebbe cicli di scrittura
 * della flash per niente.
 */
static void saveConfig()
{
  uint8_t blob[1 + sizeof(Config)];
  blob[0] = NVS_BLOB_VERSION;
  memcpy(blob + 1, &cfg, sizeof(Config));

  uint8_t salvato[sizeof(blob)];
  prefs.begin(NVS_NAMESPACE, false);
  const size_t n = prefs.getBytes("cfg", salvato, sizeof(salvato));
  if (n != sizeof(blob) || memcmp(salvato, blob, sizeof(blob)) != 0)
    prefs.putBytes("cfg", blob, sizeof(blob));
  prefs.end();
}

/** Cancella tutto il namespace, esiti compresi, e riporta cfg ai valori di fabbrica. */
static void resetConfig()
{
  prefs.begin(NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  cfg = CFG_DEFAULT;
}

// ---------------------------------------------------------------------------
// INPUT DAL MONITOR SERIALE
//
// Ogni domanda ASPETTA: nessuna scade e nessuna si risponde da sè. Un frame di
// questo pannello costa ~19 s e la schermata successiva lo cancella, quindi una
// risposta presa allo scadere di un timeout vale come una schermata guardata da
// nessuno. INVIO a vuoto resta la risposta rapida "tieni quello che c'è".
//
// Niente qui tocca l'output. Si legge solo il buffer di RICEZIONE, e solo nei
// punti in cui il test pone una domanda; ogni risposta viene ristampata, così
// il file di log spiega da sè come è stato configurato il run.
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
 * Legge una riga dal seriale e aspetta quanto serve. Ritorna i caratteri letti,
 * zero per un INVIO a vuoto, che vale "tieni il preimpostato". Accetta CR, LF o
 * entrambi come fine riga.
 */
static int leggiRiga(char* buf, uint8_t max)
{
  uint8_t n = 0;
  while (true)
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
      break;
    }
    if (n < max - 1) buf[n++] = (char)c;
  }
  buf[n] = '\0';
  return (int)n;
}

/**
 * Aspetta un tasto qualsiasi, senza pretendere l'INVIO. Serve al gate fra due
 * schermate, dove non c'è niente da digitare: vedi attendiOperatore().
 */
static void attendiTasto()
{
  scartaInputPendente();
  while (!Serial.available()) delay(10);
  scartaInputPendente();
}

/** Domanda sì / no. INVIO a vuoto tiene il preimpostato. */
static bool chiediSN(const char* domanda, bool preimpostato)
{
  scartaInputPendente();
  Serial.printf("%s [s/n, invio = %s]\n", domanda, preimpostato ? "s" : "n");
  char buf[8];
  const int n = leggiRiga(buf, sizeof(buf));
  bool esito = preimpostato;
  if (n > 0 && (buf[0] == 's' || buf[0] == 'S' || buf[0] == 'y' || buf[0] == 'Y')) esito = true;
  else if (n > 0 && (buf[0] == 'n' || buf[0] == 'N'))                              esito = false;
  Serial.printf("  risposta: %s\n", esito ? "sì" : "no");
  return esito;
}

/**
 * Domanda a scelta multipla, voci numerate da 1. Ritorna l'indice scelto
 * (0-based), o il preimpostato con un INVIO a vuoto o una risposta fuori range.
 */
static int chiediScelta(const char* domanda, const char* const* voci, int quante,
                        int preimpostato)
{
  scartaInputPendente();
  Serial.printf("%s\n", domanda);
  for (int i = 0; i < quante; ++i)
    Serial.printf("   %d) %s%s\n", i + 1, voci[i], i == preimpostato ? "   <- attuale" : "");
  Serial.printf("  [1-%d, invio = %d]\n", quante, preimpostato + 1);

  char buf[8];
  const int n = leggiRiga(buf, sizeof(buf));
  int scelta = preimpostato;
  if (n > 0)
  {
    const int v = atoi(buf);
    if (v >= 1 && v <= quante) scelta = v - 1;
    else Serial.println(F("  fuori range: tengo l'attuale"));
  }
  Serial.printf("  risposta: %s\n", voci[scelta]);
  return scelta;
}

/**
 * Domanda numerica con limiti. Un INVIO a vuoto o una risposta fuori range
 * tengono il preimpostato: i limiti li impone il menu, perchè da un campo
 * numerico libero si esce facilmente con una configurazione che non misura
 * niente.
 */
static long chiediNumero(const char* domanda, long minimo, long massimo,
                         long preimpostato)
{
  scartaInputPendente();
  Serial.printf("%s [%ld..%ld, invio = %ld]\n", domanda, minimo, massimo, preimpostato);
  char buf[16];
  const int n = leggiRiga(buf, sizeof(buf));
  long v = preimpostato;
  if (n > 0)
  {
    char* fine = nullptr;
    const long letto = strtol(buf, &fine, 0);
    if (fine == buf)                            Serial.println(F("  non è un numero: tengo l'attuale"));
    else if (letto < minimo || letto > massimo) Serial.println(F("  fuori range: tengo l'attuale"));
    else v = letto;
  }
  Serial.printf("  risposta: %ld\n", v);
  return v;
}
// ---------------------------------------------------------------------
// PIN — Waveshare E-Paper ESP32 Driver Board V3
//   Sono il cablaggio, non impostazioni: si cambiano solo se il tuo è
//   diverso da quello di docs/122c/cable_linking.html.
// ---------------------------------------------------------------------
// Segnali condivisi. La board usa i pin HSPI con SCK e MOSI scambiati, da cui
// il remap obbligatorio in hspi.begin(). MISO è un dummy: il FPC del
// connettore non ha SDO.
static const int PIN_DC   = 27;
static const int PIN_RST  = 26;
static const int PIN_SCK  = 13;
static const int PIN_MISO = 12;
static const int PIN_MOSI = 14;

// Il primo connettore FFC della board, J1.
static const int PIN_CS   = 15;
static const int PIN_BUSY = 25;

/**
 * Il secondo connettore, sulla breakout della coda corta, usato quando
 * cfg.secondFfc è vero.
 *
 * CS su GPIO32 e non 33: la netlist dello schematico mette GPIO33 sull'header
 * J3-8 (da confermare sulla serigrafia), quindi non è assente ma sta su un pin
 * dell'header che il cablaggio di questo test non usa. BUSY su GPIO22, che ha il
 * pull interno: configurato INPUT_PULLDOWN, un BUSY scollegato o aperto si legge
 * "libero" e non manda ogni attesa in timeout, e la prova "pilotato o flottante"
 * con entrambi i pull funziona anche su questo pin (sui GPIO 34..39 no).
 */
static const int PIN_CS_OTHER   = 32;
static const int PIN_BUSY_OTHER = 22;

/** Chip select del connettore bersaglio delle primitive di bus. */
static inline int csPin()
{
  return (cfg.busTarget == TARGET_SECOND) ? PIN_CS_OTHER : PIN_CS;
}

/** BUSY del connettore bersaglio delle primitive di bus. */
static inline int busyPin()
{
  return (cfg.busTarget == TARGET_SECOND) ? PIN_BUSY_OTHER : PIN_BUSY;
}

/** Chip select dell'altro connettore, quello che il bersaglio non seleziona. */
static inline int otherCsPin()
{
  return (cfg.busTarget == TARGET_SECOND) ? PIN_CS : PIN_CS_OTHER;
}

/** BUSY dell'altro connettore, letto come testimone durante un refresh. */
static inline int otherBusyPin()
{
  return (cfg.busTarget == TARGET_SECOND) ? PIN_BUSY : PIN_BUSY_OTHER;
}

/**
 * Modo di riposo di un pin BUSY: nudo su J1, con il pull-down interno sul secondo
 * connettore. Ogni punto che configura il BUSY del secondo connettore passa da
 * qui, così non resta mai in ingresso senza pull dopo una lettura con i due pull.
 */
static inline uint8_t busyRestMode(int pin)
{
  return (pin == PIN_BUSY_OTHER) ? INPUT_PULLDOWN : INPUT;
}

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

// ===== Costanti del pannello e del silicio, non impostazioni =========
// Sono fatti misurati o dichiarati dal produttore: cambiarli qui non
// riconfigura la prova, la falsa.
//
// Geometria, datasheet SOLUM Newton PRO §3.1, letta su tre taglie:
//   9.7"   672 x 960 px / 141.1 x 201.6 mm    672 gate    una coda
//   11.6"  640 x 960 px / 163.0 x 244.5 mm    640 gate    una coda
//   12.2"  768 x 960 px / 190.1 x 237.6 mm    768 gate    DUE code
// Il pitch è 0,2475 mm su tutte e tre, e solo il 12.2" sfora i 680 gate
// dell'SSD1677: è anche l'unico con due COF. La correlazione è completa, e i
// 960 px stanno sull'asse delle source, cioè sul bordo su cui ogni COF è
// bondato; l'asse da 768 si divide in due bande di gate, una per controller.
static const uint16_t SRC       = 960;           // source sull'asse RAM X
static const uint16_t ROW_BYTES = SRC / 8;       // 120 byte per riga piena
static const uint16_t PART_ROW_MAX = ROW_BYTES;  // buffer di riga più grande possibile

// Sul SSD16xx il BUSY è attivo alto: "When Busy is High, the operation of the
// chip should not be interrupted" (Table 5-2). È anche il busy_level = HIGH del
// driver, ed è il discriminante che esclude la famiglia UC8179.
static const int BUSY_ACTIVE = HIGH;

/**
 * Esiti negativi di waitBusy(). Sono distinti perchè dicono cose opposte: un BUSY
 * che non è mai salito è un controller che non ha preso il comando, un BUSY che
 * non è sceso al timeout è un controller che sta lavorando o un pin fisso alto.
 * Nessuno dei due è una durata: chi stampa "alto per N ms" o legge un valore
 * >= 0 come "ha risposto" deve escluderli.
 */
static const int32_t BUSY_NEVER_ROSE  = -1;
static const int32_t BUSY_STILL_HIGH  = -3;

/** Finestra in cui il BUSY deve salire dopo un comando, prima di dichiararlo muto. */
static const uint32_t BUSY_RISE_WINDOW_MS = 20;

// ===== Le candidate di init, come dato e non come #define ============
/**
 * Erano tre ricompilazioni, ora sono un array su cui il test itera. Quello che
 * cambia fra loro è quali registri l'init scrive; il pannello sta sullo stesso
 * silicio, quindi la domanda è quale sequenza il driver deve tenere.
 *
 *   CAND_MINIMAL  stile GxEPD2_1160c_GDEY116Z91: SWRESET e border, tutto il
 *                 resto ai default OTP. Il MUX non lo scrive.
 *   CAND_SOLUM    stile del driver 9.7" di questa libreria: soft start, MUX,
 *                 border, sensore, entry mode. È quella che il driver 12.2"
 *                 implementa oggi in _InitDisplay().
 *   CAND_OEPL     init di fabbrica OEPL per il 9.7" SOLUM: parte dai pattern
 *                 hardware e chiude con 0x21 = 0x08 0x00, che in quel codice
 *                 raddrizza un'immagine altrimenti ribaltata.
 *
 * Le tre che seguono le ha aggiunte l'audit del driver base, e ognuna porta
 * qualcosa che le prime tre non hanno:
 *
 *   CAND_1330C    GxEPD2_1330c_GDEM133Z91, il gemello 3C dell'unico driver
 *                 upstream che gira davvero a 960x680 con il MUX programmato:
 *                 è il command set SSD1677 a 960 source nella sua forma
 *                 canonica, ed è la fonte giusta per soft start e border.
 *   CAND_1160T91  GxEPD2_1160_T91, l'unico a 960 source che in init fa il ciclo
 *                 0x22 = 0xB1 + 0x20, cioè carica temperatura e waveform
 *                 dall'OTP PRIMA del primo frame, ed è anche l'unico con un
 *                 _Init_Part.
 *   CAND_PERCS    init a controller indipendenti: nessun broadcast, ogni chip
 *                 inizializzato sul PROPRIO chip select. Il secondo giro
 *                 seleziona il solo secondo connettore, manda il proprio
 *                 SWRESET e misura la salita del BUSY su GPIO22: è la misura
 *                 con cui si distingue un controller che risponde da solo da
 *                 uno slave in cascade o da un chip muto.
 */
enum InitCand : uint8_t
{
  CAND_MINIMAL  = 0,
  CAND_SOLUM    = 1,
  CAND_OEPL     = 2,
  CAND_1330C    = 3,
  CAND_1160T91  = 4,
  CAND_PERCS    = 5,
  CAND_COUNT    = 6
};

static const char* const CAND_LABEL[CAND_COUNT] =
{
  "MINIMAL  (SWRESET + border, resto ai default OTP)",
  "SOLUM    (soft start + MUX + border + sensore + entry mode)",
  "OEPL     (pattern hardware + MUX + 0x21 = 08 00)",
  "1330c    (command set SSD1677 a 960 source, canonico)",
  "1160_T91 (come 1330c piu' il ciclo 0x22 = 0xB1 + 0x20)",
  "per-CS   (ogni controller sul proprio CS, niente broadcast)"
};

// La candidata su cui il driver custom è modellato: è quella che il test usa
// per i frame che non stanno spazzando le candidate.
static const uint8_t CAND_DRIVER = CAND_SOLUM;

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

/**
 * Coda sotto test: sul secondo connettore è per definizione la corta, su J1 quella
 * che l'operatore ha dichiarato con il tasto c. È anche la colonna dell'impronta
 * in cui la sonda 4 scrive.
 */
static inline uint8_t activeTail()
{
  return (cfg.busTarget == TARGET_SECOND) ? TAIL_CORTA : cfg.coda;
}

/** Etichetta della coda per il report. */
static const char* tailLabel()
{
  if (cfg.busTarget == TARGET_SECOND)
    return "coda CORTA sul secondo connettore (CS=32, BUSY=22)";
  return (cfg.coda == TAIL_LUNGA)
         ? "coda LUNGA nel connettore J1 della board (CS=15, BUSY=25)"
         : "coda CORTA nel connettore J1 della board (CS=15, BUSY=25)";
}

/** Etichetta del cablaggio dichiarato, per il menu e per il log. */
static const char* wiringLabel()
{
  switch (cfg.wiring)
  {
    case WIRING_J1_ONLY:      return "sola lunga in J1";
    case WIRING_SECOND_LOGIC: return "corta su breakout, sola logica";
    case WIRING_CASCADE:      return "ponte cascade";
    case WIRING_DUAL_CS:      return "ponte due CS";
    default:                  return "non dichiarato";
  }
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

#endif // DUAL_PANEL_FINDER_CONFIG_H
