# GxEPD2_SOLUM_ESL — driver custom per pannelli SOLUM ESL

Libreria Arduino che estende [GxEPD2](https://github.com/ZinggJM/GxEPD2) con i
driver di pannelli e-paper recuperati da etichette elettroniche SOLUM, che
upstream non supporta.

| Driver | Pannello | Controller | Colori | Stato |
|---|---|---|---|---|
| [`src/GxEPD2_SOLUM_097c_960x672.h`](src/GxEPD2_SOLUM_097c_960x672.h) | Newton Pro 9.7", 960w × 672h | SSD1677 | B/W/R verificati, 4° colore [questione aperta](#010-il-4-colore-non-esiste-questione-chiusa) | in uso |
| [`src/GxEPD2_SOLUM_122c_960x768.h`](src/GxEPD2_SOLUM_122c_960x768.h) | Newton PRO / Core 12.2", 960w × 768h | 2 × SSD16xx, 960×384 ciascuno | B/W/R | una banda validata, le due insieme no — vedi [README_122c.md](README_122c.md) |

Il namespace `GxEPDImage` (descrittori immagine e `showImage()`) sta in
[`src/GxEPDImage.h`](src/GxEPDImage.h) e lo condividono tutti i driver: due
header driver possono coesistere nella stessa translation unit.

Le prime sezioni valgono per la libreria; da [Il driver 9.7"](#il-driver-97)
in poi il documento riguarda quel pannello. Per il 12.2" vedi
[README_122c.md](README_122c.md).

---

## Indice

- [Installazione](#installazione)
- [Selezione del driver](#selezione-del-driver)
- [Aggiungere un driver alla libreria](#aggiungere-un-driver-alla-libreria)
- [Il driver 9.7"](#il-driver-97)
- [0. Il pannello SOLUM 9.7"](#0-il-pannello-solum-97)
- [Origine](#origine)
- [1. `GxEPDImage::showImage()` — unico entry-point pubblico](#1-gxepdimageshowimage--unico-entry-point-pubblico)
- [2. Due API siblings single-channel uniformi](#2-due-api-siblings-single-channel-uniformi)
- [3. Il template a due canali e le scritture out-of-band](#3-il-template-a-due-canali-e-le-scritture-out-of-band)
- [4. Sistema di descrittori universali (`namespace GxEPDImage`)](#4-sistema-di-descrittori-universali-namespace-gxepdimage)
- [5. Ottimizzazioni rispetto al driver stock](#5-ottimizzazioni-rispetto-al-driver-stock)
- [6. API completa](#6-api-completa)
- [Licenza](#licenza)

---

## Installazione

Libreria Arduino a sè stante che **dipende da GxEPD2**: questo repo non la
contiene, la estende con i driver di pannelli che upstream non supporta.

1. installa **GxEPD2** (>= 1.6.9) e **Adafruit GFX Library** dal Library
   Manager dell'IDE;
2. installa questa libreria: *Sketch → #include libreria → Aggiungi libreria
   .ZIP*, oppure clonando il repo in `Documents/Arduino/libraries/`;
3. nello sketch:

```cpp
#define SOLUM_PANEL_097C     // oppure SOLUM_PANEL_122C
#include <GxEPD2_3C.h>
#include <GxEPD2_SOLUM.h>

// Globale, non locale a setup(): selectSPI() conserva il puntatore a hspi
SPIClass hspi(HSPI);

// Pinout nella struct uniforme: cs, dc, rst, busy, cs2, busy2, sck, miso, mosi.
// Qui il cablaggio della Waveshare E-Paper ESP32 Driver Board; i campi non
// passati restano -1, cioè assenti.
GxEPD2_3C<GxEPD2_SOLUM_DRIVER_CLASS, SOLUM_MAX_HEIGHT(GxEPD2_SOLUM_DRIVER_CLASS)>
    display(GxEPD2_SOLUM_DRIVER_CLASS(GxEPD2_SOLUM_Pins{ 15, 27, 26, 25 }));

void setup()
{
  hspi.begin(13, 12, 14, 15);                                  // SCK, MISO, MOSI, SS
  display.epd2.selectSPI(hspi, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 2, false);
  display.setRotation(0);                                      // 960w × 672h landscape
  display.setFullWindow();
}
```

Requisiti: target **ESP32** — il driver usa `SPIClass::writeBytes` e ha i
`delay(1)` di yield WDT per ESP8266 rimossi dai hot path — e alimentazione
3,3 V su VCC **e su tutte le data line** del pannello, che non sono
5V-tolerant. Adafruit_GFX serve solo se `ENABLE_GxEPD2_GFX` è attivo.

Se preferisci non installarla e tenerla dentro il progetto che la usa (come
submodule), l'include diventa relativo al path del submodule:
`#include "GxEPD2_SOLUM_ESL/src/GxEPD2_SOLUM.h"`.

---

## Selezione del driver

Lo sketch non nomina mai la classe del driver: la sceglie con un `#define` e la
usa attraverso due macro che [`src/GxEPD2_SOLUM.h`](src/GxEPD2_SOLUM.h) definisce.
È l'idioma di `GxEPD2_display_selection_new_style.h` upstream, portato dentro la
libreria invece che negli esempi.

| Simbolo | Cosa fa |
|---|---|
| `SOLUM_PANEL_097C` / `SOLUM_PANEL_122C` | selezione del pannello, da definire **prima** dell'include. Esattamente uno: zero o due danno `#error` |
| `GxEPD2_SOLUM_DRIVER_CLASS` | nome della classe del driver selezionato |
| `SOLUM_MAX_HEIGHT(EPD)` | altezza della page derivata dal budget RAM, con cap a `EPD::HEIGHT`. Due piani da 1 bpp, quindi il budget si divide per due |
| `SOLUM_MAX_DISPLAY_BUFFER_SIZE` | budget RAM dei buffer di page. Default 65536 su ESP32 (lo stesso di upstream), 15000 altrove. Definendolo prima dell'include si sovrascrive |
| `GxEPD2_SOLUM_Pins` | struct di pinout uniforme fra i driver, in [`src/GxEPD2_SOLUM_Pins.h`](src/GxEPD2_SOLUM_Pins.h) |

Con `SOLUM_MAX_DISPLAY_BUFFER_SIZE` al default, su un pannello da 960 px di
larghezza la page viene di 273 righe, cioè ~65 KB di buffer. Se la RAM serve
anche ad altro, passare al template un valore proprio: il firmware consumer usa
`EPD::HEIGHT / 8`, cioè ~20 KB e otto page.

La struct di pinout è ciò che rende sostituibile un driver con l'altro: i
pannelli non hanno tutti lo stesso numero di segnali (il 12.2" ha due CS e due
BUSY perchè ha due controller), e senza una firma comune cambiare pannello
significherebbe riscrivere la riga di costruzione del display.

---

## Aggiungere un driver alla libreria

1. **Header in `src/`**, che include [`GxEPDImage.h`](src/GxEPDImage.h) e
   [`GxEPD2_SOLUM_Pins.h`](src/GxEPD2_SOLUM_Pins.h). Deve implementare l'API
   richiesta elencata in testa a `GxEPDImage.h`, che sono due soli metodi:
   `setPaged()` e `showImagePageHint()`. Il template `showImage()` è unico e
   non ha rami per driver, e compone i due piani che `GxEPD2_3C` gestisce:
   nessun driver deve dichiarare primitive che non gli servono. Un pannello con
   un terzo piano può esporne le primitive come API propria, che il chiamante
   usa fuori dal loop paged.
2. **Costruttore `explicit Driver(const GxEPD2_SOLUM_Pins&)`**, oltre a quelli
   nativi. È la firma che gli sketch usano.
3. **Bus SPI da `_pSPIx` / `_spi_settings`** della base `GxEPD2_EPD`, mai
   dall'oggetto `SPI` globale: uno sketch che chiama `selectSPI()` si aspetta
   che valga per tutti i driver. Se il driver ha un default proprio, lo imposta
   nel costruttore chiamando `selectSPI()`, non cablandolo nelle primitive.
4. **Membro `panel`**: i driver di questa libreria prendono in prestito un
   valore di `GxEPD2::Panel` di un pannello upstream. Va scelto uno che i
   template **non** trattino in modo speciale: `GxEPD2_3C` applica workaround
   confrontando `epd2.panel` con `GDEW0154Z04`, `GxEPD2_BW` con `GDE0213B1`.
   Riusare uno di quei valori si porta dietro il workaround di un altro
   pannello.
5. **Tre righe in [`src/GxEPD2_SOLUM.h`](src/GxEPD2_SOLUM.h)**: il ramo `#elif`
   con il define di selezione, l'include e `GxEPD2_SOLUM_DRIVER_CLASS`.
6. `library.properties` non si tocca: espone `GxEPD2_SOLUM.h`, non i driver.

---

## Il driver 9.7"

Driver header-only per pannello e-paper **SOLUM ESL 9.7"** (672w × 960h px
native portrait → `setRotation(0)` → 960w × 672h px landscape,
convenzione `NwxMh` = N px larghezza × M px altezza;
bianco/nero/rosso verificati sul pannello, il quarto colore è una
[questione aperta](#010-il-4-colore-non-esiste-questione-chiusa); controller
SSD1677) su
**ESP32**. Estende [GxEPD2](https://github.com/ZinggJM/GxEPD2) di
Jean-Marc Zingg, fornendo:

- **API `showImage()` unificata** come unico entry-point one-shot di
  stampa immagine, con hibernate automatico opzionale. Due overload:
  descrittore generico (output dello script Python) e bitmap raw 1bpp
  B/N (formato [image2cpp](https://javl.github.io/image2cpp/));
- **2 API siblings uniformi** `writeImageBlack` / `writeImageRed` per
  scrittura single-channel sui due piani del controller, per il compositing
  manuale fuori dal loop paged (vedi [sezione dedicata](#3-il-template-a-due-canali-e-le-scritture-out-of-band));
- **sistema di descrittori universali** (`GxEPDImage::Descriptor`) che
  porta con sè formato e dimensioni dell'immagine (BW / BWR / BWRY, di cui
  `showImage` rende i primi due piani);
- **nessuna API per un quarto colore**: su questo pannello non esiste, e la
  misura lo ha chiuso — vedi
  [§0.10](#010-il-4-colore-non-esiste-questione-chiusa).

Per un esempio d'uso completo (sketch, moduli Weather/Calendar, flussi di
boot, OTA) vedi il progetto che la consuma:
[ePaper-weather-dashboard](https://github.com/alesimattia/ePaper-weather-dashboard).

---

## 0. Il pannello SOLUM 9.7"

Caratteristiche del pannello ricavate dai cataloghi SOLUM in [`docs/`](docs/)
(`Newton-Pro_Specifications.pdf`, `Newton-Core_Specifications.pdf`), dallo
schematico `E-Paper_ESP32_Driver_Board_V3.pdf` della board Waveshare e dalle
verifiche su hardware. Spiegano il perchè di diverse scelte del driver, quindi
conviene leggerle prima del resto.

### 0.1 Identificazione: quale 9.7" è

Il 9.7" SOLUM esiste in tre varianti, tutte 672 × 960 px a 121 dpi ma con film
e involucro diversi:

| Linea | Codice modello | Colori | Modulo ESL | Segni distintivi |
|---|---|---|---|---|
| Newton Pro gen. F5 | `EL097F5C4C/WWW` (cornice grigia), `EL097F5W4C/WWW` (bianca), `EL097F5B4C/WWW` (nera) | BWRY | 168,05 × 224,07 × 14,81 mm | nessun grado IP, **nessun pulsante**, LED 7 colori |
| Newton Pro gen. F6 | `EL097F6W4A/WWW` (bianca), `EL097F6B4A/WWW` (nera); cornice dichiarata anche grigia o personalizzata | BWRY | 170,2 × 223,6 × 14,85 mm | **IP68**, **1 pulsante**, LED 7 colori |
| Newton Core | `EL097F5CRC` (pratica FCC 2AFWN-EL097F5CRC, scheda tag `NEWTON_CORE 9.7_TAG_R01`) | **BWR** (3 colori); BW / BWR / BWRY nella variante "Core 4COLOR" | 167,65 × 220,04 × 13,80 mm | cornice solo bianca o nera, nessun grado IP, nessun pulsante |

**Decodifica del codice**, dal §3.6 "Label Marking" del datasheet ufficiale
`docs/Newton-PRO_Data-sheet_C-Lab_G_240320.pdf`:

```
EL <taglia> <generazione> <colore scocca> <colore display> <tipo tag>
   097       F5            C               4                C
```

- **taglia**: `097` = 9.7", `116` = 11.6", `122` = 12.2";
- **generazione**: `F5`, `F6`, `H3`, `H5`, `H6`;
- **colore scocca**: `W` = white, `B` = black; le unità del progetto portano `C`;
- **colore display**: **`R` = BWR**, **`4` = RED, YELLOW (BWRY)**;
- **tipo tag**: `A` = Button + NFC + LED.

Il campo colore è quello che separa `EL097F5C`**`4`**`C` (Pro, il donor del
progetto) da `EL097F5C`**`R`**`C` (Core della pratica FCC): stessa taglia,
stessa generazione, stessa scocca, stesso tipo tag, un carattere di differenza.

Il donor di questo progetto è un **Pro gen. F5**: cornice grigia, nessun
pulsante e nessun grado IP, cioè i due tratti che il F6 ha e il F5 no. La cornice
da sola non basta a distinguerli, perchè il catalogo la dichiara grigia su
entrambi, ma esclude il Core, offerto solo in bianco o nero. Nemmeno il LED a 7
colori discrimina: c'è su tutti e due i Pro.

Il codice modello **non garantisce il film a 4 colori**, e la prova sta nel
datasheet stesso: nel modello table della linea PRO *ogni* taglia da 1.6" a
12.2" è elencata come `...W4A`, cioè la cifra `4` è costante su tutta la linea.
Che non sia un dato per unità lo dimostrano le serigrafie sul vetro della
generazione F6: `EL097F6W4A` porta `NEWTON PRO 9.7" BWRY Normal`, ma
`EL116F6W4A` e `EL122H6W4A` — stessa cifra `4` — portano `BWR normal`. Il campo
colore distingue la **linea** (PRO nominalmente a 4 colori, Core `R` a 3), non
il film montato. Il set colori resta un'opzione di build dello stesso modulo: la pagina Newton Core 4COLOR 9.7"
dichiara `BW / BWR / BWRY` e la tabella di famiglia del Newton Pro (formati
1.6"–11.6") dichiara `BW / BWR / BWY`. Lo stesso 672 × 960 viene quindi prodotto
con due, uno o nessun colore d'accento, e solo una prova sul pannello dice quale
si ha in mano: vedi [§0.10](#010-il-4-colore-non-esiste-questione-chiusa).

### 0.2 Geometria e densità

| Grandezza | Valore |
|---|---|
| Risoluzione | 672 × 960 px (come da catalogo, orientamento portrait del modulo) |
| Area attiva | 141,12 × 201,6 mm |
| Pixel pitch | **0,210 mm esatti su entrambi gli assi** → pixel quadrati |
| Densità | 120,95 dpi (il catalogo arrotonda a 121 dpi) |
| Diagonale attiva | 246,08 mm = 9,69" → il "9.7" è la diagonale attiva, non il modulo |
| Aspect ratio | esattamente 7:10 |
| Conversioni | 4,7619 px/mm; 1 pt tipografico = 1,68 px (corpo 10 pt ≈ 17 px, 12 pt ≈ 20 px) |
| Curiosità utile | l'area attiva è quasi esattamente una pagina A5 (141 × 202 mm contro 148 × 210 mm) |

I pixel perfettamente quadrati sono il motivo per cui non serve nessuna
correzione di aspect ratio nè in `epd_image_converter.pyw` nè in `showImage`.

### 0.3 Source, gate e MUX

Il controller indirizza il pannello come **960 source × 672 gate**: nel driver
`WIDTH = 960` è l'asse X della RAM (comando `0x44`) e `HEIGHT = 672` l'asse Y
(comando `0x45`). Il modulo è montato in portrait — per questo il catalogo
riporta "672 × 960" — e con `setRotation(0)` il layer GFX lavora in landscape
960w × 672h.

**Il MUX era ereditato dall'upstream.** Fino alla correzione,
`_InitDisplay()` programmava il MUX (`0x01`) con `0xA7 0x02 0x00` = `0x02A7` =
679, cioè **680 gate lines**: il valore del pannello GDEM133Z91
(`HEIGHT = 680`), preso tale e quale. Questo pannello ne ha 672, e infatti
`_setPartialRamArea` imposta la finestra Y su `0..671`: il controller scandiva
8 gate lines inesistenti a ogni refresh — nessun artefatto visibile (sotto non
c'è film), ma ~1,2% del tempo di refresh, circa 280 ms sui 24 s misurati.

Ora il MUX è `0x9F 0x02 0x00` = 671 → **672 gate lines**. L'immagine attesa è
identica (la finestra RAM era già `0..671`), il refresh dovrebbe accorciarsi di
quei ~260 ms; ridurre le gate line scandite accorcia anche il tempo di frame
della waveform, quindi lo scarto di impulso è dell'ordine dell'1% — sotto la
soglia percepibile, ma **la verifica su hardware non è ancora stata fatta**.
Sul valore di `full_refresh_time` c'era un'imprecisione, ora corretta: **non è
il timeout di attesa del BUSY**. `GxEPD2_EPD::_waitWhileBusy` lo riceve come
parametro `busy_time` e lo usa soltanto come `delay()` di fallback quando il pin
BUSY non è cablato; con il BUSY presente il timeout che conta è `_busy_timeout`,
il sesto argomento del costruttore. Vedi [§0.8](#08-tempi-di-refresh).
Insieme al MUX è stato corretto anche il commento
`// Set MUX as 527`, sbagliato e anch'esso ereditato dall'upstream.

### 0.4 Temperatura: range strettissimo

Range di esercizio dichiarato **0 ~ 40 °C** (32 ~ 104 °F). Non è un dettaglio
formale: la waveform è compensata in temperatura, `_InitDisplay()` seleziona il
**sensore interno** al controller (`0x18 = 0x80`) e il byte `0xF7` del comando
`0x22` include il passo "load temperature" a ogni refresh. Fuori range il
pannello non è specificato: sotto 0 °C sono attesi ghosting e refresh
incompleti. La temperatura di stoccaggio non è dichiarata in nessuno dei due
cataloghi.

L'FPC porta anche le linee **TSCL / TSDA**, quindi esiste la via hardware per un
sensore di temperatura I²C esterno (`0x18 = 0x48` su SSD1677) se servisse
compensare fuori dal range del sensore interno. Oggi non usata.

**Luce ultravioletta.** Il controller è un die a **gold bump** montato sul COF
senza incapsulamento in resina: l'esposizione UV lo danneggia, e sotto sole
diretto il pannello sbianca. Riscontro di terzi su SSD1677 nudi
(<https://github.com/bigbag/papyrix-reader>, `docs/ssd1677-driver.md`); qui il
COF è coperto dalla cornice dell'ESL, ma la nota vale per qualunque montaggio
che lasci la coda scoperta e per il posizionamento del pannello rispetto a una
finestra.

### 0.5 Vita attesa

Nell'ESL originale il pannello era alimentato da un pacco **CR2450 × 5** con
dichiarazione "up to 10 years @ **1 update/day**" (il Newton Core 9.7" dichiara
2 update/day). Il punto di progetto del prodotto è quindi nell'ordine di
**~3.650 refresh in tutta la vita**. È una dichiarazione sul budget energetico
delle batterie, non una specifica di endurance del film, ma l'intento di
progetto è chiaro: un pannello pensato per aggiornarsi una o due volte al
giorno. Da tenere presente prima di costruirci sopra qualcosa che faccia molti
refresh al giorno.

### 0.6 Interfaccia elettrica (dallo schematico Waveshare V3)

Connettore FPC 24 pin `J10`, pin ricavati dal netlist dello schematico:

| Pin | Segnale | Pin | Segnale |
|---|---|---|---|
| 4 | HLT_CTL | 12 | SDI (MOSI) |
| 5 | GDR | 13 | VDDIO |
| 6 | RESE | 14 | VCI |
| 7 | VGL | 15 | VDD |
| 8 | BS | 16 | **VPP** |
| 9 | RST | 17 | VSH |
| 10 | D/C | 18 | PREVGH |
| 11 | CS | 1–3, 19–24 | PREVGL, VCOM, VGH, TSCL, TSDA, GND / NC |

Tre conseguenze pratiche:

- il rail del pannello è **EPD_3.3V**, non i 5 V di ingresso della board: nè
  VCC nè le data line sono 5V-tolerant;
- i rail analogici VGH / VGL / VSH / PREVGH / PREVGL / VCOM sono generati dal
  charge pump **a bordo della board** (GDR / RESE più i condensatori da
  1 µF/50 V sui pin 21/23/24), non dal pannello: se un refresh esce sbiadito
  conviene guardare lì prima che nel codice;
- il pin 8 è **BS**, cioè `BS1` del controller: sceglie l'interfaccia MCU,
  **L = 4 fili**, H = 3 fili a 9 bit. Sulla board è cablato, su una coda montata
  a mano va portato basso — lasciato flottante il controller può ignorare tutto
  il traffico a 4 fili;
- il pin 16 è **VPP**. Sui Solomon quel nome è la tensione di programmazione
  dell'OTP, quindi il percorso per *scrivere* l'OTP sarebbe fisicamente presente
  sul connettore — un motivo per limitarsi alla lettura (`0x2D`, Read Register
  for Display Option). C'è però una seconda lettura da verificare: sul tag di
  fabbrica di questi pannelli il pin che l'HAL chiama `EPD_VPP` è usato come
  **MISO**, cioè è la linea dati in uscita dal controller
  (`docs/openepaperlink/nrf52811_tag_fw/HAL_Newton_M3.h`). Se sul FPC è lo
  stesso segnale, il pin 16 è il candidato per la lettura dei registri che
  [§0.10](#010-il-4-colore-non-esiste-questione-chiusa) dà per impossibile.

### 0.7 SPI e volumi di transfer

Lo sketch configura il bus con `SPISettings(10000000, MSBFIRST, SPI_MODE0)` su
HSPI. A 10 MHz il **limite fisico è 0,8 µs/byte** (8 bit / 10 MHz): tutte le
stime di tempo SPI in questo documento derivano da qui, non da misure a
oscilloscopio.

I 10 MHz non sono solo prudenza: su alcuni pannelli SSD1677 il controller **non
tollera clock più alti** (riscontro di terzi in `docs/ssd1677-driver.md` di
papyrix-reader, dove un pannello regge 40 MHz e un altro dello stesso
controller no). Il default del driver 12.2" è invece a 20 MHz: se quel pannello
desse errori intermittenti, il clock è la prima cosa da abbassare.

| Voce | Byte | Bulk (0,8 µs/B) | Per-byte upstream (~1,5 µs/B) |
|---|---|---|---|
| 1 piano full-screen (960 × 672 / 8) | 80.640 | ~65 ms | ~121 ms |
| 3 piani, `writeScreenBuffer()` | 241.920 | ~194 ms | ~363 ms |
| 1 page, 1 piano (960 × 84 / 8) | 10.080 | ~8 ms | ~15 ms |
| loop paged completo B+R (8 page × 2 piani) | 161.280 | ~129 ms | ~242 ms |
| refresh BWRY steady-state (paged B+R + piano giallo + cleanup `0x26`) | 322.560 | ~258 ms | ~484 ms |

Il template è istanziato come `GxEPD2_3C<Layout::Panel, Panel::HEIGHT / 8>` →
page height 84 righe, **8 page** per refresh, ~20 KB di RAM per i due piani
(contro i ~157 KB dei due piani full-screen non paginati: l'ESP32-WROOM-32
della board è senza PSRAM).

### 0.8 Tempi di refresh

| Costante | Valore | Nota |
|---|---|---|
| refresh pieno **misurato** | **24.013 ms** di BUSY | `examples/097c/panel_diagnostic`, passate `0x22 = 0xF7` |
| `full_refresh_time` | 30.000 ms | solo `delay()` di fallback per `busy < 0`, **non** il timeout reale |
| `partial_refresh_time` | 30.000 ms | identico: non esiste fast partial update |
| `power_on_time` / `power_off_time` | 100 / 250 ms | anch'essi solo fallback per `busy < 0` |
| `busy_timeout` | 40.000.000 µs (40 s) | sesto argomento di `GxEPD2_EPD`, **è il timeout che conta** |
| reset duration | 2 ms | `display.init(115200, true, 2, false)` nello sketch |
| SWRESET | 1 ms di BUSY | misurato; il driver attende comunque 200 ms fissi |

**Perchè `busy_timeout` è stato portato a 40 s.** Con i 25 s precedenti il
margine sul refresh misurato era di 987 ms, non i 3 s che questa tabella
dichiarava: allo scadere `_waitWhileBusy` stampa `Busy Timeout!` ed esce,
`_Update_Full` torna con il pannello ancora in pilotaggio e `hibernate()` manda
`0x10` a metà waveform, lasciando il frame troncato fino al refresh successivo.
La waveform si allunga a bassa temperatura e `0xF7` ricarica la temperatura dal
sensore interno a ogni refresh, quindi il margine va tenuto ampio.

`hasFastPartialUpdate = false` con `partial_refresh_time == full_refresh_time`
non è una lacuna di questo driver: **nessuno** dei 36 pannelli a 3 e 4 colori
presenti in `epd3c/`, `epd4c/`, `gdem3c/`, `gdey3c/` di GxEPD2 ha il fast
partial. La ragione è strutturale: il partial veloce di SSD1677 è
**differenziale** e usa la RAM `0x26` come buffer "previous", mentre qui `0x26`
è il piano rosso — la RAM che servirebbe per il differenziale è occupata dal
colore. Il fratello monocromatico con lo stesso controller
([`GxEPD2_1330_GDEM133T91`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdem/GxEPD2_1330_GDEM133T91.h),
960 × 680) fa 4.500 ms full e **600 ms** partial proprio perchè quella RAM è
libera.

Corollario: il tempo sta nella waveform, non nei dati. **Non inviare i piani
accent non accorcia il refresh**, risparmia solo i ~130 ms di SPI della tabella
in §0.7 — che il dirty-tracking di §5 già incassa.

### 0.9 Cosa i cataloghi SOLUM non dicono

`Newton-Pro_Specifications.pdf` e `Newton-Core_Specifications.pdf` sono
cataloghi di prodotto ESL, non datasheet di pannello: non contengono tempo di
refresh, waveform o LUT, VCOM, timing SPI, controller, pinout FPC nè tensioni di
driving. Ricerche esplicite su `refresh`, `update time`, `grayscale`, `VCOM`,
`mux`, `storage temp`, `cycle` non danno alcun risultato. Tutto il lato
elettrico e di timing viene quindi dal datasheet SSD1677 di Solomon Systech più
le verifiche su hardware.

E il datasheet SSD1677 va preso con le pinze proprio sui piani colore: **il
codice `0x28` non è documentato come piano accent**, risulta come VCOM Sense,
comando senza parametri. Il datasheet Solomon non è comunque autorevole su questo
silicio: non dice quante DISPLAY Mode l'OTP contenga nè come sarebbe strutturata
una LUT a 4 colori, e soprattutto **la copia in `docs/` è la Rev 1.0 del 2018,
più vecchia del chip**. L'indizio: l'init di fabbrica della 9.7"
(`docs/openepaperlink/nrf52811_tag_fw/unissd.cpp`) scrive `0x21` con **due**
parametri, mentre la Rev 1.0 lo definisce con uno solo — la forma a due byte è
quella del SSD1683, dove il secondo porta il bit di cascade. Lo conferma un
reference SSD1677 di terzi (<https://github.com/bigbag/papyrix-reader>,
`docs/ssd1677-driver.md`), che scrive anch'esso `0x21` a due byte, `0x40 0x00`,
e commenta il secondo con **"single chip"**. Nella stessa direzione va il pin
table: `M/S#` è dato *"reserved pin, should be connected to VDDIO"* e `CL` è
dichiarato **I/O** pur essendo *"left open in application"* — sono i due pin
della cascade dell'SSD1683, presenti nel silicio e non documentati. "Non è nel
datasheet" quindi non equivale a "non esiste nel chip". E non c'è una revisione
più recente da cercare: le copie pubbliche dell'SSD1677 reperibili altrove sono
la stessa Rev 1.0 del 2018, byte per byte. Vedi
[§0.10](#010-il-4-colore-non-esiste-questione-chiusa).

### 0.10 Il 4° colore non esiste: questione chiusa

Il pannello ha **tre colori**, e la misura lo dice. `examples/097c/panel_diagnostic`
ha stampato tutte e quattro le combinazioni dei due piani sotto la waveform di
produzione: `(0,0)` nero, `(0,1)` bianco, `(1,0)` e `(1,1)` **entrambe rosse**.
LUT2 e LUT3 rendono lo stesso colore, esattamente come la Table 6-4 dichiara, e
con due bit per pixel le combinazioni sono esaurite.

Il dato che chiude la questione senza bisogno di interpretazioni è il **codice
modello dell'unità**, letto sul case: `EL097R2CRN`, pratica FCC
`2AFWN-EL097R2CRN`, certificazione KC `R-R-SLU-EL097R2CRN`. Il campo colore
display è `R`, che nella nomenclatura SOLUM vale **BWR**; la linea PRO a quattro
colori porta la cifra `4`. Non è quindi il donor `EL097F5C4C` di cui parlano le
sezioni sopra, ma la generazione **R2**, precedente — la stessa della pratica
FCC `2AFWN-EL097R2WRN` già elencata in [docs/fonti_esterne.md](docs/fonti_esterne.md).
Il datasheet SOLUM che dichiara `PIXEL COLORS = BWRY` per la taglia 9.7"
riguarda la linea PRO e non questa unità.

Anche `0x28` ha risposto: alla scrittura **alza il BUSY per ~10 s** (misurati
9953-9968 ms) e non dipinge niente. È VCOM Sense come dice il datasheet, e non
un terzo piano immagine. Il driver 9.7" non lo tocca più, e le primitive del
terzo piano non esistono più nella sua API.

Le evidenze documentali raccolte prima della misura, che ora la confermano:

- nel datasheet pubblico SSD1677 `0x28` è VCOM Sense, un comando senza
  parametri: i byte che seguono con D/C alto vengono scartati;
- **nessun driver di GxEPD2 1.6.9 usa `0x28` come piano immagine**; gli unici
  match del codice in `src/` sono dati di bitmap.

A queste si è aggiunta un'evidenza di fabbrica, che è la più pesante fra
quelle documentali perchè non è catalogo: la tabella dei byte UICR dei tag SOLUM
reali (`docs/openepaperlink/nrf52811_tag_fw/tagtype_db.cpp`) dà per la 9.7"
controller `0x19`, 672 × 960 e **terzo colore = 0x01**, dove nella stessa tabella
`0x02` è il giallo e `0x03` è BWRY. Vale per i tag censiti lì, non
necessariamente per il donor di questo progetto, che è di un'altra generazione.

L'ipotesi alternativa più solida è che il 4° colore stia nella **combinazione a
2 bit dei due piani già esistenti**. Quello che la catena produce oggi, dato che
`drawPixel` di `GxEPD2_3C` scrive bit=1 = "non questo colore" e il driver manda
`0x24` senza invert e `0x26` con invert:

| colore | `0x24` | `0x26` |
|---|---|---|
| bianco | 1 | 0 |
| nero | 0 | 0 |
| rosso | 1 | 1 |
| — | 0 | 1 |

La quarta combinazione **non viene mai generata**.

**Il datasheet dice esattamente cosa sono queste quattro combinazioni.** Table
6-4, "RAM bit and LUT mapping for 3-color display":

| bit in RED RAM `0x26` | bit in B/W RAM `0x24` | colore | LUT |
|---|---|---|---|
| 0 | 0 | nero | LUT0 |
| 0 | 1 | bianco | LUT1 |
| 1 | 0 | rosso | LUT2 |
| 1 | 1 | rosso | **LUT3 = LUT2** |

Tre cose ne seguono, e cambiano il modo di leggere la sonda:

1. **`LUT3` è una LUT distinta nel silicio**, che la waveform a 3 colori si
   limita ad *aliasare* su `LUT2`. L'SSD1677 ha `LUT0..LUT4` (§6.7: 112 byte,
   10 gruppi × 4 fasi, quattro livelli di sorgente VSS/VSH1/VSH2/VSL). Cade
   quindi l'argomento "questo silicio ha solo due piani, quindi non può fare 4
   colori": **architetturalmente può**, ed è l'OTP a decidere se `LUT3` guida un
   quarto stato o replica il rosso.
2. **Quella che il firmware scrive per il rosso è `LUT3`**, non `LUT2`: un
   pixel rosso esce dalla catena come `0x24` = 1 e `0x26` = 1. Sul pannello
   esce rosso, quindi su questa unità `LUT3` guida il rosso. Se la waveform BWRY
   mettesse il giallo su `LUT3`, l'avremmo già visto al posto del rosso.
3. **L'unica LUT mai esercitata è `LUT2`**, cioè la combinazione (`0x24` = 0,
   `0x26` = 1), che nel datasheet è il rosso canonico. È lì che un quarto colore
   può ancora nascondersi, ed è esattamente la banda che la sonda scrive.

Nota su un discriminante che sembrava esserci e non c'è: il fratello a 4 colori
di questa taglia è l'**SSD2677** (960 × 680, RAM a **2 bit/pixel**, command set
in stile UC, preset di risoluzione `960 × 680 / 960 × 672 / 960 × 640 /
880 × 528`, archiviato in `docs/`). Verrebbe da concludere "il nostro parla
SSD16xx, quindi è BWR" — ma la Table 6-4 mostra che anche l'SSD1677 le quattro
combinazioni ce le ha. Il tipo di chip non decide il numero di colori.

Il controller non è interrogabile **dal connettore della board**: sul FPC a 24
pin il pin 12 è SDI e basta (vedi
[§0.6](#06-interfaccia-elettrica-dallo-schematico-waveshare-v3)), quindi `0x27`
read RAM, `0x2E` User ID read e `0x2F` status sono inutilizzabili. Il limite è
del connettore, non del pannello: il tag di fabbrica una linea di lettura ce
l'ha, ed è il pin che il suo HAL chiama `VPP` — se è lo stesso VPP del pin 16
dello schematico, la lettura si può ricavare su una coda cablata a mano, e
`0x2E` chiuderebbe l'identificazione del modulo. L'unica
diagnostica possibile è scrivere un pattern e guardare il pannello: lo fa
[`examples/097c/panel_diagnostic`](examples/097c/panel_diagnostic/panel_diagnostic.ino),
che vive in questa libreria perchè serve a costruire il driver, non il progetto
consumer. Stampa in un solo refresh le quattro combinazioni della tabella su
bande orizzontali numerate, poi ripete il refresh in DISPLAY Mode 2, poi prova
il probe dei livelli di sorgente e le due sonde del partial — quella sul banco
di waveform dell'OTP e quella con una LUT scritta dall'MCU via `0x32` — e chiude
sul seriale con una scheda di osservazione che mappa ogni esito visibile sulla
conseguenza per il driver.

Cosa resta davvero aperto, e dove: sul **12.2"** la domanda è ancora senza
risposta, perchè quel pannello è `EL122H6W4A` con campo colore `4` contro un
vetro serigrafato `Newton PRO 12.2" BWR normal`, e il bring-up è fermo alla
seconda coda muta. Per questo il formato `FORMAT_BWRY_1BPP` e il campo `data2`
del descrittore restano nella libreria: `showImage()` ne rende i primi due
piani, e un driver che avesse davvero il terzo può esporre le primitive per
scriverlo out-of-band.

L'ultimo test possibile su questa unità è la **tensione**, non una coppia di
bit: la Table 6-4 dice che LUT2 e LUT3 sono aliasate *dalla waveform*, non che
il film abbia tre pigmenti. La sonda carica quindi via `0x32` una waveform in
cui LUT2 va a VSH1 e LUT3 a VSH2 — tensioni dell'OTP, perchè quelle stanno ai
byte 105..109 della LUT e `0x32` scrive solo 0..104 — e stampa le due bande
adiacenti. Colori diversi vorrebbero dire quarto pigmento; colori identici sono
la prova diretta che ne esistono tre.

---

## Origine

Il driver è **header-only** (`inline` nell'`.h`, nessuna `.cpp`) e nasce
come fork del driver upstream
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)
(pannello Good Display 13.3" 3-colori). Eredita da `GxEPD2_EPD` (classe
base della libreria) e implementa la sequenza di comandi specifica del
pannello SOLUM 9.7" su controller SSD1677:

- soft-reset (`0x12`)
- soft-start (`0x0C`)
- MUX per 672 gate lines (`0x01 = 0x9F 0x02 0x00`) — corretto rispetto alle
  680 dell'upstream, vedi [§0.3](#03-source-gate-e-mux)
- bordo bianco (`0x3C = 0x01`)
- sensore di temperatura interno (`0x18 = 0x80`)
- entry-mode x/y increase (`0x11 = 0x03`)
- finestra RAM full-window (`0x44` / `0x45` / `0x4E` / `0x4F`)
- full-window refresh (`0x22 = 0xF7` + `0x20`)
- power-off (`0x22 = 0xC3`)

Rispetto ai driver stock di GxEPD2 per pannelli simili, questa versione
introduce:

## 1. `GxEPDImage::showImage()` — unico entry-point pubblico

Free function template nel namespace `GxEPDImage` (vive nel driver `.h`):

```cpp
template<typename DisplayT>
void GxEPDImage::showImage(DisplayT& display,
                           const GxEPDImage::Descriptor& d,
                           int16_t x = 0, int16_t y = 0);
```

È **l'unica funzione pubblica per stampare un'immagine** sul pannello.
Va chiamata **dentro** un loop `firstPage()`/`nextPage()` del template
`GxEPD2_3C`, dopo `fillScreen()` e prima di `nextPage()`. Accetta tutti
e 3 i formati del descrittore (BW / BWR / BWRY) e rende i piani black e red:
di un descrittore a tre piani, `data2` non viene toccato.

Pattern minimale one-shot:

```cpp
display.firstPage();
do {
  display.fillScreen(GxEPD_WHITE);
  GxEPDImage::showImage(display, *my_desc_ptr);
} while (display.nextPage());
display.hibernate();
```

Per immagini raw image2cpp B/N basta wrappare con la macro `GXEPD_BW_IMAGE`:

```cpp
GxEPDImage::showImage(display, GXEPD_BW_IMAGE(my_array, 960, 672));
```

Il chiamante è responsabile di: aprire il loop paged e chiamare
`hibernate()` se vuole spegnere il pannello.

**Multi-call per page.** `showImage` può essere chiamata 0, 1 o N volte
all'interno di una stessa iterazione del paged loop senza problemi: il
page-tracking interno avanza il counter solo quando il template chiude
la page (`writeImage(black, color)` chiamato da `nextPage()`), non a
ogni chiamata `showImage`. Utile per **compositing multi-immagine**:
sovrapporre più descrittori con offset `(x, y)` diversi, ognuno
disegnato correttamente nella sola porzione che intersecta la page
corrente. Vedi §5 "Loop pixel di `showImage` row-skip" per i dettagli
del meccanismo.

### Casi d'uso e firme

| # | Caso d'uso | Firma array (in `.h` incluso) | Firma chiamata |
|---|---|---|---|
| 1 | Bitmap **B/N raw** (image2cpp) | `const unsigned char img_xxx[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BW_IMAGE(img_xxx, w, h));` |
| 2 | **BWR** da `epd_image_converter.pyw` | `const GxEPDImage::Descriptor img_xxx_desc;` *(auto-generato)* | `GxEPDImage::showImage(display, img_xxx_desc);` |
| 3 | **BWRY** da `epd_image_converter.pyw` | `const GxEPDImage::Descriptor img_xxx_desc;` *(auto-generato)* | `GxEPDImage::showImage(display, img_xxx_desc);` — rende i piani black e red, `data2` non viene toccato |
| 4 | **BWR raw inline** (2 piani separati) | `const unsigned char img_b[], img_r[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BWR_IMAGE(img_b, img_r, w, h));` |
| 5 | **BWRY raw inline** (3 piani separati) | `const unsigned char img_b[], img_r[], img_y[] PROGMEM = { … };` | `GxEPDImage::showImage(display, GXEPD_BWRY_IMAGE(img_b, img_r, img_y, w, h));` — come il caso 3 |
| 6 | **Single-channel diretto** (no GFX) | `const unsigned char img_b[], img_r[] PROGMEM = { … };` | `display.epd2.writeImageBlack(img_b, x, y, w, h, true);` + `…Red(…)` + `display.epd2.refresh(false);` |

Casi **1–5** vanno chiamati dentro un loop `firstPage()` / `nextPage()`
con `fillScreen()` prima, e seguiti da `display.hibernate()` se si vuole
spegnere il pannello. Il caso **6** sta fuori dal loop paged.

- Nei casi **3** e **5** il terzo piano del descrittore viene ignorato: il
  pannello ha due piani, e `showImage` rende `data0` e `data1`. Il descrittore
  resta valido, non serve rigenerare gli asset.
- Il caso **6** bypassa il template GFX e si chiama standalone, `refresh()`
  esplicito compreso. Non va mescolato a un loop paged nello stesso frame: il
  template riscrive entrambi i piani e la scrittura manuale andrebbe persa.

## 2. Due API siblings single-channel uniformi

I due piani immagine del controller SSD1677 sono esposti con shape identica per
scritture single-channel (no refresh):

```cpp
void writeImageBlack(const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x24
void writeImageRed  (const uint8_t* bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool pgm = true);  // cmd 0x26
```

Convenzione bitmap input: bit=1 dove il pixel **non** appartiene a quel
canale (formato compatibile con lo script Python e image2cpp). Per l'accent
il driver applica `~data` prima del transfer SPI per allinearsi alla polarity
nativa SSD1677 (bit=1 in RAM = colorante acceso).

`writeImageRed` **non maschera** il piano BW sotto i pixel accesi, e non serve:
LUT2 = LUT3, quindi con l'accent a 1 il valore di `0x24` non cambia il colore
reso ([§0.10](#010-il-4-colore-non-esiste-questione-chiusa)).

## 3. Il template a due canali e le scritture out-of-band

Il driver custom origina da `GxEPD2_1330c_GDEM133Z91`, un driver del
ramo **`gdem3c`** di GxEPD2, pensato per pannelli a **3 colori**
(bianco/nero/+1 accent). Tutta l'organizzazione del rendering di GxEPD2
ruota attorno al template `GxEPD2_3C<Driver, page_height>`, che fa da
intermediario tra il layer GFX (Adafruit_GFX) e il driver, e ha
un'architettura **hard-coded su 2 canali**:

- mantiene un buffer GFX paged in RAM con **due piani** (black + accent)
- nel loop `firstPage()` / `nextPage()` invoca **una sola hook** sul
  driver: `writeImage(black, color, ...)` in modalità full-window
  ([GxEPD2_3C.h:368](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/GxEPD2_3C.h#L368)); la variante
  `writeImagePart(black, color, ...)` è usata solo con `setPartialWindow`
  ([GxEPD2_3C.h:273](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/GxEPD2_3C.h#L273))
- non ha nè campi nè API per un terzo canale

Su questo pannello i due canali del template corrispondono **esattamente** ai
due piani del controller, quindi non c'è niente da aggirare: il rendering paged
copre tutto ciò che il pannello sa mostrare. È il motivo per cui il driver 9.7"
non espone primitive per un terzo piano e il contratto di `GxEPDImage.h` chiede
due soli metodi, `setPaged()` e `showImagePageHint()`.

**Il meccanismo out-of-band resta, ma per altro.** Una scrittura "out-of-band"
è semplicemente una scrittura sulla RAM del controller fatta **fuori** dal loop
paged, con le API siblings di [§2](#2-due-api-siblings-single-channel-uniformi):

```
┌────────────────────────────────────────────────────────────────┐
│  writeImageBlack(buf, ...)   ← scrive 0x24 fuori dal paged     │
│  writeImageRed(buf, ...)     ← scrive 0x26 fuori dal paged     │
│  display.refresh(false);     ← e si aggiorna senza GFX         │
└────────────────────────────────────────────────────────────────┘
```

Serve al compositing manuale, per esempio a comporre un frame senza passare da
Adafruit_GFX. **Attenzione al conflitto**: se dopo una scrittura out-of-band si
apre un loop paged, il template riscrive entrambi i piani per intero e la
scrittura manuale viene persa. Le due strade non si mescolano nello stesso
frame.

Su un pannello che avesse davvero un terzo piano, sarebbe questa la strada per
pilotarlo: il driver esporrebbe una primitiva con la stessa forma delle due di
[§2](#2-due-api-siblings-single-channel-uniformi), il
chiamante la userebbe prima di `firstPage()` e il piano sopravvivrebbe al loop
perchè il template non lo tocca. È il caso del driver 12.2", dove il quarto
colore è ancora da determinare.

**Il page-hint.** `showImagePageHint()` resta necessario, ma non per il giallo:
serve alla skip-by-row di `showImage`, che salta a priori le righe sorgente
fuori dalla page corrente e riduce il loop pixel a 1/8 delle iterazioni. Il
template tiene `_current_page` privato senza getter, quindi il driver mantiene
un contatore parallelo: azzerato in `setPaged()`, avanzato in
`writeImage(black, color, ...)`.

### `drawPixel(x, y, GxEPD_YELLOW)` finisce sul rosso, ed è corretto

Nel sorgente di `GxEPD2_3C.h` la funzione `drawPixel` ha questa condizione:

```cpp
else if ((color == GxEPD_RED) || (color == GxEPD_YELLOW))
  _color_buffer[i] = (_color_buffer[i] & ...);   // scrive nel piano red (0x26)
```

`GxEPD_YELLOW` viene **trattato come `GxEPD_RED`**: la libreria non distingue.
Su questo pannello è l'unico esito possibile, perchè un terzo colore non c'è
([§0.10](#010-il-4-colore-non-esiste-questione-chiusa)): chi scrive
`GxEPD_YELLOW` ottiene il solo accent che il film ha, e non c'è nessun pitfall
da aggirare. Su un pannello a quattro colori, invece, si tradurrebbe in un
pixel rosso al posto del giallo atteso.

## 4. Sistema di descrittori universali (`namespace GxEPDImage`)

```cpp
namespace GxEPDImage {
  enum Format : uint8_t {
    FORMAT_BW_1BPP   = 0,   // 1 buffer 1bpp (compat image2cpp)
    FORMAT_BWR_1BPP  = 1,   // buffer separati black + red
    FORMAT_BWRY_1BPP = 2,   // buffer separati black + red + yellow
  };

  struct Descriptor {
    Format format;
    uint16_t width, height;
    const uint8_t *data0, *data1, *data2;
  };
}
```

Il descrittore porta con sè formato e dimensioni. `showImage()` rende `data0` e
`data1`; `data2` non viene toccato, ed è a disposizione di un chiamante che
voglia scriverlo con le primitive di un driver che abbia davvero un terzo piano.
Per costruire descrittori inline lo header espone tre macro di comodo:

```cpp
GXEPD_BW_IMAGE(ptr, w, h)
GXEPD_BWR_IMAGE(black, red, w, h)
GXEPD_BWRY_IMAGE(black, red, yellow, w, h)
```

Lo script Python `epd_image_converter.pyw` genera automaticamente una
variabile `img_<nome>_desc` ad ogni conversione, pronta per essere
passata a `showImage()`.

## 5. Ottimizzazioni rispetto al driver stock

### Confronto driver custom vs. base GDEM133Z91 e patterns moderni di GxEPD2

La tabella sotto mette in evidenza ogni divergenza dal driver upstream
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)
(che è il driver SSD1677 più vicino al pannello SOLUM) e dai patterns
adottati negli altri driver moderni della libreria.

| Aspetto | GDEM133Z91 base | Driver custom SOLUM | Stato |
|---|---|---|---|
| Init RAM | solo B+R (0x24, 0x26) | solo B+R (0x24, 0x26) | = parità: sono i due piani che il pannello ha |
| `delay(1)` ESP8266 WDT in hot path | presente in `_writeImage`/`_writeImagePart` | rimosso (target ESP32, task WDT 5 s) | ✓ migliore |
| Delay dopo SWRESET | 10 ms (sotto-stimato) | 200 ms (datasheet SSD1677 ~100-300 ms) | ✓ migliore |
| Entry-mode `0x11` | inviato a ogni `_setPartialRamArea` | inviato 1 volta in `_InitDisplay` | ✓ migliore |
| Cleanup accent dirty-tracking | n/a (cleanup sempre o mai) | flag `_color_dirty` + `_cleanColorIfDirty()` | ✓ ottimizzato |
| Polarity cleanup accent | n/a | `0x00` esplicito (= accent spento, polarity nativa) | ✓ corretto vs. bug latente `0xFF` |
| `hibernate()` guard idempotente | scrive 0x10 sempre, anche su re-entry | early return se già `_hibernating` | ✓ migliore |
| `_init_display_done` reset on hibernate | sì | sì | = parità |
| 4° canale (`0x28`) | non gestito | non gestito | = parità: misurato come VCOM Sense, vedi [§0.10](#010-il-4-colore-non-esiste-questione-chiusa) |
| Cleanup accent in `writeImage(bitmap, ...)` BW | non fa | sì (0x26 dirty-checked) | ✓ migliore |
| Cleanup accent in `writeImagePart(bitmap, ...)` BW | non fa | sì (allineato a `writeImage` per simmetria) | ✓ migliore |
| Attesa dopo SWRESET | `delay(10)` fisso | attesa sul BUSY, misurato 2 ms | ✓ ~190 ms recuperati per init |
| Loop pixel di `showImage` | itera tutti i pixel × tutte le 8 page (drawPixel early-return) | row-skip rotation-aware via page-hint counter (1/8 delle iterazioni) | ✓ ottimizzato (~170 ms/refresh) |
| Transfer SPI verso il controller | per-byte `_pSPIx->transfer(uint8_t)` (~1.5 μs/byte) | row-buffered via `_pSPIx->writeBytes(buf, n)` (FIFO 64-byte ESP32, ~0.8 μs/byte = limite del clock a 10 MHz) | ✓ ottimizzato (~225 ms/refresh) |
| MUX gate lines (`0x01`) | 680, corretto per GDEM133Z91 (`HEIGHT = 680`) | **672** (`0x9F 0x02 0x00`), quante il pannello SOLUM ne ha davvero | ✓ corretto, ~260 ms/refresh — da verificare su hardware, vedi [§0.3](#03-source-gate-e-mux) |

### Dettaglio delle ottimizzazioni

- **`_setPartialRamArea()`** non riscrive più l'entry-mode ad ogni draw
  (spostato una tantum in `_InitDisplay`).
- **Dirty-flag `_color_dirty`**: il cleanup di 0x26 in `writeImage(bw)` viene
  saltato se il flag è zero, evitando SPI inutile quando si concatenano frame
  B/N. Il cleanup stesso passa dal pattern hardware, 8-9 ms invece di 70-72
  (vedi [§0.7](#07-spi-e-volumi-di-transfer)).
- **Helper `_cleanColorIfDirty()`** centralizza la pulizia scrivendo `0x00`
  (polarity nativa SSD1677 = accent spento), corretto rispetto al precedente
  `0xFF` che scriveva "accent ON ovunque" — bug latente mascherato da
  hibernate+SWRESET ad ogni wake.
- **`hibernate()`** protetto contro chiamate multiple (early return se
  `_hibernating == true`); resetta il dirty-flag perchè il SWRESET successivo
  riporta la RAM controller a stato noto.
- **`writeImagePart(bitmap, ...)` BW** allineato a `writeImage(bitmap, ...)`:
  pulisce gli accent dirty prima di scrivere il piano `0x24`, evitando
  che red/yellow residui di un draw colorato precedente trasparissero
  sotto la zona BW.
- **Reset `_preserve_yellow`** centralizzato in `_Update_Full()` (chiamato
  da entrambi gli overload di `refresh()`) invece di duplicarlo. Auto-reset
  garantito al termine di ogni ciclo di rendering.
- **`delay(1)` rimossi** da `_writeImage` / `_writeImagePart` (servivano
  come yield WDT per ESP8266; ESP32 ha task WDT 5 s, ampio margine). In un
  refresh paged full-window `_writeImage` è chiamato 2 volte per page (piani
  `0x24` e `0x26`) × 8 page: **~16 ms** risparmiati, un tick FreeRTOS per
  chiamata, più i tick delle eventuali `_writeScreenBuffer`.
- **`GxEPDImage::showImage` row-skip**: il loop pixel skippa a priori le
  righe sorgente fuori dalla page corrente del template `GxEPD2_3C`,
  riducendo le iterazioni di un fattore ~8: 7 passate da ~24 ms evitate su
  8, **~170 ms** risparmiati per refresh full-screen. Il template upstream tiene `_current_page`
  privato senza getter pubblico, quindi il driver custom mantiene un
  counter `_show_image_page_hint` parallelo: reset a 0 dentro
  `setPaged()` (override del hook `virtual` di `GxEPD2_EPD` chiamato da
  `firstPage()` del template), avanzato dentro `writeImage(black, color, ...)`
  (invocato dal template ESATTAMENTE una volta per page in `nextPage()`),
  reset difensivo dentro `_Update_Full()`. Bonus: `showImage` può essere
  chiamata 0, 1 o N volte all'interno della stessa page senza
  desincronizzare il counter (il counter avanza solo a chiusura page,
  non per ogni chiamata `showImage`). Include anche hoist del row offset
  fuori dal loop interno e fallback al loop completo per rotation 1/3
  (90°, dove la skip-by-row non si applica perchè le righe sorgente
  mappano sull'asse x dell'output).
- **Bulk SPI transfer**: i tre hot path SPI (`_writeImage`,
  `_writeImagePart`, `_writeScreenBuffer`) usano `_pSPIx->writeBytes(buf, n)`
  invece di `_transfer(uint8_t)` per-byte. Il base class `GxEPD2_EPD`
  espone `_pSPIx` come `protected`, quindi il subclass può chiamarlo
  direttamente. Il loop interno popola un buffer di stack
  (120 byte/riga per le immagini, 256 byte di chunk per la scrittura
  costante) e lo scarica con `writeBytes`, che usa la FIFO 64-byte
  dell'ESP32 e arriva al limite del clock SPI
  (~0.8 μs/byte a 10 MHz, contro ~1.5 μs/byte della transfer per-byte upstream
  — `_writeData(buf, n)` di `GxEPD2_EPD` è anch'esso un loop per-byte di
  `transfer()`, NON un bulk vero, vedi
  [GxEPD2_EPD.cpp:197-207](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/GxEPD2_EPD.cpp#L197-L207)).
  Su un refresh BWRY steady-state (322.560 byte: paged B+R + piano giallo +
  cleanup `0x26`) il transfer SPI passa da ~484 ms a ~258 ms:
  **risparmio ~225 ms per refresh** (derivazione in
  [§0.7](#07-spi-e-volumi-di-transfer)). Stack temporaneo aggiunto:
  ~120 byte per `_writeImage`/`_writeImagePart`, ~256 byte per
  `_writeScreenBuffer`. API pubblica invariata, ottimizzazione
  trasparente.

## 6. API completa

La lista degli overload `drawImage*` / `writeImage*` / `writeImagePart*`
ereditati dalla base class è in
[drawImage_overloads_it.md](drawImage_overloads_it.md) (o la versione
inglese [drawImage_overloads.md](drawImage_overloads.md)).
Sono override di virtual del base class `GxEPD2_EPD` necessari al
contratto della libreria — non sono pensati per uso diretto: lo sketch
chiama `showImage()` per immagini singole, oppure il template
`GxEPD2_3C` invoca `writeImage(black, color)` a ogni `nextPage()` nel flusso
paged full-window (`writeImagePart` solo con `setPartialWindow`).

---

## Licenza

**GPL-3.0**: il driver nasce come copia modificata di
[`GxEPD2_1330c_GDEM133Z91`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdem3c/GxEPD2_1330c_GDEM133Z91.cpp)

Questo repo è un **fork** di [ZinggJM/GxEPD2](https://github.com/ZinggJM/GxEPD2) ma non ne duplica l'albero: contiene solo questa libreria. 
I riferimenti ai sorgenti upstream in questa doc puntano al **tag 1.6.9**, la versione su cui il driver è stato scritto e verificato, non a `master`: così le righe citate restano valide anche quando upstream avanza.
