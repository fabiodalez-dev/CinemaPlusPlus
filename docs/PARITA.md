# Parità con Cinema

Ogni funzione di Cinema (app web, 0.3.9) e dove sta in Cinema++. Fonte: lettura completa del codice di Cinema (`src/js`, `scripts`), 1° novembre 2026.

Legenda: ✅ fatto · 🟡 c'è in Native Tv, da adattare · ⬜ da fare.

## Home
- ✅ Home nativa su webOS 5
- ✅ Italiano in tutta l'app TV e nella dashboard, con inglese e spagnolo
- 🟡 Schede Per te · Film · Serie · Musica · App · Ingressi (mancano Cerca, Scopri, Live, orologio e data a sinistra, stato di Jellyfin)
- 🟡 Riquadro grande con titolo, dati, descrizione e copertina della libreria (mancano i pulsanti Apri titolo e La mia libreria)
- ✅ Sfondo dalla libreria (backdrop del titolo a fuoco)
- ⬜ Riga di scorciatoie quando Jellyfin non ha titoli
- ⬜ Tasto giallo: Cinema ↔ vista essenziale

## Jellyfin
- ⬜ Collegamento con Quick Connect (dalla TV e dalla dashboard); riuso di `/var/lib/launch-home-cinema/jellyfin.json`
- ✅ Righe: Continua a guardare (Resume + NextUp senza doppioni), Aggiunti di recente, I tuoi film, Le tue serie, La tua musica
- ⬜ Ricerca nella libreria
- ✅ Copertine dal server con cache su disco (due misure, al massimo 240), token solo nel servizio
- ✅ Aggiornamento ogni 10 minuti e a richiesta; stati offline e non autorizzato
- ✅ Apertura in LiteFin con `cinemaItemId`/`cinemaAction`, o nel primo client installato
- ⬜ Scelta del client preferito
- ⬜ Scopri e Seerr (`/Litefin/Seerr/Status`), pannello con le spiegazioni

## Live e ingressi
- 🟡 Ingressi HDMI (scheda Ingressi di Native Tv)
- ⬜ Live dentro la home (video `tv://` e `ext://hdmi:N`), comandi che spariscono dopo 10 s, Schermo intero, Riprova, Canali, Apri fonte LG
- ⬜ Zapping CH+/CH− e numeri (450 ms, 1800 ms), banner del canale con programma e A seguire, Info e Guida
- ⬜ Striscia dei canali (preferiti, tutti, spenta)

## Barra delle app
- 🟡 Riga delle app (Le tue app)
- ⬜ Barra Estesa con nomi e ingressi HDMI («Nessun segnale») o Compatta; Tutte le app; Impostazioni TV
- ⬜ App fissate, app personalizzate, importa dalla home LG, numeri del telecomando
- 🟡 Tutte le app (scheda App)

## Aspetto
- ✅ Sfondi di Cinema nelle quattro fasce orarie di Native Tv (cielo stellato, montagne, caletta, tramonto)
- ⬜ Sfondi di Cinema: gradienti, foto incluse, online, USB, video aerei Apple, presentazione, zoom lento, oscuramento
- ⬜ Orologio: 12/24 h, fuso, posizione, dimensione; data in italiano
- ⬜ Colori delle tessere, bordo, colore del focus, modalità prestazioni
- ⬜ Profili Standard, Notte, Cinema

## Musica, meteo, salvaschermo
- ⬜ Musica ambiente (brani inclusi in streaming, USB, casuale, ripetizione, dissolvenze, tasti rosso/verde/volume)
- ⬜ Meteo Open-Meteo con cinque giorni e ricerca della località
- 🟡 Salvaschermo (Native Tv ne ha diversi)
- ⬜ Salvaschermo di Cinema: sfondi in dissolvenza, orologio, spegnimento dopo N ore; blocco di quello di LG

## Strumenti TV (tappa 1)
- ✅ App tenute in memoria, ID pubblicitario, avvisi sportivi, LG Channels
- ✅ Precaricamento, servizi, accordi sui dati, tasti del telecomando, blocco aggiornamenti LG (Native Tv)
- 🟡 Spegnimento a un orario (ora di andare a letto di Native Tv)
- ⬜ Cinema++ all'accensione (ultimo ingresso, `eim/addDevice`, lastinput fissato)
- ⬜ Tasto Home a Cinema++ (LG Input Hook), tre volte Home per la home di LG, Indietro tenuto premuto
- ⬜ Aggiornamenti di Cinema++ e Bloomee dalle release con SHA-256

## Striscia LED (tappa 2)
- ✅ Tutto, in C++

## Gestione
- ⬜ Backup e ripristino delle impostazioni (TV e USB, automatici prima degli aggiornamenti)
- ⬜ Diagnostica
- ⬜ Assistente vocale (richiede Python 3, che la TV non ha)
- ⬜ USB: `lounge/config.json`, musica e sfondi
