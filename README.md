# POS – Semestrálna práca
## Viacprocesová klient-server hra Snake

Semestrálna práca z predmetu **Princípy operačných systémov**.
Cieľom je vytvoriť viacprocesovú a viacvláknovú aplikáciu v jazyku C
s využitím medziprocesnej komunikácie (IPC).

Aplikácia implementuje hru **Snake** v architektúre **klient – server**
s použitím **UNIX socketov**.

## Architektúra aplikácie

Aplikácia pozostáva z dvoch procesov:
- **server** – riadi hernú logiku
- **client** – zabezpečuje vstup od používateľa a vykresľovanie hry

### Server
Server je zodpovedný za:
- hernú logiku (pohyb hada, ovocie, kolízie),
- generovanie ovocia,
- herné režimy (časový a štandardný),
- spracovanie pauzy a návratu do hry,
- správu herného sveta (prekážky).

Server používa dve vlákna:
- **tick thread** – pravidelne aktualizuje stav hry,
- **recv thread** – spracováva vstup od klienta.

### Klient
Klient je zodpovedný za:
- textové menu (nová hra, pripojenie ku hre, koniec),
- vstup od používateľa,
- vykresľovanie herného sveta,
- komunikáciu so serverom.

Klient používa dve vlákna:
- **input thread** – číta vstup od používateľa,
- **recv thread** – prijíma stav hry zo servera a vykresľuje ho.

---

## Medziprocesná komunikácia (IPC)

Klient a server komunikujú prostredníctvom **UNIX domain socketu**

## Herné režimy

### 1. Časový režim (time)
Hra končí po uplynutí vopred definovaného času.

###  Štandardný režim (standard)
Po skončení hry server čaká **10 sekúnd** na pripojenie nového klienta.
Ak sa nový klient nepripojí, server sa korektne ukončí.

## Ovládanie

| Kláves | Akcia |
|------|------|
| `w` | pohyb hore |
| `s` | pohyb dole |
| `a` | pohyb doľava |
| `d` | pohyb doprava |
| `p` | pauza / návrat do hry |
| `q` | ukončenie hry |

---

## Spustenie aplikácie

### Build
make all

### Spustenie servera (manuálne)
./server maps/map1.txt --mode standard

alebo časový režim:
./server maps/map1.txt --mode time --seconds 30


### Spustenie klienta
./client

Klient obsahuje textové menu, ktoré umožňuje:
- vytvoriť novú hru (spustí server),
- pripojiť sa k existujúcej hre,
- ukončiť aplikáciu.



---

## Autor
- Juraj J.
