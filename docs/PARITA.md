# Parità con Cinema

Ogni funzione di Cinema (app web, 0.3.9) e dove sta in Cinema++. Fonte: lettura completa del codice di Cinema (`src/js`, `scripts`), 1° novembre 2026; aggiornato il 2 novembre 2026 (release 0.2.0).

Legenda: ✅ fatto · 🟡 c'è in Native Tv, da adattare · ⬜ da fare · ➖ non serve o non possibile.

## Home
- ✅ Home nativa su webOS 5
- ✅ Italiano in tutta l'app TV e nella dashboard, con inglese e spagnolo
- ✅ Schede Per te · Film · Serie · Musica · Live · App · Ingressi, ricerca dalla lente in alto
- ✅ Orologio, data in italiano, meteo e avviso quando Jellyfin non risponde
- ✅ Riquadro grande con titolo, dati, descrizione e copertina della libreria
- ✅ Sfondo dalla libreria (backdrop del titolo a fuoco); righe che svaniscono sotto le schede
- ✅ Riga «Le tue scorciatoie» quando il feed è vuoto (Jellyfin, YouTube, TV e HDMI, Tutte le app)
- ✅ Tasto giallo: vista Cinema ↔ vista essenziale (solo sfondo, app e ingressi); il rosso mette in pausa la musica, come in Cinema

## Jellyfin
- ✅ Collegamento con Quick Connect dalla dashboard (App → Jellyfin), dietro il PIN amministratore; riuso di `/var/lib/launch-home-cinema/jellyfin.json`
- ✅ Righe: Continua a guardare (Resume + NextUp senza doppioni), Aggiunti di recente, I tuoi film, Le tue serie, La tua musica
- ✅ Ricerca nella libreria (film e serie prima della musica), anche senza chiave TMDB
- ✅ Scheda Musica come pagina a griglia: album in ordine alfabetico, filtro per lettera (# e A–Z, CH+/CH− per cambiare lettera), ricerca solo nella musica (album, brani, artisti)
- ✅ Focus leggibile dal divano: bordo di 6 px, card che cresce del 16%, pagina che scorre fino alla riga selezionata; canale in onda con bordo e pallino colorati
- ✅ Copertine dal server con cache su disco (due misure, al massimo 240), token solo nel servizio
- ✅ Aggiornamento ogni 10 minuti e a richiesta; stati offline e non autorizzato
- ✅ Apertura in LiteFin con `cinemaItemId`/`cinemaAction`, o nel primo client installato
- ✅ Quick Connect dalla TV (Impostazioni → Home → Jellyfin): server trovati in rete con il rilevamento di Jellyfin, o «Collega di nuovo» al server in uso; dietro il PIN amministratore
- ✅ Scelta del client che apre i titoli (App → Jellyfin)
- ✅ Scheda Scopri: apre LiteFin sulla sua pagina Scopri (Seerr) e dice cosa manca (`/Litefin/Seerr/Status` di LiteFin Companion)

## Live e ingressi
- ✅ TV live dentro la Home (scheda Live): `utp.broadcast` nella finestra video esportata dell'app, pagina trasparente sopra, comandi che spariscono dopo 10 s
- ✅ Zapping CH+/CH− (450 ms) e numeri (1,8 s), banner del canale con programma e A seguire, OK su un canale lo sintonizza; il canale in onda ha il fuoco, un puntino e «In onda»
- ✅ Ingressi HDMI dietro la Home (scheda Ingressi, `utp.extinputs`), «Nessun segnale su HDMI N», secondo OK a schermo intero
- ✅ Tutti i canali quando non ci sono preferiti
- ➖ Guida a griglia: Cinema non l'aveva (ora e dopo sul banner e sulle tessere, come Cinema++)
- ➖ Registrazione: il firmware ha `utp.dvr`, ma la 43UN70006LA ha `tv.model.supportDvrReady: false`; forzarlo vorrebbe dire cambiare la configurazione di sistema della TV

## Barra delle app
- ✅ Riga delle app (Le tue app), ordinabile e con app nascoste (Native Tv)
- ✅ Riga Ingressi con «Nessun segnale» in Per te (interruttore nelle impostazioni rapide)
- ✅ Tessere Tutte le app e Impostazioni TV in fondo (interruttore nelle impostazioni rapide)
- ✅ Numeri del telecomando: 1–9 aprono l'app in quella posizione
- ✅ Tessere personalizzate: un'app aperta con parametri di avvio (dashboard, Home)
- ➖ Importa dalla home LG: Cinema++ mostra già tutte le app della home LG, da ordinare e nascondere

## Aspetto
- ✅ Sfondo nebulosa predefinito (Unsplash) nelle quattro fasce orarie, caricabili dalla dashboard
- ✅ Sfondi di Cinema: gradienti, le sue foto (scaricate una volta), chiavetta USB `lounge/backgrounds`, a turno ogni N minuti, zoom lento
- ⬜ Video aerei Apple come sfondo della Home (ci sono nel salvaschermo di Native Tv)
- 🟡 Orologio 12/24 h (Native Tv); fuso, posizione e dimensione no
- ✅ Colore del focus (dashboard, Home)
- ➖ Modalità prestazioni: in Cinema alleggeriva la pagina web; Cinema++ è nativa (app 16-19 MB)
- ✅ Ambienti Standard/Notte/Cinema (sfondo più scuro o gradiente notturno, musica a un quarto o spenta); i profili di Native Tv restano le persone con la loro Home

## Musica, meteo, salvaschermo
- ✅ Musica ambiente: i sei brani di Cinema in streaming o la cartella `lounge/music` di una chiavetta, casuale, dissolvenze, Verde = brano successivo, Giallo = pausa (Schermo → Musica)
- ✅ Meteo Open-Meteo: ora nella barra, cinque giorni e ricerca della località nella dashboard (Schermo → Meteo)
- ✅ Salvaschermo Cinema: sfondi della Home in dissolvenza con zoom lento, ora e data in italiano, al posto di quello di LG
- 🟡 Spegnimento dopo N ore: l'inattività del Tempo davanti alla TV di Native Tv

## Strumenti TV (tappa 1)
- ✅ App tenute in memoria, ID pubblicitario, avvisi sportivi, LG Channels
- ✅ Precaricamento, servizi, accordi sui dati, tasti del telecomando, blocco aggiornamenti LG (Native Tv)
- 🟡 Spegnimento a un orario (ora di andare a letto di Native Tv)
- ✅ Tasto Home a Cinema++ (LG Input Hook → `cinemapp-home-key`, apre la Home, anche da Cinema++ già aperta sulle impostazioni); tre volte Home e Indietro tenuto premuto restano quelli di Cinema
- ✅ Servizio di Cinema++ a ogni accensione (`57-cinemapp-service`)
- ✅ Cinema++ come prima app all'accensione e al risveglio (Impostazioni → Server): ultimo ingresso bloccato su Cinema++, l'avvio di Cinema web messo da parte e restituito quando si spegne; provato con un riavvio
- ✅ Aggiornamenti di Cinema++ dalle release GitHub con SHA-256 (Impostazioni → Server)
- ✅ Aggiornamenti di Bloomee e Cinema web dalle loro release GitHub, con l'installazione da URL e lo SHA-256 di SHA256SUMS.txt

## Striscia LED (tappa 2)
- ✅ Tutto, in C++

## Gestione
- ✅ Backup e ripristino di Native Tv, con meteo e musica di Cinema++
- ✅ Diagnostica (Impostazioni → Server)
- ⬜ Assistente vocale (richiede Python 3, che la TV non ha)
- ✅ USB: musica da `lounge/music`, sfondi da `lounge/backgrounds`, `lounge/config.json` applicato una volta per contenuto (ambiente, orologio, colore del focus, sfondo, musica, meteo)
