# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Novi tab
newtab-settings-button =
    .title = Prilagodite svoju početnu stranicu novog taba
newtab-customize-panel-icon-button =
    .title = Prilagodi ovu stranicu
newtab-customize-panel-icon-button-label = Prilagodi
newtab-personalize-settings-icon-label =
    .title = Personalizujte novi tab
    .aria-label = Postavke
newtab-settings-dialog-label =
    .aria-label = Postavke
newtab-personalize-icon-label =
    .title = Personalizujte novi tab
    .aria-label = Personalizujte novi tab
newtab-personalize-dialog-label =
    .aria-label = Personalizuj
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Traži
    .aria-label = Traži
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Pretražite pomoću { $engine } ili unesite adresu
newtab-search-box-handoff-text-no-engine = Unesite termin za pretragu ili adresu
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Pretražite pomoću { $engine } ili unesite adresu
    .title = Pretražite pomoću { $engine } ili unesite adresu
    .aria-label = Pretražite pomoću { $engine } ili unesite adresu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Tražite ili upišite adresu
    .title = Tražite ili upišite adresu
    .aria-label = Tražite ili upišite adresu
newtab-search-box-text = Pretraži web
newtab-search-box-input =
    .placeholder = Pretraži web
    .aria-label = Pretraži web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Dodaj pretraživač
newtab-topsites-add-shortcut-header = Nova prečica
newtab-topsites-edit-topsites-header = Uredi najbolju stranicu
newtab-topsites-edit-shortcut-header = Uredi prečicu
newtab-topsites-add-shortcut-label = Dodaj prečicu
newtab-topsites-add-shortcut-title =
    .title = Dodaj prečicu
    .aria-label = Dodaj prečicu
newtab-topsites-title-label = Naslov
newtab-topsites-title-input =
    .placeholder = Unesi naslov
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Upišite ili zalijepite URL
newtab-topsites-url-validation = Potrebno je unijeti ispravan URL
newtab-topsites-image-url-label = Prilagođena URL slika
newtab-topsites-use-image-link = Koristite prilagođenu sliku…
newtab-topsites-image-validation = Neuspjelo učitavanje slike. Probajte drugi URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Otkaži
newtab-topsites-delete-history-button = Izbriši iz historije
newtab-topsites-save-button = Sačuvaj
newtab-topsites-preview-button = Pregled
newtab-topsites-add-button = Dodaj

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Jeste li sigurni da želite izbrisati sve primjere ove stranice iz vaše historije?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ova radnja se ne može opozvati.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponzorisano

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Otvori meni
    .aria-label = Otvori meni
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Ukloni
    .aria-label = Ukloni
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Otvori meni
    .aria-label = Otvori kontekstni meni za { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Uredi ovu stranicu
    .aria-label = Uredi ovu stranicu

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Uredi
newtab-menu-open-new-window = Otvori u novom prozoru
newtab-menu-open-new-private-window = Otvori u novom privatnom prozoru
newtab-menu-dismiss = Odbaci
newtab-menu-pin = Zakači
newtab-menu-unpin = Otkači
newtab-menu-delete-history = Izbriši iz historije
newtab-menu-save-to-pocket = Sačuvaj na { -pocket-brand-name }
newtab-menu-delete-pocket = Izbriši iz { -pocket-brand-name }a
newtab-menu-archive-pocket = Arhiviraj u { -pocket-brand-name }
newtab-menu-show-privacy-info = Naši sponzori i vaša privatnost
newtab-menu-about-fakespot = O { -fakespot-brand-name }u
# Report is a verb (i.e. report issue with the content).
newtab-menu-report = Prijavi
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokiraj
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Prestani pratiti temu

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Upravljajte sponzoriranim sadržajem
newtab-menu-our-sponsors-and-your-privacy = Naši sponzori i vaša privatnost
newtab-menu-report-this-ad = Prijavi ovaj oglas

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gotovo
newtab-privacy-modal-button-manage = Upravljajte postavkama sponzorisanog sadržaja
newtab-privacy-modal-header = Vaša privatnost je važna.
newtab-privacy-modal-paragraph-2 =
    Osim što donosimo zadivljujuće priče, prikazujemo vam i relevantne,
    visoko provjereni sadržaj odabranih sponzora. Budite sigurni, <strong>vaši podaci
    pretraživanja nikada ne napuštaju vašu ličnu kopiju { -brand-product-name }a</strong> — mi to ne vidimo, a također
    ni naši sponzori.
newtab-privacy-modal-link = Saznajte kako funkcioniše privatnost na novom tabu

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Ukloni zabilješku
# Bookmark is a verb here.
newtab-menu-bookmark = Zabilježi

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiraj link za preuzimanje
newtab-menu-go-to-download-page = Idi na stranicu za preuzimanje
newtab-menu-remove-download = Ukloni iz historije

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Prikaži u Finderu
       *[other] Otvori direktorij u kojem se nalazi
    }
newtab-menu-open-file = Otvori datoteku

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Posjećeno
newtab-label-bookmarked = Zabilježeno
newtab-label-removed-bookmark = Zabilješka uklonjena
newtab-label-recommended = Popularno
newtab-label-saved = Sačuvano u { -pocket-brand-name }
newtab-label-download = Preuzeto
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponzorisano
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponzorisano od { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min
# This string is used under fixed size ads to indicate sponsored content
newtab-label-sponsored-fixed = Sponzorisano

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Ukloni sekciju
newtab-section-menu-collapse-section = Skupi sekciju
newtab-section-menu-expand-section = Proširi sekciju
newtab-section-menu-manage-section = Upravljaj sekcijom
newtab-section-menu-manage-webext = Upravljanje ekstenzijom
newtab-section-menu-add-topsite = Dodajte omiljenu stranicu
newtab-section-menu-add-search-engine = Dodaj pretraživač
newtab-section-menu-move-up = Pomjeri gore
newtab-section-menu-move-down = Pomjeri dole
newtab-section-menu-privacy-notice = Polica privatnosti

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Skupi sekciju
newtab-section-expand-section-label =
    .aria-label = Proširi sekciju

## Section Headers.

newtab-section-header-topsites = Najposjećenije stranice
newtab-section-header-recent-activity = Nedavne aktivnosti
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Preporučeno od { $provider }
newtab-section-header-stories = Priče koje podstiču na razmišljanje
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Današnji izbori za vas

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Započnite pretraživati i pokazat ćemo vam neke od izvrsnih članaka, videa i drugih web stranica prema vašim nedavno posjećenim stranicama ili zabilješkama.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Provjerite kasnije za više najpopularnijih priča od { $provider }. Ne možete čekati? Odaberite popularne teme kako biste pronašli više kvalitetnih priča s cijelog weba.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = U toku ste. Provjerite kasnije za više priča. Ne možete čekati? Odaberite popularnu temu da pronađete još sjajnih priča sa cijelog weba.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = U toku ste!
newtab-discovery-empty-section-topstories-content = Provjerite kasnije za više priča.
newtab-discovery-empty-section-topstories-try-again-button = Pokušaj ponovo
newtab-discovery-empty-section-topstories-loading = Učitavanje…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ups! Skoro smo učitali ovu sekciju, ali ne sasvim.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popularne teme:
newtab-pocket-new-topics-title = Želite još više priča? Pogledajte ove popularne teme od { -pocket-brand-name }a
newtab-pocket-more-recommendations = Više preporuka
newtab-pocket-learn-more = Saznajte više
newtab-pocket-cta-button = Preuzmite { -pocket-brand-name }
newtab-pocket-cta-text = Sačuvajte priče koje volite u { -pocket-brand-name } i podstaknite svoj um fascinantnim čitanjem.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je dio { -brand-product-name } porodice
newtab-pocket-save = Sačuvaj
newtab-pocket-saved = Sačuvano

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Više ovakvih
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nije za mene
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Hvala. Vaše povratne informacije pomoći će nam da poboljšamo predlaganje tema.
newtab-toast-dismiss-button =
    .title = Odbaci
    .aria-label = Odbaci

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Otkrijte najbolje od weba
newtab-pocket-onboarding-cta = { -pocket-brand-name } istražuje raznovrsne publikacije kako bi donio najinformativniji, inspirativniji i najpouzdaniji sadržaj pravo u vaš { -brand-product-name } pretraživač.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ups, došlo je do greške pri učitavanju ovog sadržaja.
newtab-error-fallback-refresh-link = Osvježite stranicu da biste pokušali ponovo.

## Customization Menu

newtab-custom-shortcuts-title = Prečice
newtab-custom-shortcuts-subtitle = Web stranice koje sačuvate ili posjetite
newtab-custom-shortcuts-toggle =
    .label = Prečice
    .description = Stranice koje ste sačuvali ili posjetili
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } red
        [few] { $num } reda
       *[other] { $num } redova
    }
newtab-custom-sponsored-sites = Sponzorisane prečice
newtab-custom-pocket-title = Preporučuje { -pocket-brand-name }
newtab-custom-pocket-subtitle = Izuzetan sadržaj koji je kurirao { -pocket-brand-name }, dio porodice { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Preporučene priče
    .description = Izuzetan sadržaj koji je kurirala porodica { -brand-product-name }
newtab-custom-pocket-sponsored = Sponzorisane priče
newtab-custom-pocket-show-recent-saves = Prikaži nedavno sačuvane
newtab-custom-recent-title = Nedavne aktivnosti
newtab-custom-recent-subtitle = Izbor najnovijih stranica i sadržaja
newtab-custom-weather-toggle =
    .label = Vrijeme
    .description = Ukratko o današnjoj prognozi
newtab-custom-trending-search-toggle =
    .label = Popularne pretrage
    .description = Popularne i često tražene teme
newtab-custom-close-button = Zatvori
newtab-custom-settings = Upravljajte više postavki

## New Tab Wallpapers

newtab-wallpaper-title = Pozadine
newtab-wallpaper-reset = Vrati na izvorno
newtab-wallpaper-upload-image = Učitaj sliku
newtab-wallpaper-custom-color = Izaberite boju
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Slika je premašila ograničenje veličine datoteke od { $file_size }MB. Molimo pokušajte s učitavanjem manje datoteke.
newtab-wallpaper-error-file-type = Nismo mogli učitati vašu datoteku. Pokušajte ponovo s drugom vrstom datoteke.
newtab-wallpaper-light-red-panda = Crvena panda
newtab-wallpaper-light-mountain = Bijela planina
newtab-wallpaper-light-sky = Nebo sa ljubičastim i ružičastim oblacima
newtab-wallpaper-light-color = Plavi, ružičasti i žuti oblici
newtab-wallpaper-light-landscape = Plava magla planinski pejzaž
newtab-wallpaper-light-beach = Plaža sa palmama
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Crveni i plavi oblici
newtab-wallpaper-dark-panda = Crvena panda skrivena u šumi
newtab-wallpaper-dark-sky = Gradski pejzaž sa noćnim nebom
newtab-wallpaper-dark-mountain = Pejzažna planina
newtab-wallpaper-dark-city = Ljubičasti gradski pejzaž
newtab-wallpaper-dark-fox-anniversary = Lisica na pločniku u blizini šume
newtab-wallpaper-light-fox-anniversary = Lisica u travnatom polju sa maglovitim planinskim pejzažom

## Solid Colors

newtab-wallpaper-category-title-colors = Čvrste boje
newtab-wallpaper-blue = Plava
newtab-wallpaper-light-blue = Svijetlo plava
newtab-wallpaper-light-purple = Svijetlo ljubičasta
newtab-wallpaper-light-green = Svijetlo zelena
newtab-wallpaper-green = Zelena
newtab-wallpaper-beige = Bež
newtab-wallpaper-yellow = Žuta
newtab-wallpaper-orange = Narandžasta
newtab-wallpaper-pink = Roza
newtab-wallpaper-light-pink = Svijetlo roza
newtab-wallpaper-red = Crvena
newtab-wallpaper-dark-blue = Tamno plava
newtab-wallpaper-dark-purple = Tamna ljubičasta
newtab-wallpaper-dark-green = Tamno zelena
newtab-wallpaper-brown = Smeđa

## Abstract

newtab-wallpaper-category-title-abstract = Apstraktno
newtab-wallpaper-abstract-green = Zeleni oblici
newtab-wallpaper-abstract-blue = Plavi oblici
newtab-wallpaper-abstract-purple = Ljubičasti oblici
newtab-wallpaper-abstract-orange = Narandžasti oblici
newtab-wallpaper-gradient-orange = Gradijent narandžaste i roze
newtab-wallpaper-abstract-blue-purple = Plavi i ljubičasti oblici
newtab-wallpaper-abstract-white-curves = Bijela sa zasjenjenim krivuljama
newtab-wallpaper-abstract-purple-green = Gradijent ljubičastog i zelenog svjetla
newtab-wallpaper-abstract-blue-purple-waves = Plavi i ljubičasti valoviti oblici
newtab-wallpaper-abstract-black-waves = Crni valoviti oblici

## Celestial

newtab-wallpaper-category-title-photographs = Fotografije
newtab-wallpaper-beach-at-sunrise = Plaža u izlasku sunca
newtab-wallpaper-beach-at-sunset = Plaža na zalasku sunca
newtab-wallpaper-storm-sky = Olujno nebo
newtab-wallpaper-sky-with-pink-clouds = Nebo sa ružičastim oblacima
newtab-wallpaper-red-panda-yawns-in-a-tree = Crvena panda zijeva na drvetu
newtab-wallpaper-white-mountains = Bijele planine
newtab-wallpaper-hot-air-balloons = Različite boje balona na vrući zrak tokom dana
newtab-wallpaper-starry-canyon = Plava zvjezdana noć
newtab-wallpaper-suspension-bridge = Fotografija sivog visećeg mosta tokom dana
newtab-wallpaper-sand-dunes = Bijele pješčane dine
newtab-wallpaper-palm-trees = Silueta kokosovih palmi tokom zlatnog sata
newtab-wallpaper-blue-flowers = Fotografija krupnog plana cvijeća s plavim laticama u cvatu
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotografija od <a data-l10n-name="name-link">{ $author_string }</a> na <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Probajte nove boje
newtab-wallpaper-feature-highlight-content = Dajte svojom novom tabu svjež izgled pomoću pozadina.
newtab-wallpaper-feature-highlight-button = Razumijem
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Odbaci
    .aria-label = Zatvori iskočni prozor
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Nebeski
newtab-wallpaper-celestial-lunar-eclipse = Pomračenje Mjeseca
newtab-wallpaper-celestial-earth-night = Noćna fotografija iz niske Zemljine orbite
newtab-wallpaper-celestial-starry-sky = Zvjezdano nebo
newtab-wallpaper-celestial-eclipse-time-lapse = Ubrzani snimak pomračenja Mjeseca
newtab-wallpaper-celestial-black-hole = Ilustracija galaksije crne rupe
newtab-wallpaper-celestial-river = Satelitski snimak rijeke

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Pogledajte prognozu na { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponzorisano
newtab-weather-menu-change-location = Promijeni lokaciju
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Traži lokaciju
    .aria-label = Traži lokaciju
newtab-weather-menu-weather-display = Prikaz vremena
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Jednostavno
newtab-weather-menu-change-weather-display-simple = Prebacite se na jednostavan prikaz
newtab-weather-menu-weather-display-option-detailed = Detaljno
newtab-weather-menu-change-weather-display-detailed = Prebacite se na detaljan prikaz
newtab-weather-menu-temperature-units = Jedinice temperature
newtab-weather-menu-temperature-option-fahrenheit = Farenhajt
newtab-weather-menu-temperature-option-celsius = Celzijus
newtab-weather-menu-change-temperature-units-fahrenheit = Prebacite na Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Prebacite na Celzijus
newtab-weather-menu-hide-weather = Sakrij vrijeme na novom tabu
newtab-weather-menu-learn-more = Saznajte više
# This message is shown if user is working offline
newtab-weather-error-not-available = Vremenski podaci trenutno nisu dostupni.

## Topic Labels

newtab-topic-label-business = Posao
newtab-topic-label-career = Karijera
newtab-topic-label-education = Obrazovanje
newtab-topic-label-arts = Zabava
newtab-topic-label-food = Hrana
newtab-topic-label-health = Zdravlje
newtab-topic-label-hobbies = Igre
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Novac
newtab-topic-label-society-parenting = Roditeljstvo
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Nauka
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Životni savjeti
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Tehnologija
newtab-topic-label-travel = Putovanja
newtab-topic-label-home = Kuća i bašta

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Odaberite teme za fino podešavanje vašeg feeda
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Odaberite dvije ili više tema. Naši stručni kustosi daju prioritet pričama prilagođenim vašim interesovanjima. Ažurirajte bilo kada.
newtab-topic-selection-save-button = Sačuvaj
newtab-topic-selection-cancel-button = Otkaži
newtab-topic-selection-button-maybe-later = Možda kasnije
newtab-topic-selection-privacy-link = Saznajte kako štitimo i upravljamo podacima
newtab-topic-selection-button-update-interests = Ažurirajte svoja interesovanja
newtab-topic-selection-button-pick-interests = Odaberite svoja interesovanja

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Prati
newtab-section-following-button = Pratite
newtab-section-unfollow-button = Prestani pratiti
# A modal may appear next to the Follow button, directing users to try out the feature
newtab-section-follow-highlight-title = Precizno podesite svoj feed
newtab-section-follow-highlight-subtitle = Pratite svoja interesovanja da biste vidjeli više onoga što vam se sviđa.

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokiraj
newtab-section-blocked-button = Blokirano
newtab-section-unblock-button = Odblokiraj

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ne sada
newtab-section-confirm-block-topic-p1 = Jeste li sigurni da želite blokirati ovu temu?
newtab-section-confirm-block-topic-p2 = Blokirane teme se više neće pojavljivati u vašem feedu.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokiraj { $topic }

## Strings for custom wallpaper highlight

newtab-section-mangage-topics-title = Teme
newtab-section-manage-topics-button-v2 =
    .label = Upravljaj temama
newtab-section-mangage-topics-followed-topics = Praćeno
newtab-section-mangage-topics-followed-topics-empty-state = Još niste pratili nijednu temu.
newtab-section-mangage-topics-blocked-topics = Blokirano
newtab-section-mangage-topics-blocked-topics-empty-state = Još niste blokirali nijednu temu.
newtab-custom-wallpaper-title = Prilagođene pozadine su ovdje
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Otpremite vlastitu pozadinu ili odaberite prilagođenu boju kako biste { -brand-product-name } prilagodili sebi.
newtab-custom-wallpaper-cta = Probaj

## Strings for download mobile highlight

newtab-download-mobile-highlight-title = Preuzmite { -brand-product-name } za mobilne uređaje
# "Scan the code" refers to scanning the QR code that appears above the body text that leads to Firefox for mobile download.
newtab-download-mobile-highlight-body-variant-a = Skenirajte kod za sigurno pregledavanje u pokretu.
newtab-download-mobile-highlight-body-variant-b = Nastavite tamo gdje ste stali prilikom sinhronizacije tabova, lozinki i još mnogo toga.
newtab-download-mobile-highlight-body-variant-c = Jeste li znali da { -brand-product-name } možete ponijeti sa sobom? Isti preglednik. U vašem džepu.
newtab-download-mobile-highlight-image =
    .aria-label = QR kod za preuzimanje { -brand-product-name } za mobilne uređaje

## Strings for shortcuts highlight

newtab-shortcuts-highlight-title = Vaši favoriti na dohvat ruke
newtab-shortcuts-highlight-subtitle = Dodajte prečicu da biste svoje omiljene stranice imali pri ruci na jedan klik.

## Strings for reporting ads and content

newtab-report-content-why-reporting-this =
    .label = Zašto ovo prijavljujete?
newtab-report-ads-reason-not-interested =
    .label = Nisam zainteresovan/a
newtab-report-ads-reason-inappropriate =
    .label = To je neprikladno
newtab-report-ads-reason-seen-it-too-many-times =
    .label = Vidio/la sam to previše puta
newtab-report-content-wrong-category =
    .label = Pogrešna kategorija
newtab-report-content-outdated =
    .label = Zastarjelo
newtab-report-content-inappropriate-offensive =
    .label = Neprimjereno ili uvredljivo
newtab-report-content-spam-misleading =
    .label = Neželjena pošta ili obmanjujući sadržaj
newtab-report-cancel = Otkaži
newtab-report-submit = Pošalji
newtab-toast-thanks-for-reporting =
    .message = Hvala vam što ste ovo prijavili.

## Strings for trending searches

newtab-trending-searches-show-trending =
    .title = Prikaži popularne pretrage
newtab-trending-searches-hide-trending =
    .title = Sakrij popularne pretrage
newtab-trending-searches-learn-more = Saznajte više
newtab-trending-searches-dismiss = Sakrij popularne pretrage
# "Trending searches refers to popular searches from search engines
newtab-trending-searches-title = Popularne pretrage
