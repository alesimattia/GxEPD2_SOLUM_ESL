// =============================================================================
// partial_refresh — verifica sul vetro del partial update del driver 9.7"
//
// A differenza di examples/097c/panel_diagnostic, che parla al controller a SPI
// diretta per scoprire cosa il pannello sa fare, questo sketch esercita il
// DRIVER: chiama solo API pubbliche di GxEPD2_SOLUM_097c_960x672 e risponde a
// una domanda sola — il partial da 641 ms che la sonda ha misurato arriva
// davvero sul vetro passando dal driver?
//
// Il template GxEPD2_3C non compare: il partial vive fuori da lì, perchè in
// modalità partial il template scriverebbe il piano accent dentro 0x26, che
// sotto la LUT custom è il frame precedente. Qui si usa il driver nudo.
//
// COME LEGGERE IL LOG INSIEME AL PANNELLO:
//   ogni schermata da guardare porta un numero in un riquadro in alto a destra
//   dell'area appena ridipinta, e le righe che la riguardano iniziano con lo
//   stesso numero fra parentesi quadre. Dove il test chiede di guardare il
//   vetro, il blocco sta fra una barra di v e una barra di ^. È la stessa
//   convenzione di panel_diagnostic, così i due log si leggono allo stesso
//   modo. Le fasi che NON toccano il vetro lo dichiarano e non hanno numero.
//
// LE FASI, e quanto costano:
//   [1] fondo di riferimento, refresh pieno       ~24 s   fasce testimone
//    -  allineamento di 0x26                      ~30 ms  NIENTE sul vetro
//   [3] fascia A a NERO, in partial               ~660 ms SOLO la fascia A
//   [4] fascia A di nuovo BIANCA, in partial      ~660 ms senza preparazione
//   [5] fascia B a righe, in partial              ~660 ms la A resta bianca
//   [6] otto alternanze sulla fascia A            ~5,3 s  quanto sbiadisce
//   [7] accent ricostruito + refresh pieno        ~24 s   tutto torna netto
//
// COME SI LEGGE L'ESITO:
//   - una fascia che cambia in meno di un secondo    -> il partial funziona
//   - una fascia che cambia in ~24 s                 -> la LUT custom non ha
//     preso, e il controller ha ripiegato sulla waveform dell'OTP
//   - una fascia che non cambia affatto              -> la LUT viene accettata
//     ma non pilota il film
//   - i testimoni ROSSO e NERO che spariscono o si sporcano -> 0x26 non era
//     allineato a 0x24 quando la catena è partita, e il partial ha pilotato
//     pixel che non doveva
//
// I due TESTIMONI sono il vero controllo. Stanno fuori da ogni fascia di lavoro
// e nessun partial li indirizza: se restano dove sono, la confinatura per LUT
// funziona. Che sbiadiscano un po' dopo la fase [6] è atteso — i pixel non
// pilotati si schiariscono comunque — ed è la ragione per cui una catena lunga
// va chiusa con un refresh pieno.
//
// Il testimone ROSSO risponde a una domanda in più: il partial non porta mai la
// sorgente a VSH2, che su questo film è la tensione del pigmento rosso, quindi
// il rosso già sul vetro non deve muoversi affatto.
//
// Hardware: Waveshare E-Paper ESP32 Driver Board V3, che scambia SCK e MOSI
// rispetto al default HSPI — da cui hspi.begin(13, 12, 14, 15).
//
// Author: Mattia Alesi
// =============================================================================

#define SOLUM_PANEL_097C

#include <SPI.h>
#include <GxEPD2_SOLUM.h>

// ---------------------------------------------------------------------------
// Board: Waveshare E-Paper ESP32 Driver Board V3.
// GPIO12 è un MISO fittizio: sul FPC a 24 pin del pannello la linea dati di
// ritorno non esiste, quindi nessuna lettura di registro è attendibile. Qui non
// se ne fa nessuna.
// ---------------------------------------------------------------------------
SPIClass hspi(HSPI);
GxEPD2_SOLUM_DRIVER_CLASS epd(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 });

static const int16_t W = GxEPD2_SOLUM_DRIVER_CLASS::WIDTH;   // 960
static const int16_t H = GxEPD2_SOLUM_DRIVER_CLASS::HEIGHT;  // 672

// Geometria: due fasce di lavoro per il partial e due testimoni che nessun
// partial indirizza.
static const int16_t BAND_H     = 168;
static const int16_t BAND_A_Y   = 0;     // fascia di lavoro A
static const int16_t WIT_RED_Y  = 196;   // testimone rosso
static const int16_t WIT_H      = 56;
static const int16_t BAND_B_Y   = 280;   // fascia di lavoro B
static const int16_t WIT_BLK_Y  = 476;   // testimone nero

// Riquadro del numero di schermata: sta in alto a destra dell'area ridipinta.
// X e larghezza sono multipli di 8 perchè le API del driver indirizzano la RAM
// a byte lungo X.
static const int16_t BOX_W  = 64;
static const int16_t BOX_H  = 72;
static const int16_t BOX_X  = W - BOX_W - 16;   // 880, multiplo di 8
static const int16_t BOX_DY = 8;                // rientro dal bordo alto dell'area

// Buffer della fascia, il più grande che serve: 120 byte per riga per 168 righe.
// drawImagePartial() lo legge due volte — in 0x24 prima del refresh e in 0x26
// dopo — ma sempre dentro la chiamata, quindi uno solo basta.
static const uint32_t BAND_BYTES = (uint32_t)(W / 8) * BAND_H;
static uint8_t band[BAND_BYTES];

// Buffer del solo riquadro, per le fasi a refresh pieno che non passano da una
// fascia.
static uint8_t box[(BOX_W / 8) * BOX_H];

// ---------------------------------------------------------------------------
// Disegno nel buffer. Convenzione delle bitmap del driver: bit = 1 è bianco,
// bit = 0 è nero, quindi "accendere" un pixel nero vuol dire azzerare il bit.
// ---------------------------------------------------------------------------

/** Font 5x7 delle sole cifre, identico a quello di panel_diagnostic. */
static const uint8_t FONT_DIGIT[10][7] =
{
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
};

/** Un pixel dentro un buffer largo wb byte. */
static inline void setPx(uint8_t* buf, int16_t wb, int16_t x, int16_t y, bool nero)
{
  const uint32_t i = (uint32_t)y * wb + (x >> 3);
  const uint8_t  m = 0x80 >> (x & 7);
  if (nero) buf[i] &= (uint8_t)~m;
  else      buf[i] |= m;
}

static void fillRect(uint8_t* buf, int16_t wb, int16_t x0, int16_t y0,
                     int16_t w, int16_t h, bool nero)
{
  for (int16_t y = y0; y < y0 + h; ++y)
    for (int16_t x = x0; x < x0 + w; ++x)
      setPx(buf, wb, x, y, nero);
}

/**
 * Riquadro col numero di schermata, in alto a destra dell'area passata.
 * Fondo bianco e cornice nera, così si legge sia su una fascia nera sia su una
 * bianca: è la stessa idea del riquadro di panel_diagnostic.
 * `bufW` è la larghezza in pixel del buffer, `x0` e `y0` l'angolo del riquadro
 * dentro quel buffer: dentro una fascia il riquadro è rientrato di BOX_DY,
 * dentro il buffer che contiene solo lui parte da zero.
 */
static void drawScreenBox(uint8_t* buf, int16_t bufW, int16_t x0, int16_t y0,
                          uint8_t numero)
{
  const int16_t wb = bufW / 8;
  fillRect(buf, wb, x0, y0, BOX_W, BOX_H, false);                // fondo bianco
  fillRect(buf, wb, x0, y0, BOX_W, 3, true);                     // cornice
  fillRect(buf, wb, x0, y0 + BOX_H - 3, BOX_W, 3, true);
  fillRect(buf, wb, x0, y0, 3, BOX_H, true);
  fillRect(buf, wb, x0 + BOX_W - 3, y0, 3, BOX_H, true);

  // Cifra 5x7 ingrandita 8 volte: 40x56 px, centrata nel riquadro.
  const int16_t S  = 8;
  const int16_t gx = x0 + (BOX_W - 5 * S) / 2;
  const int16_t gy = y0 + (BOX_H - 7 * S) / 2;
  const uint8_t* g = FONT_DIGIT[numero % 10];
  for (int16_t r = 0; r < 7; ++r)
    for (int16_t c = 0; c < 5; ++c)
      if (g[r] & (0x10 >> c))
        fillRect(buf, wb, gx + c * S, gy + r * S, S, S, true);
}

/**
 * Riempie il buffer di fascia e ci mette dentro il riquadro numerato.
 *   0 = tutto bianco, 1 = tutto nero, 2 = righe verticali larghe 32 px.
 */
static void fillBand(uint8_t pattern, int16_t h, uint8_t numero)
{
  const int16_t wb = W / 8;
  for (int16_t r = 0; r < h; ++r)
    for (int16_t c = 0; c < wb; ++c)
    {
      uint8_t v;
      if (pattern == 0)      v = 0xFF;
      else if (pattern == 1) v = 0x00;
      else                   v = ((c / 4) & 1) ? 0xFF : 0x00;
      band[(uint32_t)r * wb + c] = v;
    }
  if (numero) drawScreenBox(band, W, BOX_X, BOX_DY, numero);
}

/** La stessa fascia senza riquadro, per i testimoni e per il piano precedente. */
static void fillBandPlain(uint8_t pattern, int16_t h)
{
  fillBand(pattern, h, 0);
}

// ---------------------------------------------------------------------------
// Blocco di osservazione sul seriale, stessa forma di panel_diagnostic: barra
// di v, righe prefissate dal numero della schermata, barra di ^.
// ---------------------------------------------------------------------------
static void inizioOsservazione(uint8_t n, const char* titolo)
{
  Serial.println();
  Serial.printf("[%u] vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n", n);
  Serial.printf("[%u] GUARDA IL PANNELLO: SCHERMATA %u, il numero sta nel riquadro in\n", n, n);
  Serial.printf("[%u] alto a destra dell'area appena ridipinta\n", n);
  Serial.printf("[%u] %s\n", n, titolo);
  Serial.printf("[%u]\n", n);
}

static void rigaOsservazione(uint8_t n, const char* riga)
{
  Serial.printf("[%u] %s\n", n, riga);
}

static void fineOsservazione(uint8_t n)
{
  Serial.printf("[%u] ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n", n);
  Serial.println();
}

/** Pausa di osservazione: si chiude da sè o con INVIO, come nella sonda. */
static void pausaOsservazione(uint8_t n, const char* cosaSuccede, uint32_t ms)
{
  while (Serial.available()) Serial.read();
  Serial.println();
  Serial.printf("[%u] GUARDA IL PANNELLO E ANNOTA: %s\n", n, cosaSuccede);
  Serial.printf("[%u] premi INVIO quando hai finito, o aspetta %lu s\n",
                n, (unsigned long)(ms / 1000));
  const uint32_t t0 = millis();
  uint32_t prossimo = 5000;
  while ((millis() - t0) < ms)
  {
    if (Serial.available())
    {
      Serial.printf("[%u]   ripartito su richiesta dopo %lu s\n",
                    n, (unsigned long)((millis() - t0) / 1000));
      return;
    }
    if ((millis() - t0) >= prossimo)
    {
      Serial.printf("[%u]   %lu s\n", n, (unsigned long)((ms - prossimo) / 1000));
      prossimo += 5000;
    }
    delay(20);
  }
}

/** Riga di misura: la durata accanto al verdetto che quella durata implica. */
static void report(uint8_t n, const char* fase, uint32_t ms)
{
  Serial.printf("[%u] >> %-40s %6lu ms", n, fase, (unsigned long)ms);
  if (ms < 3000) Serial.println("   <- partial");
  else           Serial.println("   <- durata da refresh pieno");
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n=== partial_refresh: il partial del driver, sul vetro ==="));
  Serial.printf("pannello %dx%d, partial_refresh_time dichiarato %u ms\n",
                (int)W, (int)H,
                (unsigned)GxEPD2_SOLUM_DRIVER_CLASS::partial_refresh_time);
  Serial.printf("hasFastPartialUpdate = %s (atteso false: il partial sta fuori dal template)\n",
                GxEPD2_SOLUM_DRIVER_CLASS::hasFastPartialUpdate ? "true" : "false");
  Serial.println(F("fasce di lavoro: A y=0..167, B y=280..447"));
  Serial.println(F("testimoni, mai indirizzati da un partial: ROSSO y=196..251, NERO y=476..531"));
  Serial.println(F("\ncome leggere: ogni schermata da guardare ha il numero in un riquadro"));
  Serial.println(F("in alto a destra dell'area ridipinta, e le sue righe iniziano con lo"));
  Serial.println(F("stesso numero fra parentesi quadre"));

  // La board scambia SCK e MOSI rispetto al default HSPI: il remap è
  // obbligatorio, non stilistico.
  hspi.begin(13, 12, 14, 15);           // SCK, MISO(fittizio), MOSI, SS
  epd.selectSPI(hspi, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  epd.init(115200, true, 2, false);

  uint32_t t0;

  // -------------------------------------------------------------------------
  // [1] Fondo di riferimento. Schermo bianco più i due testimoni, in un refresh
  //     pieno: parte da uno stato noto e stabilisce il riferimento di durata
  //     contro cui leggere le fasi successive.
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 1: fondo di riferimento, refresh pieno ---"));
  fillBandPlain(1, WIT_H);                                  // fascia tutta nera
  memset(box, 0xFF, sizeof(box));
  drawScreenBox(box, BOX_W, 0, 0, 1);
  epd.writeScreenBuffer(0xFF, 0x00);                        // 0x24 bianco, accent spento
  epd.writeImageRed(band, 0, WIT_RED_Y, W, WIT_H, false);   // testimone rosso
  epd.writeImageBlack(band, 0, WIT_BLK_Y, W, WIT_H, false); // testimone nero
  epd.writeImageBlack(box, BOX_X, BOX_DY, BOX_W, BOX_H, false);
  t0 = millis();
  epd.refresh(false);
  report(1, "refresh pieno di riferimento", millis() - t0);

  inizioOsservazione(1, "FONDO DI RIFERIMENTO: da qui parte tutto");
  rigaOsservazione(1, "atteso: fondo BIANCO, una fascia ROSSA a y=196..251 e una");
  rigaOsservazione(1, "fascia NERA a y=476..531");
  rigaOsservazione(1, "");
  rigaOsservazione(1, "se il fondo non e' bianco o i testimoni non ci sono, il");
  rigaOsservazione(1, "refresh pieno non funziona e nient'altro di questo test vale");
  rigaOsservazione(1, "");
  rigaOsservazione(1, "guarda bene QUANTO sono saturi i due testimoni: la fase [6]");
  rigaOsservazione(1, "chiedera' di confrontarli con adesso");
  fineOsservazione(1);
  pausaOsservazione(1, "parte l'allineamento di 0x26, che non tocca il vetro", 10000);

  // -------------------------------------------------------------------------
  //  -  Allineamento di 0x26. Il passo che non si vede e senza il quale non
  //     funziona niente: dopo un refresh pieno 0x26 contiene l'accent, mentre
  //     il partial la legge come frame precedente. La regola operativa è
  //     portare 0x26 uguale a 0x24, così i pixel che non cambiano cadono su
  //     LUT0 o LUT3, che nella waveform del partial sono a zero.
  //     Il piano BW è bianco ovunque tranne il testimone nero e il riquadro
  //     della schermata 1, quindi servono tre chiamate.
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 2: allineamento di 0x26 al piano BW (NESSUNA schermata) ---"));
  t0 = millis();
  epd.writeScreenBufferPrevious(0xFF);                         // precedente = bianco
  epd.writeImagePrevious(band, 0, WIT_BLK_Y, W, WIT_H, false); // tranne il testimone nero
  epd.writeImagePrevious(box, BOX_X, BOX_DY, BOX_W, BOX_H, false); // e il riquadro
  Serial.printf("    0x26 := 0x24 in %lu ms, nessun refresh: il vetro non cambia e\n",
                (unsigned long)(millis() - t0));
  Serial.println(F("    non c'e' niente da guardare, la schermata resta la [1]"));

  // -------------------------------------------------------------------------
  // [3] Il partial vero e proprio: la fascia A diventa nera.
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 3: fascia A a NERO, in partial ---"));
  fillBand(1, BAND_H, 3);
  t0 = millis();
  epd.drawImagePartial(band, 0, BAND_A_Y, W, BAND_H, false);
  report(3, "drawImagePartial fascia A -> nero", millis() - t0);

  inizioOsservazione(3, "IL PARTIAL: la fascia A e' diventata nera?");
  rigaOsservazione(3, "la fascia A e' y=0..167, e il riquadro col 3 sta dentro di lei");
  rigaOsservazione(3, "");
  rigaOsservazione(3, "  fascia NERA con la durata sotto il secondo -> IL PARTIAL");
  rigaOsservazione(3, "    FUNZIONA attraverso il driver, non solo a SPI diretta");
  rigaOsservazione(3, "  fascia nera ma con ~24 s -> la LUT custom non ha preso e il");
  rigaOsservazione(3, "    controller ha usato la waveform dell'OTP");
  rigaOsservazione(3, "  fascia non cambiata -> la LUT viene accettata ma non pilota");
  rigaOsservazione(3, "");
  rigaOsservazione(3, "e soprattutto: i due TESTIMONI sono ancora al loro posto?");
  rigaOsservazione(3, "se si', la confinatura per LUT funziona; se il rosso e'");
  rigaOsservazione(3, "cambiato, il partial ha pilotato pixel che non doveva");
  rigaOsservazione(3, "");
  rigaOsservazione(3, "guarda anche la CORNICE del pannello: con 0x3C=0xC0 non deve");
  rigaOsservazione(3, "lampeggiare");
  fineOsservazione(3);
  pausaOsservazione(3, "la fascia A torna bianca", 15000);

  // -------------------------------------------------------------------------
  // [4] Ritorno al bianco SENZA riallineare 0x26: è la prova che
  //     drawImagePartial() mantiene da sè l'invariante sul frame precedente.
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 4: fascia A di nuovo BIANCA, senza preparazione ---"));
  fillBand(0, BAND_H, 4);
  t0 = millis();
  epd.drawImagePartial(band, 0, BAND_A_Y, W, BAND_H, false);
  report(4, "drawImagePartial fascia A -> bianco", millis() - t0);

  inizioOsservazione(4, "L'INVARIANTE: la fascia A torna bianca da sola?");
  rigaOsservazione(4, "fra la schermata 3 e questa NON e' stato riallineato 0x26: se la");
  rigaOsservazione(4, "fascia torna bianca vuol dire che drawImagePartial() ricopia da");
  rigaOsservazione(4, "se' il frame nuovo nella RAM del frame precedente, e una catena");
  rigaOsservazione(4, "di partial si puo' allungare quanto si vuole");
  rigaOsservazione(4, "");
  rigaOsservazione(4, "  fascia BIANCA col riquadro 4 -> invariante a posto");
  rigaOsservazione(4, "  fascia rimasta nera          -> la ricopia in 0x26 non funziona");
  rigaOsservazione(4, "  fascia grigia o a chiazze    -> ricopia parziale");
  fineOsservazione(4);
  pausaOsservazione(4, "si dipinge la fascia B, piu' in basso", 15000);

  // -------------------------------------------------------------------------
  // [5] Seconda area. Serve a vedere che la confinatura non dipende dalla
  //     finestra RAM, che su questo pannello non confina niente, ma dalla LUT.
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 5: fascia B a righe, in partial ---"));
  fillBand(2, BAND_H, 5);
  t0 = millis();
  epd.drawImagePartial(band, 0, BAND_B_Y, W, BAND_H, false);
  report(5, "drawImagePartial fascia B -> righe", millis() - t0);

  inizioOsservazione(5, "CONFINATURA: il resto dello schermo si e' mosso?");
  rigaOsservazione(5, "la fascia B e' y=280..447, a righe verticali, e il riquadro col");
  rigaOsservazione(5, "5 sta dentro di lei: il numero compare in alto a destra di OGNI");
  rigaOsservazione(5, "area ridipinta, non sempre nello stesso posto");
  rigaOsservazione(5, "");
  rigaOsservazione(5, "il refresh scandisce TUTTE le gate line, perche' la finestra RAM");
  rigaOsservazione(5, "su questo pannello non confina niente: a limitare l'area");
  rigaOsservazione(5, "ridipinta e' la LUT, che non pilota i pixel uguali nelle due RAM");
  rigaOsservazione(5, "");
  rigaOsservazione(5, "  fascia A ancora bianca e testimoni intatti -> confermato");
  rigaOsservazione(5, "  qualcos'altro e' cambiato -> 0x26 non era allineato a 0x24");
  fineOsservazione(5);
  pausaOsservazione(5, "partono otto alternanze sulla fascia A", 15000);

  // -------------------------------------------------------------------------
  // [6] Catena lunga sulla stessa area, per misurare lo sbiadimento. I pixel
  //     non pilotati si schiariscono comunque a ogni passata.
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 6: otto alternanze sulla fascia A ---"));
  uint32_t somma = 0;
  for (int i = 0; i < 8; ++i)
  {
    fillBand((i & 1) ? 0 : 1, BAND_H, 6);
    t0 = millis();
    epd.drawImagePartial(band, 0, BAND_A_Y, W, BAND_H, false);
    somma += millis() - t0;
  }
  Serial.printf("[6] >> %-40s %6lu ms   <- %lu ms per passata\n",
                "otto passate di partial", (unsigned long)somma,
                (unsigned long)(somma / 8));

  inizioOsservazione(6, "SBIADIMENTO: quanto costa una catena lunga");
  rigaOsservazione(6, "la fascia A ha alternato nero e bianco otto volte e adesso e'");
  rigaOsservazione(6, "bianca, col riquadro 6");
  rigaOsservazione(6, "");
  rigaOsservazione(6, "confronta ADESSO con la schermata 1, che avevi guardato bene:");
  rigaOsservazione(6, "  il testimone ROSSO e' meno saturo?");
  rigaOsservazione(6, "  il testimone NERO e' diventato grigio?");
  rigaOsservazione(6, "  il fondo bianco si e' sporcato?");
  rigaOsservazione(6, "");
  rigaOsservazione(6, "sono pixel che nessun partial ha indirizzato: si schiariscono");
  rigaOsservazione(6, "lo stesso perche' il pannello viene pilotato tutto. Quanto si");
  rigaOsservazione(6, "vede qui detta ogni quanti partial serve un refresh pieno");
  fineOsservazione(6);
  pausaOsservazione(6, "parte il refresh pieno di pulizia", 20000);

  // -------------------------------------------------------------------------
  // [7] Uscita dalla catena, e non è un refresh e basta. Alla fine della catena
  //     0x26 contiene il frame precedente in polarità BW, cioè bit a 1 dove il
  //     vetro è bianco: un refresh pieno rilegge quella RAM come ACCENT, e 0xFF
  //     vuol dire rosso. Una refresh(false) nuda qui dipingerebbe tutto lo
  //     schermo di rosso. Si riscrive quindi un piano accent valido, che tanto
  //     vale far coincidere col fondo della fase [1] per confrontarli a occhio.
  //     Border di produzione e waveform dell'OTP li rimette il driver da sè
  //     dentro _Update_Full().
  // -------------------------------------------------------------------------
  Serial.println(F("\n--- fase 7: accent ricostruito + refresh pieno ---"));
  fillBandPlain(1, WIT_H);
  memset(box, 0xFF, sizeof(box));
  drawScreenBox(box, BOX_W, 0, 0, 7);
  epd.writeScreenBuffer(0xFF, 0x00);                        // accent valido, spento
  epd.writeImageRed(band, 0, WIT_RED_Y, W, WIT_H, false);   // testimone rosso
  epd.writeImageBlack(band, 0, WIT_BLK_Y, W, WIT_H, false); // testimone nero
  epd.writeImageBlack(box, BOX_X, BOX_DY, BOX_W, BOX_H, false);
  t0 = millis();
  epd.refresh(false);
  report(7, "refresh pieno finale", millis() - t0);

  inizioOsservazione(7, "RITORNO: si torna alla waveform di produzione?");
  rigaOsservazione(7, "il vetro deve tornare netto come alla schermata 1, col riquadro");
  rigaOsservazione(7, "che pero' adesso porta il 7: fondo bianco pulito, testimone");
  rigaOsservazione(7, "ROSSO saturo, testimone NERO pieno, nessuna traccia delle fasce");
  rigaOsservazione(7, "");
  rigaOsservazione(7, "  torna netto        -> il ritorno dalla LUT custom alla");
  rigaOsservazione(7, "    waveform dell'OTP funziona, e il driver rimette il border da se'");
  rigaOsservazione(7, "  schermo ROSSO      -> l'accent non e' stato ricostruito prima del");
  rigaOsservazione(7, "    refresh: e' la trappola che questa fase esiste per dimostrare");
  rigaOsservazione(7, "  restano fantasmi   -> un solo refresh pieno non basta a");
  rigaOsservazione(7, "    cancellare la catena, e ne serve piu' di uno");
  fineOsservazione(7);

  epd.powerOff();
  epd.hibernate();
  Serial.println(F("\npannello in deep sleep. Fine del test."));
}

void loop() {}
