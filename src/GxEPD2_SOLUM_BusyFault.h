// Esito dell'attesa del BUSY di un refresh, senza nessuna dipendenza.
//
// Il driver lo alza quando il pannello non si comporta come un refresh vero:
// TooShort se il BUSY è sceso prima del minimo plausibile (il controller non ha
// eseguito la waveform), Timeout se è ancora alto allo scadere dell'attesa. Un
// refresh con un guasto non convalida nè il frame nè il riferimento del
// differenziale, e chi lo legge può rifare il frame in pieno.

#ifndef _GxEPD2_SOLUM_BusyFault_H_
#define _GxEPD2_SOLUM_BusyFault_H_

#include <stdint.h>

enum class GxEPD2_SOLUM_BusyFault : uint8_t
{
  None     = 0,   // nessun guasto
  TooShort = 1,   // BUSY sceso prima della durata minima plausibile del refresh
  Timeout  = 2    // BUSY ancora alto allo scadere dell'attesa
};

#endif
