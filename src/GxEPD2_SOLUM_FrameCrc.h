// =============================================================================
// Confronto del frame con quello già sul vetro, senza nessun I/O.
//
// Il driver salta il refresh quando il frame che il template gli ha appena
// passato è identico a quello che ha mandato al vetro l'ultima volta. Il
// confronto è sull'uscita del disegno, cioè sui due piani nero e colore, e non
// sui dati che l'hanno prodotta: qualunque ingresso che cambia ciò che si vede
// passa da lì, quindi nessuno si può dimenticare.
//
// Lo stato tiene due CRC32 concatenati su tutte le pagine del frame in corso,
// uno per piano, e quelli dell'ultimo frame effettivamente refreshato. Un frame
// è confrontabile solo se le pagine arrivano contigue dall'alto e coprono lo
// schermo, come per il riferimento del refresh differenziale.
//
// Header puro: la CRC arriva come funzione, così su host si prova senza la ROM
// dell'ESP32. Vale per qualunque driver che riceva i piani a pagine dal
// template GxEPD2_3C, con o senza il refresh differenziale.
// =============================================================================

#ifndef _GxEPD2_SOLUM_FrameCrc_H_
#define _GxEPD2_SOLUM_FrameCrc_H_

#include <stdint.h>

class GxEPD2_SOLUM_FrameCrc
{
  public:
    /** Stessa firma di esp_rom_crc32_le: il CRC parziale entra e quello aggiornato esce. */
    using CrcFunction = uint32_t (*)(uint32_t crc, const uint8_t* data, uint32_t length);

    explicit GxEPD2_SOLUM_FrameCrc(CrcFunction crcFunction) : _crcFunction(crcFunction) {}

    /** Attiva o spegne il confronto: spento non calcola niente e unchanged() è sempre falso. */
    void setEnabled(bool on)
    {
      _enabled = on;
      if (!on)
      {
        invalidateShown();
        endFrame();
      }
    }

    bool enabled() const { return _enabled; }
    bool shownValid() const { return _shownValid; }

    /** Inizio di un loop paged (setPaged() del driver): riparte l'accumulo. */
    void beginFrame()
    {
      _blackCrc = 0;
      _colorCrc = 0;
      _pagesSeen = 0;
      _nextY = 0;
      _runValid = _enabled;
    }

    /**
     * Riceve una pagina con i suoi due piani, `rowBytes` byte per riga. Una
     * pagina fuori ordine, un piano mancante o più pagine del tetto rendono il
     * frame non confrontabile, e il driver lo manda al vetro come sempre.
     */
    void addPage(const uint8_t* black, const uint8_t* color, int16_t y, int16_t h, uint16_t rowBytes)
    {
      if (!_runValid) return;
      if (black == nullptr || color == nullptr || h <= 0 || y != _nextY || _pagesSeen == 255)
      {
        _runValid = false;
        return;
      }
      const uint32_t length = uint32_t(rowBytes) * uint32_t(h);
      _blackCrc = _crcFunction(_blackCrc, black, length);
      _colorCrc = _crcFunction(_colorCrc, color, length);
      ++_pagesSeen;
      _nextY = (int16_t)(y + h);
    }

    /** Il frame in corso contiene una scrittura che il confronto non conosce: non è confrontabile. */
    void invalidateRun() { _runValid = false; }

    /** Vero se il frame in corso è completo, uguale all'ultimo refreshato e quello è ancora sul vetro. */
    bool unchanged(int16_t height) const
    {
      return _enabled && complete(height) && _shownValid &&
             _blackCrc == _shownBlackCrc && _colorCrc == _shownColorCrc;
    }

    /**
     * Il frame in corso è stato mandato al vetro: diventa quello mostrato, ma
     * solo se completo. Chiude comunque l'accumulo, così un refresh senza nuove
     * pagine non rilegge CRC vecchi.
     */
    void commit(int16_t height)
    {
      if (_enabled && complete(height))
      {
        _shownBlackCrc = _blackCrc;
        _shownColorCrc = _colorCrc;
        _shownValid = true;
      }
      else
      {
        _shownValid = false;
      }
      endFrame();
    }

    /** Chiude l'accumulo del frame in corso senza toccare quello mostrato. */
    void endFrame()
    {
      _pagesSeen = 0;
      _nextY = 0;
      _runValid = false;
    }

    /**
     * Il vetro sta per cambiare, o è cambiato in un modo che questo frame non
     * descrive (refresh pieno o partial, anche fuori dal loop paged): quello
     * che si credeva mostrato non lo è più.
     */
    void invalidateShown() { _shownValid = false; }

  private:
    /** Pagine contigue dall'alto che coprono l'altezza. */
    bool complete(int16_t height) const { return _runValid && _pagesSeen >= 1 && _nextY == height; }

    CrcFunction _crcFunction;
    bool _enabled = false;
    bool _runValid = false;
    bool _shownValid = false;
    uint8_t _pagesSeen = 0;
    int16_t _nextY = 0;
    uint32_t _blackCrc = 0;
    uint32_t _colorCrc = 0;
    uint32_t _shownBlackCrc = 0;
    uint32_t _shownColorCrc = 0;
};

#endif
