# Piano di Cinema++

Come passare da Cinema (app web) a Cinema++ (servizio C++ e interfaccia nativa) senza mai lasciare la TV senza home. Le due app convivono sulla TV fino all'ultima tappa.

TV di riferimento: LG 43UN70006LA, webOS 5.6.2, 1 GB di memoria, GPU Mali, root con Homebrew Channel.

## Perché

Su questa TV il processo delle app web di LG (`WebAppMgr`) arriva a 270-420 MB con Cinema, Bloomee e LiteFin aperti, e oltre circa 420 MB LG riavvia la TV. Un'interfaccia nativa con SDL2 e OpenGL ES sta in poche decine di MB e si apre all'istante. Anche i servizi di Cinema in Python 2 passano a C++: niente più un processo `luna-send` per ogni chiamata, che su questa TV fa perdere file al servizio Bluetooth di LG.

## Tappe

### 0. Native Tv sulla TV di riferimento

- SDK nativo di webosbrew per macOS arm64, build del servizio e dell'app TV.
- Lettura di `deploy.sh` e del primo avvio prima di installare: cosa cambia sulla TV, porte usate (8080), conflitti con gli strumenti di Cinema che toccano le stesse impostazioni.
- Installazione sulla 43UN70006LA, webOS 5.6: il servizio parte, la dashboard risponde, l'app TV si apre con SDL2 e OpenGL ES.
- Misure: memoria e CPU del servizio e dell'app, tempo di apertura, a confronto con Cinema.

Risultato del 1° novembre 2026: Native Tv compila con l'SDK `webos-d7ed7ee.6` su macOS arm64 e gira sulla 43UN70006LA con webOS 5.6.2. Installato con `deploy.sh --no-persist` (senza aggancio all'avvio) e con `allowNetworkDebugger: true` nel config della TV, così la porta 9998 di DevTools resta aperta alla rete. Il servizio occupa circa 8 MB di memoria residente, l'app nativa SDL2 e OpenGL ES circa 17,5 MB e si apre sulla configurazione iniziale; i processi delle app web di LG, con Cinema, Bloomee e YouTube aperti, ne occupano 35-56 MB ciascuno più quanto è finito nello swap (memchute li conta a 270-420 MB in tutto).

### 1. Strumenti TV di Cinema nel servizio C++

Molti esistono già in Native Tv: privacy e accordi sui dati, debloat dei servizi, mappatura dei tasti, blocco degli aggiornamenti LG. Da portare o allineare a Cinema:

- app tenute in memoria (Bloomee che non si ferma quando si apre un'altra app);
- precaricamento LG ridotto alle app usate;
- apertura di Cinema++ all'accensione da corrente;
- aggiornamenti di Cinema++ e Bloomee dalle release GitHub con verifica SHA-256;
- controllo del canale Homebrew e modalità di emergenza.

Stato al 1° novembre 2026:

- **App tenute in memoria**: nel servizio C++ (`service/memory_keep`, `daemon/memory_keeper`) e nella dashboard, scheda App → «Apps kept open». Usa lo stesso segno di Cinema per il file di memchute, quindi le due convivono; provato sulla TV aggiungendo e togliendo YouTube.
- **ID pubblicitario, avvisi sportivi, LG Channels**: aggiunti alle impostazioni LG di Native Tv (gruppo promozioni).
- **Spegnimento a un orario**: non serve riscriverlo. Su webOS 5 il timer di spegnimento di LG non c'è, ma l'ora di andare a letto di Native Tv (Tempo davanti alla TV) spegne la TV con un avviso 5 minuti prima: le 02:00 di Cinema diventano una fascia dalle 02:00 alle 02:01 con azione «spegni».
- **Già in Native Tv**: precaricamento, servizi in background, accordi sui dati e Live Plus, tasti del telecomando, blocco degli aggiornamenti di LG, pubblicità.
- **Restano a Cinema fino al passaggio**: riavvio di memchute all'accensione (l'hook `45-cinema-tvtools-memory`), aggiornamenti di Cinema e Bloomee, controllo del canale Homebrew.

### 2. Striscia LED in C++

Il controller di Cinema (`ambilight.py`) riscritto nel servizio:

- cattura dello schermo con `com.webos.service.capture` in un thread suo, dissolvenze a 20 fotogrammi al secondo;
- colori della 0.3.9: tinta dominante mescolata con la scena, curva fra i canali, bianco corretto verso il caldo;
- scritture GATT su ELK-BLEDOM attraverso un solo client del bus: il `LunaClient` di Native Tv apre un `luna-send` per ogni chiamata, che sul servizio Bluetooth di LG fa perdere un file a ogni comando (vedi Cinema 0.3.9);
- luce della stanza all'annuncio di spegnimento di LG, registro persistente, riavvio del Bluetooth bloccato al massimo ogni due minuti;
- impostazioni nella dashboard e nell'app TV: Cerca, Prova, Salva, luminosità separate.

### 3. Home nativa di Cinema

- barra delle app Estesa o Compatta, ingressi HDMI con «Nessun segnale»;
- Live TV dietro i controlli, a schermo intero, cambio canale con CH+ e CH−;
- sfondi, meteo, profili, salvaschermo.

### 4. Jellyfin e Scopri

- collegamento con Quick Connect, righe Riprendi, Prossimi episodi, Film, Serie, Musica;
- copertine in cache alla risoluzione dello schermo;
- apertura del client preferito (LiteFin con i collegamenti diretti, Moonfin, Jellyfin e gli altri);
- Scopri e richieste con Seerr.

### 5. Impostazioni, backup e diagnostica

Impostazioni a schermo intero come in Cinema, backup e ripristino, diagnostica, assistente vocale opzionale.

### 6. Il passaggio

Cinema++ diventa la home al posto di Cinema: tasto Home, apertura all'accensione, impostazioni importate da `tvtools.json` e dalle preferenze di Cinema. Poi Cinema web si può disinstallare.

## Regole sulla TV

- Mai riavviare mentre esiste `/var/luna/preferences/webosbrew_failsafe`.
- Mai riavviare `preload-manager` o `memchute` mentre la TV è in uso: chiudono tutte le app.
- Mai registrarsi alle fasi `registerPrepareSuspend` / `registerPreparePowerOff` di tvpowerd: ogni standby diventa un riavvio completo.
- Le lezioni sulla TV sono nel README di Cinema, sezione «Cosa ho imparato su questa TV».
