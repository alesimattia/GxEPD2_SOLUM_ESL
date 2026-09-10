// =============================================================================
// Report.h — frame numerati, gate fra due schermate, registro delle misure e
// log a una riga per fatto.
//
// IL LOG È IL PRODOTTO DI QUESTA SUITE: insieme a quello che si vede sul vetro
// deve bastare a correggere src/GxEPD2_SOLUM_122c_960x768.h. Da qui due scelte.
//
// UNA RIGA PER FATTO, con il tag in colonna fissa, così il log si legge a colpo
// d'occhio e si filtra con grep:
//
//   == 9  4 bande dei piani piu' i box (1 refresh) ==
//   [12] MEAS  4 BANDE E BOX                 22=F7   19012 ms
//   [12] guarda: la quarta banda che colore rende?
//   [12]   1) come la banda dell'accent, nessun colore nuovo
//   [12]   2) un colore distinto dalle altre tre
//   [12] SEEN  1
//   [12] VERDICT tre colori: le primitive del terzo piano restano senza corpo
//
// Le motivazioni non stanno sul seriale ma nei commenti e nel README: un run
// completo produce una trentina di refresh, e un paragrafo per sonda rende il
// log illeggibile proprio dove servono i numeri. Il toggle `v` accende le righe
// di dettaglio, spento di default.
//
// IL NUMERO STA SUL FRAME, non sulla sonda: con una trentina di refresh e una
// ventina di sonde che riusano le stesse fasce, sapere quale passata si sta
// guardando è l'unico modo di attribuire un'osservazione alla riga di log
// giusta. Una sonda con quattro passate produce quindi quattro numeri, non uno,
// e lo stesso numero compare nel riquadro in alto a destra dell'area ridipinta.
//
// PERCHÈ IL REGISTRO DELLE MISURE NON SOSTITUISCE GLI ARRAY PER SONDA. Il 9.7"
// ha un registro unico e interroga per prefisso della didascalia; qui gli array
// di risultato per sonda restano, perchè la scheda per il driver li confronta
// PER INDICE — il rapporto fra due valori di MUX, fra due passate d'area, fra
// due candidate — e una ricerca per prefisso su didascalie composte a runtime
// sarebbe più fragile, non più semplice. Il registro serve al log e alla
// tabella cronologica; gli array ai confronti.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef DUAL_PANEL_FINDER_REPORT_H
#define DUAL_PANEL_FINDER_REPORT_H

#include <Arduino.h>
#include <stdarg.h>
#include "Config.h"

// ---------------------------------------------------------------------------
// TIPI DELLA SONDA
//
// Stanno qui, sopra la prima funzione del file, perchè il preprocessore di
// Arduino inserisce i prototipi generati proprio lì: un tipo definito più in
// basso non sarebbe visibile a quei prototipi. È la stessa ragione per cui
// enum Schermata è dichiarato in anticipo in testa al file.
// ---------------------------------------------------------------------------

/**
 * Come il riquadro del numero va scritto nei due piani.
 *   BADGE_NORMALE       0x24 e 0x26 complementari: è il riquadro leggibile, e
 *                       fra i due piani introduce una differenza.
 *   BADGE_PIANI_UGUALI  lo stesso pattern nei due piani. Serve alla passata a
 *                       differenza zero della differenziale, dove una qualsiasi
 *                       differenza fra i piani falserebbe la misura: scritto
 *                       identico, la differenza resta zero e il frame ha
 *                       comunque il suo numero sul vetro.
 */
enum ModoBadge : uint8_t
{
  BADGE_NORMALE,
  BADGE_PIANI_UGUALI
};

/**
 * Le caratteristiche del pannello che il bring-up deve determinare, cioè quelle
 * da cui dipende una riga del driver custom. Sono lo stato noto della sessione:
 * le riempie la risposta dell'operatore al gate di un frame, oppure la voce di
 * menu che le dichiara.
 */
enum Caratteristica : uint8_t
{
  C_POLARITA,   // 0x24 bit = 1 è bianco o nero
  C_META,       // quale metà del pannello dipinge la coda infilata
  C_MIRROR,     // specchiature della banda, -> setMasterMirror / setSlaveMirror
  C_QUARTO,     // esiste un quarto stato sul film
  C_PARTIAL,    // una leva accorcia il refresh e dipinge
  C_SLAVE,      // il secondo controller esegue gli opcode offset
  C_COUNT
};

/** Esito di una caratteristica nella sessione corrente. */
struct Esito
{
  int8_t  risposta;     // -1 = non determinata, altrimenti indice della voce
  uint8_t frame;        // frame che l'ha decisa, 0 = dichiarata dal menu
  bool    dichiarata;   // true = detta dall'operatore, non misurata qui
};

/**
 * Le domande che il gate pone. Solo dove la risposta cambia una riga del
 * driver: sugli altri frame il gate aspetta un tasto e basta, perchè una
 * domanda con una sola risposta plausibile è rumore.
 */
enum Domanda : uint8_t
{
  D_NESSUNA = 0,
  D_META,       // quale metà ha dipinto: una per frame di identificazione
  D_QUARTO,     // colore della quarta combinazione dei piani
  D_DIPINTO,    // l'area dentro la finestra è cambiata? alimenta C_PARTIAL
  D_SLAVE,      // ha eseguito il secondo controller
  D_COUNT
};
/**
 * Non tutte le caratteristiche passano da qui: la polarità del piano BW e le
 * specchiature le chiedono le sonde che le misurano, con domande proprie e più
 * ricche di una scelta secca, e registrano l'esito con registraEsito(). Il gate
 * pone le domande che stanno bene su un frame solo.
 */

/**
 * Un frame osservabile: quello che runRefresh() serve a far comparire sul
 * vetro. L'etichetta è la narrazione della passata e finisce sia nel registro
 * dei frame sia nel gate che annuncia il frame successivo.
 */
struct Frame
{
  const char* etichetta;
  ModoBadge   badge;
  Domanda     domanda;
  uint8_t     opcodeOffset;   // 0x80 per i frame scritti sullo slave
  bool        senzaBadge;     // la waveform di questa passata non lo piloterebbe
};

/**
 * Le sonde che producono frame: servono ad attribuire i frame e a far citare
 * alla scheda finale l'intervallo di numeri di ciascuna.
 */
enum Schermata : uint8_t
{
  SCH_IDENT,       // frame di identificazione, uno per candidata di init
  SCH_POL,         // polarità del piano BW, due frame
  SCH_BANDE,       // 4 bande dei piani più i box a finestra parziale
  SCH_AREA,        // partial d'area
  SCH_DIFF_DEEP,   // differenziale approfondita
  SCH_LUT,         // partial con LUT caricata via 0x32
  SCH_LIVELLI,     // quarto colore per livello di sorgente
  SCH_SLAVE,       // secondo controller a opcode|0x80
  SCH_TEMP,        // banchi di waveform per temperatura
  SCH_PINGPONG,    // RAM ping-pong via 0x37
  SCH_SLEEP,       // refresh di prova dopo il risveglio dal deep sleep
  SCH_DRIVER,      // fase driver, un frame per modello di indirizzamento
  SCH_COUNT
};

/** Di cosa una sonda ha bisogno per misurare qualcosa invece che niente. */
enum Prereq : uint8_t
{
  P_BANCO     = 1 << 0,   // pin, POR e livello del BUSY a riposo
  P_INIT      = 1 << 1,   // pannello inizializzato con la candidata del driver
  P_CANDIDATE = 1 << 2,   // sweep delle candidate: serve candProbe[].vivo
  P_POLARITA  = 1 << 4,   // polarità del piano BW determinata o dichiarata
  P_FFC2      = 1 << 5,   // secondo connettore FFC cablato
};

struct Sonda
{
  char        tasto;       // tasto del menu, unico: lo garantisce uno static_assert
  uint32_t    bit;         // SondaBit
  Schermata   sonda;       // a chi attribuire i frame, SCH_COUNT se non ne produce
  const char* nome;
  uint8_t     refresh;     // refresh previsti, per il preventivo del banner
  uint8_t     prereq;
  void      (*esegui)();
};

/** Frame osservabile senza domanda, il caso normale. */
static inline Frame frame(const char* etichetta)
{
  return Frame{ etichetta, BADGE_NORMALE, D_NESSUNA, 0x00, false };
}

/** Frame osservabile che chiede il suo esito. */
static inline Frame frameCon(const char* etichetta, Domanda domanda)
{
  return Frame{ etichetta, BADGE_NORMALE, domanda, 0x00, false };
}

/**
 * Frame il cui riquadro non arriverebbe sul vetro, perchè la waveform della
 * passata non pilota i pixel del riquadro: è il caso del probe dei livelli di
 * sorgente, dove LUT0 e LUT1 sono a zero. Scriverlo comunque lascerebbe sul
 * vetro il numero della schermata PRECEDENTE, cioè l'associazione sbagliata;
 * il gate dice quindi quale numero è visibile e quale blocco lo commenta.
 */
static inline Frame frameSenzaBadge(const char* etichetta, Domanda domanda)
{
  return Frame{ etichetta, BADGE_NORMALE, domanda, 0x00, true };
}
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
// ---------------------------------------------------------------------------
// FRAME NUMERATI
//
// Il numero lega il log al vetro, e sta sul FRAME e non sulla sonda: con una
// trentina di refresh e una decina di sonde che riusano le stesse fasce, sapere
// quale passata si sta guardando è l'unico modo di attribuire un'osservazione
// alla riga di log giusta. Una sonda con quattro passate produce quindi quattro
// numeri, non uno.
//
// Il numero lo assegna runRefresh(), che è il punto da cui passa ogni frame e
// che il riquadro lo disegna già: assegnarlo altrove vorrebbe dire assegnarlo
// più volte per frame o mai.
// ---------------------------------------------------------------------------


/** Un frame prodotto nella sessione, come la scheda finale lo indicizza. */
struct FrameOsservato
{
  uint8_t numero;
  uint8_t sonda;          // Schermata che lo ha prodotto
  char    etichetta[48];
  Domanda domanda;
  int8_t  risposta;       // -1 = non chiesta
  int32_t ms;             // durata del refresh, -1 se non misurata
  bool    senzaBadge;     // il suo numero non è arrivato sul vetro
};

static const uint8_t FRAME_MAX = 64;
static FrameOsservato registroFrame[FRAME_MAX];
static uint8_t registroFrameN = 0;

static uint8_t contatoreFrame  = 0;            // numeri assegnati, anche oltre FRAME_MAX
static uint8_t sondaCorrente   = SCH_COUNT;    // sonda in esecuzione
static uint8_t numeroSulVetro  = 0;            // ultimo frame dipinto
static uint8_t numeroRiquadro  = 0;            // ultimo riquadro arrivato sul vetro
static uint8_t frameInAttesa   = 0;            // indice+1 del frame che attende il gate
static uint8_t framePrenotato  = 0;            // indice+1 del frame scritto e non ancora dipinto
static uint8_t numeroPrenotato = 0;            // suo numero, anche col registro pieno

/**
 * Stato noto della sessione: una voce per caratteristica, riempita dalla
 * risposta dell'operatore a un frame o dichiarata dal menu. Niente NVS, quindi
 * dura quanto la sessione; la voce "caratteristiche note" del menu è il modo di
 * ridichiarare quello che si sa già.
 */
static Esito esiti[C_COUNT] =
{
  { -1, 0, false }, { -1, 0, false }, { -1, 0, false },
  { -1, 0, false }, { -1, 0, false }, { -1, 0, false },
};
static_assert(C_COUNT == 6, "esiti[] va esteso insieme a enum Caratteristica");

/** Nome di ogni caratteristica, per il menu, il banner e la scheda. */
static const char* const NOME_CARATTERISTICA[C_COUNT] =
{
  "polarità del piano BW",
  "metà pilotata dalla coda infilata",
  "specchiature della banda",
  "quarto colore sul film",
  "partial: una leva accorcia il refresh",
  "secondo controller a opcode|0x80",
};

/** Una risposta possibile e la conseguenza che ha sul driver custom. */
struct VoceEsito
{
  const char* risposta;
  const char* conseguenza;
};

static const VoceEsito VOCI_POLARITA[] =
{
  { "bit = 1 BIANCO, come la Table 6-4",
    "il driver scrive i due piani come li scrive oggi" },
  { "bit = 1 NERO, piano invertito",
    "ogni writeImage del driver va invertita, oppure l'init deve scrivere 0x21" },
};
static const VoceEsito VOCI_META[] =
{
  { "la metà ALTA, righe 0..383",
    "la coda infilata è il master: PART_HEIGHT e l'offset delle righe sono giusti" },
  { "la metà BASSA, righe 384..767",
    "la coda infilata è lo slave: il driver deve scambiare le due bande" },
  { "tutto il pannello",
    "un controller solo pilota 768 gate, e lo split per righe non esiste" },
  { "niente, non ha dipinto",
    "questa candidata di init non fa rispondere il pannello: provane un'altra" },
};
static const VoceEsito VOCI_MIRROR[] =
{
  { "nessuna, origine in alto a sinistra", "setMasterMirror(false, false)" },
  { "specchiata in X",                     "setMasterMirror(true, false)" },
  { "specchiata in Y",                     "setMasterMirror(false, true)" },
  { "specchiata in X e in Y",              "setMasterMirror(true, true)" },
};
static const VoceEsito VOCI_QUARTO[] =
{
  { "come la banda dell'accent, nessun colore nuovo",
    "tre colori: le primitive del terzo piano restano senza corpo" },
  { "un colore distinto dalle altre tre",
    "il film ha un quarto stato: writeImageYellow va implementata" },
};
static const VoceEsito VOCI_PARTIAL[] =
{
  { "nessuna leva accorcia il refresh",
    "hasFastPartialUpdate = false è un fatto misurato, non prudenza" },
  { "una leva accorcia il refresh e dipinge",
    "il driver può avere un partial: quale leva lo dice il log della sonda" },
};
static const VoceEsito VOCI_SLAVE[] =
{
  { "no, la banda dell'altro controller non è cambiata",
    "ADDRESSING_CASCADE non è confermato, o lo slave non è sul bus" },
  { "sì, ha eseguito gli opcode offset",
    "ADDRESSING_CASCADE confermato: un solo CS e slave a opcode|0x80" },
};

/** Voci di risposta di ogni caratteristica, per il menu e per la scheda. */
static const VoceEsito* const VOCI_DI[C_COUNT] =
{
  VOCI_POLARITA, VOCI_META, VOCI_MIRROR, VOCI_QUARTO, VOCI_PARTIAL, VOCI_SLAVE
};
static const uint8_t QUANTE_DI[C_COUNT] = { 2, 4, 4, 2, 2, 2 };

/** Risposte della domanda che non alimenta una caratteristica. */
static const VoceEsito VOCI_DIPINTO[] =
{
  { "sì, l'area dentro la finestra è cambiata", "" },
  { "no, niente è cambiato",                    "" },
  { "in parte, a chiazze o smarginata",         "" },
};

/**
 * Le domande del gate. `caratteristica` a C_COUNT vuol dire che la risposta
 * resta sul frame e non chiude una caratteristica: è il caso di D_DIPINTO, da
 * cui la scheda deriva C_PARTIAL confrontando le durate.
 */
struct DomandaEsito
{
  uint8_t          caratteristica;
  const char*      testo;
  const VoceEsito* voci;
  uint8_t          quante;
};

static const DomandaEsito DOMANDE[D_COUNT] =
{
  { C_COUNT,  nullptr,                                                        nullptr,      0 },
  { C_META,   "quale metà del pannello ha dipinto?",                          VOCI_META,    4 },
  { C_QUARTO, "la quarta banda (BW = 0 con accent acceso) che colore rende?",  VOCI_QUARTO,  2 },
  { C_COUNT,  "l'area dentro la finestra è cambiata?",                         VOCI_DIPINTO, 3 },
  { C_SLAVE,  "ha dipinto la banda dell'altro controller?",                    VOCI_SLAVE,   2 },
};
static_assert(D_COUNT == 5, "DOMANDE va estesa insieme a enum Domanda");
/**
 * Numero in testa alle righe di log: l'ultimo frame dipinto, cioè quello che in
 * questo momento sta sul vetro.
 */
static uint8_t numeroDiRiga()
{
  return numeroSulVetro;
}

/** Dichiara quale sonda sta girando, così i frame le vengono attribuiti. */
static void iniziaSonda(Schermata quale)
{
  sondaCorrente = (uint8_t)quale;
}


// ---------------------------------------------------------------------------
// RIGHE DI LOG
//
// Il prefisso è il numero del frame che in questo momento sta sul vetro. Prima
// del primo frame non c'è niente da commentare, quindi il prefisso NON si
// stampa: un `[0]` indicherebbe una schermata che non esiste.
// ---------------------------------------------------------------------------

/** Prefisso delle righe: "[12] " se un frame è sul vetro, cinque spazi se no. */
static void prefissoRiga(char* out, uint8_t max)
{
  const unsigned n = numeroDiRiga();
  if (n) snprintf(out, max, "[%u] ", n);
  else   snprintf(out, max, "     ");
}

/** Intestazione di una sonda: la riga che apre un blocco. */
static void logProbeHeader(char tasto, const char* nome, uint8_t refresh)
{
  Serial.println();
  Serial.printf("== %c  %s (%u refresh) ==\n", tasto, nome, (unsigned)refresh);
}

/** Riga normale, col numero della schermata in testa. */
static void logLine(const char* fmt, ...)
{
  char buf[240], pre[8];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  prefissoRiga(pre, sizeof(pre));
  Serial.printf("%s%s\n", pre, buf);
}

/**
 * Riga di dettaglio: byte, microsecondi, salita del BUSY, passi intermedi di
 * una sequenza. Stampata solo col toggle `v`, che è spento di default perchè
 * queste righe sono utili quando una misura non torna, non mentre torna.
 */
static void logDetail(const char* fmt, ...)
{
  if (!cfg.verbose) return;
  char buf[240], pre[8];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  prefissoRiga(pre, sizeof(pre));
  Serial.printf("%s .   %s\n", pre, buf);
}

/** Riga di verdetto: una conclusione che una misura decide da sè. */
static void verdict(const char* fmt, ...)
{
  char buf[240], pre[8];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  prefissoRiga(pre, sizeof(pre));
  Serial.printf("%sVERDICT %s\n", pre, buf);
}

/** Cosa guardare sul vetro: una riga, al massimo due per schermata. */
static void look(const char* text)
{
  char pre[8];
  prefissoRiga(pre, sizeof(pre));
  Serial.printf("%sguarda: %s\n", pre, text);
}

// ---------------------------------------------------------------------------
// REGISTRO DELLE MISURE
//
// La tabella cronologica di tutto ciò che è stato cronometrato, che il
// riepilogo ristampa. La didascalia è tenuta PER PUNTATORE, quindi deve essere
// un letterale o un buffer `static` di file: una stringa composta in una
// variabile locale sarebbe già sovrascritta quando il riepilogo la stampa.
// ---------------------------------------------------------------------------
struct Misura
{
  char        sonda;     // tasto della sonda che l'ha prodotta
  const char* caption;   // la stessa stringa che la passata ha scritto sul vetro
  uint8_t     seq22;     // parametro di 0x22, 0 se la misura non è un refresh
  int32_t     ms;        // BUSY in ms, negativo se non conclusa
  uint8_t     frame;     // riquadro sul vetro, 0 se nessuno
  char        visto;     // esito registrato dalla risposta dell'operatore
};

static const uint8_t MISURE_MAX = 96;
static Misura misure[MISURE_MAX];
static uint8_t misureN = 0;

/**
 * Sonda in esecuzione, per etichettare le misure senza passarlo a ogni
 * chiamata. È il TASTO e non un puntatore a stringa: una stringa composta a
 * runtime vivrebbe in un buffer riusato dalla sonda successiva, e tutte le
 * righe del registro finirebbero etichettate con l'ultima eseguita.
 */
static char sondaAttivaTasto = '-';


/** Refresh non avviato, perchè il controller era occupato. */
static const int32_t REFRESH_NON_ESEGUITO = -2;

/**
 * Riga di misura, e registrazione. `caption` è LA STESSA STRINGA che la passata
 * ha scritto sul vetro: chi legge il pannello la ritrova nel log e viceversa.
 * `seq22` a 0 per una misura che non è un refresh (power on, carico di LUT,
 * pattern hardware, reset).
 */
static void measure(const char* caption, uint8_t seq22, int32_t ms)
{
  char pre[8];
  prefissoRiga(pre, sizeof(pre));
  if (seq22)
    Serial.printf("%sMEAS  %-32s 22=%02X %7ld ms\n", pre, caption, seq22, (long)ms);
  else
    Serial.printf("%sMEAS  %-32s        %7ld ms\n", pre, caption, (long)ms);

  if (misureN < MISURE_MAX)
  {
    Misura& m = misure[misureN++];
    m.sonda   = sondaAttivaTasto;
    m.caption = caption;
    m.seq22   = seq22;
    m.ms      = ms;
    m.frame   = numeroDiRiga();
    m.visto   = ' ';
  }
}

/** Ultima durata misurata con quel parametro di 0x22, -1 se non c'è. */
static int32_t ultimaMisura(uint8_t seq22)
{
  for (int8_t i = (int8_t)misureN - 1; i >= 0; --i)
    if (misure[i].seq22 == seq22 && misure[i].ms > 0)
      return misure[i].ms;
  return -1;
}

/** Annota l'esito visivo sull'ultima misura registrata. */
static void annotaVisto(char c)
{
  if (misureN > 0) misure[misureN - 1].visto = c;
}
/** Intervallo di numeri prodotto da una sonda; primo = 0 se non ha prodotto. */
static void rangeFrame(uint8_t sonda, uint8_t* primo, uint8_t* ultimo)
{
  *primo = 0;
  *ultimo = 0;
  for (uint8_t i = 0; i < registroFrameN; ++i)
  {
    if (registroFrame[i].sonda != sonda) continue;
    if (!*primo) *primo = registroFrame[i].numero;
    *ultimo = registroFrame[i].numero;
  }
}

/**
 * PRENOTA il frame che sta per essere dipinto e ne ritorna il numero, che è
 * quello da scrivere nel riquadro. Prenotato e non ancora "sul vetro", perchè
 * fra la scrittura della RAM e il cambio del vetro c'è il gate, e in quel
 * momento la schermata da guardare è ancora la precedente.
 *
 * Il numero cresce sempre, anche col registro pieno: si perde una voce della
 * scheda, non la corrispondenza fra vetro e log.
 */
static uint8_t registraFrame(const Frame& f)
{
  const uint8_t numero = ++contatoreFrame;
  numeroPrenotato = numero;
  if (registroFrameN < FRAME_MAX)
  {
    FrameOsservato& r = registroFrame[registroFrameN];
    r.numero   = numero;
    r.sonda    = sondaCorrente;
    snprintf(r.etichetta, sizeof(r.etichetta), "%s", f.etichetta ? f.etichetta : "");
    r.domanda    = f.domanda;
    r.risposta   = -1;
    r.ms         = -1;
    r.senzaBadge = f.senzaBadge;
    framePrenotato = ++registroFrameN;
  }
  else
  {
    Serial.println(F("   registro dei frame pieno: questo non finisce nella scheda"));
    framePrenotato = 0;
  }
  return numero;
}

/** Etichetta del frame prenotato, per il messaggio del gate. */
static const char* etichettaPrenotata()
{
  return framePrenotato ? registroFrame[framePrenotato - 1].etichetta : nullptr;
}

/**
 * Il frame prenotato è arrivato sul vetro: da adesso è lui che il log commenta
 * e che il gate successivo chiuderà.
 */
static void attivaFramePrenotato()
{
  if (!numeroPrenotato) return;
  numeroSulVetro  = numeroPrenotato;
  frameInAttesa   = framePrenotato;
  framePrenotato  = 0;
  numeroPrenotato = 0;
}

/** Annota la durata misurata sul frame appena dipinto. */
static void durataFrame(int32_t ms)
{
  if (frameInAttesa) registroFrame[frameInAttesa - 1].ms = ms;
}


/**
 * Registra l'esito di una caratteristica e stampa la conseguenza che ha sul
 * driver: è il punto in cui una schermata guardata diventa una riga da cambiare
 * in GxEPD2_SOLUM_122c_960x768.h. Con frame = 0 la chiamata viene dal menu e
 * non da una schermata, quindi la riga esce senza prefisso.
 */
static void registraEsito(uint8_t car, int8_t risposta, uint8_t frame, bool dichiarata)
{
  if (car >= C_COUNT || risposta < 0 || risposta >= (int8_t)QUANTE_DI[car]) return;
  esiti[car].risposta   = risposta;
  esiti[car].frame      = frame;
  esiti[car].dichiarata = dichiarata;
  if (frame)
    verdict("%s -> %s", VOCI_DI[car][risposta].risposta,
            VOCI_DI[car][risposta].conseguenza);
  else
    Serial.printf("  dichiarato: %s -> %s\n",
                  VOCI_DI[car][risposta].risposta,
                  VOCI_DI[car][risposta].conseguenza);
}

/**
 * Pone la domanda di un frame e registra la risposta. Le voci si numerano da 1
 * e la risposta stessa vale come avanzamento: non serve un tasto in più.
 *
 * Non passa da chiediScelta() di proposito: là un INVIO a vuoto tiene il
 * preimpostato, e qui non esiste un preimpostato onesto — nessuna di queste
 * risposte è più probabile delle altre, e registrarne una che l'operatore non
 * ha dato falserebbe la scheda. Lo zero, e l'INVIO a vuoto, valgono quindi
 * "non l'ho guardata" e non annotano niente.
 */
static void chiediEsitoFrame(uint8_t idx)
{
  FrameOsservato& f = registroFrame[idx];
  const DomandaEsito& d = DOMANDE[f.domanda];
  if (!d.voci) return;

  scartaInputPendente();
  look(d.testo);
  for (uint8_t i = 0; i < d.quante; ++i)
    logLine("  %u) %s", i + 1, d.voci[i].risposta);
  logLine("  0) non l'ho guardata     [1-%u, invio = 0]", d.quante);

  char buf[8];
  const int n = leggiRiga(buf, sizeof(buf));
  const int scelta = (n > 0) ? atoi(buf) : 0;
  if (scelta < 1 || scelta > d.quante)
  {
    logLine("SEEN  -");
    return;
  }
  f.risposta = (int8_t)(scelta - 1);
  logLine("SEEN  %d", scelta);
  annotaVisto((char)('0' + scelta));
  if (d.caratteristica < C_COUNT)
    registraEsito(d.caratteristica, f.risposta, f.numero, false);
  else
    verdict("%s", d.voci[f.risposta].risposta);
}

/**
 * GATE FRA DUE SCHERMATE: chiude il frame che sta sul vetro e aspetta
 * l'operatore. È l'input a far cambiare schermata, non un timeout: un frame di
 * questo pannello costa ~19 s e quello successivo lo cancella, quindi una
 * schermata che avanza da sè è una misura buttata.
 *
 * Non spegne il pannello, al contrario della pausa della suite 9.7": la
 * differenziale usa di proposito sequenze senza power down per misurare il
 * costo del boost e della ricarica di LUT, e uno spegnimento fra le passate
 * cambierebbe la misura. Non tocca nemmeno SPI, così vale anche dentro la sonda
 * del deep sleep, dove il controller è sordo per costruzione.
 */
static void chiudiFrame(const char* prossimo)
{
  if (!frameInAttesa) return;
  const uint8_t idx = (uint8_t)(frameInAttesa - 1);
  frameInAttesa = 0;                 // prima della domanda: non deve rientrare
  FrameOsservato& f = registroFrame[idx];

  Serial.println();
  logLine("SUL VETRO: %s", f.etichetta);
  if (f.senzaBadge)
    logLine("riquadro non dipinto da questa waveform: nell'angolo resta il %u",
            (unsigned)numeroRiquadro);

  if (f.domanda != D_NESSUNA)
  {
    chiediEsitoFrame(idx);
    if (prossimo) logLine("arriva la %u: %s", (unsigned)numeroPrenotato, prossimo);
    return;
  }
  if (prossimo) logLine("un tasto per dipingere la %u: %s",
                        (unsigned)numeroPrenotato, prossimo);
  else          logLine("un tasto per continuare");
  attendiTasto();
}

/**
 * PRENOTA il numero del frame prima che la passata dipinga la RAM, e lo
 * ritorna. Serve alle passate che scrivono il numero DENTRO l'immagine — la
 * cifra sulla fascia — perchè quella cifra e il riquadro in alto a destra
 * devono portare lo STESSO numero: due numeri diversi sul vetro, con uno solo
 * nel log, è la cosa che rende un'osservazione inattribuibile.
 *
 * runRefresh() usa la prenotazione pendente se c'è, altrimenti prenota da sè:
 * una passata che non stampa cifre non ha bisogno di chiamare questa.
 */
static uint8_t prenotaNumero(const Frame& f)
{
  return registraFrame(f);
}

/** Numero già prenotato e non ancora dipinto, 0 se non ce n'è. */
static uint8_t numeroInPrenotazione()
{
  return numeroPrenotato;
}

/**
 * Butta una prenotazione che non arriverà sul vetro, perchè il refresh non è
 * nemmeno partito.
 *
 * Serve, e non è teoria: sulla coda che non risponde il caso normale è proprio
 * quello, e una prenotazione lasciata appesa verrebbe raccolta dal refresh
 * SUCCESSIVO — che quindi non registrerebbe il proprio frame, ne erediterebbe
 * numero ed etichetta, e scriverebbe la propria durata sulla riga di quello
 * fallito. Il numero speso si perde, e va bene così: un buco nella numerazione
 * è leggibile, un numero che indica la schermata sbagliata no.
 */
static void annullaPrenotazione()
{
  if (framePrenotato && registroFrameN == framePrenotato)
    --registroFrameN;              // la voce non descrive niente di visibile
  framePrenotato  = 0;
  numeroPrenotato = 0;
}
/** Nome di ogni schermata, per l'indice e le voci della scheda finale. */
static const char* const NOME_SCHERMATA[SCH_COUNT] =
{
  "frame di identificazione, uno per candidata di init",
  "polarità del piano BW, due frame",
  "4 bande dei piani più i box a finestra parziale",
  "partial d'area",
  "differenziale approfondita",
  "partial con LUT custom",
  "quarto colore, livelli di sorgente",
  "secondo controller a opcode|0x80",
  "banchi di waveform per temperatura",
  "RAM ping-pong via 0x37",
  "deep sleep, refresh di prova dopo il risveglio",
  "fase driver, un frame per modello di indirizzamento",
};
static_assert(SCH_COUNT == 12, "NOME_SCHERMATA va esteso insieme a enum Schermata");

// ---------------------------------------------------------------------------
// I DUE ELENCHI CHE IL RIEPILOGO STAMPA
// ---------------------------------------------------------------------------

/** Il registro delle misure: la prima metà del riepilogo. */
static void printMisure()
{
  Serial.println(F("\n-- misure --"));
  if (misureN == 0) { Serial.println(F("  nessuna")); return; }
  Serial.println(F("  sonda  0x22      ms  vis  descrizione"));
  for (uint8_t i = 0; i < misureN; ++i)
  {
    const Misura& m = misure[i];
    Serial.printf("  %c       ", m.sonda);
    if (m.seq22) Serial.printf("%02X  ", m.seq22); else Serial.print(F("--  "));
    if (m.ms < 0) Serial.print(F("      -"));
    else          Serial.printf("%7ld", (long)m.ms);
    Serial.printf("   %c   %s", m.visto, m.caption);
    if (m.frame) Serial.printf("  [%u]", (unsigned)m.frame);
    Serial.println();
  }
}

/** Indice delle schermate: lega i numeri sul vetro alle sonde che li hanno prodotti. */
static void printIndiceSchermate()
{
  Serial.println(F("\n-- schermate --"));
  if (registroFrameN == 0) { Serial.println(F("  nessuna")); return; }
  for (uint8_t i = 0; i < registroFrameN; ++i)
  {
    const FrameOsservato& f = registroFrame[i];
    Serial.printf("  [%u] %-44s", (unsigned)f.numero, f.etichetta);
    if (f.sonda < SCH_COUNT)
    {
      uint8_t primo = 0, ultimo = 0;
      rangeFrame(f.sonda, &primo, &ultimo);
      Serial.printf("  %s", NOME_SCHERMATA[f.sonda]);
      if (primo != ultimo) Serial.printf(" [%u-%u]", (unsigned)primo, (unsigned)ultimo);
    }
    if (f.ms > 0)            Serial.printf("  %ld ms", (long)f.ms);
    Serial.println();
  }
}

#endif // DUAL_PANEL_FINDER_REPORT_H
