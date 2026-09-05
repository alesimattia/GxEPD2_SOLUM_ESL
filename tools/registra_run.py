#!/usr/bin/env python3
"""
registra_run.py - apre la porta seriale di una sonda di bring-up, inoltra la
tastiera verso la board e scrive tutto quello che arriva sia a schermo sia su
file. Serve perchè l'ESP32 non può scrivere sul disco del PC e le sonde ora
fanno domande a runtime: il run dev'essere interattivo e registrato insieme.

Nessuna dipendenza: solo libreria standard. La porta viene messa in raw con
stty, poi si fa select() su stdin e sulla porta.

  python3 registra_run.py /dev/cu.usbmodemXXXX
  python3 registra_run.py /dev/cu.usbmodemXXXX -b 115200 -o cartella/

Ctrl-C chiude il file e stampa quante righe ha scritto.
"""

import argparse
import glob
import os
import select
import subprocess
import sys
import termios
import tty
from datetime import datetime

BAUD_DEFAULT = 115200


def porte_disponibili():
    return sorted(glob.glob("/dev/cu.usb*") + glob.glob("/dev/ttyUSB*")
                  + glob.glob("/dev/ttyACM*"))


def apri_porta(porta, baud):
    """Mette la porta in raw al baud richiesto e la apre in lettura/scrittura."""
    subprocess.run(["stty", "-f", porta, str(baud), "raw", "-echo",
                    "-hupcl", "cs8", "-cstopb", "-parenb"], check=True)
    return os.open(porta, os.O_RDWR | os.O_NOCTTY)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("porta", nargs="?", help="es. /dev/cu.usbmodem5ABA0264671")
    ap.add_argument("-b", "--baud", type=int, default=BAUD_DEFAULT)
    ap.add_argument("-o", "--out", default=".", help="cartella del file di log")
    ap.add_argument("-n", "--nome", default=None, help="prefisso del file di log")
    args = ap.parse_args()

    disponibili = porte_disponibili()
    if not args.porta:
        print("Serve la porta. Disponibili adesso:", file=sys.stderr)
        for p in disponibili or ["  (nessuna)"]:
            print(f"  {p}", file=sys.stderr)
        return 2
    if not os.path.exists(args.porta):
        print(f"{args.porta} non esiste. Disponibili adesso:", file=sys.stderr)
        for p in disponibili or ["  (nessuna)"]:
            print(f"  {p}", file=sys.stderr)
        return 2

    prefisso = args.nome or os.path.basename(os.path.abspath(args.out))
    nome = f"{prefisso}_{datetime.now():%Y%m%d-%H%M%S}.txt"
    percorso = os.path.join(args.out, nome)

    fd = apri_porta(args.porta, args.baud)
    log = open(percorso, "w", encoding="utf-8", errors="replace", buffering=1)
    log.write(f"# {args.porta} @ {args.baud} baud, {datetime.now():%Y-%m-%d %H:%M:%S}\n")

    print(f"registro su {percorso}   (Ctrl-C per chiudere)", file=sys.stderr)

    # stdin in raw, così i tasti arrivano alla board senza aspettare l'invio;
    # ma l'invio va inoltrato come '\n', che è quello che leggiRiga() si aspetta.
    stdin_fd = sys.stdin.fileno()
    tty_prima = termios.tcgetattr(stdin_fd) if os.isatty(stdin_fd) else None
    if tty_prima:
        tty.setcbreak(stdin_fd)

    righe = 0
    try:
        while True:
            pronti, _, _ = select.select([fd, stdin_fd], [], [], 0.2)
            if fd in pronti:
                dati = os.read(fd, 4096)
                if not dati:
                    break
                testo = dati.decode("utf-8", errors="replace")
                sys.stdout.write(testo)
                sys.stdout.flush()
                log.write(testo)
                righe += testo.count("\n")
            if stdin_fd in pronti:
                tasti = os.read(stdin_fd, 64)
                if not tasti:
                    break
                os.write(fd, tasti.replace(b"\r", b"\n"))
    except KeyboardInterrupt:
        pass
    finally:
        if tty_prima:
            termios.tcsetattr(stdin_fd, termios.TCSADRAIN, tty_prima)
        log.write(f"\n# chiuso {datetime.now():%Y-%m-%d %H:%M:%S}, {righe} righe\n")
        log.close()
        os.close(fd)
        print(f"\n{percorso}: {righe} righe", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
