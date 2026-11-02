# Parità con Cinema

Ogni funzione di Cinema (app web, 0.3.9) e dove sta in Cinema++. Fonte: lettura completa del codice di Cinema (`src/js`, `scripts`), 1° novembre 2026; aggiornato il 2 novembre 2026.

Legenda: ✅ fatto · 🟡 c'è in Native Tv, da adattare · ⬜ da fare.

## Home
- ✅ Home nativa su webOS 5
- ✅ Italiano in tutta l'app TV e nella dashboard, con inglese e spagnolo
- ✅ Schede Per te · Film · Serie · Musica · Live · App · Ingressi, ricerca dalla lente in alto
- ✅ Orologio, data in italiano, meteo e avviso quando Jellyfin non risponde
- ✅ Riquadro grande con titolo, dati, descrizione e copertina della libreria
- ✅ Sfondo dalla libreria (backdrop del titolo a fuoco); righe che svaniscono sotto le schede
- ⬜ Riga di scorciatoie quando Jellyfin non ha titoli
- ⬜ Tasto giallo: Cinema ↔ vista essenziale (il giallo ora mette in pausa la musica)

## Jellyfin
- ✅ Collegamento con Quick Connect dalla dashboard (App → Jellyfin), dietro il PIN amministratore; riuso di `/var/lib/launch-home-cinema/jellyfin.json`
- ✅ Righe: Continua a guardare (Resume + NextUp senza doppioni), Aggiunti di recente, I tuoi film, Le tue serie, La tua musica
- ✅ Ricerca nella libreria (film e serie prima della musica), anche senza chiave TMDB
- ✅ Copertine dal server con cache su disco (due misure, al massimo 240), token solo nel servizio
- ✅ Aggiornamento ogni 10 minuti e a richiesta; stati offline e non autorizzato
- ✅ Apertura in LiteFin con `cinemaItemId`/`cinemaAction`, o nel primo client installato
- ⬜ Quick Connect dalla TV (oggi dalla dashboard)
- ⬜ Scelta del client preferito
- ⬜ Scopri e Seerr (`/Litefin/Seerr/Status`), pannello con le spiegazioni

## Live e ingressi
- ✅ TV live dentro la Home (scheda Live): `utp.broadcast` nella finestra video esportata dell'app, pagina trasparente sopra, comandi che spariscono dopo 10 s
- ✅ Zapping CH+/CH− (450 ms) e numeri (1,8 s), banner del canale con programma e A seguire, OK su un canale lo sintonizza
- ✅ Ingressi HDMI dietro la Home (scheda Ingressi, `utp.extinputs`), «Nessun segnale su HDMI N», secondo OK a schermo intero
- ✅ Tutti i canali quando non ci sono preferiti
- ⬜ Guida a griglia; registrazione (il DVR di LG c'è: `utp.dvr`, da provare)

## Barra delle app
- ✅ Riga delle app (Le tue app), ordinabile e con app nascoste (Native Tv)
- ✅ Riga Ingressi con «Nessun segnale» in Per te (interruttore nelle impostazioni rapide)
- ✅ Tessere Tutte le app e Impostazioni TV in fondo (interruttore nelle impostazioni rapide)
- ✅ Numeri del telecomando: 1–9 aprono l'app in quella posizione
- ⬜ App personalizzate, importa dalla home LG

## Aspetto
- ✅ Sfondo nebulosa predefinito (Unsplash) nelle quattro fasce orarie, caricabili dalla dashboard
- ⬜ Sfondi di Cinema: gradienti, foto incluse, online, USB, video aerei Apple, presentazione, zoom lento
- 🟡 Orologio 12/24 h (Native Tv); fuso, posizione e dimensione no
- ⬜ Colori delle tessere, colore del focus, modalità prestazioni (`layout.colours` è salvato ma non usato)
- 🟡 Profili: quelli di Native Tv (persone con la loro Home); Standard/Notte/Cinema no

## Musica, meteo, salvaschermo
- ✅ Musica ambiente: i sei brani di Cinema in streaming o la cartella `lounge/music` di una chiavetta, casuale, dissolvenze, Verde = brano successivo, Giallo = pausa (Schermo → Musica)
- ✅ Meteo Open-Meteo: ora nella barra, cinque giorni e ricerca della località nella dashboard (Schermo → Meteo)
- ✅ Salvaschermo Cinema: sfondi della Home in dissolvenza con zoom lento, ora e data in italiano, al posto di quello di LG
- 🟡 Spegnimento dopo N ore: l'inattività del Tempo davanti alla TV di Native Tv

## Strumenti TV (tappa 1)
- ✅ App tenute in memoria, ID pubblicitario, avvisi sportivi, LG Channels
- ✅ Precaricamento, servizi, accordi sui dati, tasti del telecomando, blocco aggiornamenti LG (Native Tv)
- 🟡 Spegnimento a un orario (ora di andare a letto di Native Tv)
- ✅ Tasto Home a Cinema++ (LG Input Hook → `cinemapp-home-key`, apre la Home); tre volte Home e Indietro tenuto premuto restano quelli di Cinema
- ✅ Servizio di Cinema++ a ogni accensione (`57-cinemapp-service`)
- ⬜ Cinema++ come prima app all'accensione (oggi parte Cinema web)
- ✅ Aggiornamenti di Cinema++ dalle release GitHub con SHA-256 (Impostazioni → Server)
- ⬜ Aggiornamenti di Bloomee

## Striscia LED (tappa 2)
- ✅ Tutto, in C++

## Gestione
- ✅ Backup e ripristino di Native Tv, con meteo e musica di Cinema++
- ✅ Diagnostica (Impostazioni → Server)
- ⬜ Assistente vocale (richiede Python 3, che la TV non ha)
- 🟡 USB: musica da `lounge/music`; `lounge/config.json` e sfondi no
