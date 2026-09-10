// =============================================================================
// Driver custom per pannello e-paper SOLUM 9.7" 672w x 960h (native portrait) su ESP32.
// Convenzione: NwxMh = N px larghezza (X) x M px altezza (Y).
//
// Origine:
//   - Libreria base: GxEPD2 (https://github.com/ZinggJM/GxEPD2).
//   - Driver di partenza: GxEPD2_1330c_GDEM133Z91 (Good Display GDEM133Z91,
//     controller SSD1677). La logica di init, write RAM e refresh è ereditata
//     da lì e poi adattata al pannello SOLUM recuperato da ESL dismesse.
//
// Pannello pilotato (personalizzazione rispetto all'originale):
//   - Produttore: SOLUM (modulo ESL 9.7" riusato).
//   - Risoluzione: 672w x 960h native portrait (usato in landscape 960w x 672h dopo setRotation(0)).
//   - Colori: bianco, nero e rosso, tutti e tre misurati sul pannello e
//     pilotati dai comandi 0x24 (BW plane) e 0x26 (accent) del controller
//     SSD1677. Un quarto colore non esiste: vedi la sezione sotto.
//   - Refresh: pieno 24015 ms di BUSY a temperatura ambiente e di più al
//     freddo, più un partial in BIANCO E NERO da 639 ms che gira su una
//     waveform scritta dall'MCU, tensioni di sorgente comprese: vedi la
//     sezione PARTIAL più sotto.
//   - Controller: SSD1677, indirizzamento full-window a finestra parziale.
//   - Alimentazione: 3.3V su VCC e su tutte le data line (non 5V-tolerant).
//
// PARAMETRI MISURATI, DA NON RITOCCARE A OCCHIO.
//   Tutti i numeri di questo driver vengono da examples/097c/panel_diagnostic,
//   e i conti sono già stati fatti: rifarli senza una nuova misura è tempo
//   perso.
//     - full_refresh_time 60000 ms: è solo il delay() di fallback per un
//       display costruito senza pin BUSY, e deve coprire il banco freddo, che
//       misura 59067 ms.
//     - _busy_timeout 120 s, passato al costruttore. Non è sovradimensionato:
//       il refresh con temperatura forzata a 0 °C dura 59067 ms, cioè più del
//       doppio dei 24 s a temperatura ambiente e ben oltre i 40 s che stavano
//       qui prima, e allo scadere del timeout il frame esce troncato. È una
//       guardia, non un'attesa.
//     - Banchi di waveform per temperatura: l'OTP ne ha più di uno, ma nel
//       range utile il guadagno è ~1 s su 24 (40 °C danno 22963 ms contro i
//       24007 di 20 °C) mentre verso il freddo la waveform si allunga fino ai
//       59067 ms di 0 °C. Forzare la temperatura con 0x18/0x1A non è una leva:
//       0x18 = 0x80, sensore interno, resta la scelta giusta.
//       Nota per chi rilegge il log della sonda: la passata di controllo a
//       70 °C, fuori dal range dichiarato, NON è stata rifiutata — ha dipinto
//       in 22961 ms, e sul vetro ha cancellato la fascia dei 40 °C scrivendole
//       sopra bianco. Il datasheet promette un rifiuto quando nessun TR
//       corrisponde; qui un TR corrisponde comunque.
//     - Blocchi SPI da 120 byte per riga in _writeImage: 0,902 us/byte, l'89%
//       del limite teorico a 10 MHz. La curva è piatta sopra i 120 byte, 0,897
//       a 256 e 0,895 a 4096, cioè lo 0,8% di guadagno per un buffer otto volte
//       più grande. Il totale di sessione della sonda, 3.270.960 byte in
//       2.948.736 us, dà 0,901 us/byte sul traffico reale: la stessa cifra da
//       due misure indipendenti.
//     - Pattern hardware 0x47/0x46 per riempire un piano: 8-9 ms contro i
//       73 del push SPI, 8x.
//     - Nessuna leva sull'OTP accorcia il refresh pieno, e sono state provate
//       tutte: il differenziale 0x22 = 0xFC (che non dipinge affatto, vedi il
//       punto qui sotto), la finestra RAM, il MUX ridotto e i banchi per
//       temperatura. Vedi _Update_Full(). La waveform scritta dall'MCU via 0x32
//       invece lo accorcia, ed è il partial qui sotto.
//     - LE SEQUENZE DI MODE 2 DELL'OTP NON DIPINGONO: 0xFF esce in 225 ms e
//       0xFC in 85 senza toccare il vetro, mentre 0xF4, che è Mode 1, dura
//       23871 ms e dipinge. In OTP non c'è nessuna waveform di Mode 2, ed è la
//       ragione per cui il partial deve portarsi la propria. Vedi _Update_Full().
//     - Refresh pieno B/N veloce dall'MCU: PROVATO E SCARTATO, e non perchè sia
//       brutto. La lut_full di GxEPD2_370_TC1 gira in 979 ms contro 24011, cioè
//       25x, ma su questo film NON PILOTA IN MODO COERENTE: lascia in piedi
//       parte della schermata precedente, aggiunge righe verticali grigie
//       sbiadite, e sbiadisce la fascia rossa — di più alla seconda passata.
//       Che il riquadro del numero esca corretto dice che la waveform pilota
//       davvero: è incoerente, non inerte. Vedi anche la nota sulle tensioni
//       nel commento della costante del partial.
//     - 0x13 non esiste in questo silicio: non alza il BUSY e non ha effetti
//       osservabili, quindi la coppia 0x13 + 0x10 del firmware di fabbrica non
//       va imitata. Vedi hibernate().
//     - Power off: 0x83 e 0x03 chiudono in 139 ms contro i 221 di 0xC3, cioè
//       82 ms su un ciclo da 24 s. _PowerOff() resta quindi su 0xC3.
//
// PARTIAL IN BIANCO E NERO, 639 ms MISURATI SUL VETRO ATTRAVERSO IL DRIVER.
//   La waveform è quella del GDEH116T91 (stesso SSD1677, stessi 960 source),
//   caricata via 0x32, e gira con 0x22 = 0xCC: DISPLAY Mode 2 e bit 4 spento
//   perchè l'OTP non sovrascriva la LUT custom.
//
//   DOVE SI COLLOCA, perchè senza il confronto il grigio residuo di cui sotto
//   sembra un difetto e non un compromesso. In GxEPD2 sono cinque i driver a
//   tre colori che espongono un B/N differenziale con refresh_bw(), e le
//   durate che dichiarano con la misura accanto sono queste:
//       GDEY042Z98  4.2"  SSD1683   pieno 22,8 s   B/N 1,46 s, "using refresh_bw"
//       GDEY075Z08  7.5"  UC8179    pieno 26,3 s   il commento cita anche 16,8 s
//       GDEW075Z08  7.5"  GD7965    pieno 17,1 s   costante ripetuta dal pieno
//       GDEW0213Z19 2.13" UC8151D   pieno 16,8 s   costante ripetuta dal pieno
//       GDEH029Z13  2.9"  UC8151D   pieno 17,8 s   costante ripetuta dal pieno
//       questo      9.7"  SSD1677   pieno 24,0 s   B/N 0,64 s misurati
//   Come si legge, perchè partial_refresh_time è un delay di FALLBACK e non una
//   misura: solo il GDEY042Z98 ne dichiara uno distinto, annotandolo "using
//   refresh_bw". Per gli altri tre la costante ripete quella del pieno e non
//   dice niente sul loro differenziale. L'unico paragone davvero misurato è
//   quindi 1,46 s contro i nostri 0,64, su un vetro che ha più del quadruplo
//   dell'area.
//
//   E SCRIVERE LA LUT VIA 0x32 NON È UNA SCELTA DI STILE, È L'UNICA STRADA.
//   Il GDEY042Z98 il suo 1,46 s lo ottiene con 0x22 = 0xDC e nessuna LUT
//   custom: 0xDC è il nostro 0xCC più il bit 4, cioè si fa dare dall'OTP il
//   banco di Mode 2. Su questo pannello quel banco NON ESISTE, e le osservazioni
//   concordi sono quattro (vedi _Update_Full()): la via dell'OTP è chiusa e resta
//   la waveform scritta dall'MCU.
//
//   UN WAVEFORM SETTING È DI 110 BYTE E 0x32 NE SCRIVE 105. Il resto sono le
//   tensioni, e vanno mandate a parte (SSD1677 §6.7, Figura 6-6):
//       byte   0.. 49   VS, dieci byte per LUT0..LUT4, quattro fasi da 2 bit
//       byte  50.. 99   per gruppo TP[nA..nD] e RP[n], un byte ciascuno    0x32
//       byte 100..104   frame rate, dieci nibble FR[0..9]
//       byte     105    VGH                                               0x03
//       byte 106..108   VSH1, VSH2, VSL                                   0x04
//       byte     109    VCOM                                             0x2C
//   Chi scrive quei cinque byte decide la RESA, e il driver deve farlo perchè
//   non c'è nessun altro che lo faccia con i valori giusti:
//     - il SWRESET li riporta ai POR: VSH1 15 V (0x41), VSH2 5 V (0xA8),
//       VSL -15 V (0x32);
//     - un load della waveform dall'OTP, cioè il bit 4 di 0x22 che
//       _Update_Full() accende con 0xF7, li riscrive TUTTI con quelli della
//       waveform BWR di produzione, tarata su 24 secondi.
//   Misurato: la stessa LUT e la stessa sequenza dipingono un nero PIENO con le
//   tensioni POR (è il caso della sonda, che gira dopo un reset e senza load
//   dall'OTP) e un nero PALLIDO con quelle dell'OTP (è il caso del driver, che
//   parte dopo un refresh pieno). Il probe dei livelli di sorgente aveva già
//   misurato su questo film che VSH1 dà il NERO e VSH2 il ROSSO, a POR: il nero
//   di questo pigmento si ottiene a 15 V. Da qui _Init_Part(), che dopo 0x32
//   manda 0x04 con i byte 106..108 della LUT. VGH e VCOM restano dell'OTP di
//   proposito: vedi il commento di _Init_Part().
//
//   TEMPO = 20,0 ms x FRAME + 83 ms, e non dipende dalla tensione. I cinque
//   byte di frame rate valgono 0x22, codice 0010 = 50 Hz (Table 6-7 del
//   SSD1683; il SSD1677 Rev 1.0 non la riporta), gli 83 ms sono la rampa enable
//   clock + enable analog dentro la sequenza di 0x22, che il _PowerOn isolato
//   misura a 82 ms. Validato su QUATTRO punti attraverso il driver — 28, 46, 82
//   e 154 frame, cioè 639, 998, 1715 e 3149 ms di BUSY — che regrediscono a
//   19,92 ms per frame più 82, e su due punti a SPI diretta a 7x di distanza,
//   28 frame in 641 ms e 200 in 4067. Il tempo lo fissano TP e RP, quindi due
//   passate con tensioni diverse misurano la stessa durata e rendono un nero
//   diverso.
//   Il modello vale per le sequenze che NON spengono, ed è il caso di 0xCC:
//   28 frame misurano 640 ms contro 643 attesi, e 0xC4 a 200 frame ne misura
//   4067 contro 4083. Quelle che spengono aggiungono la coda di disable analog
//   più disable OSC, circa 140 ms: 0xCF a 38 frame misura 979 ms contro gli 843
//   del modello, e il power off 0xC3 isolato ne misura 221 da solo.
//
//   L'INDICE DI LUT È (bit di 0x26, bit di 0x24) = (FRAME PRECEDENTE, FRAME
//   NUOVO), sempre: Table 6-4 e Table 6-5 indicizzano entrambe così, e fra le
//   due cambia solo l'aliasing che la waveform di fabbrica mette nelle LUT.
//   Quindi 0x26 cambia significato, ed è tutto il contratto dell'API. Vedi
//   writeImagePrevious() e drawImagePartial().
//   Tre conseguenze che si pagano se ignorate:
//     - un frame aggiornato in partial è per forza SENZA ROSSO, perchè 0x26 sta
//       facendo il frame precedente e non l'accent. La scelta è per frame e non
//       per pixel. Il secondo motivo, indipendente, è che la LUT non porta mai
//       la sorgente a VSH2, che su questo film è la tensione del rosso.
//     - la confinatura la dà la LUT e non la finestra: LUT0 e LUT3 sono a zero,
//       quindi i pixel il cui bit non cambia fra 0x26 e 0x24 non vengono
//       pilotati. La misura gira con la finestra PIENA, ed è la configurazione
//       che _Update_Part() riproduce.
//     - il partial vive FUORI dal template, e le due ragioni vanno tenute
//       distinte perchè agiscono su leve diverse.
//       La prima: in modalità finestra parziale GxEPD2_3C scrive il piano
//       accent dentro 0x26 a OGNI pagina (GxEPD2_3C.h:340), cioè proprio la RAM
//       che qui è il frame precedente. Questo dipende da hasPartialUpdate più
//       setPartialWindow() e NON dal flag di fast partial: a proteggere è
//       refresh(x, y, w, h), che manda a _Update_Full(), così l'esito è un
//       frame corretto e lento invece di un partial rotto.
//       La seconda: hasFastPartialUpdate è consultato in un solo punto
//       (GxEPD2_3C.h:355) e fa una cosa sola, ripetere l'intero loop paged dopo
//       il refresh. Quella seconda passata riscrive i due piani senza
//       rinfrescare e senza riallineare 0x26, quindi sarebbe un rendering e un
//       push interi buttati. Resta false.
//   IL LIMITE VERO, ED È DIREZIONALE: il partial aggiunge inchiostro bene e lo
//   toglie male. Bianco -> nero rende un nero pieno; nero -> bianco NON torna
//   al bianco e lascia un grigio molto leggero. La causa sta nel film e non
//   nella sequenza: su un BWR il pigmento nero e quello rosso hanno la STESSA
//   carica positiva, il bianco negativa, quindi un campo li muove insieme, ma
//   il nero è leggero e veloce e si pilota a +15 V mentre il rosso è pesante e
//   lento e si pilota a 4-7 V. I 28 frame a 50 Hz sono 560 ms di pilotaggio:
//   bastano al pigmento nero e non a quello rosso, che resta disturbato e non
//   si riassesta.
//   NON È UNA CARENZA DELLA LUT, e ora lo dicono CINQUE assi esclusi per
//   misura, non quattro. La taratura del silicio aveva già escluso sette
//   tensioni fra 9 e 15 V con waveform simmetrica, la LUT1 resa duale di LUT2,
//   il multiciclo a frame costanti e tre valori di VCOM. L'ultimo rimasto era
//   la DURATA, e examples/097c/partial_api_check l'ha provata attraverso il
//   driver scalando i TP del gruppo di drive:
//       drive x1    28 frame   nero 765 ms   bianco  682 ms
//       drive x2    46 frame   nero 1124     bianco 1040
//       drive x4    82 frame   nero 1841     bianco 1758
//       drive x8   154 frame   nero 3275     bianco 3192
//   Le quattro bande escono MOLTO SIMILI fra loro, con la x8 appena meglio
//   della x4: il guadagno satura già lì. Moltiplicare per otto il drive, cioè
//   passare da 0,64 a 3,2 secondi, compra un miglioramento marginale del bianco
//   e NIENTE sul nero. Il residuo è quindi del film e non del budget di tempo.
//
//   PERCHÈ 28 FRAME, e non è più un numero ereditato dalla tabella upstream: è
//   il punto in cui il NERO SATURA. Nelle quattro bande il nero esce pieno e
//   uguale, dalla x1 alla x8, quindi ogni frame oltre i 28 si paga in tempo
//   senza comprare resa, e lo pagherebbe per UNA SOLA delle due transizioni.
//   Le dieci fasi a VSS della LUT1 non sono d'altra parte tempo sprecato: la
//   letteratura sui BWR descrive inserzioni ripetute a 0 V come stadio di
//   stabilizzazione delle particelle, e il compositore simmetrico della sonda,
//   che porta tutte le fasi di un gruppo a un solo livello, quelle pause le
//   elimina.
//   Quello che invece NON serve, ed è misurato: le aree MAI pilotate non
//   sbiadiscono, quindi una catena non ha bisogno di refresh pieni periodici
//   per proteggerle. Undici passate consecutive attraverso il driver non hanno
//   degradato nè i due testimoni nè il fondo bianco, e un solo refresh pieno
//   alla fine ha riportato il vetro netto con l'accent rosso saturo. Quello che
//   si vedeva nella sonda a SPI diretta era pilotaggio non voluto, perchè lì
//   0x26 non era allineata e quei pixel cadevano su LUT1 o LUT2. Da non
//   confondere con il pavimento di grigio dell'AREA DI LAVORO, che invece si
//   accumula e che solo un refresh pieno azzera: la letteratura raccomanda di
//   intervallarne uno ogni 5-10 passate.
//   Il DISPLAY MODE, per contro, non è dimostrato necessario: le due varianti
//   della sonda cambiavano LUT e modo insieme, quindi Mode 1 con questa LUT non
//   è mai stato provato. Il registro 0x37 spiega perchè probabilmente non
//   conta: i suoi byte B..F dichiarano, un bit per waveform set WS0..WS35, se
//   quel set è Mode 1 o Mode 2, quindi il bit 3 di 0x22 sceglie quale set
//   caricare dall'OTP, e con una LUT custom il bit 4 è spento e nessun set
//   viene caricato. Resta 0xCC perchè è la configurazione misurata.
//   Una cosa che il log dice e che vale fuori da qui: l'init di fabbrica SOLUM
//   riempie i pattern in 1 ms invece di 8-9, ma usa entry mode 0x02 e finestra
//   X da 959 a 0. Sono 16 ms per frame contro il rischio di specchiare
//   l'immagine: non conviene.
//
// TRE COLORI, MISURATI: NESSUN CANALE GIALLO.
//   Il driver pilota due piani, 0x24 (BW) e 0x26 (accent), e non ne esiste un
//   terzo. La questione è stata chiusa da examples/097c/panel_diagnostic, che
//   ha esercitato tutte e quattro le combinazioni dei due piani sotto la
//   waveform di produzione, e sette evidenze indipendenti concordano:
//
//     1. Codice modello dell'unità, letto sul case: EL097R2CRN (pratica FCC
//        2AFWN-EL097R2CRN, certificazione KC R-R-SLU-EL097R2CRN). Il campo
//        colore display è R, che nella nomenclatura SOLUM (docs/fonti_esterne.md)
//        vale BWR; la linea PRO a quattro colori porta invece la cifra 4.
//        Non è il donor EL097F5C4C: è la generazione R2, precedente.
//     2. Le quattro LUT della Table 6-4, tutte viste sul vetro: (0,0) nero
//        LUT0, (0,1) bianco LUT1, (1,0) e (1,1) ENTRAMBE ROSSE, cioè LUT2 e
//        LUT3 rendono lo stesso colore come la tabella dichiara. Con due bit
//        per pixel le combinazioni sono esaurite: non c'è un quinto stato.
//     3. 0x28 è VCOM Sense, non un piano immagine: alla scrittura alza il
//        BUSY per ~10 s (misurati 9953-9968 ms) e non dipinge niente. Il
//        datasheet SSD1677 Rev 1.0 lo dà per tale, la misura lo conferma.
//     4. OpenEPaperLink (docs/openepaperlink/) cataloga il modulo come
//        SOLUM_M3_BWR_97, e la tabella UICR dei tag di fabbrica
//        (nrf52811_tag_fw/tagtype_db.cpp) dà la 9.7" con terzo colore = 0x01,
//        che in quella scala significa BWR (0x02 = giallo, 0x03 = BWRY).
//     5. Sulla linea grande di Good Display, a parità di risoluzione e
//        connettore, i 3 colori stanno su SSD1677 e i 4 su SSD2677
//        (GDEM102Z91 BWR contro GDEM102F91 BWRY, entrambi 960x640 su FPC 24
//        pin).
//     6. I pannelli a 4 colori non usano tre piani da 1 bit: scrivono un solo
//        stream 0x10 a 2 bit per pixel, come il path epdvarbwry di OEPL. Su
//        SSD1677 quel codice è invece Deep Sleep, quindi quella strada qui è
//        fisicamente esclusa.
//     7. Il probe dei livelli di sorgente, che è l'evidenza DIRETTA e l'ultima
//        arrivata: una waveform custom via 0x32 pilota LUT2 a VSH1 e LUT3 a
//        VSH2, tempi identici, e sul vetro le due bande escono di colore
//        DIVERSO — VSH1 dà il nero, VSH2 dà il rosso. Il film separa quindi i
//        due pigmenti per soglia di tensione, che è esattamente come lavora un
//        BWR, e nessuna delle due tensioni tira fuori un giallo. La domanda che
//        restava aperta ha una risposta misurata, non dedotta. Riprodotta poi
//        in una seconda campagna, e concorde con la letteratura sui BWR, che dà
//        il nero pilotato a +15 V e il rosso a 4-7 V: i valori che
//        _Init_Part() scrive con 0x04 sono il punto di lavoro pubblicato del
//        film, non default presi per comodità.
//
//   Conseguenza pratica per chi compone immagini: poichè LUT2 = LUT3, sotto un
//   pixel di accent il valore del piano BW è INDIFFERENTE. Scrivere l'accent
//   non richiede di mascherare 0x24, e writeImageRed() non lo fa.
//
//   Un residuo da non riaprire per errore: il datasheet SOLUM della linea PRO
//   dichiara PIXEL COLORS = BWRY per la taglia 9.7", ma riguarda la linea PRO
//   (campo colore 4) e non questa unità. Resta fuori portata la LUT4 del
//   silicio, che due bit di RAM non sanno indirizzare; la waveform custom che
//   separa le due tensioni di sorgente è invece stata provata, ed è l'evidenza
//   7 qui sopra.
//
// Requisiti build:
//   - HW SPI (HSPI su ESP32 tramite la Waveshare E-Paper ESP32 Driver Board).
//   - Target ESP32 (Arduino core): i delay(1) di yield WDT ESP8266 sono
//     stati rimossi dai hot path; il firmware deve girare solo su ESP32.
//   - Adafruit_GFX opzionale: se ENABLE_GxEPD2_GFX=0 la libreria compila
//     senza le primitive grafiche (risparmio ~15 KB di flash).
//
// Aggiunte custom rispetto alla base GxEPD2:
//   - GxEPDImage::showImage(display, descriptor) come UNICO entry-point
//     pubblico per stampare un'immagine. Free function template (vive nel
//     namespace GxEPDImage del .h, non come metodo classe) che accetta
//     descrittori BW / BWR e va chiamata dentro un loop paged
//     firstPage()/nextPage() del template GxEPD2_3C. Un descrittore a tre
//     piani è accettato e ne rende i primi due.
//   - 2 API siblings writeImageBlack / writeImageRed per scrittura
//     single-channel diretta sul controller (no GFX), per compositing manuale
//     fuori dal loop paged.
//
// display.drawPixel(x, y, GxEPD_YELLOW) FINISCE SUL ROSSO, ED È CORRETTO:
//   il template upstream GxEPD2_3C tratta GxEPD_YELLOW come GxEPD_RED, in
//   GxEPD2_3C.h il drawPixel ha la condizione
//     else if ((color == GxEPD_RED) || (color == GxEPD_YELLOW))
//       _color_buffer[i] = ... // scrive nel piano red
//   e su questo pannello è l'unico esito possibile, perchè un terzo colore non
//   c'è. Non è più una trappola da aggirare: chi scrive GxEPD_YELLOW ottiene
//   il solo accent che il film ha.
//
// PAGE-TRACKING di showImage:
//   showImage skippa le righe sorgente che non intersecano la page corrente
//   del template GxEPD2_3C, riducendo il loop pixel a 1/8 delle iterazioni
//   complessive (7 passate da ~24 ms evitate su 8, ~170 ms risparmiati per
//   refresh full-screen). Per dedurre quale page è in corso (il template
//   tiene _current_page private senza getter pubblico) il driver mantiene un
//   counter _show_image_page_hint:
//     - reset a 0 dentro setPaged() (override del virtual base, chiamato da
//       GxEPD2_3C::firstPage() del template all'inizio di ogni loop paged)
//     - avanzato dentro writeImage(black, color, ...) (chiamato dal template
//       da nextPage() ESATTAMENTE una volta per page in full-window mode)
//     - reset difensivo dentro _Update_Full() al refresh finale
//
//   Conseguenza: showImage può essere chiamata 0, 1 o N volte all'interno
//   di una stessa page senza desincronizzare il counter — il counter avanza
//   solo quando il template chiude la page con writeImage(black, color).
//
//   LIMITAZIONE residua (irrilevante per il progetto): in modalità partial
//   window del template (setPartialWindow), nextPage() salta il writeImage()
//   per le pages che non intersecano la window — il counter si disallinea
//   per quelle iterazioni. Il progetto attuale usa solo setFullWindow,
//   quindi non incappa in questo edge case.
//
// Author: Mattia Alesi
// =============================================================================

#ifndef _GxEPD2_SOLUM_097c_960x672_H_
#define _GxEPD2_SOLUM_097c_960x672_H_

#include <GxEPD2_EPD.h>

// Sistema di descrittori immagine e showImage(): condivisi con gli altri driver
// della libreria, vedi src/GxEPDImage.h.
#include "GxEPDImage.h"

// Pinout uniforme fra i driver della libreria.
#include "GxEPD2_SOLUM_Pins.h"

// ---------------------------------------------------------------------------
// WAVEFORM DEL PARTIAL IN BIANCO E NERO.
//
// Sta prima della classe perchè ne inizializza un membro. È il waveform
// setting COMPLETO del SSD1677, 110 byte, non i soli 105 che 0x32 accetta:
//   byte   0.. 49   VS, dieci byte per LUT0..LUT4, quattro fasi da 2 bit
//   byte  50.. 99   dieci gruppi da { TP[nA], TP[nB], TP[nC], TP[nD], RP[n] }   0x32
//   byte 100..104   frame rate, dieci nibble FR[0..9]
//   byte     105    VGH                                                        0x03
//   byte 106..108   VSH1, VSH2, VSL                                            0x04
//   byte     109    VCOM                                                       0x2C
// _Init_Part() manda 0..104 e 106..108; VGH e VCOM restano dell'OTP, e il
// perchè sta nel commento di quella funzione.
//
// I byte 0..104 sono quelli di GxEPD2_1160_T91::lut_partial, non ritoccati:
// GDEH116T91, stesso SSD1677 e stessi 960 source. Le tensioni sono i POR del
// controller, ed è una scelta misurata e non un default preso per pigrizia:
// il probe dei livelli di sorgente, che gira dopo un reset e quindi a POR, ha
// visto su questo film VSH1 dare il NERO e VSH2 il ROSSO, e la sonda del
// partial, anch'essa a POR, ha dipinto un nero pieno con questa stessa LUT.
//
// Come si legge la parte VS, e perchè funziona qui: l'indice di LUT è la coppia
// (bit di 0x26, bit di 0x24) = (frame precedente, frame nuovo), che è come
// indicizzano sia la Table 6-4 sia la Table 6-5 del datasheet.
//   LUT0 (0,0) e LUT3 (1,1)  a ZERO: il pixel non cambia e non viene pilotato,
//                                    ed è questa la confinatura del partial
//   LUT1 (0,1)               nero -> bianco
//   LUT2 (1,0)               bianco -> nero
// TP e RP sono per GRUPPO e non per LUT: un solo set di lunghezze di fase
// condiviso da LUT0..LUT4, quindi le due transizioni stanno negli stessi 28
// frame (10 nel gruppo 0 e 18 nel gruppo 1, nessuno ripetuto) e non si può
// allungare il drive del nero senza allungare quello del bianco. Al frame rate
// 0x22, cioè 50 Hz, 28 frame fanno 560 ms, più 83 ms di rampa clock e analog:
// i 639 ms misurati.
//
// LA RIPARTIZIONE PER LUT, che è quella che decide la resa. Ogni fase dura TP
// frame, e con TP del gruppo 0 = {0, 0, 5, 5} e del gruppo 1 = {5, 3, 5, 5} i
// 28 frame si distribuiscono così:
//   LUT1 nero -> bianco    gruppo 0:  5 fr VSS +  5 fr VSH1
//                          gruppo 1:  5 fr VSS + 13 fr VSL
//                          = 13 frame nel verso utile, 10 fermi a VSS
//   LUT2 bianco -> nero    gruppo 0: 10 fr VSL
//                          gruppo 1: 18 fr VSH1
//                          = 18 frame nel verso utile, nessuno a VSS
// Il bianco riceve quindi meno drive del nero, il che spiega perchè il nero
// riesce meglio. NON è però la causa del grigio residuo, ed è misurato:
// rendere LUT1 il duale esatto di LUT2, cioè 10 frame di reset e 18 di drive,
// non pulisce. Le fasi a VSS non sono tempo sprecato ma stabilizzazione delle
// particelle: vedi la sezione PARTIAL in testa al file.
//
// Perchè questa waveform NON SPORCA DI ROSSO, e l'ordine degli argomenti conta
// perchè il più ovvio non è quello che regge.
//
// LA PROVA È LA MISURA: in una catena di partial il testimone rosso resta
// FERMO. Otto alternanze consecutive più le passate di contorno non lo hanno
// spostato, e un refresh pieno alla fine lo ritrova saturo. È questo che
// autorizza a usare il partial su un vetro che deve conservare l'accent.
//
// E LA PROTEZIONE È STRUTTURALE, NON TEMPORALE, che è il punto su cui è facile
// sbagliare. Il confronto lo rende netto: la lut_full del 370_TC1 sbiadisce
// l'accent in 979 ms, mentre questa LUT allungata fino a 154 frame non lo tocca
// in 3149 — cinque volte il drive, un sesto del danno, anzi nessuno. A separare
// le due non è quindi la durata ma com'è fatta la waveform: due sole LUT
// popolate, nessuna fase a VSH2, VGH e VCOM lasciati all'OTP.
//
// L'argomento sulle tensioni è un INDIZIO DI SUPPORTO, non una garanzia, e va
// letto sapendolo. Decodificando i 50 byte di VS a due bit per fase (00 VSS,
// 01 VSH1, 10 VSL, 11 VSH2) si vede che LUT1 e LUT2 usano solo VSH1 e VSL, e
// che il code point 11 non compare mai; su questo pannello VSH1 muove il
// pigmento NERO e VSH2 quello ROSSO, quindi la waveform resta sotto la soglia
// del rosso. Ma NON BASTA: la lut_full del 370_TC1, che il code point 11 non lo
// contiene affatto, ha comunque SBIADITO la fascia rossa, e di più alla seconda
// passata. La ragione è nella fisica del film — nero e rosso hanno la stessa
// carica positiva, quindi un campo li muove insieme — e VSH2 è la soglia per
// PORTARE il rosso, non per non disturbarlo.
// Conseguenza operativa: una waveform nuova va provata sapendo che può
// sbiadire l'accent, e il danno si accumula a ogni passata. Un refresh pieno
// dell'OTP lo rimette.
//
// Resta il secondo motivo, indipendente e strutturale, per cui un frame
// aggiornato in partial è in bianco e nero: 0x26 sta facendo il frame
// precedente e non l'accent. Una waveform che volesse un partial a colori
// dovrebbe portare a VSH2 la LUT dell'accent, ed è terreno mai provato.
// ---------------------------------------------------------------------------
static const uint8_t GxEPD2_SOLUM_097c_lut_partial[110] PROGMEM =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0, pixel fermo
  0x01, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1, nero -> bianco
  0x0A, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2, bianco -> nero
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3, pixel fermo
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4
  0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x03, 0x05, 0x05, 0x00, // TP/RP gruppi 0-1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 2-3
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 4-5
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 6-7
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // gruppi 8-9
  0x22, 0x22, 0x22, 0x22, 0x22,                               // frame rate, 50 Hz
  0x00,                                                       // VGH  20 V,   non inviato
  0x41, 0xA8, 0x32,                                           // VSH1 15 V, VSH2 5 V, VSL -15 V
  0x00                                                        // VCOM,        non inviato
};

class GxEPD2_SOLUM_097c_960x672 : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 960;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 672;
    static const GxEPD2::Panel panel = GxEPD2::GDEM133Z91;
    static const bool hasColor = true;
    static const bool hasPartialUpdate = true; // has partial window addressing, but uses full window refresh
    /** Il partial esiste ed è nel driver, ma non passa dal template. Il flag è
     *  consultato in UN solo punto di GxEPD2_3C (riga 355) e fa una cosa sola:
     *  ripetere l'intero loop paged dopo il refresh, riscrivendo i due piani
     *  senza rinfrescare e senza riallineare 0x26, cioè lavoro buttato.
     *  Da non confondere con la scrittura dell'accent dentro 0x26, che in
     *  modalità finestra parziale avviene a prescindere da questo flag: da
     *  quella protegge refresh(x, y, w, h), che fa un refresh pieno.
     *  Vedi la sezione PARTIAL in testa e drawImagePartial(). */
    static const bool hasFastPartialUpdate = false;
    static const uint16_t power_on_time = 100; // ms, e.g. 82001us
    static const uint16_t power_off_time = 250; // ms, e.g. 222001us
    /** Refresh pieno misurato sul pannello: 24015 ms di BUSY a temperatura
     *  ambiente, vedi examples/097c/panel_diagnostic.
     *  Attenzione: _waitWhileBusy usa questo valore soltanto come delay() di
     *  fallback quando il pin BUSY non è cablato (busy < 0). Con il BUSY
     *  presente il timeout che conta è _busy_timeout, passato al costruttore.
     *  60000 perchè il fallback deve coprire lo stesso banco di waveform freddo
     *  del _busy_timeout: a 0 °C forzati il refresh misura 59067 ms. Il tipo è
     *  uint16_t, quindi 65535 è il massimo esprimibile. */
    static const uint16_t full_refresh_time = 60000;
    /** Durata del partial di _Update_Part(): 639 ms misurati, qui con margine.
     *  Il modello è durata = 20,0 ms x frame + 83 ms, e la LUT del driver ne
     *  usa 28: vedi la sezione PARTIAL in testa. Non si allunga col freddo come
     *  la waveform dell'OTP, perchè la LUT custom porta con sè conteggi di
     *  frame fissi, e non dipende dalle tensioni. Come full_refresh_time vale
     *  solo da delay() di fallback per un display senza pin BUSY, e va
     *  riallineato se si cambia la lunghezza della LUT. */
    static const uint16_t partial_refresh_time = 2000;
    /** Lunghezza del waveform setting che setPartialLut() pretende: i 105 byte
     *  di 0x32 più i cinque delle tensioni. Vedi il commento della costante
     *  GxEPD2_SOLUM_097c_lut_partial. */
    static const uint16_t LUT_PARTIAL_BYTES = 110;
    // constructor
    GxEPD2_SOLUM_097c_960x672(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    /**
     * Costruttore a pinout uniforme: accetta la struct comune ai driver della
     * libreria e legge i quattro campi che servono a questo pannello. Di cs2 e
     * busy2 non ha nulla da fare (single-controller) e i pin del bus li apre
     * lo sketch via selectSPI(), quindi anche quelli vengono ignorati.
     * È la firma che permette a uno sketch di cambiare pannello senza
     * riscrivere la riga di costruzione del display.
     */
    explicit GxEPD2_SOLUM_097c_960x672(const GxEPD2_SOLUM_Pins& pins);
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void clearScreen(uint8_t black_value, uint8_t color_value); // init controller memory and screen
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value); // init controller memory
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false); // screen refresh from controller memory to full screen
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h); // screen refresh from controller memory, partial screen
    void powerOff(); // turns off generation of panel driving voltages, avoids screen fading over time
    void hibernate(); // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)

    // ------------------------------------------------------------------
    // API siblings per scrittura single-channel (senza refresh).
    //
    // Stesso shape per i due piani del controller:
    //   writeImageBlack -> cmd 0x24 (black/white plane, no invert)
    //   writeImageRed   -> cmd 0x26 (accent, invert applicato)
    //
    // Convenzione bitmap input: bit=1 dove il pixel NON appartiene a quel
    // canale (stesso formato prodotto da epd_image_converter.pyw e
    // image2cpp). Il driver applica ~data prima del transfer per l'accent
    // per allinearsi alla polarity del controller (bit=1 nativo = accent ON).
    //
    // Servono al compositing manuale fuori dal loop paged, dove il template
    // GxEPD2_3C non arriva. NON chiamano refresh: è responsabilità del
    // chiamante.
    // ------------------------------------------------------------------
    void writeImageBlack (const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);
    void writeImageRed   (const uint8_t* bitmap, int16_t x, int16_t y,
                          int16_t w, int16_t h, bool pgm = true);

    // ------------------------------------------------------------------
    // API del partial in bianco e nero, 639 ms contro i 24015 del refresh
    // pieno. Stanno fuori dal template GxEPD2_3C e vanno chiamate fuori da un
    // loop paged: il perchè è nella sezione PARTIAL in testa al file.
    //
    // CONTRATTO, ed è dove si sbaglia: sotto la LUT custom la RAM 0x26 non è
    // l'accent ma il FRAME PRECEDENTE in polarità BW, cioè deve contenere
    // quello che sta sul vetro adesso. Dopo un _Update_Full() ci sta invece
    // l'accent, quindi il PRIMO partial di una catena va preceduto
    // dall'allineamento di 0x26. In pratica la regola operativa è più semplice
    // di come suona: basta portare **0x26 uguale a 0x24**, perchè così i pixel
    // fuori dall'area che cambia finiscono su LUT0 o LUT3, che sono a zero, e
    // non vengono pilotati qualunque cosa mostri il vetro. Su un fondo uniforme
    // è una sola chiamata a writeScreenBufferPrevious() (9 ms, nessun buffer);
    // su un fondo qualsiasi la si completa con writeImagePrevious() sulle sole
    // aree che differiscono dal fondo. Dal secondo partial in poi ci pensa
    // drawImagePartial(), che riallinea 0x26 da sè.
    //
    // Un frame aggiornato in partial è senza rosso. La catena invece non ha
    // costo cumulativo: undici passate consecutive non hanno degradato nè i
    // testimoni nè il fondo, quindi non serve nessun refresh pieno periodico.
    //
    // USCIRE dalla catena NON richiede nessun passo esplicito, e la trappola
    // che c'era è chiusa nel driver. Alla fine di una catena 0x26 contiene il
    // frame precedente in polarità BW, cioè bit a 1 dove il vetro è bianco, e
    // un refresh pieno quella RAM la rilegge come ACCENT: 0xFF vuol dire rosso.
    // Una refresh(false) nuda dipingerebbe quindi lo schermo di rosso, e un
    // frame a colori dal loop paged uscirebbe con sette ottavi di rosso spurio,
    // perchè il template riscrive 0x26 una page alla volta. Ci pensa
    // _cleanColorIfPrevious(), chiamata da _Update_Full() e da setPaged():
    // entrambe le trappole sono strutturalmente impossibili. Vedi il commento
    // di _previous_in_color_ram per il perchè quella pulizia non può mai
    // cancellare un accent legittimo.
    //
    // Dentro una catena di partial NON vanno usate writeImage(bitmap, ...),
    // writeImagePart(bitmap, ...) nè le drawImage() che ci passano sopra: sono
    // le API a canale singolo che chiamano _cleanColorIfDirty(), e siccome
    // writeImagePrevious() alza il dirty flag azzererebbero 0x26, cioè proprio
    // il frame precedente. Per il piano BW dentro la catena c'è
    // writeImageBlack(), che il dirty flag non lo guarda.
    // ------------------------------------------------------------------

    /** Riempie tutta la RAM 0x26 con un valore costante in polarità BW
     *  (0xFF = frame precedente tutto bianco) usando il pattern hardware del
     *  controller: 9 ms e nessun buffer, contro gli 80.640 byte che servirebbero
     *  a writeImagePrevious() a schermo pieno. È il modo normale di aprire una
     *  catena di partial. */
    void writeScreenBufferPrevious(uint8_t value = 0xFF);

    /** Scrive la RAM 0x26 come frame precedente, in polarità BW (bit=1 =
     *  bianco) e quindi SENZA l'invert che writeImageRed() applica all'accent.
     *  Alza il dirty flag e quello di frame precedente: il prossimo disegno a
     *  colori, e qualunque refresh pieno, ripuliscono 0x26. */
    void writeImagePrevious(const uint8_t* bitmap, int16_t x, int16_t y,
                            int16_t w, int16_t h, bool pgm = true);

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
     * La taratura di examples/097c/panel_diagnostic rispecchia questa stessa
     * sequenza a SPI diretta, e ne misura le varianti sul vetro.
     */
    void setPartialLut(const uint8_t* lut);

    // L'entry-point pubblico di stampa immagine è la free function template
    // GxEPDImage::showImage(display, desc) definita nel namespace sopra.
    // Va chiamata dentro un loop firstPage()/nextPage() del template GFX.

    // Hook virtual chiamato da GxEPD2_3C::firstPage() (vedi GxEPD2_3C.h:323)
    // all'inizio di ogni loop paged. Override del no-op base in GxEPD2_EPD.h:92.
    // Reset del page-hint per allinearlo a _current_page del template che
    // viene riportato a 0 in firstPage.
    //
    // Qui sta anche l'unico punto in cui si può ripulire 0x26 quando contiene
    // un frame precedente: firstPage() riempie solo il buffer locale e non ha
    // ancora scritto niente sul controller, mentre nextPage() riscriverebbe
    // 0x26 una page alla volta lasciando sette ottavi di frame precedente
    // riletti come accent. Costa 9 ms e solo quando il flag è alto.
    void setPaged() override
    {
      _cleanColorIfPrevious();
      _show_image_page_hint = 0;
    }

    // Getter del page-hint usato da GxEPDImage::showImage come surrogato di
    // _current_page del template GxEPD2_3C (privato, senza getter pubblico).
    // Permette a showImage di skippare a priori le righe sorgente fuori dalla
    // page corrente, riducendo il loop pixel a 1/8 delle iterazioni.
    int16_t showImagePageHint() const { return _show_image_page_hint; }
  private:
    // Pulizia del piano accent: se _color_dirty è attivo scrive 0x00 ovunque
    // (polarity nativa SSD1677 = "accent spento") e resetta il flag.
    // Centralizza la semantica "clean accent" per evitare il bug latente
    // 0xFF (= accent ON ovunque) che esisteva in versioni precedenti.
    void _cleanColorIfDirty();

    /** Gemella della precedente per il caso del partial: se in 0x26 c'è un
     *  frame precedente lo cancella scrivendo 0x00 ovunque, cioè accent spento,
     *  col pattern hardware (9 ms). Chiamata da _Update_Full() e da setPaged(),
     *  cioè nei due soli punti da cui quella RAM può essere riletta come
     *  accent. Se il display non è inizializzato non c'è niente da pulire,
     *  perchè il deep sleep ha già perduto la RAM. */
    void _cleanColorIfPrevious();

    /** Porta le due RAM a uno stato definito prima della PRIMA passata di
     *  partial, e va chiamata da ogni porta che può aprire una catena. Senza,
     *  _writeImage() chiamerebbe writeScreenBuffer(), che azzera 0x26 a 0x00
     *  cioè frame precedente tutto NERO, e ogni pixel bianco cadrebbe su LUT1
     *  facendo pilotare tutto lo schermo.
     *  I due piani si scrivono SEPARATAMENTE di proposito: se il chiamante ha
     *  già allineato 0x26 senza toccare 0x24, una writeScreenBuffer() gli
     *  cancellerebbe quel lavoro sostituendolo col caso peggiore. */
    void _ensurePartialRamDefined();

    /** Da chiamare dopo ogni scrittura in 0x26 fatta come FRAME PRECEDENTE:
     *  alza i due flag, così un refresh pieno o l'inizio di un loop paged
     *  ripuliscono quella RAM prima che venga riletta come accent. */
    void _markPreviousInColorRam() { _color_dirty = true; _previous_in_color_ram = true; }

    void _writeScreenBuffer(uint8_t command, uint8_t value);

    /** Riempimento di un piano immagine tramite i comandi Auto Write RAM for
     *  Regular Pattern del SSD1677 (0x47 per 0x24, 0x46 per 0x26): il pattern
     *  lo genera il controller, quindi sul bus va un solo byte invece di
     *  80.640 e il piano si riempie in 8-9 ms invece di 67, misurati sul
     *  pannello. È la stessa coppia di comandi che il firmware SOLUM di
     *  fabbrica usa in init.
     *  Ritorna false quando il piano o il valore non sono esprimibili come
     *  pattern, e il chiamante ripiega sul transfer SPI. */
    bool _fillPlaneByPattern(uint8_t command, uint8_t value);
    /** Ritorna true se ha davvero scritto: false quando il rettangolo, dopo
     *  il taglio sullo schermo, non ha superficie. Serve alle due
     *  drawImagePartial*(), che senza lancerebbero una passata da 640 ms per
     *  un rettangolo che nessuno dipinge. Gli altri chiamanti lo ignorano. */
    bool _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    /** Come _writeImage(): true se ha scritto, false se non c'era niente da
     *  scrivere. */
    bool _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();

    /** Carica la waveform custom, tensioni di sorgente comprese, e mette il
     *  border in HiZ. */
    void _Init_Part();
    /** Passata di partial: la sequenza cronometrata dalla sonda, 639 ms. */
    void _Update_Part();

    /** Alto quando nel controller c'è la LUT custom del partial al posto di
     *  quella dell'OTP. Lo abbassano _InitDisplay(), perchè il SWRESET rimette
     *  l'OTP, e _Update_Full(), perchè 0xF7 ha il bit 4 di ricarica acceso. */
    bool _using_partial_mode = false;

    // Dirty flag del piano accent: traccia quando la RAM 0x26 contiene dati
    // non puliti dall'ultima writeScreenBuffer(). Permette di saltare il
    // clean pre-draw quando non serve, tipicamente in una catena di immagini
    // B/N consecutive. Il clean costa 8-9 ms misurati, perchè lo fa il
    // pattern hardware del controller e non il bus.
    bool _color_dirty = false;   // 0x26 (accent)

    /** Alto quando in 0x26 c'è un FRAME PRECEDENTE del partial e non un
     *  accent. È un flag distinto da _color_dirty, e la distinzione è ciò che
     *  rende la pulizia automatica sempre corretta: lo alzano soltanto
     *  writeScreenBufferPrevious() e writeImagePrevious(), quindi quando è alto
     *  l'ultima scrittura in quella RAM È un frame precedente, e un refresh
     *  pieno non ha nessun uso possibile per quel contenuto perchè lo
     *  rileggerebbe come accent. Cancellarlo non può quindi mai distruggere un
     *  accent legittimo: qualunque punto che scrive un accent vero abbassa il
     *  flag. */
    bool _previous_in_color_ram = false;   // 0x26 (frame precedente)

    /** Waveform del partial in uso: la costante del driver, oppure quella
     *  passata da setPartialLut(). Mai nullptr. */
    const uint8_t* _lut_partial = GxEPD2_SOLUM_097c_lut_partial;

    // Counter usato da GxEPDImage::showImage per dedurre la page corrente del
    // template GxEPD2_3C, che mantiene _current_page private senza getter.
    // Avanzamento: dentro writeImage(black, color, ...), che il template chiama
    // ESATTAMENTE una volta per page in nextPage() (full-window mode).
    // Reset:
    //   - setPaged() (chiamato da firstPage() del template) → riallinea
    //   - _Update_Full() (al refresh finale) → simmetria difensiva
    int16_t _show_image_page_hint = 0;
};

// =============================================================================
// Implementazione inline dei metodi della classe.
//
// Scelta header-only: l'intero driver vive qui (no compilation unit .cpp).
// Tutti i metodi sono definiti `inline` per permettere l'inclusione da più
// TU senza violare la ODR; nel progetto attuale l'header viene incluso solo
// dal .ino, quindi è sempre una sola TU.
// =============================================================================

inline GxEPD2_SOLUM_097c_960x672::GxEPD2_SOLUM_097c_960x672(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  /** Il sesto argomento di GxEPD2_EPD è _busy_timeout in MICROSECONDI, non una
   *  frequenza SPI: è il tempo oltre il quale _waitWhileBusy smette di attendere
   *  il BUSY, stampa "Busy Timeout!" e prosegue. Allo scadere _Update_Full torna
   *  con il pannello ancora in pilotaggio, e il powerOff() o l'hibernate() che
   *  seguono mandano 0x22 o 0x10 a metà waveform: frame troncato.
   *
   *  Il refresh pieno a temperatura ambiente misura 24015 ms di BUSY, ma la
   *  waveform non è una sola: l'OTP tiene un banco per range di temperatura, e
   *  verso il freddo si ALLUNGA. Con la temperatura forzata a 0 °C la sonda ha
   *  cronometrato 59067 ms, cioè due volte e mezzo il caso caldo e ben oltre i
   *  40 s che stavano qui prima: quel valore non copriva il pannello che lavora
   *  al freddo, che è il caso d'uso di una dashboard meteo.
   *  120 s sono una guardia e non un'attesa: se il BUSY scende prima
   *  _waitWhileBusy esce sul pin e il valore non costa niente. Restano poco
   *  più del doppio del banco più lento misurato. */
  GxEPD2_EPD(cs, dc, rst, busy, HIGH, 120000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

inline GxEPD2_SOLUM_097c_960x672::GxEPD2_SOLUM_097c_960x672(const GxEPD2_SOLUM_Pins& pins) :
  GxEPD2_SOLUM_097c_960x672(pins.cs, pins.dc, pins.rst, pins.busy)
{
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t value)
{
  clearScreen(value, 0x00);
}

inline void GxEPD2_SOLUM_097c_960x672::clearScreen(uint8_t black_value, uint8_t color_value)
{
  writeScreenBuffer(black_value, color_value);
  refresh(false);
}

inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t value)
{
  writeScreenBuffer(value, 0x00);
}

// Init dei due buffer del controller: B/N (0x24) e accent (0x26). Sono i soli
// piani immagine che il pannello ha, quindi qui finisce tutta la RAM che il
// driver conosce. Entrambi si riempiono col pattern hardware, 8-9 ms per piano
// invece dei 67 del bus.
inline void GxEPD2_SOLUM_097c_960x672::writeScreenBuffer(uint8_t black_value, uint8_t color_value)
{
  if (!_init_display_done) _InitDisplay();
  _writeScreenBuffer(0x24, black_value);   // set black/white
  _writeScreenBuffer(0x26, color_value);   // set accent
  _initial_write = false; // initial full screen buffer clean done
  // Dopo una pulizia completa dei buffer il piano accent è "clean" per definizione,
  // e non ci può più essere un frame precedente del partial.
  _color_dirty = false;
  _previous_in_color_ram = false;
}

// Riempie un piano a schermo pieno con un valore costante. Se il controller
// sa generare il pattern da sè la scrittura non passa dal bus; il transfer
// SPI resta come fallback per i casi che il generatore non copre.
inline void GxEPD2_SOLUM_097c_960x672::_writeScreenBuffer(uint8_t command, uint8_t value)
{
  if (_fillPlaneByPattern(command, value)) return;
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(command);
  _startTransfer();
  // Bulk SPI: invece di chiamare _transfer(value) 80640 volte (full-window
  // a WIDTH*HEIGHT/8 byte), pre-riempiamo un buffer di stack con il valore
  // costante e lo scarichiamo a chunk via writeBytes(). Misurato: 0,897 us/byte
  // a blocchi di 256, cioè 73 ms per piano, l'89% del limite teorico del
  // clock a 10 MHz. Entrambi i piani del driver hanno il pattern hardware,
  // quindi questo percorso serve solo a un valore che non sia 0x00 o 0xFF: nel
  // firmware non capita mai, ed è qui perchè writeScreenBuffer è pubblica.
  // Buffer 256 byte: più grande della FIFO 64-byte ESP32 così la
  // primitiva interna gestisce più write concatenate senza overhead extra.
  uint8_t buf[256];
  memset(buf, value, sizeof(buf));
  uint32_t remaining = uint32_t(WIDTH) * uint32_t(HEIGHT) / 8;
  while (remaining > 0)
  {
    uint32_t chunk = remaining > sizeof(buf) ? (uint32_t)sizeof(buf) : remaining;
    _pSPIx->writeBytes(buf, chunk);
    remaining -= chunk;
  }
  _endTransfer();
}

inline bool GxEPD2_SOLUM_097c_960x672::_fillPlaneByPattern(uint8_t command, uint8_t value)
{
  // I due piani immagine, cioè tutti quelli del driver, hanno un comando Auto
  // Write Pattern dedicato. Il ramo di uscita resta come guardia: qualunque
  // altro comando non è un piano e non va riempito per pattern.
  uint8_t pattern_command;
  if (command == 0x24) pattern_command = 0x47;
  else if (command == 0x26) pattern_command = 0x46;
  else return false;
  // Il generatore emette un livello per step, quindi sa esprimere soltanto i
  // due valori a bit uniformi: qualunque altro deve passare dal bus.
  if (value != 0x00 && value != 0xFF) return false;
  /** Finestra piena impostata comunque, per lasciare area e cursore nello
   *  stesso stato in cui li lascia il percorso SPI. */
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  /** A[7] = valore del primo step, A[6:4] = 111 -> step height 680,
   *  A[2:0] = 111 -> step width 960: un unico step copre tutta la RAM nativa.
   *  Il pattern ignora la finestra di 0x44/0x45 e riempie tutti i 960x680;
   *  le 8 gate line oltre la 672 non vengono mai scandite (MUX 671). */
  _writeCommand(pattern_command);
  _writeData(value ? 0xF7 : 0x77);
  // Il controller alza BUSY per tutta la generazione del pattern.
  _waitWhileBusy("_fillPlaneByPattern", 50);
  return true;
}

// Scrive una bitmap B/W sul canale nero (0x24) lasciando l'accent pulito.
// Al primo write _writeImage richiama writeScreenBuffer() che azzera già
// tutto; nei draw successivi 0x26 si pulisce SOLO se il flag dirty è attivo
// (risparmio SPI quando si incatenano draw B/N).
inline void GxEPD2_SOLUM_097c_960x672::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write) _cleanColorIfDirty();
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

inline bool GxEPD2_SOLUM_097c_960x672::_writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  // Note: delay(1) yield WDT rimosso (target ESP32 task WDT 5s, refresh <30ms)
  uint16_t wb = (w + 7) / 8; // width bytes, bitmaps are padded
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return false;
  if (!_init_display_done) _InitDisplay();
  /** La pulizia iniziale va DOPO _InitDisplay(), e l'ordine non è indifferente:
   *  è _InitDisplay() a riarmare _initial_write quando si arriva da un
   *  hibernate, perchè il deep sleep lascia la RAM immagine indefinita.
   *  Controllando il flag prima, la prima scrittura dopo un risveglio troverebbe
   *  la guardia già passata e lascerebbe in 0x26 contenuto casuale, che il
   *  refresh successivo rileggerebbe come accent. Il loop paged non se ne
   *  accorgerebbe, perchè riscrive entrambi i piani per intero, ma le API a
   *  canale singolo e il partial sì. */
  if (_initial_write) writeScreenBuffer(); // initial full screen buffer clean
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  // Bulk SPI: invece di chiamare _transfer(byte) per-byte (overhead ~1.5us
  // per byte su ESP32 a 10 MHz), riempiamo un buffer riga (max WIDTH/8 =
  // 120 byte) e lo flushiamo via _pSPIx->writeBytes(), che usa la FIFO
  // 64-byte e arriva all'89% del limite del clock SPI (~0,9us/byte misurati,
  // 0,902 a blocchi da 120). Su 8 page x 2 canali x 10080 byte = 161.280 byte
  // per refresh, che è il refresh completo di questo pannello a due piani:
  // da ~242 ms a ~145 ms, saving ~97 ms.
  // Buffer dimensionato per la WIDTH max del pannello.
  const int16_t rowBytes = w1 / 8;
  uint8_t rowBuf[120]; // WIDTH(960)/8 = 120
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < rowBytes; j++)
    {
      uint8_t data;
      // use wb, h of bitmap for index!
      uint32_t idx = mirror_y ? j + dx / 8 + uint32_t((h - 1 - (i + dy))) * wb : j + dx / 8 + uint32_t(i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      rowBuf[j] = data;
    }
    _pSPIx->writeBytes(rowBuf, rowBytes);
  }
  _endTransfer();
  return true;
}

// Allineato al fratello writeImage(bitmap[], ...): pulisce l'accent dirty
// prima di scrivere il piano BW, altrimenti il rosso residuo di un draw
// colorato precedente trasparirebbe sotto la zona BW disegnata in part.
inline void GxEPD2_SOLUM_097c_960x672::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!_initial_write) _cleanColorIfDirty();
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

inline bool GxEPD2_SOLUM_097c_960x672::_writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  // Note: delay(1) yield WDT rimosso (target ESP32 task WDT 5s, refresh <30ms)
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return false;
  if ((x_part < 0) || (x_part >= w_bitmap)) return false;
  if ((y_part < 0) || (y_part >= h_bitmap)) return false;
  uint16_t wb_bitmap = (w_bitmap + 7) / 8; // width bytes, bitmaps are padded
  x_part -= x_part % 8; // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w; // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h; // limit
  x -= x % 8; // byte boundary
  w = 8 * ((w + 7) / 8); // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return false;
  if (!_init_display_done) _InitDisplay();
  // Stesso ordine di _writeImage, e per la stessa ragione: vedi il commento lì.
  if (_initial_write) writeScreenBuffer(); // initial full screen buffer clean
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  // Bulk SPI: stesso pattern di _writeImage (vedi commento sopra). Buffer
  // di riga di max WIDTH/8 = 120 byte, flush via writeBytes una volta per
  // riga invece di per-byte transfer().
  // Nota: nel progetto attuale il template GxEPD2_3C usa solo full-window
  // mode (setFullWindow), quindi questo overload non è in hot path. Lo
  // refactoriamo per simmetria con _writeImage.
  const int16_t rowBytes = w1 / 8;
  uint8_t rowBuf[120];
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < rowBytes; j++)
    {
      uint8_t data;
      // use wb_bitmap, h_bitmap of bitmap for index!
      uint32_t idx = mirror_y ? x_part / 8 + j + dx / 8 + uint32_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + uint32_t(y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      rowBuf[j] = data;
    }
    _pSPIx->writeBytes(rowBuf, rowBytes);
  }
  _endTransfer();
  return true;
}

// HOT PATH (paged full-window): GxEPD2_3C::nextPage() in modalità full-window
// chiama questa overload (non writeImagePart) - vedi GxEPD2_3C.h:368.
// Qui non serve nessun cleanup preliminare: il template passa entrambi i piani
// del pannello a ogni page, quindi 0x26 viene riscritto per intero e un
// residuo di accent non può sopravvivere.
inline void GxEPD2_SOLUM_097c_960x672::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) _writeImage(0x24, black, x, y, w, h, invert, mirror_y, pgm);
  if (color)
  {
    _writeImage(0x26, color, x, y, w, h, !invert, mirror_y, pgm);
    _color_dirty = true;
  }
  // GxEPD2_3C::nextPage() ha appena flushato la page corrente sul controller
  // chiamando questo overload (vedi commento sopra). Avanza il page-hint per
  // allinearlo alla prossima iterazione del loop paged: showImage usa il hint
  // per skippare le righe sorgente fuori dalla page corrente.
  _show_image_page_hint++;
}

// HOT PATH (paged): chiamato 8 volte per refresh dal template GxEPD2_3C
// durante nextPage() in modalità partial window. Come il fratello a full
// window non fa cleanup: entrambi i piani arrivano dal template.
inline void GxEPD2_SOLUM_097c_960x672::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) _writeImagePart(0x24, black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  if (color)
  {
    // Scrittura d'area sull'accent: quello che resta fuori dall'area va
    // ripulito, altrimenti un frame precedente del partial ancora in 0x26
    // verrebbe riletto come rosso. Dentro il loop paged ci ha già pensato
    // setPaged(), e qui la chiamata è un no-op; serve a chi usa questa API
    // fuori dal template.
    _cleanColorIfPrevious();
    _writeImagePart(0x26, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, !invert, mirror_y, pgm);
    _color_dirty = true;
  }
}

inline void GxEPD2_SOLUM_097c_960x672::writeNative(const uint8_t* data1, const uint8_t* /*data2*/, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1)
  {
    writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

inline void GxEPD2_SOLUM_097c_960x672::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

inline void GxEPD2_SOLUM_097c_960x672::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

// Entrambi gli overload passano da _Update_Full, e quello con le coordinate le
// ignora: la finestra RAM non confina l'area ridipinta (vedi _Update_Full) e non
// accorcia la waveform — misurata identica su una fascia di 168 righe, sulla
// stessa ristretta anche in X e su 48 righe con Mode 1 — quindi un refresh
// d'area non avrebbe niente da guadagnare.
// Il partial vero è un'altra cosa e ha un'altra porta: drawImagePartial(). Qui
// non entra di proposito, perchè GxEPD2_3C::nextPage() in setPartialWindow
// arriva su questo overload e lo farebbe girare con 0x26 pieno di accent.
inline void GxEPD2_SOLUM_097c_960x672::refresh(bool /*partial_update_mode*/)
{
  _Update_Full(); // always uses full window refresh
}

inline void GxEPD2_SOLUM_097c_960x672::refresh(int16_t /*x*/, int16_t /*y*/, int16_t /*w*/, int16_t /*h*/)
{
  _Update_Full(); // always uses full window refresh
}

inline void GxEPD2_SOLUM_097c_960x672::powerOff()
{
  _PowerOff();
}

// Porta il controller in deep sleep. Protetto contro chiamate multiple:
// se è già _hibernating la funzione non invia nuovamente la sequenza 0x10.
// Il flag dirty viene azzerato perchè al prossimo wake _InitDisplay()
// invocherà SWRESET che riporta la RAM del controller a uno stato noto.
inline void GxEPD2_SOLUM_097c_960x672::hibernate()
{
  if (_hibernating) return;
  _PowerOff();
  if (_rst >= 0)
  {
    /** Deep sleep. I due parametri fanno cose diverse, ed è MISURATO e non
     *  dedotto: 0x10 = 0x01 (A[1:0]=01) addormenta e RITIENE la RAM, mentre
     *  0x10 = 0x03 (A[1:0]=11) addormenta e la PERDE, e al risveglio i due
     *  piani contengono pixel casuali nei tre colori, cioè sono indefiniti e
     *  non azzerati. È il comportamento a due modi che il SSD1683 documenta e
     *  che la Rev 1.0 del SSD1677 non riporta, avendo in tabella solo
     *  A[1:0] = 00 e 11.
     *  La sordità la prova la stessa osservazione: mentre dorme, la sonda gli
     *  manda un riempimento a nero di TUTTA la RAM B/N più un refresh, e da
     *  sveglio lo schermo sarebbe diventato nero. Con 0x01 al risveglio è
     *  tornata l'immagine di prima, quindi nè la scrittura nè il refresh sono
     *  stati eseguiti. Il risveglio costa 234 ms di reset più init.
     *  Si manda 0x03 perchè il firmware fa comunque un refresh pieno a ogni
     *  risveglio: la ritenzione varrebbe i 18 ms del pattern di pulizia al
     *  prezzo di qualche microampere continuo. È anche l'accoppiata che non può
     *  sbagliare, perchè perdere la RAM senza riarmare _initial_write
     *  stamperebbe il frame casuale che la sonda ha fotografato. Per uscire
     *  serve un HW reset, che _InitDisplay() fa già quando _hibernating è
     *  alto. */
    _writeCommand(0x10);
    _writeData(0x03);
    _hibernating = true;
    _init_display_done = false;
    // Il deep sleep non ritiene la RAM del controller, quindi nè un accent
    // sporco nè un frame precedente del partial sopravvivono al risveglio.
    _color_dirty = false;
    _previous_in_color_ram = false;
  }
}

// Imposta l'area RAM parziale del controller SSD1677.
// L'entry mode (comando 0x11) non è più inviato qui: è configurato una sola
// volta in _InitDisplay() per evitare scritture SPI ridondanti ad ogni draw.
inline void GxEPD2_SOLUM_097c_960x672::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  _writeCommand(0x44);
  _writeData(x % 256);
  _writeData(x / 256);
  _writeData((x + w - 1) % 256);
  _writeData((x + w - 1) / 256);
  _writeCommand(0x45);
  _writeData(y % 256);
  _writeData(y / 256);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeCommand(0x4e);
  _writeData(x % 256);
  _writeData(x / 256);
  _writeCommand(0x4f);
  _writeData(y % 256);
  _writeData(y / 256);
}

inline void GxEPD2_SOLUM_097c_960x672::_PowerOn()
{
  if (!_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xc0);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

inline void GxEPD2_SOLUM_097c_960x672::_PowerOff()
{
  if (_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xc3);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
}

inline void GxEPD2_SOLUM_097c_960x672::_InitDisplay()
{
  // Va letto prima del reset, che azzera _hibernating (GxEPD2_EPD::_reset).
  const bool was_hibernating = _hibernating;
  if (_hibernating) _reset();
  delay(10);
  //_waitWhileBusy("_InitDisplay", power_on_time);
  _writeCommand(0x12); //SWRESET
  /** Il SWRESET si attende sul BUSY, che il pannello alza e riabbassa in 2 ms
   *  misurati: al posto dei 200 ms fissi che c'erano prima, in gran parte
   *  attesa a vuoto. I 10 ms che seguono sono il margine dopo la discesa, ed
   *  è la stessa forma che usa il driver GDEM133T91 di GxEPD2 sullo stesso
   *  silicio. Il timeout passato qui vale solo se il BUSY non è cablato. */
  _waitWhileBusy("_InitDisplay SWRESET", 200);
  delay(10);
  /** Soft start. L'ultimo byte è 0x80 e non 0x40 come in GxEPD2 GDEH116T91:
   *  0x80 è il valore dell'init di fabbrica SOLUM ed è anche quello che Good
   *  Display scrive sui propri pannelli BWR con questo controller
   *  (docs/097c/gooddisplay_GDEM102Z91_arduino/). */
  _writeCommand(0x0C);  // Soft start setting
  _writeData(0xAE);
  _writeData(0xC7);
  _writeData(0xC3);
  _writeData(0xC0);
  _writeData(0x80);
  /** MUX = 671 -> 672 gate lines, quante il pannello SOLUM ne ha davvero.
   *  L'upstream GDEM133Z91 programmava 679 (680 linee) perchè quel pannello
   *  è 960x680: erano 8 gate line inesistenti scandite a ogni refresh.
   *  680 è anche il massimo assoluto del controller, e in commercio viene usato
   *  tutto: il Good Display GDEM133T91 è 960x680 con un solo SSD1677 e
   *  programma MUX = 679.
   *  IL TERZO BYTE RESTA 0x00, e la ragione è sul vetro: la variante della sonda
   *  che riproduce GDEQ0426T82, l'unica a mettere B[1] = SM a 1, non perde
   *  qualche pixel — DIMEZZA IL PILOTAGGIO. Le due zone che dovrebbero essere
   *  nere, la fascia del nome e la riga sulle ultime gate, escono GRIGIE a
   *  righe alternate imprecise, mentre il bianco resta bianco: l'artefatto
   *  colpisce solo le aree pilotate, in un pattern alternato per riga, che è
   *  quello che produce una scansione pari-poi-dispari in cui una delle due
   *  passate non prende.
   *  È un'attribuzione e non un isolamento, perchè quella riga di tabella
   *  cambia anche altri campi, ma SM è il solo la cui funzione documentata
   *  produca quel sintomo, e upstream stesso lo commenta "SM (interlaced) ??".
   *  Insieme al bypass della RED RAM qui sopra sono i due idiomi dei driver
   *  SSD1677 recenti provati e bocciati sul vetro. */
  _writeCommand(0x01);  // Set MUX as 671
  _writeData(0x9F);
  _writeData(0x02);
  _writeData(0x00);
  _writeCommand(0x3C); // VBD
  // LUT1 = bianco nella Table 6-4 del datasheet; la stessa numerazione la
  // commenta così il demo Good Display per un BWR su questo controller.
  _writeData(0x01); // LUT1, for white
  _writeCommand(0x18);
  _writeData(0x80);
  // Entry mode x/y increase: impostato una sola volta in init,
  // _setPartialRamArea non deve più riscriverlo ad ogni draw.
  _writeCommand(0x11);
  _writeData(0x03);
  /** 0x21 non viene scritto e resta al POR, cioè entrambi i piani Normal, ed è
   *  una scelta obbligata su un pannello a tre colori: QUEL COMANDO FUNZIONA,
   *  forma a due byte compresa, e scriverlo come fanno i driver SSD1677
   *  recenti COSTEREBBE L'ACCENT.
   *  Misurato sul vetro in due passate della sonda. Con 0x21 = {08 00}, cioè
   *  il BW Inverse dell'init di fabbrica, lo schermo esce in negativo: fondo
   *  nero e fascia del nome bianca con testo nero. Con 0x21 = {40 00}, che è
   *  il Bypass RAM-as-0 sul nibble RED e l'idioma del refresh pieno di
   *  GDEQ0426T82 e GDEM0397T81, la fascia rossa SPARISCE. Corretto su un
   *  monocromatico, distruttivo qui.
   *  La Rev 1.0 documenta 0x21 con un solo parametro, ma la forma a due byte è
   *  usata da tre implementazioni indipendenti su questo controller — init di
   *  fabbrica SOLUM, dualssd.cpp di OpenEPaperLink e il reference
   *  papyrix-reader, che il secondo byte lo commenta "single chip" — ed è il
   *  B[4] ckouten del SSD1683. */
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _init_display_done = true;
  // Il SWRESET ha rimesso in RAM le LUT dell'OTP e le tensioni ai POR, e il
  // border è tornato a 0x01 qui sopra: la waveform custom del partial, se
  // c'era, non c'è più.
  _using_partial_mode = false;
  // Se si arriva da un hibernate, la RAM immagine del controller è INDEFINITA,
  // non azzerata: il deep sleep non la ritiene (datasheet, tabella elettrica,
  // "Cannot retain RAM data"), mentre il SWRESET da solo non la toccherebbe
  // ("Note: RAM are unaffected by this command"). Riarmare _initial_write fa
  // ripulire entrambi i piani alla prima scrittura, 18 ms col pattern
  // hardware: senza, una scrittura a canale singolo più refresh rileggerebbe
  // come accent quello che è rimasto in 0x26.
  if (was_hibernating) _initial_write = true;
}

// Esegue il ciclo di refresh elettroforetico full-window, 24015 ms di BUSY
// misurati a temperatura ambiente. Il byte 0xF7 al cmd 0x22 attiva clock +
// analog + load temp + load LUT + DISPLAY Mode 1 + disable analog + disable
// clock: include power-on/off implicito, perciò non serve chiamare _PowerOn()
// prima nè _PowerOff() dopo (oltre a settare il flag).
//
// La durata è quella della waveform in OTP e non si comprime, e la ragione è
// netta: LE SEQUENZE DI MODE 2 NON DIPINGONO AFFATTO.
// 0xFF esce in 225 ms e 0xFC in 85, due volte su due, mentre 0xF4, che è
// Mode 1, dura 23871 ms e dipinge: a separarle è il bit 3. Quattro osservazioni
// concordi, e la prima è diretta - la schermata della sonda che gira 0xFF non
// è mai comparsa sul vetro, il pannello passa alla successiva senza nessun
// refresh; le bande di accent sono rimaste rosse invece di uscire bianca e nera
// come vorrebbe la Table 6-5; e dopo due passate 0xFC il vetro mostra ancora la
// frase della passata 0xF7 precedente.
// In OTP non c'è quindi nessuna waveform di Mode 2, e nessuna scorciatoia
// differenziale: non è che il controller confronta i due piani e non trova
// differenze, è che quel banco è vuoto. Il partial funziona lo stesso perchè
// 0xCC ha il bit 4 spento e gira sulla LUT scritta via 0x32, non sull'OTP.
// Non rieseguiti in questa campagna 0xCF e 0xC7 con la waveform dell'OTP.
//
// LA FINESTRA NON CONFINA IL REFRESH, ed è misurato: la sonda d'area ha
// lasciato in 0x24 una fascia di trappola a y=176..215 che nessuna finestra
// comprendeva, e sul vetro è comparsa NERA già alla prima passata. Ogni refresh
// ridipinge tutto il pannello leggendo la RAM, e le passate d'area sembravano
// confinate solo perchè la RAM è cumulativa e nessuna la ripuliva.
// La finestra piena resta dichiarata qui perchè normalizza area e cursore per
// le scritture che seguono — all'ingresso è quella lasciata dall'ultima page
// del loop paged — e costa 250 us su 24 s.
inline void GxEPD2_SOLUM_097c_960x672::_Update_Full()
{
  // Se in 0x26 c'è un frame precedente del partial, questo refresh lo
  // rileggerebbe come accent e dipingerebbe di rosso tutto quello che sul vetro
  // è bianco. Costa 9 ms e solo in quel caso.
  _cleanColorIfPrevious();
  if (_using_partial_mode)
  {
    // Rimette il border sulla waveform di produzione. La LUT dell'OTP, e con
    // essa le tensioni che _Init_Part() ha scritto con 0x04, le ricarica il
    // bit 4 di 0xF7: non serve disfare niente a mano.
    _writeCommand(0x3C);
    _writeData(0x01); // LUT1, for white
    _using_partial_mode = false;
  }
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x22); // Display Update Sequence Options
  _writeData(0xF7);    //
  _writeCommand(0x20); // Master Activation
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
  _show_image_page_hint = 0;   // simmetrico al ciclo di rendering
}

// ---------------------------------------------------------------------------
// API siblings single-channel: scrivono un solo piano del controller con la
// stessa shape. Non chiamano refresh. Convenzione bitmap identica a
// writeImage(black, color): bit=1 = pixel NON in quel canale; il driver
// applica ~data (invert=true) sull'accent in modo che la polarity nativa
// SSD1677 (bit=1 = accent ON) combaci.
// ---------------------------------------------------------------------------
inline void GxEPD2_SOLUM_097c_960x672::writeImageBlack(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x24, bitmap, x, y, w, h, false, false, pgm);
  // canale black non ha dirty flag: viene sempre riscritto a ogni frame.
}

/** Scrive il piano accent. Non maschera 0x24 sotto i pixel accesi, e non
 *  serve: la Table 6-4 dà LUT2 = LUT3 e la misura sul pannello lo conferma,
 *  cioè con l'accent a 1 il valore del piano BW non cambia il colore reso.
 *  Ripulisce prima 0x26 se ci trova un frame precedente del partial: è una
 *  scrittura d'area, e quello che resta fuori verrebbe riletto come rosso. */
inline void GxEPD2_SOLUM_097c_960x672::writeImageRed(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _cleanColorIfPrevious();
  _writeImage(0x26, bitmap, x, y, w, h, true, false, pgm);
  _color_dirty = true;
}

// ---------------------------------------------------------------------------
// Pulizia del piano accent.
//
// SSD1677 RAM polarity (datasheet Rev 1.0, tabella comandi, verbatim):
//   cmd 0x24 Write RAM (Black White): bit=1 -> pixel white, bit=0 -> black
//   cmd 0x26 Write RAM (RED):         bit=1 -> red, bit=0 -> non-red
//
// Le bitmap in input alle API writeImage* adottano convenzione inversa
// (bit=1 = NOT color) per comodità visiva e compatibilità con il formato
// dello script python / image2cpp; il driver applica ~data prima del
// transfer SPI sull'accent. Il cleanup scrive invece DIRETTAMENTE 0x00 =
// polarity nativa "accent spento", senza invert. Era 0xFF in versioni
// precedenti -> equivaleva a "accent ON ovunque", bug latente mascherato dal
// fatto che il deep sleep perde la RAM del controller.
// ---------------------------------------------------------------------------
inline void GxEPD2_SOLUM_097c_960x672::_cleanColorIfDirty()
{
  if (_color_dirty)
  {
    _writeScreenBuffer(0x26, 0x00);
    _color_dirty = false;
    // La RAM è stata riscritta per intero, quindi non ci può più essere un
    // frame precedente del partial.
    _previous_in_color_ram = false;
  }
}

// Gemella della precedente per il frame precedente del partial. La pulizia è
// sempre corretta e non serve nessuna euristica: il flag lo alzano soltanto
// writeScreenBufferPrevious() e writeImagePrevious(), quindi se è alto in 0x26
// c'è un frame precedente e non un accent, e un refresh pieno quel contenuto lo
// rileggerebbe come rosso. Vedi il commento di _previous_in_color_ram.
inline void GxEPD2_SOLUM_097c_960x672::_cleanColorIfPrevious()
{
  if (!_previous_in_color_ram) return;
  if (!_init_display_done)
  {
    // Il deep sleep ha perduto la RAM: niente da pulire, solo lo stato da
    // riallineare.
    _previous_in_color_ram = false;
    return;
  }
  _writeScreenBuffer(0x26, 0x00);
  _previous_in_color_ram = false;
  _color_dirty = false;
}

/** Prepara il controller al partial, e sono tre cose:
 *
 *  1. border in HiZ, così la cornice non viene agganciata a LUT1 e non
 *     lampeggia a ogni passata;
 *  2. i byte 0..104 della waveform via 0x32, cioè VS, TP/RP e frame rate;
 *  3. le TENSIONI DI SORGENTE via 0x04, dai byte 106..108.
 *
 *  Il punto 3 è la differenza fra un nero pieno e un nero pallido, e non è
 *  opzionale: un waveform setting è di 110 byte, 0x32 ne scrive 105, e i
 *  cinque che restano sono le tensioni. Se non le si scrive, restano quelle
 *  dell'ultimo load dall'OTP, cioè quelle della waveform BWR di produzione
 *  tarata su 24 secondi, e la stessa LUT rende un nero visibilmente più
 *  chiaro. Misurato sul vetro: nero pieno con le tensioni della LUT, pallido
 *  con quelle dell'OTP, a durata identica perchè il tempo lo fissano TP e RP.
 *
 *  VGH (byte 105, comando 0x03) e VCOM (byte 109, comando 0x2C) NON vengono
 *  scritti di proposito, e restano quelli dell'OTP: VGH pilota i transistor e
 *  non il pigmento, mentre il VCOM governa il bilanciamento DC, quindi
 *  fantasmi e vita del film, ed è tarato di fabbrica su questa unità. Il POR
 *  di 0x2C è 0x00, che nella tabella del datasheet non compare nemmeno, e
 *  scriverlo alla cieca sarebbe un peggioramento.
 *
 *  Il ritorno alla waveform di produzione è automatico e non serve disfare
 *  niente: _Update_Full() manda 0x22 = 0xF7, il cui bit 4 ricarica dall'OTP i
 *  byte 0..109, tensioni comprese.
 *
 *  L'ordine rispetto alla scrittura delle due RAM è indifferente, perchè nè
 *  0x32 nè 0x04 toccano la RAM immagine. */
inline void GxEPD2_SOLUM_097c_960x672::_Init_Part()
{
  _writeCommand(0x3C); // Border Waveform Control
  _writeData(0xC0);    // HiZ, floating
  _writeCommand(0x32); // Write LUT register, byte 0..104
  _writeDataPGM(_lut_partial, 105);
  _writeCommand(0x04); // Source Driving voltage: VSH1, VSH2, VSL
  _writeDataPGM(_lut_partial + 106, 3);
  _using_partial_mode = true;
}

// Sostituisce la waveform del partial. Vedi la dichiarazione per il contratto.
inline void GxEPD2_SOLUM_097c_960x672::setPartialLut(const uint8_t* lut)
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

/** Passata di partial, 639 ms di BUSY misurati. Riproduce la sequenza che la
 *  sonda ha cronometrato, nell'ordine:
 *    0x22 = 0xC0 + 0x20   power on esplicito. Le sequenze senza i bit 1 e 0 non
 *                         spengono niente alla fine, ma non è detto che
 *                         accendano, e il GDEH116T91 fa lo stesso.
 *    finestra piena       il refresh scandisce tutto il pannello comunque
 *    0x22 = 0xCC + 0x20   clock + analog + DISPLAY Mode 2, bit 5 e 4 SPENTI:
 *                         nè temperatura nè LUT vengono ricaricate dall'OTP,
 *                         che è la ragione per cui la waveform custom
 *                         sopravvive fino al refresh.
 *  0xCC non ha i bit 1 e 0, quindi clock e analog restano accesi all'uscita e
 *  _power_is_on resta alto: al contrario di _Update_Full(), qui il pannello va
 *  spento da chi chiama, con powerOff() o hibernate(). */
inline void GxEPD2_SOLUM_097c_960x672::_Update_Part()
{
  if (!_using_partial_mode) _Init_Part();
  _PowerOn();
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x22); // Display Update Sequence Options
  _writeData(0xCC);    // Mode 2, senza ricarica di LUT e temperatura
  _writeCommand(0x20); // Master Activation
  _waitWhileBusy("_Update_Part", partial_refresh_time);
  _show_image_page_hint = 0;   // simmetrico al ciclo di rendering
}

// Scrive il frame precedente in 0x26. Nessun invert, al contrario di
// writeImageRed(): sotto la LUT del partial quella RAM non è l'accent ma il
// piano BW del frame che sta sul vetro, quindi vale la stessa convenzione di
// writeImageBlack() (bit=1 = bianco).
inline void GxEPD2_SOLUM_097c_960x672::writeScreenBufferPrevious(uint8_t value)
{
  // Non basta l'init: dopo un hibernate _InitDisplay() riarma _initial_write e
  // questo metodo, che non passa da _writeImage(), non lo consumerebbe mai. La
  // prima scrittura successiva farebbe allora scattare writeScreenBuffer(),
  // che azzera 0x26 a 0x00 cioè frame precedente tutto NERO, cancellando
  // proprio l'allineamento appena fatto.
  _ensurePartialRamDefined();
  _writeScreenBuffer(0x26, value);
  // Vale come per writeImagePrevious(): in 0x26 non c'è più un accent pulito,
  // ma un frame precedente. Il secondo flag è quello che fa ripulire quella RAM
  // a un refresh pieno o all'inizio di un loop paged.
  _markPreviousInColorRam();
}

inline void GxEPD2_SOLUM_097c_960x672::writeImagePrevious(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool pgm)
{
  if (!bitmap) return;
  _writeImage(0x26, bitmap, x, y, w, h, false, false, pgm);
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
inline void GxEPD2_SOLUM_097c_960x672::refreshPartial()
{
  _ensurePartialRamDefined();
  _Update_Part();
}

// Le coordinate sono dichiarative: vedi il commento della dichiarazione.
inline void GxEPD2_SOLUM_097c_960x672::refreshPartial(int16_t /*x*/, int16_t /*y*/,
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
inline void GxEPD2_SOLUM_097c_960x672::_ensurePartialRamDefined()
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
   *  scritture sotto usano quindi le primitive PRIVATE, che non rientrano. */
  _initial_write = false;
  _writeScreenBuffer(0x24, 0xFF);                                // frame nuovo bianco
  if (!_previous_in_color_ram)
  {
    _writeScreenBuffer(0x26, 0xFF);                              // precedente bianco
    _markPreviousInColorRam();
  }
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
// Non passa da writeImageBlack() e writeImagePrevious() ma scrive i due piani
// direttamente, perchè quelle due firme non hanno invert e cambiargliela
// romperebbe i chiamanti che passano pgm per posizione.
inline void GxEPD2_SOLUM_097c_960x672::drawImagePartial(const uint8_t* bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool pgm)
{
  if (!bitmap) return;
  _ensurePartialRamDefined();
  // Se il rettangolo non ha superficie sullo schermo non si lancia la passata:
  // costerebbe 640 ms per niente e alzerebbe i flag di 0x26 a vuoto.
  if (!_writeImage(0x24, bitmap, x, y, w, h, invert, false, pgm)) return;
  refreshPartial();
  _writeImage(0x26, bitmap, x, y, w, h, invert, false, pgm);   // ora è il precedente
  _markPreviousInColorRam();
}

// Gemella della precedente su un rettangolo di una bitmap più grande. Stessa
// sequenza e stessa invariante, con _writeImagePart al posto di _writeImage.
inline void GxEPD2_SOLUM_097c_960x672::drawImagePartialPart(const uint8_t* bitmap,
    int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool pgm)
{
  if (!bitmap) return;
  _ensurePartialRamDefined();
  // Come nella gemella: niente passata per un rettangolo che nessuno dipinge.
  if (!_writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap,
                       x, y, w, h, invert, false, pgm)) return;
  refreshPartial();
  _writeImagePart(0x26, bitmap, x_part, y_part, w_bitmap, h_bitmap,
                  x, y, w, h, invert, false, pgm);             // ora è il precedente
  _markPreviousInColorRam();
}

#endif
