// =============================================================================
// Macchina a stati del refresh differenziale B/N, senza nessun I/O.
//
// Il refresh differenziale decide a ogni frame fra due strade: il refresh pieno
// (waveform dell'OTP, ~24 s, scrive anche il rosso) e il partial B/N (LUT
// custom, ~0,64 s, non scrive rosso e lascia fermo quello già sul vetro). Il
// partial è corretto solo se il piano rosso del frame nuovo è identico a quello
// che sta sul vetro, e il controllo si fa su un CRC per pagina del piano colore
// che il template GxEPD2_3C passa al driver mentre disegna.
//
// Questa struct tiene solo lo stato e le regole: quale RAM contiene il B/N che
// sta sul vetro, i CRC dell'ultimo frame pieno, se il riferimento è ancora
// affidabile. Le scritture sul controller e i refresh restano nel driver, così
// la logica si prova su host senza una board.
//
// Ruoli delle RAM. Dopo un refresh pieno il B/N sul vetro sta in 0x24 e 0x26
// contiene il rosso. Il partial legge la coppia di bit (0x26, 0x24) e sceglie
// la LUT dal valore dei bit, quindi il frame nuovo si scrive nell'altra RAM e a
// ogni passata i due ruoli si scambiano: `glassInRam26` dice dov'è il B/N del
// vetro, e con il nuovo in 0x26 le righe VS di LUT1 e LUT2 vanno scambiate.
//
// Header puro: solo <stdint.h>, niente memcpy.
// =============================================================================

#ifndef _GxEPD2_SOLUM_Differential_H_
#define _GxEPD2_SOLUM_Differential_H_

#include <stdint.h>

class GxEPD2_SOLUM_Differential
{
  public:
    /** Pagine del loop paged che il riferimento sa tenere: il template ne usa 8 a 84 righe. */
    static constexpr uint8_t MAX_PAGES = 16;

    /** Cosa deve fare il driver con la pagina appena ricevuta. */
    enum class PageAction : uint8_t
    {
      WriteFull,  // frame pieno: scrive B/N in 0x24 e rosso in 0x26 come sempre
      WriteNew,   // frame differenziale: scrive il B/N nuovo nella RAM libera
      Skip        // frame rifiutato: nessuna scrittura, il pannello non si tocca
    };

    /** Attiva o spegne il meccanismo; spento non calcola niente e non tiene riferimenti. */
    void setEnabled(bool on)
    {
      _enabled = on;
      if (!on)
      {
        invalidate();
        disarm();
      }
    }

    bool enabled() const { return _enabled; }
    bool refValid() const { return _refValid; }
    bool dirty() const { return _dirty; }
    bool armed() const { return _armed; }
    bool refused() const { return _refused; }
    uint8_t chain() const { return _chain; }

    /** Pagine in cui il piano rosso è cambiato rispetto al riferimento, un bit per pagina. */
    uint16_t refusedMask() const { return _refusedMask; }

    /** Vero se, dopo un frame, un riferimento affidabile giustifica di non addormentare il controller. */
    bool keepsReference() const { return _enabled && _refValid; }

    /** Vero se il frame nuovo va scritto in 0x26, e allora la LUT va usata con le righe scambiate. */
    bool newGoesToRam26() const { return !_glassInRam26; }
    bool lutSwapped() const { return newGoesToRam26(); }

    /**
     * Inizio di un loop paged (setPaged() del driver): riparte il conteggio
     * delle pagine. Non tocca lo stato armato, che si decide prima di firstPage().
     */
    void beginFrame()
    {
      _pagesSeen = 0;
      _nextY = 0;
      _dirty = false;
    }

    /**
     * Prova ad armare il frame differenziale: riesce solo con un riferimento
     * valido. Azzera esito e mask del frame precedente, che restano leggibili
     * dopo un rifiuto fino al frame dopo.
     */
    bool arm()
    {
      if (!_enabled || !_refValid || _dirty) return false;
      _armed = true;
      _refused = false;
      _refusedMask = 0;
      return true;
    }

    /** Fine del frame armato, a prescindere dall'esito. */
    void disarm() { _armed = false; }

    /**
     * Riceve una pagina con il CRC del suo piano colore e la sua posizione.
     * Le pagine devono arrivare contigue dall'alto: una pagina fuori ordine
     * non è un loop paged e il riferimento non è più affidabile. In armato
     * confronta con il riferimento e continua a confrontare anche dopo il
     * primo rifiuto, così la mask dice dove il rosso è cambiato. Un chiamante
     * che esce dal loop alla prima pagina rifiutata ottiene però una mask solo
     * fino a quella pagina, perchè le pagine successive non arrivano mai qui.
     */
    PageAction onPage(uint32_t crc, int16_t y, int16_t h)
    {
      if (y != _nextY || h <= 0 || _pagesSeen >= MAX_PAGES)
      {
        invalidate();
        return _armed ? PageAction::Skip : PageAction::WriteFull;
      }
      const uint8_t page = _pagesSeen;
      ++_pagesSeen;
      _nextY = (int16_t)(y + h);

      if (_armed)
      {
        const bool mismatch = !_refValid || _dirty || page >= _refPages || crc != _refCrc[page];
        if (mismatch)
        {
          _refused = true;
          _refusedMask |= (uint16_t)(1u << page);
        }
        return _refused ? PageAction::Skip : PageAction::WriteNew;
      }

      _nextCrc[page] = crc;
      _refValid = false;
      return PageAction::WriteFull;
    }

    /** Vero se il frame armato ha passato tutte le pagine senza rifiuti e può lanciare il partial. */
    bool canFire(int16_t height) const
    {
      return _armed && _refValid && !_dirty && !_refused &&
             _pagesSeen == _refPages && _nextY == height;
    }

    /**
     * Un refresh pieno è finito: i CRC del frame appena disegnato diventano il
     * riferimento, ma solo se le pagine coprono lo schermo e nessuna scrittura
     * estranea è intervenuta. Azzera comunque il conteggio, così un refresh
     * pieno fuori da un loop paged non rivalida il riferimento con CRC vecchi.
     */
    void commitFull(int16_t height)
    {
      const bool complete = _enabled && !_dirty && _pagesSeen >= 1 && _pagesSeen <= MAX_PAGES &&
                            _nextY == height;
      if (complete)
      {
        for (uint8_t i = 0; i < _pagesSeen; ++i) _refCrc[i] = _nextCrc[i];
        _refPages = _pagesSeen;
        _refValid = true;
        _glassInRam26 = false;
        _chain = 0;
      }
      else
      {
        _refValid = false;
      }
      _pagesSeen = 0;
      _nextY = 0;
    }

    /** Un partial è finito: il B/N del vetro ora sta nell'altra RAM e la catena cresce. */
    void commitPartial()
    {
      _glassInRam26 = !_glassInRam26;
      if (_chain < 255) ++_chain;
      _pagesSeen = 0;
      _nextY = 0;
    }

    /**
     * Il riferimento non è più affidabile (RAM riscritta, controller
     * reinizializzato, refresh andato male). Un frame armato in corso viene
     * rifiutato anche se le sue pagine combaciano.
     */
    void invalidate()
    {
      _refValid = false;
      _dirty = true;
      if (_armed) _refused = true;
    }

  private:
    bool _enabled = false;
    bool _refValid = false;
    bool _dirty = false;
    bool _glassInRam26 = false;
    bool _armed = false;
    bool _refused = false;
    uint8_t _refPages = 0;
    uint8_t _pagesSeen = 0;
    uint8_t _chain = 0;
    int16_t _nextY = 0;
    uint16_t _refusedMask = 0;
    uint32_t _refCrc[MAX_PAGES] = {};
    uint32_t _nextCrc[MAX_PAGES] = {};
};

#endif
