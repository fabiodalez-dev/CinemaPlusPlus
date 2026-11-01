# Cinema++

Cinema riscritto in C++: il centro TV per le LG webOS con root, nativo invece che web.

Cinema è nato come app web, e su una TV con un gigabyte di memoria le app web costano care: il processo che le ospita arriva a 300-400 MB, e oltre circa 420 MB LG riavvia la TV da sola. Cinema++ è un servizio C++ sulla TV con un'interfaccia nativa disegnata con SDL2 e OpenGL ES, più leggera e immediata, che riunisce le funzioni di Cinema e quelle di [Native Tv](https://github.com/ihtisham1211/native-tv).

## Da dove parte

Il codice di partenza è [Native Tv](https://github.com/ihtisham1211/native-tv) di ihtisham arif, a sua volta nato da [Glasshouse](https://github.com/rorygallagher2024/glasshouse) di Rory Gallagher, alla revisione `8987f2c`, con licenza MIT. Il README originale è in [README.native-tv.md](README.native-tv.md). Native Tv porta la dashboard da browser, i controlli per la privacy (ACR, pubblicità, telemetria), il debloat, la mappatura dei tasti, lo spegnimento quando la TV non si usa, Home Assistant via MQTT e un'app nativa in stile Google TV.

Da [Cinema](https://github.com/fabiodalez-dev/Cinema) arriveranno, riscritti in C++: la home con Jellyfin e la Live, il collegamento a LiteFin, gli strumenti TV, la striscia LED che segue lo schermo e la luce della stanza a TV spenta.

## Stato

Appena importato: non ancora provato sulla TV di riferimento, una LG 43UN70006LA con webOS 5.6. Native Tv è verificato su webOS 10.3.

## Licenza

MIT, come Native Tv. Le librerie incluse hanno le loro licenze, elencate in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
