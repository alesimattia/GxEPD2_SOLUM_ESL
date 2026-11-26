#ifndef GXEPD_FRAME_H
#define GXEPD_FRAME_H

#include <stdint.h>
#include <type_traits>
#include <utility>

#include "GxEPD2_SOLUM_BusyFault.h"

/**
 * Un frame intero sul pannello in una chiamata, sopra il loop paged di
 * GxEPD2_3C: finestra piena, refresh differenziale armato se chi chiama lo
 * consente, uscita anticipata appena il driver rifiuta il frame perchè il rosso
 * è cambiato, rifacimento in pieno e salto del refresh a frame invariato. Lo
 * sketch scrive solo il disegno di una pagina e la politica che decide se
 * provare il partial; il protocollo con il driver sta qui.
 *
 * Le API del driver usate qui sono opzionali e si rilevano dal tipo: un driver
 * che non dichiara il differenziale o il salto fa un refresh pieno a ogni frame,
 * come un driver GxEPD2 qualunque. I rilevamenti sono template, così il ramo di
 * un'API assente non si istanzia nemmeno.
 */
namespace GxEPDFrame
{
  /** Vero se il driver dichiara hasDifferentialRefresh = true. */
  template <class Panel, class = void>
  struct SupportsDifferential : std::false_type {};

  template <class Panel>
  struct SupportsDifferential<Panel, std::void_t<decltype(Panel::hasDifferentialRefresh)>>
      : std::bool_constant<Panel::hasDifferentialRefresh> {};

  /** Vero se il driver sa saltare il refresh di un frame identico a quello sul
   *  vetro: servono sia setSkipUnchanged(bool) sia frameSkipped(). */
  template <class Panel, class = void>
  struct SupportsSkip : std::false_type {};

  template <class Panel>
  struct SupportsSkip<Panel, std::void_t<decltype(std::declval<Panel&>().setSkipUnchanged(true)),
                                         decltype(std::declval<const Panel&>().frameSkipped())>>
      : std::true_type {};

  /** Vero se il driver segnala i guasti del BUSY e la durata dell'ultimo refresh. */
  template <class Panel, class = void>
  struct SupportsBusyFault : std::false_type {};

  template <class Panel>
  struct SupportsBusyFault<Panel, std::void_t<decltype(std::declval<const Panel&>().busyFault()),
                                              decltype(std::declval<const Panel&>().lastUpdateMs())>>
      : std::true_type {};

  /** Accende o spegne il differenziale nel driver; senza l'API non fa niente. */
  template <class Epd>
  inline void setDifferential(Epd& epd, bool on)
  {
    if constexpr (SupportsDifferential<Epd>::value) epd.setDifferentialRefresh(on);
    else { (void)epd; (void)on; }
  }

  /** Accende o spegne il salto del frame invariato; senza l'API non fa niente. */
  template <class Epd>
  inline void setSkipUnchanged(Epd& epd, bool on)
  {
    if constexpr (SupportsSkip<Epd>::value) epd.setSkipUnchanged(on);
    else { (void)epd; (void)on; }
  }

  /** Partial consecutivi dall'ultimo refresh pieno, 0 senza differenziale. */
  template <class Epd>
  inline uint8_t chain(const Epd& epd)
  {
    if constexpr (SupportsDifferential<Epd>::value) return epd.differentialChain();
    else { (void)epd; return 0; }
  }

  /** Come è finito un frame. */
  enum class FrameResult : uint8_t
  {
    Full,     // refresh pieno, senza tentativo di partial
    Partial,  // partial B/N partito
    Skipped,  // identico a quello sul vetro: nessun refresh
    Refused   // partial tentato e non partito, frame rifatto in pieno (Skipped se è risultato invariato)
  };

  /** Esito di render(): refusedPages dice dove il rosso è cambiato (0 se il
   *  partial non è partito per un altro motivo), busyFault e updateMs vengono
   *  dall'ultimo refresh del driver. */
  struct Outcome
  {
    FrameResult result = FrameResult::Full;
    bool armed = false;
    uint16_t refusedPages = 0;
    GxEPD2_SOLUM_BusyFault busyFault = GxEPD2_SOLUM_BusyFault::None;
    uint32_t updateMs = 0;
  };

  namespace detail
  {
    template <class Epd>
    inline bool wasSkipped(const Epd& epd)
    {
      if constexpr (SupportsSkip<Epd>::value) return epd.frameSkipped();
      else { (void)epd; return false; }
    }

    template <class Epd>
    inline uint16_t refusedPages(const Epd& epd)
    {
      if constexpr (SupportsDifferential<Epd>::value) return epd.differentialRefusedPages();
      else { (void)epd; return 0; }
    }

    /** Copia nell'esito guasto e durata dell'ultimo refresh, se il driver li dà. */
    template <class Epd>
    inline void readBusy(const Epd& epd, Outcome& out)
    {
      if constexpr (SupportsBusyFault<Epd>::value)
      {
        out.busyFault = epd.busyFault();
        out.updateMs = epd.lastUpdateMs();
      }
      else (void)epd;
    }

    /**
     * Un loop paged completo. Nella passata armata esce appena il driver ha
     * rifiutato una pagina: le pagine dopo non cambierebbero l'esito, e il
     * driver disarma il frame anche senza arrivare al refresh.
     */
    template <class Display, class DrawPage>
    inline void runPages(Display& display, DrawPage& drawPage, bool armed)
    {
      uint16_t page = 0;
      display.firstPage();
      do
      {
        drawPage(page++);
      } while (display.nextPage() && !(armed && refusedPages(display.epd2) != 0));
    }
  }

  /**
   * Disegna e manda al pannello un frame. drawPage(pageIndex) disegna la
   * pagina corrente del loop paged, partendo da un buffer bianco, e può essere
   * chiamata due volte per pagina quando il partial viene rifiutato. Con
   * tryPartial il frame si prova prima come partial B/N; se il driver non ha un
   * riferimento il primo disegno è già pieno.
   */
  template <class Display, class DrawPage>
  inline Outcome render(Display& display, DrawPage&& drawPage, bool tryPartial)
  {
    using Epd = std::remove_reference_t<decltype(display.epd2)>;
    Outcome out;
    display.setFullWindow();

    if constexpr (SupportsDifferential<Epd>::value)
    {
      // Va armato prima di firstPage(), che chiama setPaged() nel driver.
      if (tryPartial && display.epd2.beginDifferentialFrame())
      {
        out.armed = true;
        detail::runPages(display, drawPage, true);
        const bool fired = display.epd2.endDifferentialFrame();
        // La mask si legge ora: la passata piena la ripulirebbe.
        out.refusedPages = detail::refusedPages(display.epd2);
        if (fired || detail::wasSkipped(display.epd2))
        {
          out.result = fired ? FrameResult::Partial : FrameResult::Skipped;
          detail::readBusy(display.epd2, out);
          return out;
        }
        detail::runPages(display, drawPage, false);
        out.result = detail::wasSkipped(display.epd2) ? FrameResult::Skipped : FrameResult::Refused;
        detail::readBusy(display.epd2, out);
        return out;
      }
    }
    else
      (void)tryPartial;

    detail::runPages(display, drawPage, false);
    out.result = detail::wasSkipped(display.epd2) ? FrameResult::Skipped : FrameResult::Full;
    detail::readBusy(display.epd2, out);
    return out;
  }
}

#endif // GXEPD_FRAME_H
