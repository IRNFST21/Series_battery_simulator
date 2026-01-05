Series Battery Simulator
Overzicht

De Series Battery Simulator is een embedded systeemproject dat een programmeermodel voor een seriële batterij implementeert op basis van een microcontrollerplatform. Het systeem meet elektrische grootheden, bestuurt hardware-uitgangen en presenteert status- en meetinformatie via een grafische interface.

De architectuur is modulair opgezet en volgt een duidelijke scheiding tussen hardware-interactie, logica en presentatie.

Functionele doelstelling

De Series Battery Simulator heeft als doel:

Het simuleren van batterijgedrag in een serieconfiguratie

Het meten en verwerken van elektrische parameters

Het visualiseren van systeemstatus en metingen

Het logisch aansturen van systeemtoestanden via een statemachine

Het loggen van systeeminformatie voor debugging en analyse

Architectuur en ontwerpkeuzes

Het project is opgebouwd rond een modulaire software-architectuur, waarbij elke functionele verantwoordelijkheid in een eigen module is ondergebracht:

Hardware-abstractie per subsysteem

Centrale statemachine voor systeemgedrag

Gescheiden display- en UI-logica

Expliciete logginglaag

Heldere main.cpp als applicatie-ingangspunt

Deze aanpak vergemakkelijkt onderhoud, testen en toekomstige uitbreiding.

Projectstructuur
Series_battery_simulator/
│
├── src/
│   ├── main.cpp                 # Applicatie entry point
│   │
│   ├── system/
│   │   └── system.cpp           # Systeeminitialisatie en kernlogica
│   │
│   ├── statemachine/
│   │   └── statemachine.cpp     # Centrale statemachine
│   │
│   ├── measure/
│   │   └── measure.cpp          # Metingen en signaalverwerking
│   │
│   ├── display/
│   │   ├── display.cpp          # Display hardware-aansturing
│   │   └── ui_screens.cpp       # UI-schermen en layout
│   │
│   ├── ioexpander/
│   │   └── ioExpander.cpp       # I/O-expander abstractie
│   │
│   └── log/
│       └── log.cpp              # Logging en debug-output
│
├── test/
│   └── README                   # Testdocumentatie
│
└── .pio/
    └── libdeps/                 # PlatformIO dependencies (o.a. LVGL)

Belangrijkste modules
main.cpp

Startpunt van de applicatie

Roept systeeminitialisatie aan

Beheert de hoofdlus

system

Centrale configuratie en setup

Coördineert interactie tussen modules

statemachine

Definieert systeemtoestanden

Regelt transities op basis van events en metingen

Vormt de kern van het functionele gedrag

measure

Verwerkt meetdata (bijv. spanning, stroom)

Voorziet andere modules van gevalideerde waarden

display en ui_screens

Gebruikt LVGL voor de grafische interface

Scheidt hardware-aansturing van UI-logica

Maakt het eenvoudig om schermen uit te breiden

ioexpander

Abstractielaag voor externe I/O-expanders

Houdt hardware-afhankelijkheid buiten de applicatielogica

log

Uniforme logging-interface

Geschikt voor UART/debug-uitvoer

Gebruikte technologieën

C++ (Embedded)

PlatformIO

LVGL (Light and Versatile Graphics Library)

Microcontrollerplatform (ESP32-klasse, afhankelijk van target)

Ontwikkelprincipes

Modulaire opbouw

Single Responsibility per module

Hardware-abstractie

Duidelijke scheiding tussen logica en presentatie

Schaalbaar voor toekomstige uitbreidingen

Testen

De map test/ is gereserveerd voor testdocumentatie en (toekomstige) testimplementaties.
De architectuur is zodanig opgezet dat unit- en integratietests eenvoudig toegevoegd kunnen worden.

Toekomstige uitbreidingen

Uitgebreidere batterijmodellen (interne weerstand, temperatuurinvloed)

Communicatie-interfaces (USB / UART / CAN)

Logging naar extern geheugen

Uitgebreide fout- en veiligheidsafhandeling

Automatische testframework-integratie

Context

Dit project is ontwikkeld binnen een technisch embedded systems-traject, met nadruk op architectuur, betrouwbaarheid en professionele codeopbouw.

Als je wilt, kan ik dit nog:

herschrijven naar V-model / schoolopdracht-stijl

uitbreiden met een hardware-overzicht

aanpassen voor opdrachtgever / beoordelaar

verkorten tot een strakke repo-README
