// Le API opt-in del partial B/N del driver 9.7", quelle che lavorano fuori dal
// template GxEPD2_3C e che il firmware non chiama.
//
// Il driver src/GxEPD2_SOLUM_097c_960x672.h tiene del partial solo ciò che serve
// al refresh differenziale e al refresh pieno. Qui sta il resto, nell'esempio
// che misura il partial: per usarlo si costruisce il display con questa classe
// al posto del driver, perchè GxEPD2_3C va istanziato con il tipo della
// sottoclasse.
//
//   GxEPD2_3C<GxEPD2_SOLUM_097c_PartialProbe, GxEPD2_SOLUM_097c_PartialProbe::HEIGHT / 8>
//       display(GxEPD2_SOLUM_097c_PartialProbe(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }));
//
// Il perchè del partial, la sua LUT e i suoi limiti sono nella sezione PARTIAL in
// testa al driver. La suite panel_diagnostic parla al pannello a SPI diretta e
// non istanzia questa classe: l'include serve a tenerla compilata insieme al
// driver, e Controller.h dice quale API ciascuna primitiva rispecchia.

#ifndef PANEL_DIAGNOSTIC_PARTIAL_PROBE_DRIVER_H
#define PANEL_DIAGNOSTIC_PARTIAL_PROBE_DRIVER_H

#include <GxEPD2_SOLUM_097c_960x672.h>

// CONTRATTO DELLE API DEL PARTIAL, ed è dove si sbaglia: sotto la LUT custom la
// RAM 0x26 non è l'accent ma il FRAME PRECEDENTE in polarità BW, cioè deve
// contenere quello che sta sul vetro adesso. Dopo un refresh pieno ci sta invece
// l'accent, quindi il PRIMO partial di una catena va preceduto dall'allineamento
// di 0x26. La regola operativa è più semplice di come suona: basta portare 0x26
// uguale a 0x24, perchè così i pixel fuori dall'area che cambia finiscono su
// LUT0 o LUT3, che sono a zero, e non vengono pilotati qualunque cosa mostri il
// vetro. Su un fondo uniforme è una sola chiamata a writeScreenBufferPrevious()
// (9 ms, nessun buffer); su un fondo qualsiasi la si completa con
// writeImageToPrevious() sulle sole aree che differiscono dal fondo. Dal secondo
// partial in poi ci pensa drawImagePartial(), che riallinea 0x26 da sè.
//
// Un frame aggiornato in partial non scrive rosso, e quello già sul vetro resta
// fermo. Le aree mai pilotate non degradano lungo la catena (undici passate
// consecutive non hanno toccato nè i testimoni nè il fondo), mentre nell'area di
// lavoro si accumula un pavimento di grigio che solo un refresh pieno azzera,
// uno ogni 5-10 passate.
//
// USCIRE dalla catena non richiede nessun passo esplicito: alla fine 0x26
// contiene il frame precedente in polarità BW, cioè bit a 1 dove il vetro è
// bianco, e un refresh pieno quella RAM la rilegge come accent, quindi 0xFF
// vorrebbe dire rosso. Il driver la ripulisce da sè in _Update_Full(), in
// setPaged() e a ogni scrittura di un accent vero, perchè i due soli punti che
// portano 0x26 allo stato Previous sono writeScreenBufferPrevious() e
// writeImageToPrevious(), e un frame precedente non può mai essere un accent
// legittimo.
//
// Dentro una catena NON vanno usate writeImage(bitmap, ...), writeImagePart(
// bitmap, ...) nè le drawImage() che ci passano sopra: sono le API a canale
// singolo che ripuliscono 0x26 se non è Clean, e siccome writeImageToPrevious()
// la porta a Previous ne azzererebbero proprio il frame precedente. Per il piano
// BW dentro la catena c'è writeImageBlack(), che 0x26 non lo guarda.
class GxEPD2_SOLUM_097c_PartialProbe : public GxEPD2_SOLUM_097c_960x672
{
  public:
    // Stessi costruttori del driver, quello nativo e quello a pinout uniforme.
    using GxEPD2_SOLUM_097c_960x672::GxEPD2_SOLUM_097c_960x672;

    /** Scrive una bitmap nel piano nero (0x24), senza refresh e senza invert: bit
     *  a 1 dove il pixel è bianco. Passa da _ensurePartialRamDefined(), quindi può
     *  essere la prima scrittura di una catena dopo un init o un hibernate. */
    void writeImageBlack(const uint8_t* bitmap, int16_t x, int16_t y,
                         int16_t w, int16_t h, bool pgm = true);

    /** Scrive una bitmap nel piano accent (0x26), senza refresh. La bitmap ha bit
     *  a 1 dove il pixel NON è accent, il formato di epd_image_converter.pyw e
     *  image2cpp: il driver la inverte per allinearla alla polarità del
     *  controller, dove il bit a 1 è accent acceso. Non maschera 0x24 sotto i
     *  pixel accesi, e non serve: la Table 6-4 dà LUT2 = LUT3 e la misura sul
     *  pannello lo conferma. Ripulisce prima 0x26 se ci trova un frame precedente
     *  del partial: è una scrittura d'area, e quello che resta fuori verrebbe
     *  riletto come rosso. */
    void writeImageRed(const uint8_t* bitmap, int16_t x, int16_t y,
                       int16_t w, int16_t h, bool pgm = true);

    /** Riempie tutta la RAM 0x26 con un valore costante in polarità BW
     *  (0xFF = frame precedente tutto bianco) usando il pattern hardware del
     *  controller: 9 ms e nessun buffer, contro gli 80.640 byte che servirebbero
     *  a writeImageToPrevious() a schermo pieno. È il modo normale di aprire una
     *  catena di partial. */
    void writeScreenBufferPrevious(uint8_t value = 0xFF);

    /** Override del virtuale nativo di GxEPD2_EPD: scrive la RAM 0x26 come
     *  frame precedente, in polarità BW (bit=1 = bianco), come fanno i driver
     *  SSD1677 di GxEPD2. Porta 0x26 allo stato Previous: il prossimo disegno a
     *  colori, e qualunque refresh pieno, la ripuliscono. */
    void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                              bool invert = false, bool mirror_y = false, bool pgm = false) override;

    /** Passata di partial sulla RAM già scritta. Senza coordinate, e non è una
     *  semplificazione: la finestra di 0x44/0x45 non confina il refresh, il
     *  pannello viene scandito tutto e a limitare l'area ridipinta è la LUT,
     *  che non pilota i pixel il cui bit è uguale nelle due RAM. */
    void refreshPartial();

    /** Overload con il rettangolo, per simmetria con refresh(x, y, w, h). Le
     *  coordinate sono DICHIARATIVE e vengono ignorate, per la stessa misura
     *  citata sopra: una fascia scritta in RAM ma mai compresa in nessuna
     *  finestra di refresh è comparsa lo stesso. Esiste perchè un chiamante che
     *  ragiona per aree possa dire quale intende, senza credere che il driver
     *  stia confinando qualcosa. */
    void refreshPartial(int16_t x, int16_t y, int16_t w, int16_t h);

    /** Il ciclo completo su una bitmap B/N: la scrive in 0x24, fa la passata di
     *  partial e la ricopia in 0x26, così il frame precedente resta allineato
     *  al vetro e il partial successivo non ha bisogno di preparazione.
     *  È drawImage() più writeImageAgain() del GxEPD2_1160_T91.
     *
     *  invert è la chiave per scriverci TESTO E FORME senza comporre la bitmap
     *  a mano: Adafruit_GFX ha già GFXcanvas1, una tela a 1 bit con tutte le
     *  primitive di disegno, che però mette a 1 i pixel disegnati mentre in
     *  0x24 il bit 1 è il BIANCO. Con invert = true e pgm = false il buffer
     *  della tela entra così com'è. La ricetta completa sta nel README della
     *  libreria.
     *
     *  x E w VANNO MULTIPLI DI 8, ed è un vincolo del controller e non una
     *  preferenza: sull'asse source la finestra lavora per byte. Una x non
     *  allineata viene arrotondata per DIFETTO, quindi l'immagine atterra fino
     *  a sette pixel più a sinistra senza che nessuno lo segnali; una w non
     *  allineata viene arrotondata per eccesso, e i pixel di riempimento della
     *  tela finiscono sul vetro. */
    void drawImagePartial(const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool invert = false, bool pgm = true);

    /** Come drawImagePartial() ma su un rettangolo di una bitmap più grande,
     *  con la stessa forma di writeImagePart(): serve a ridipingere un riquadro
     *  preso da una tela unica senza doverla ritagliare. Valgono gli stessi
     *  vincoli di allineamento della gemella, su x, w e x_part. */
    void drawImagePartialPart(const uint8_t* bitmap,
                              int16_t x_part, int16_t y_part,
                              int16_t w_bitmap, int16_t h_bitmap,
                              int16_t x, int16_t y, int16_t w, int16_t h,
                              bool invert = false, bool pgm = true);

    /**
     * Sostituisce la waveform del partial con una fornita dal chiamante.
     * Il buffer deve essere di LUT_PARTIAL_BYTES byte nel layout del waveform
     * setting SSD1677: 0..104 per 0x32, 105 VGH, 106..108 VSH1/VSH2/VSL,
     * 109 VCOM. Il driver ne manda 0..104 e le tensioni di sorgente, come con
     * la LUT di default; VGH e VCOM restano dell'OTP.
     * Memorizza il puntatore, che deve restare valido, e abbassa
     * _using_partial_mode così che la prossima passata la ricarichi. nullptr
     * torna alla LUT di default.
     * La taratura di questo esempio rispecchia la stessa sequenza a SPI diretta,
     * e ne misura le varianti sul vetro.
     */
    void setPartialLut(const uint8_t* lut);

  protected:
    /** Porta le due RAM a uno stato definito prima della PRIMA passata di
     *  partial, e va chiamata da ogni porta che può aprire una catena. Senza,
     *  _writeImagePart() chiamerebbe writeScreenBuffer(), che azzera 0x26 a 0x00
     *  cioè frame precedente tutto NERO, e ogni pixel bianco cadrebbe su LUT1
     *  facendo pilotare tutto lo schermo.
     *  I due piani si scrivono SEPARATAMENTE di proposito: se il chiamante ha
     *  già allineato 0x26 senza toccare 0x24, una writeScreenBuffer() gli
     *  cancellerebbe quel lavoro sostituendolo col caso peggiore. */
    void _ensurePartialRamDefined();

    /** La passata di partial che cambia il vetro fuori dal differenziale, che
     *  quindi perde il suo riferimento. */
    void _partialPass();
};

// Scrive la bitmap nel piano nero. Dopo un init o un hibernate _writeImage()
// riempirebbe 0x26 di 0x00, cioè frame precedente tutto nero, e la prossima
// passata di partial pilotrebbe tutto lo schermo: la guardia porta prima le due
// RAM al bianco.
inline void GxEPD2_SOLUM_097c_PartialProbe::writeImageBlack(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _ensurePartialRamDefined();
  _writeImage(0x24, bitmap, x, y, w, h, false, false, pgm);
}

inline void GxEPD2_SOLUM_097c_PartialProbe::writeImageRed(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeAccent(bitmap, 0, 0, w, h, x, y, w, h, false, false, pgm);
}

// Scrive il frame precedente in 0x26. Nessun invert, al contrario di
// writeImageRed(): sotto la LUT del partial quella RAM non è l'accent ma il
// piano BW del frame che sta sul vetro, quindi vale la stessa convenzione di
// writeImageBlack() (bit=1 = bianco).
inline void GxEPD2_SOLUM_097c_PartialProbe::writeScreenBufferPrevious(uint8_t value)
{
  // Non basta l'init: dopo un hibernate _InitDisplay() riarma _initial_write e
  // questo metodo, che non passa da _writeImage(), non lo consumerebbe mai. La
  // prima scrittura successiva farebbe allora scattare writeScreenBuffer(),
  // che azzera 0x26 a 0x00 cioè frame precedente tutto NERO, cancellando
  // proprio l'allineamento appena fatto.
  _ensurePartialRamDefined();
  _writeScreenBuffer(0x26, value);
  // In 0x26 non c'è più un accent pulito ma un frame precedente: lo stato
  // Previous fa ripulire quella RAM a un refresh pieno o all'inizio di un loop
  // paged.
  _markPreviousInColorRam();
}

inline void GxEPD2_SOLUM_097c_PartialProbe::writeImageToPrevious(const uint8_t bitmap[],
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!bitmap) return;
  // Stessa guardia di writeImageBlack(): può essere la prima scrittura dopo un
  // init o un hibernate.
  _ensurePartialRamDefined();
  _writeImage(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm);
  // In 0x26 non c'è più un accent pulito: il prossimo disegno a colori deve
  // ripulirla prima di usarla come piano rosso.
  _markPreviousInColorRam();
}

// La guardia definisce la RAM, non solo il display: il solo init, dopo un
// hibernate, riarmerebbe _initial_write senza consumarlo e la passata girerebbe
// contro due piani indefiniti. Così invece i piani partono bianco e bianco,
// ogni pixel cade su LUT3 che è a zero, e una chiamata fuori posto diventa un
// no-op invece di pilotare tutto lo schermo. In una catena normale il flag è
// già basso e l'helper esce subito.
// Il no-op però NON è gratis, ed è misurato: la passata parte davvero e costa
// 787 ms fra i 639 di BUSY, il power on e il power off. La guardia rende
// innocua una chiamata fuori posto, non la intercetta.
inline void GxEPD2_SOLUM_097c_PartialProbe::refreshPartial()
{
  _clearBusyFault();
  _ensurePartialRamDefined();
  _partialPass();
}

// Le coordinate sono dichiarative: vedi il commento della dichiarazione.
inline void GxEPD2_SOLUM_097c_PartialProbe::refreshPartial(int16_t /*x*/, int16_t /*y*/,
                                                           int16_t /*w*/, int16_t /*h*/)
{
  refreshPartial();
}

// Stato definito prima della prima passata di partial: vedi il commento della
// dichiarazione per il perchè i due piani si scrivono separatamente.
// Il frame nuovo va scritto perchè la RAM vergine è indefinita, e 0x24 = bianco
// codifica la premessa di questa strada: il vetro è bianco, cioè lo stato in
// cui lo lascia una clearScreen(). Un vetro non bianco richiede al chiamante di
// scrivere 0x24 per intero, cosa che azzera già _initial_write e fa saltare
// questa guardia.
inline void GxEPD2_SOLUM_097c_PartialProbe::_ensurePartialRamDefined()
{
  // L'init va PRIMA del controllo del flag, e l'ordine non è indifferente: è
  // _InitDisplay() a riarmare _initial_write quando si arriva da un hibernate,
  // perchè il deep sleep lascia la RAM indefinita. Controllando il flag prima,
  // una catena aperta subito dopo un risveglio troverebbe la guardia già
  // passata e girerebbe contro due piani di contenuto casuale.
  if (!_init_display_done) _InitDisplay();
  if (!_initial_write) return;
  /** Abbassato PRIMA delle scritture. Da qui si entra nelle primitive che
   *  definiscono i due piani, e writeScreenBufferPrevious() passa a sua volta
   *  di qui: col flag ancora alto la rientranza non terminerebbe. Le due
   *  scritture sotto usano quindi la primitiva _writeScreenBuffer(), che non
   *  rientra. */
  _initial_write = false;
  _writeScreenBuffer(0x24, 0xFF);                                // frame nuovo bianco
  if (_ram26 != Ram26::Previous)
  {
    _writeScreenBuffer(0x26, 0xFF);                              // precedente bianco
    _markPreviousInColorRam();
  }
}

// Questa API presuppone 0x26 = frame precedente e cambia il vetro fuori dal
// differenziale, che quindi perde il suo riferimento.
inline void GxEPD2_SOLUM_097c_PartialProbe::_partialPass()
{
  _diff.invalidate();
  _Update_Part(false);
}

// Il ciclo completo su una fascia B/N. La ricopia finale in 0x26 costa il push
// SPI dell'area (18 ms su una fascia di 168 righe, 73 a schermo pieno) e serve
// a lasciare il frame precedente allineato al vetro: senza, il partial
// successivo confronterebbe il nuovo frame con quello di due passate fa e
// ripilotarebbe pixel che non sono cambiati.
//
// Il ping-pong hardware delle due RAM (0x37, bit F[6]) farebbe la ricopia nel
// controller, ma non conviene: alterna i ruoli delle due RAM a ogni passata,
// mentre questa LUT non è simmetrica (LUT1 diverso da LUT2), quindi
// servirebbero due waveform alternate per risparmiare 18 ms.
//
// Non passa da writeImageBlack() e writeImageToPrevious() ma scrive i due piani
// direttamente, perchè quelle due firme non hanno invert e cambiargliela
// romperebbe i chiamanti che passano pgm per posizione. L'esito del BUSY riparte
// da qui e la passata lo ritrova, senza passare da refreshPartial() che lo
// azzererebbe di nuovo.
inline void GxEPD2_SOLUM_097c_PartialProbe::drawImagePartial(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool pgm)
{
  if (!bitmap) return;
  _clearBusyFault();
  _ensurePartialRamDefined();
  // Se il rettangolo non ha superficie sullo schermo non si lancia la passata:
  // costerebbe 640 ms per niente e porterebbe 0x26 a Previous a vuoto.
  if (!_writeImage(0x24, bitmap, x, y, w, h, invert, false, pgm)) return;
  _partialPass();
  _writeImage(0x26, bitmap, x, y, w, h, invert, false, pgm);   // ora è il precedente
  _markPreviousInColorRam();
}

// Gemella della precedente su un rettangolo di una bitmap più grande. Stessa
// sequenza e stessa invariante, con _writeImagePart al posto di _writeImage.
inline void GxEPD2_SOLUM_097c_PartialProbe::drawImagePartialPart(const uint8_t* bitmap,
    int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool pgm)
{
  if (!bitmap) return;
  _clearBusyFault();
  _ensurePartialRamDefined();
  // Come nella gemella: niente passata per un rettangolo che nessuno dipinge.
  if (!_writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap,
                       x, y, w, h, invert, false, pgm)) return;
  _partialPass();
  _writeImagePart(0x26, bitmap, x_part, y_part, w_bitmap, h_bitmap,
                  x, y, w, h, invert, false, pgm);             // ora è il precedente
  _markPreviousInColorRam();
}

// Sostituisce la waveform del partial. Vedi la dichiarazione per il contratto.
inline void GxEPD2_SOLUM_097c_PartialProbe::setPartialLut(const uint8_t* lut)
{
  _lut_partial = lut ? lut : GxEPD2_SOLUM_097c_lut_partial;
  // La waveform nel controller è quella vecchia: la prossima passata deve
  // ripassare da _Init_Part().
  _using_partial_mode = false;
  // E il booster va fatto riscendere, altrimenti le tensioni della nuova
  // waveform vengono scritte ad analog già acceso e _PowerOn() salta: la
  // passata girerebbe con quelle di prima. Costa 250 ms, e solo a chi cambia
  // waveform a sessione aperta.
  if (_power_is_on) _PowerOff();
}

#endif
