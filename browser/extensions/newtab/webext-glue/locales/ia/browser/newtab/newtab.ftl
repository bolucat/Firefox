# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nove scheda
newtab-settings-button =
    .title = Personalisar tu pagina de nove scheda
newtab-customize-panel-icon-button =
    .title = Personalisar iste pagina
newtab-customize-panel-icon-button-label = Personalisar
newtab-personalize-settings-icon-label =
    .title = Personalisar le scheda nove
    .aria-label = Parametros
newtab-settings-dialog-label =
    .aria-label = Parametros
newtab-personalize-icon-label =
    .title = Personalisar nove scheda
    .aria-label = Personalisar nove scheda
newtab-personalize-dialog-label =
    .aria-label = Personalisar
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Cercar
    .aria-label = Cercar
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Cercar con { $engine } o inserer un adresse
newtab-search-box-handoff-text-no-engine = Cercar o inserer un adresse
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Cercar con { $engine } o inserer adresse
    .title = Cercar con { $engine } o inserer adresse
    .aria-label = Cercar con { $engine } o inserer adresse
newtab-search-box-handoff-input-no-engine =
    .placeholder = Cercar o inserer un adresse
    .title = Cercar o inserer un adresse
    .aria-label = Cercar o inserer un adresse
newtab-search-box-text = Cercar in le Web
newtab-search-box-input =
    .placeholder = Cercar in le Web
    .aria-label = Cercar in le Web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Adder un motor de recerca
newtab-topsites-add-shortcut-header = Nove accesso directe
newtab-topsites-edit-topsites-header = Modificar le sito preferite
newtab-topsites-edit-shortcut-header = Modificar accesso directe
newtab-topsites-add-shortcut-label = Adder via-breve
newtab-topsites-add-shortcut-title =
    .title = Adder via-breve
    .aria-label = Adder via-breve
newtab-topsites-title-label = Titulo
newtab-topsites-title-input =
    .placeholder = Scriber un titulo
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Scriber o collar un URL
newtab-topsites-url-validation = Es necessari un URL valide
newtab-topsites-image-url-label = URL de imagine personal
newtab-topsites-use-image-link = Usar un imagine personalisate…
newtab-topsites-image-validation = Error durante le cargamento del imagine. Prova un altere URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancellar
newtab-topsites-delete-history-button = Deler del chronologia
newtab-topsites-save-button = Salvar
newtab-topsites-preview-button = Vista preliminar
newtab-topsites-add-button = Adder

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Desira tu vermente deler cata instantia de iste pagina de tu chronologia?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Iste action es irreversibile.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsorisate

## Label used by screen readers for pinned top sites

# Variables:
#   $title (string) - The label or hostname of the site.
topsite-label-pinned =
    .aria-label = { $title } (appunctate)
    .title = { $title }

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Aperir le menu
    .aria-label = Aperir le menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Remover
    .aria-label = Remover
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Aperir le menu
    .aria-label = Aperir le menu contextual pro { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Modificar iste sito
    .aria-label = Modificar iste sito

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Modificar
newtab-menu-open-new-window = Aperir in un nove fenestra
newtab-menu-open-new-private-window = Aperir in un nove fenestra private
newtab-menu-dismiss = Dimitter
newtab-menu-pin = Clavar
newtab-menu-unpin = Disclavar
newtab-menu-delete-history = Deler del chronologia
newtab-menu-save-to-pocket = Salvar in { -pocket-brand-name }
newtab-menu-delete-pocket = Deler de { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar in { -pocket-brand-name }
newtab-menu-show-privacy-info = Nostre sponsores e tu vita private
newtab-menu-about-fakespot = A proposito de { -fakespot-brand-name }
# Report is a verb (i.e. report issue with the content).
newtab-menu-report = Reportar
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blocar
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Non plus sequer le argumento

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gerer contento sponsorisate
newtab-menu-our-sponsors-and-your-privacy = Nostre sponsores e tu confidentialitate
newtab-menu-report-this-ad = Reportar iste annuncio

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Facite
newtab-privacy-modal-button-manage = Gerer parametros de contento sponsorisate
newtab-privacy-modal-header = Tu vita private es importante.
newtab-privacy-modal-paragraph-2 = In addition a servir te historias captivante, nos te monstra anque contento pertinente e ben curate ab sponsores seligite. Sia assecurate que <strong>tu datos de navigation non essera jammais divulgate ab tu copia personal de { -brand-product-name }</strong>: nos non los vide, ni nostre sponsores.
newtab-privacy-modal-link = Saper plus sur le respecto del vita private in le pagina de nove scheda

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Remover le marcapaginas
# Bookmark is a verb here.
newtab-menu-bookmark = Adder marcapagina

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar le ligamine de discargamento
newtab-menu-go-to-download-page = Ir al pagina de discargamento
newtab-menu-remove-download = Remover del chronologia

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Monstrar in Finder
       *[other] Aperir le dossier que lo contine
    }
newtab-menu-open-file = Aperir le file

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitate
newtab-label-bookmarked = Marcapagina addite
newtab-label-removed-bookmark = Marcapagina removite
newtab-label-recommended = Tendentias
newtab-label-saved = Salvate in { -pocket-brand-name }
newtab-label-download = Discargate
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsorisate
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsorisate per { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min
# This string is used under fixed size ads to indicate sponsored content
newtab-label-sponsored-fixed = Sponsorisate

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Remover le section
newtab-section-menu-collapse-section = Collaber le section
newtab-section-menu-expand-section = Expander le section
newtab-section-menu-manage-section = Gerer le section
newtab-section-menu-manage-webext = Gerer extension
newtab-section-menu-add-topsite = Adder sito preferite
newtab-section-menu-add-search-engine = Adder un motor de recerca
newtab-section-menu-move-up = Mover in alto
newtab-section-menu-move-down = Mover in basso
newtab-section-menu-privacy-notice = Aviso de confidentialitate

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Collaber le section
newtab-section-expand-section-label =
    .aria-label = Expander le section

## Section Headers.

newtab-section-header-topsites = Sitos preferite
newtab-section-header-recent-activity = Recente activitate
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recommendate per { $provider }
newtab-section-header-stories = Historias que face pensar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Selectiones hodierne pro te

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Comencia a navigar e nos te monstrara hic alcunes del excellente articulos, videos e altere paginas que tu ha recentemente visitate o addite al marcapaginas.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Tu ja es toto al currente. Reveni plus tarde pro plus historias popular de { $provider }. Non vole attender? Selige un subjecto popular pro discoperir altere articulos interessante sur le web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Tu ja es actualisate con toto. Re-controla plus tarde pro altere historias. Non vole attender? Selectiona un thema popular pro trovar le plus grande historias del web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Il ha nihil plus!
newtab-discovery-empty-section-topstories-content = Reveni plus tarde pro leger altere articulos.
newtab-discovery-empty-section-topstories-try-again-button = Retentar
newtab-discovery-empty-section-topstories-loading = Cargamento…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ups! Nos non ha potite cargar tote le articulos de iste section.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Subjectos popular:
newtab-pocket-new-topics-title = Vole ancora plus historias? Vide iste topicos popular de { -pocket-brand-name }
newtab-pocket-more-recommendations = Altere recommendationes
newtab-pocket-learn-more = Saper plus
newtab-pocket-cta-button = Obtener { -pocket-brand-name }
newtab-pocket-cta-text = Salva le articulos que tu ama in { -pocket-brand-name }, e alimenta tu mente con lecturas fascinante.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } es parte del familia de { -brand-product-name }
newtab-pocket-save = Salvar
newtab-pocket-saved = Salvate

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Plus como isto
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Non me interessa
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Gratias. Cognoscer tu opinion nos adjuta a meliorar tu canal.
newtab-toast-dismiss-button =
    .title = Dimitter
    .aria-label = Dimitter

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Discoperi le melio del Web
newtab-pocket-onboarding-cta = { -pocket-brand-name } explora un grande varietate de publicationes pro apportar le contento plus informative, fonte de inspiration, e digne de fide, justo a tu navigator { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Un error ha occurrite durante le cargamento de iste contento.
newtab-error-fallback-refresh-link = Refresca le pagina pro tentar de novo.

## Customization Menu

newtab-custom-shortcuts-title = Accessos directe
newtab-custom-shortcuts-subtitle = Sitos que tu salva o visita
newtab-custom-shortcuts-toggle =
    .label = Accessos directe
    .description = Sitos que tu salva o visita
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } linea
       *[other] { $num } lineas
    }
newtab-custom-sponsored-sites = Accessos directe sponsorisate
newtab-custom-pocket-title = Recommendate per { -pocket-brand-name }
newtab-custom-pocket-subtitle = Contento exceptional a cura de { -pocket-brand-name }, parte del familia { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Historias recommendate
    .description = Exceptional contento curate per le familia de { -brand-product-name }
newtab-custom-pocket-sponsored = Articulos sponsorisate
newtab-custom-pocket-show-recent-saves = Monstrar salvamentos recente
newtab-custom-recent-title = Activitate recente
newtab-custom-recent-subtitle = Un selection de sitos e contento recente
newtab-custom-weather-toggle =
    .label = Meteo
    .description = Prevision hodierne a un colpo de oculos
newtab-custom-trending-search-toggle =
    .label = Recercas popular
    .description = Themas popular e frequentemente recercate
newtab-custom-widget-weather-toggle =
    .label = Meteo
newtab-custom-widget-trending-search-toggle =
    .label = Recercas popular
newtab-custom-widget-lists-toggle =
    .label = Listas
newtab-custom-widget-timer-toggle =
    .label = Temporisator
newtab-custom-widget-section-title = Widgets
# Tooltip for close button
newtab-custom-close-menu-button =
    .title = Clauder
    .aria-label = Clauder menu
newtab-custom-close-button = Clauder
newtab-custom-settings = Gerer altere parametros

## New Tab Wallpapers

newtab-wallpaper-title = Fundos
newtab-wallpaper-reset = Restaurar le predefinition
newtab-wallpaper-upload-image = Incargar un imagine
newtab-wallpaper-custom-color = Eliger un color
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Le dimension del imagine excede le limite de { $file_size }MB. Tenta incargar un file minus grande.
newtab-wallpaper-error-file-type = Impossibile incargar tu file. Retenta con un altere typo de file.
newtab-wallpaper-light-red-panda = Panda rubie
newtab-wallpaper-light-mountain = Montania blanc
newtab-wallpaper-light-sky = Celo con nubes purpuree e rosate
newtab-wallpaper-light-color = Formas blau, rosate e jalne
newtab-wallpaper-light-landscape = Paisage montan con bruma blau
newtab-wallpaper-light-beach = Plagia con arbore de palma
newtab-wallpaper-dark-aurora = Aurora Boreal
newtab-wallpaper-dark-color = Formas rubie e blau
newtab-wallpaper-dark-panda = Panda rubie celate in bosco
newtab-wallpaper-dark-sky = Paisage urban con un celo nocturne
newtab-wallpaper-dark-mountain = Paisage montan
newtab-wallpaper-dark-city = Paisage urban purpuree
newtab-wallpaper-dark-fox-anniversary = Un vulpe sur le pavimento presso un bosco
newtab-wallpaper-light-fox-anniversary = Un vulpe in un prato con un brumose paisage montan

## Solid Colors

newtab-wallpaper-category-title-colors = Colores unite
newtab-wallpaper-blue = Blau
newtab-wallpaper-light-blue = Blau clar
newtab-wallpaper-light-purple = Purpuree clar
newtab-wallpaper-light-green = Verde clar
newtab-wallpaper-green = Verde
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Jalne
newtab-wallpaper-orange = Orange
newtab-wallpaper-pink = Rosate
newtab-wallpaper-light-pink = Rosate clar
newtab-wallpaper-red = Rubie
newtab-wallpaper-dark-blue = Blau obscur
newtab-wallpaper-dark-purple = Purpuree obscur
newtab-wallpaper-dark-green = Verde obscur
newtab-wallpaper-brown = Brun

## Abstract

newtab-wallpaper-category-title-abstract = Abstracte
newtab-wallpaper-abstract-green = Formas verde
newtab-wallpaper-abstract-blue = Formas blau
newtab-wallpaper-abstract-purple = Formas purpuree
newtab-wallpaper-abstract-orange = Formas orange
newtab-wallpaper-gradient-orange = Gradiente orange e rosate
newtab-wallpaper-abstract-blue-purple = Formas blau e purpuree
newtab-wallpaper-abstract-white-curves = Blanc con curvas umbrate
newtab-wallpaper-abstract-purple-green = Gradiente purpuree e verde clar
newtab-wallpaper-abstract-blue-purple-waves = Formas undulate blau e purpuree
newtab-wallpaper-abstract-black-waves = Formas undulate nigre

## Celestial

newtab-wallpaper-category-title-photographs = Photos
newtab-wallpaper-beach-at-sunrise = Plagia al levar del sol
newtab-wallpaper-beach-at-sunset = Plagia al poner del sol
newtab-wallpaper-storm-sky = Celo tempestuose
newtab-wallpaper-sky-with-pink-clouds = Celo con nubes rosate
newtab-wallpaper-red-panda-yawns-in-a-tree = Un panda rubie que balla sur un arbore
newtab-wallpaper-white-mountains = Montanias blanc
newtab-wallpaper-hot-air-balloons = Aerostatos de colores assortite durante le die
newtab-wallpaper-starry-canyon = Nocte stellate blau
newtab-wallpaper-suspension-bridge = Photographia de un ponte suspendite gris durante le die
newtab-wallpaper-sand-dunes = Dunas de arena blanc
newtab-wallpaper-palm-trees = Silhouette de palmas de coco durante le hora aurate
newtab-wallpaper-blue-flowers = Photographia in prime plano de flores con petalos blau florescente
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Photo per <a data-l10n-name="name-link">{ $author_string }</a> sur <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prova un tocco de color
newtab-wallpaper-feature-highlight-content = Da a tu nove schedas un apparentia fresc con le fundos.
newtab-wallpaper-feature-highlight-button = OK
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Dimitter
    .aria-label = Claude le message comparente
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celo
newtab-wallpaper-celestial-lunar-eclipse = Eclipse lunar
newtab-wallpaper-celestial-earth-night = Photo nocturne ab orbita terrestre basse
newtab-wallpaper-celestial-starry-sky = Celo stellate
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse de eclipse lunar
newtab-wallpaper-celestial-black-hole = Illustration de galaxia con foramine nigre
newtab-wallpaper-celestial-river = Imagine de satellite de un fluvio

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Vider prevision in { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsorisate
newtab-weather-menu-change-location = Cambiar loco
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Cercar loco
    .aria-label = Cercar loco
newtab-weather-menu-weather-display = Visualisation meteo
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Passar al vista simple
newtab-weather-menu-weather-display-option-detailed = Detaliate
newtab-weather-menu-change-weather-display-detailed = Passar al vista detaliate
newtab-weather-menu-temperature-units = Unitates de temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Passar a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Passar a Celsius
newtab-weather-menu-hide-weather = Celar meteo sur Nove scheda
newtab-weather-menu-learn-more = Pro saper plus
# This message is shown if user is working offline
newtab-weather-error-not-available = Datos meteo non es disponibile al momento.

## Topic Labels

newtab-topic-label-business = Negotios
newtab-topic-label-career = Carriera
newtab-topic-label-education = Education
newtab-topic-label-arts = Intertenimento
newtab-topic-label-food = Alimentos
newtab-topic-label-health = Sanitate
newtab-topic-label-hobbies = Jocos
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Moneta
newtab-topic-label-society-parenting = Education
newtab-topic-label-government = Politica
newtab-topic-label-education-science = Scientia
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Auto-amelioration
newtab-topic-label-sports = Sports
newtab-topic-label-tech = Technologia
newtab-topic-label-travel = Viages
newtab-topic-label-home = Casa e jardin

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Selige le themas pro le accordo fin de tu fluxo
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Selige duo o plus themas. Nostre curatores experte da prioritate al historias apte a tu interesses. Actualisa quando tu lo vole.
newtab-topic-selection-save-button = Salvar
newtab-topic-selection-cancel-button = Cancellar
newtab-topic-selection-button-maybe-later = Forsan un altere vice
newtab-topic-selection-privacy-link = Apprende como nos protege e gere le datos
newtab-topic-selection-button-update-interests = Actualisa tu interesses
newtab-topic-selection-button-pick-interests = Selige tu interesses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sequer
newtab-section-following-button = Sequente
newtab-section-unfollow-button = Non plus sequer
# A modal may appear next to the Follow button, directing users to try out the feature
newtab-section-follow-highlight-title = Perfectiona tu fluxo
newtab-section-follow-highlight-subtitle = Seque tu interesses pro vider plus de lo que te place.

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blocar
newtab-section-blocked-button = Blocate
newtab-section-unblock-button = Disblocar

## Confirmation modal for blocking a section

newtab-section-cancel-button = Non ora
newtab-section-confirm-block-topic-p1 = Desira tu vermente blocar iste topico?
newtab-section-confirm-block-topic-p2 = Le topicos blocate non apparera plus in tu fluxo.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blocar { $topic }

## Strings for custom wallpaper highlight

newtab-section-mangage-topics-title = Topicos
newtab-section-manage-topics-button-v2 =
    .label = Gerer topicos
newtab-section-mangage-topics-followed-topics = Sequite
newtab-section-mangage-topics-followed-topics-empty-state = Tu non ha ancora sequite alcun topico.
newtab-section-mangage-topics-blocked-topics = Blocate
newtab-section-mangage-topics-blocked-topics-empty-state = Tu non ha ancora blocate alcun topico.
newtab-custom-wallpaper-title = Ecce le fundos personalisate
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Incarga tu proprie fundo o selige un color pro personalisar tu { -brand-product-name }.
newtab-custom-wallpaper-cta = Prova lo

## Strings for new user activation custom wallpaper highlight

newtab-new-user-custom-wallpaper-title = Selige un fundo pro render tue { -brand-product-name }
newtab-new-user-custom-wallpaper-subtitle = Rende cata nove scheda como tu casa con fundos e colores personal.
newtab-new-user-custom-wallpaper-cta = Proba lo subito

## Strings for download mobile highlight

newtab-download-mobile-highlight-title = Discarga { -brand-product-name } pro apparatos mobile
# "Scan the code" refers to scanning the QR code that appears above the body text that leads to Firefox for mobile download.
newtab-download-mobile-highlight-body-variant-a = Scanna le codice pro navigar con securitate quando tu es in cammino.
newtab-download-mobile-highlight-body-variant-b = Reprende de ubi tu cessava quando tu synchronisa tu schedas, contrasignos, e plus.
newtab-download-mobile-highlight-body-variant-c = Sape tu que tu pote prender { -brand-product-name } quando tu es in cammino? Le mesme navigator, in tu tasca.
newtab-download-mobile-highlight-image =
    .aria-label = Codice QR pro discargar { -brand-product-name } pro apparatos mobile

## Strings for shortcuts highlight

newtab-shortcuts-highlight-title = Tu preferentias a tu punctas de digitos
newtab-shortcuts-highlight-subtitle = Adde un via-breve pro mantener tu sitos favorite a portata de clic.

## Strings for reporting ads and content

newtab-report-content-why-reporting-this =
    .label = Proque reporta tu isto?
newtab-report-ads-reason-not-interested =
    .label = Io non es interessate
newtab-report-ads-reason-inappropriate =
    .label = Es inappropriate
newtab-report-ads-reason-seen-it-too-many-times =
    .label = Io lo ha vidite troppo de vices
newtab-report-content-wrong-category =
    .label = Categoria incorrecte
newtab-report-content-outdated =
    .label = Obsolete
newtab-report-content-inappropriate-offensive =
    .label = Inappropriate o offensive
newtab-report-content-spam-misleading =
    .label = Spam o deception
newtab-report-cancel = Cancellar
newtab-report-submit = Inviar
newtab-toast-thanks-for-reporting =
    .message = Gratias pro iste reporto.

## Strings for trending searches

newtab-trending-searches-show-trending =
    .title = Monstrar recercas popular
newtab-trending-searches-hide-trending =
    .title = Celar recercas popular
newtab-trending-searches-learn-more = Pro saper plus
newtab-trending-searches-dismiss = Celar recercas popular
# "Trending searches refers to popular searches from search engines
newtab-trending-searches-title = Recercas popular

## Strings for task / to-do list productivity widget

# "Add one" means adding a new task to the list (e.g., "Walk the dog")
newtab-widget-lists-empty-cta = Le possibilitates es infinite. Adde un.
# A simple label next to the default list name letting users know this is a new / beta feature
newtab-widget-lists-label-new =
    .label = Nove
newtab-widget-lists-label-beta =
    .label = Beta
# When tasks have been previous marked as complete, they will appear in their own separate list beneath incomplete items
# Variables:
#   $number (number) - Amount of list items marked complete
newtab-widget-lists-completed-list = ({ $number }) completate
newtab-widget-task-list-menu-copy = Copiar
newtab-widget-lists-menu-edit = Rediger nomine del lista
newtab-widget-lists-menu-create = Crear un nove lista
newtab-widget-lists-menu-delete = Deler iste lista
newtab-widget-lists-menu-copy = Copiar lista al area de transferentia
newtab-widget-lists-menu-hide = Celar tote le listas
newtab-widget-lists-menu-learn-more = Pro saper plus
newtab-widget-lists-input-add-an-item =
    .placeholder = Adder un elemento
newtab-widget-lists-input-error = Include texto pro adder un elemento.
newtab-widget-lists-input-menu-open-link = Aperir ligamine
newtab-widget-lists-input-menu-move-up = Mover in alto
newtab-widget-lists-input-menu-move-down = Mover in basso
newtab-widget-lists-input-menu-delete = Deler
newtab-widget-lists-input-menu-edit = Rediger
newtab-widget-lists-name-label-default =
    .label = Agenda
newtab-widget-lists-name-placeholder-default =
    .placeholder = Agenda
# The placeholder value of the name field for a newly created list
newtab-widget-lists-name-placeholder-new =
    .placeholder = Nove lista

## Strings for timer productivity widget
## When the timer ends, a system notification may be shown. Depending on which mode the timer is in, that message would be shown

newtab-widget-timer-notification-title = Temporisator
newtab-widget-timer-notification-focus = Le tempore del concentration es expirate. Belle labor. Besonia tu un pausa?
newtab-widget-timer-notification-break = Tu pausa ha terminate, Preste a concentrar te?
newtab-widget-timer-notification-warning = Notificationes disactivate
newtab-widget-timer-mode-focus =
    .label = Concentration
newtab-widget-timer-mode-break =
    .label = Pausa
newtab-widget-timer-play =
    .title = Reproduction
newtab-widget-timer-pause =
    .title = Pausa
newtab-widget-timer-reset =
    .title = Reinitialisation
newtab-widget-timer-menu-notifications = Notificationes de extinction
newtab-widget-timer-menu-notifications-on = Activar notificationes
newtab-widget-timer-menu-hide = Celar temporisator
newtab-widget-timer-menu-learn-more = Pro saper plus
# Message that appears when widgets are full-height. This reminds users that there is more New Tab content to see if they scroll
newtab-widget-keep-scrolling = Rolar pro altero
newtab-widget-message-title = Remane concentrate con le listas e un temporisator integrate
# to-dos stands for "things to do".
newtab-widget-message-copy = Ab le rapide mementos al agendas quotidian, ab le sessiones de concentration al pausas de relaxamento: remane attente a activitate e juste tempore.
newtab-promo-card-title = Supporta { -brand-product-name }
newtab-promo-card-body = Nostre patrocinatores supporta nostre mission pro construer un web melior
newtab-promo-card-cta = Pro saper plus
newtab-promo-card-dismiss-button =
    .title = Ignorar
    .aria-label = Ignorar
