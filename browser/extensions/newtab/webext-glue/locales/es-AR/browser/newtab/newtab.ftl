# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nueva pestaña
newtab-settings-button =
    .title = Personalizar la página nueva pestaña
newtab-customize-panel-icon-button =
    .title = Personalizar esta página
newtab-customize-panel-icon-button-label = Personalizar
newtab-personalize-settings-icon-label =
    .title = Personalizar nueva pestaña
    .aria-label = Configuración
newtab-settings-dialog-label =
    .aria-label = Configuración
newtab-personalize-icon-label =
    .title = Personalizar la nueva pestaña
    .aria-label = Personalizar la nueva pestaña
newtab-personalize-dialog-label =
    .aria-label = Personalizar
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Buscar
    .aria-label = Buscar
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Buscar con { $engine } o ingresar dirección
newtab-search-box-handoff-text-no-engine = Buscar o ingresar dirección
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Buscar con { $engine } o ingresar dirección
    .title = Buscar con { $engine } o ingresar dirección
    .aria-label = Buscar con { $engine } o ingresar dirección
newtab-search-box-handoff-input-no-engine =
    .placeholder = Buscar o ingresar dirección
    .title = Buscar o ingresar dirección
    .aria-label = Buscar o ingresar dirección
newtab-search-box-text = Buscar en la web
newtab-search-box-input =
    .placeholder = Buscar en la web
    .aria-label = Buscar en la web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Agregar buscador
newtab-topsites-add-shortcut-header = Nuevo atajo
newtab-topsites-edit-topsites-header = Editar sitio más visitado
newtab-topsites-edit-shortcut-header = Editar acceso directo
newtab-topsites-add-shortcut-label = Agregar atajo
newtab-topsites-add-shortcut-title =
    .title = Agregar atajo
    .aria-label = Agregar atajo
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Ingresar un título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Escribir o pegar URL
newtab-topsites-url-validation = Se requiere URL válida
newtab-topsites-image-url-label = URL de Imagen personalizada
newtab-topsites-use-image-link = Usar imagen personalizada…
newtab-topsites-image-validation = La imagen no se pudo cargar. Pruebe una URL diferente.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancelar
newtab-topsites-delete-history-button = Borrar del historial
newtab-topsites-save-button = Guardar
newtab-topsites-preview-button = Vista previa
newtab-topsites-add-button = Agregar

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ¿Está seguro de querer borrar cualquier instancia de esta página del historial?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Esta acción no puede deshacerse.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Patrocinado

## Label used by screen readers for pinned top sites

# Variables:
#   $title (string) - The label or hostname of the site.
topsite-label-pinned =
    .aria-label = { $title } (pegado)
    .title = { $title }

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Abrir menú
    .aria-label = Abrir menú
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Eliminar
    .aria-label = Eliminar
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Abrir menú
    .aria-label = Abrir el menú para { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar este sitio
    .aria-label = Editar este sitio

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Abrir en nueva ventana
newtab-menu-open-new-private-window = Abrir en nueva ventana privada
newtab-menu-dismiss = Descartar
newtab-menu-pin = Pegar
newtab-menu-unpin = Despegar
newtab-menu-delete-history = Borrar del historial
newtab-menu-save-to-pocket = Guardar en { -pocket-brand-name }
newtab-menu-delete-pocket = Borrar de { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar en { -pocket-brand-name }
newtab-menu-show-privacy-info = Nuestros patrocinadores y su privacidad
newtab-menu-about-fakespot = Acerca de { -fakespot-brand-name }
# Report is a verb (i.e. report issue with the content).
newtab-menu-report = Informar
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloquear
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Dejar de seguir el tema

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Administrar contenido patrocinado
newtab-menu-our-sponsors-and-your-privacy = Nuestros patrocinadores y su privacidad
newtab-menu-report-this-ad = Informar sobre esta publicidad

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Listo
newtab-privacy-modal-button-manage = Administrar la configuración de contenido patrocinado
newtab-privacy-modal-header = Su privacidad es importante.
newtab-privacy-modal-paragraph-2 =
    Además de ofrecer historias cautivadoras, también le vamos a mostrar información relevante,
    contenido sumamente revisado de patrocinadores seleccionados. No se preocupe, <strong>la seguridad de los datos de su navegación
     nunca dejan su copia personal de { -brand-product-name }</strong>: nosotros no la vemos y nuestros patrocinadores tampoco.
newtab-privacy-modal-link = Aprenda cómo funciona la privacidad en la pestaña nueva

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Eliminar marcador
# Bookmark is a verb here.
newtab-menu-bookmark = Marcador

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar Dirección del enlace
newtab-menu-go-to-download-page = Ir a la página de descarga
newtab-menu-remove-download = Eliminar del Historial

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mostrar en Finder
       *[other] Abrir Carpeta contenedora
    }
newtab-menu-open-file = Abrir archivo

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitados
newtab-label-bookmarked = Marcados
newtab-label-removed-bookmark = Marcador eliminado
newtab-label-recommended = Tendencias
newtab-label-saved = Guardado en { -pocket-brand-name }
newtab-label-download = Descargada
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Patrocinado
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Patrocinado por { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min
# This string is used under fixed size ads to indicate sponsored content
newtab-label-sponsored-fixed = Patrocinado

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Eliminar sección
newtab-section-menu-collapse-section = Colapsar sección
newtab-section-menu-expand-section = Expandir sección
newtab-section-menu-manage-section = Administrar sección
newtab-section-menu-manage-webext = Administrar extensión
newtab-section-menu-add-topsite = Agregar Sitio más visitado
newtab-section-menu-add-search-engine = Agregar buscador
newtab-section-menu-move-up = Subir
newtab-section-menu-move-down = Bajar
newtab-section-menu-privacy-notice = Nota de privacidad

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Contraer sección
newtab-section-expand-section-label =
    .aria-label = Expandir sección

## Section Headers.

newtab-section-header-topsites = Más visitados
newtab-section-header-recent-activity = Actividad reciente
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recomendado por { $provider }
newtab-section-header-stories = Historias que hacen reflexionar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Las selecciones de hoy para vos

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Comenzá a navegar y te mostraremos algunos de los mejores artículos, videos y otras páginas que hayás visitado o marcado acá.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ya te pusiste al día. Volvé más tarde para más historias de { $provider }. ¿No podés esperar? Seleccioná un tema popular para encontrar más historias de todo el mundo.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Ya está al día. Vuelva más tarde para más historias. ¿No puede esperar? Seleccione un tema popular para encontrar más historias de todo el mundo.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ¡Estás atrapado!
newtab-discovery-empty-section-topstories-content = Revisá más tarde para ver si hay historias nuevas.
newtab-discovery-empty-section-topstories-try-again-button = Probar de nuevo
newtab-discovery-empty-section-topstories-loading = Cargando…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡Uy! Casi cargamos esta sección, pero no del todo.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populares:
newtab-pocket-new-topics-title = ¿Quiere aún más historias? Vea estos temas populares de { -pocket-brand-name }
newtab-pocket-more-recommendations = Más recomendaciones
newtab-pocket-learn-more = Conocer más
newtab-pocket-cta-button = Obtener { -pocket-brand-name }
newtab-pocket-cta-text = Guarde las historias que quiera en { -pocket-brand-name } y potencie su mente con lecturas fascinantes.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } es parte de la familia { -brand-product-name }
newtab-pocket-save = Guardar
newtab-pocket-saved = Guardado

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Más como esta
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = No es para mí
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Gracias Su opinión nos ayudará a mejorar su canal.
newtab-toast-dismiss-button =
    .title = Ignorar
    .aria-label = Ignorar

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Descubrir lo mejor de la web
newtab-pocket-onboarding-cta = { -pocket-brand-name } explora un rango diverso de publicaciones para traer el contenido más informativo, inspiracional y confiable directamente al navegador { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Epa, algo salió mal al cargar este contenido.
newtab-error-fallback-refresh-link = Refrescar la página para reintentar.

## Customization Menu

newtab-custom-shortcuts-title = Accesos directos
newtab-custom-shortcuts-subtitle = Sitios guardados o visitados
newtab-custom-shortcuts-toggle =
    .label = Accesos directos
    .description = Sitios guardados o visitados
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } fila
       *[other] { $num } filas
    }
newtab-custom-sponsored-sites = Accesos directos patrocinados
newtab-custom-pocket-title = Recomendado por { -pocket-brand-name }
newtab-custom-pocket-subtitle = Contenido excepcional seleccionado por { -pocket-brand-name }, parte de la familia { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Historias recomendadas
    .description = Contenido excepcional seleccionado por la familia de { -brand-product-name }
newtab-custom-pocket-sponsored = Historias patrocinadas
newtab-custom-pocket-show-recent-saves = Mostrar guardados recientes
newtab-custom-recent-title = Actividad reciente
newtab-custom-recent-subtitle = Una selección de sitios y contenido recientes
newtab-custom-weather-toggle =
    .label = Clima
    .description = El pronóstico de hoy de un vistazo
newtab-custom-trending-search-toggle =
    .label = Búsquedas más populares
    .description = Temas populares y buscados con frecuencia
newtab-custom-widget-weather-toggle =
    .label = Clima
newtab-custom-widget-trending-search-toggle =
    .label = Búsquedas más populares
newtab-custom-widget-lists-toggle =
    .label = Listas
newtab-custom-widget-timer-toggle =
    .label = Temporizador
newtab-custom-widget-section-title = Widgets
# Tooltip for close button
newtab-custom-close-menu-button =
    .title = Cerrar
    .aria-label = Cerrar menú
newtab-custom-close-button = Cerrar
newtab-custom-settings = Administrar más configuraciones

## New Tab Wallpapers

newtab-wallpaper-title = Fondos de pantalla
newtab-wallpaper-reset = Reniciar como predeterminado
newtab-wallpaper-upload-image = Subir una imagen
newtab-wallpaper-custom-color = Elegir un color
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = La imagen excedió el límite de tamaño de archivo de { $file_size }MB. Pruebe subir un archivo más chico.
newtab-wallpaper-error-file-type = No pudimos subir el archivo. Vuelva a probar con un tipo de archivo diferente.
newtab-wallpaper-light-red-panda = Panda rojo
newtab-wallpaper-light-mountain = Montaña blanca
newtab-wallpaper-light-sky = Cielo con nubes violetas y rosas
newtab-wallpaper-light-color = Formas azules, rosas y amarillas
newtab-wallpaper-light-landscape = Paisaje de montaña con neblina azul
newtab-wallpaper-light-beach = Playa con palmera
newtab-wallpaper-dark-aurora = Aurora boreal
newtab-wallpaper-dark-color = Formas rojas y azules
newtab-wallpaper-dark-panda = Panda rojo oculto en el bosque
newtab-wallpaper-dark-sky = Paisaje de ciudad con cielo nocturno
newtab-wallpaper-dark-mountain = Paisaje de montaña
newtab-wallpaper-dark-city = Paisaje de ciudad violeta
newtab-wallpaper-dark-fox-anniversary = Un zorro en el pavimento cerca de un bosque
newtab-wallpaper-light-fox-anniversary = Un zorro en un campo cubierto de césped con un paisaje montañoso brumoso

## Solid Colors

newtab-wallpaper-category-title-colors = Colores lisos
newtab-wallpaper-blue = Azul
newtab-wallpaper-light-blue = Celeste
newtab-wallpaper-light-purple = Violeta claro
newtab-wallpaper-light-green = Verde claro
newtab-wallpaper-green = Verde
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Amarillo
newtab-wallpaper-orange = Naranja
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa claro
newtab-wallpaper-red = Rojo
newtab-wallpaper-dark-blue = Azul oscuro
newtab-wallpaper-dark-purple = Violeta oscuro
newtab-wallpaper-dark-green = Verde oscuro
newtab-wallpaper-brown = Marrón

## Abstract

newtab-wallpaper-category-title-abstract = Abstracto
newtab-wallpaper-abstract-green = Formas verdes
newtab-wallpaper-abstract-blue = Formas azules
newtab-wallpaper-abstract-purple = Formas violetas
newtab-wallpaper-abstract-orange = Formas naranjas
newtab-wallpaper-gradient-orange = Degradado naranja y rosa
newtab-wallpaper-abstract-blue-purple = Formas azules y violetas
newtab-wallpaper-abstract-white-curves = Blanco con curvas sombreadas
newtab-wallpaper-abstract-purple-green = Gradiente de luz violeta y verde
newtab-wallpaper-abstract-blue-purple-waves = Formas onduladas azules y violetas
newtab-wallpaper-abstract-black-waves = Formas onduladas negras

## Celestial

newtab-wallpaper-category-title-photographs = Fotografías
newtab-wallpaper-beach-at-sunrise = Playa al amanecer
newtab-wallpaper-beach-at-sunset = Playa al atardecer
newtab-wallpaper-storm-sky = Cielo tormentoso
newtab-wallpaper-sky-with-pink-clouds = Cielo con nubes rosas
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda rojo bosteza en un árbol
newtab-wallpaper-white-mountains = Montañas blancas
newtab-wallpaper-hot-air-balloons = Colores surtidos de globos aerostáticos durante el día
newtab-wallpaper-starry-canyon = Noche estrellada azul
newtab-wallpaper-suspension-bridge = Fotografía gris de puente colgante durante el día
newtab-wallpaper-sand-dunes = Dunas de arena blanca
newtab-wallpaper-palm-trees = Silueta de cocoteros durante la hora dorada
newtab-wallpaper-blue-flowers = Fotografía en primer plano de flores de pétalos azules floreciendo
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto de <a data-l10n-name="name-link">{ $author_string }</a> en <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Pruebe un toque de color
newtab-wallpaper-feature-highlight-content = Dele a su nueva pestaña una apariencia renovada con fondos de pantalla.
newtab-wallpaper-feature-highlight-button = Entendido
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Descartar
    .aria-label = Cerrar ventana emergente
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celeste
newtab-wallpaper-celestial-lunar-eclipse = Eclipse de luna
newtab-wallpaper-celestial-earth-night = Foto nocturna desde la órbita baja de la tierra
newtab-wallpaper-celestial-starry-sky = Cielo estrellado
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse de un eclipse lunar
newtab-wallpaper-celestial-black-hole = Ilustración de una galaxia con un agujero negro
newtab-wallpaper-celestial-river = Imagen de satelite de un rio

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Ver el pronóstico en { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Patrocinado
newtab-weather-menu-change-location = Cambiar ubicación
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Buscar ubicación
    .aria-label = Buscar ubicación
newtab-weather-menu-weather-display = Visualización del clima
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Cambiar a vista simple
newtab-weather-menu-weather-display-option-detailed = Detallada
newtab-weather-menu-change-weather-display-detailed = Cambiar a vista detallada
newtab-weather-menu-temperature-units = Unidades de temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Cambiar a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Cambiar a Celsius
newtab-weather-menu-hide-weather = Ocultar clima en Nueva pestaña
newtab-weather-menu-learn-more = Conocer más
# This message is shown if user is working offline
newtab-weather-error-not-available = Los datos meteorológicos no están disponibles en este momento.

## Topic Labels

newtab-topic-label-business = Negocios
newtab-topic-label-career = Empleo
newtab-topic-label-education = Educación
newtab-topic-label-arts = Entretenimiento
newtab-topic-label-food = Comida
newtab-topic-label-health = Salud
newtab-topic-label-hobbies = Juegos
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Dinero
newtab-topic-label-society-parenting = Padres
newtab-topic-label-government = Política
newtab-topic-label-education-science = Ciencia
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Trucos para la vida
newtab-topic-label-sports = Deportes
newtab-topic-label-tech = Tecnología
newtab-topic-label-travel = Viajes
newtab-topic-label-home = Hogar y jardín

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Seleccioná temas para mejorar tu canal
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Elegí dos o más temas. Nuestros curadores expertos priorizan las historias adaptadas a tus intereses. Actualizá en cualquier momento.
newtab-topic-selection-save-button = Guardar
newtab-topic-selection-cancel-button = Cancelar
newtab-topic-selection-button-maybe-later = Quizá más tarde
newtab-topic-selection-privacy-link = Conocer cómo protegemos y administramos los datos
newtab-topic-selection-button-update-interests = Actualizá tus intereses
newtab-topic-selection-button-pick-interests = Elegí tus intereses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Seguir
newtab-section-following-button = Siguiendo
newtab-section-unfollow-button = Dejar de seguir
# A modal may appear next to the Follow button, directing users to try out the feature
newtab-section-follow-highlight-title = Ajuste su canal
newtab-section-follow-highlight-subtitle = Siga sus intereses para ver más de lo que le guste.

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloquear
newtab-section-blocked-button = Bloqueado
newtab-section-unblock-button = Desbloquear

## Confirmation modal for blocking a section

newtab-section-cancel-button = No ahora
newtab-section-confirm-block-topic-p1 = ¿Está seguro de querer bloquear este tema?
newtab-section-confirm-block-topic-p2 = Los temas bloqueados ya no aparecerán en los canales.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloquear { $topic }

## Strings for custom wallpaper highlight

newtab-section-mangage-topics-title = Tópicos
newtab-section-manage-topics-button-v2 =
    .label = Administrar temas
newtab-section-mangage-topics-followed-topics = Seguido
newtab-section-mangage-topics-followed-topics-empty-state = Todavía no hay tópicos seguidos
newtab-section-mangage-topics-blocked-topics = Bloqueado
newtab-section-mangage-topics-blocked-topics-empty-state = Todavía no hay tópicos bloqueados
newtab-custom-wallpaper-title = Acá están los fondos de pantalla
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Suba su propio fondo de pantalla o elija un color personalizado para que { -brand-product-name } sea suyo.
newtab-custom-wallpaper-cta = Pruébelo

## Strings for new user activation custom wallpaper highlight

newtab-new-user-custom-wallpaper-title = Elegí un fondo de pantalla para hacer { -brand-product-name } tuyo.
newtab-new-user-custom-wallpaper-subtitle = Haga que cada nueva pestaña se sienta como en casa con fondos de pantalla y colores personalizados.
newtab-new-user-custom-wallpaper-cta = Probar ahora

## Strings for download mobile highlight

newtab-download-mobile-highlight-title = Descargar { -brand-product-name } para dispositivos móviles
# "Scan the code" refers to scanning the QR code that appears above the body text that leads to Firefox for mobile download.
newtab-download-mobile-highlight-body-variant-a = Escanee el código para navegar de forma segura en movimiento.
newtab-download-mobile-highlight-body-variant-b = Continúe donde lo dejó al sincronizar pestañas, contraseñas y más.
newtab-download-mobile-highlight-body-variant-c = ¿Sabía que puede llevar { -brand-product-name } a cualquier parte? Mismo navegador. En su bolsillo.
newtab-download-mobile-highlight-image =
    .aria-label = Código QR para descargar { -brand-product-name } para móviles

## Strings for shortcuts highlight

newtab-shortcuts-highlight-title = Sus favoritos en la punta de sus dedos
newtab-shortcuts-highlight-subtitle = Agregue un acceso directo para mantener a sus favoritos a un clic de distancia.

## Strings for reporting ads and content

newtab-report-content-why-reporting-this =
    .label = ¿Por qué está informando de esto?
newtab-report-ads-reason-not-interested =
    .label = No estoy interesado
newtab-report-ads-reason-inappropriate =
    .label = Es inapropiado
newtab-report-ads-reason-seen-it-too-many-times =
    .label = Lo vi demasiadas veces
newtab-report-content-wrong-category =
    .label = Categoría incorrecta
newtab-report-content-outdated =
    .label = Desactualizado
newtab-report-content-inappropriate-offensive =
    .label = Inapropiado u ofensivo
newtab-report-content-spam-misleading =
    .label = Spam o engañoso
newtab-report-cancel = Cancelar
newtab-report-submit = Enviar
newtab-toast-thanks-for-reporting =
    .message = Gracias por informar esto.

## Strings for trending searches

newtab-trending-searches-show-trending =
    .title = Mostrar búsquedas más populares
newtab-trending-searches-hide-trending =
    .title = Ocultar búsquedas más populares
newtab-trending-searches-learn-more = Conocer más
newtab-trending-searches-dismiss = Ocultar búsquedas más populares
# "Trending searches refers to popular searches from search engines
newtab-trending-searches-title = Búsquedas más populares

## Strings for task / to-do list productivity widget

# "Add one" means adding a new task to the list (e.g., "Walk the dog")
newtab-widget-lists-empty-cta = Las posibilidades son infinitas. Agregar una.
# A simple label next to the default list name letting users know this is a new / beta feature
newtab-widget-lists-label-new =
    .label = Nuevo
newtab-widget-lists-label-beta =
    .label = Beta
# When tasks have been previous marked as complete, they will appear in their own separate list beneath incomplete items
# Variables:
#   $number (number) - Amount of list items marked complete
newtab-widget-lists-completed-list = Completa ({ $number })
newtab-widget-task-list-menu-copy = Copiar
newtab-widget-lists-menu-edit = Editar nombre de lista
newtab-widget-lists-menu-create = Crear nueva lista
newtab-widget-lists-menu-delete = Borrar esta lista
newtab-widget-lists-menu-copy = Copiar lista al portapapeles
newtab-widget-lists-menu-hide = Ocultar todas las listas
newtab-widget-lists-menu-learn-more = Conocer más
newtab-widget-lists-input-add-an-item =
    .placeholder = Agregar un ítem
newtab-widget-lists-input-error = Incluir texto para agregar un ítem.
newtab-widget-lists-input-menu-open-link = Abrir enlace
newtab-widget-lists-input-menu-move-up = Mover arriba
newtab-widget-lists-input-menu-move-down = Mover abajo
newtab-widget-lists-input-menu-delete = Borrar
newtab-widget-lists-input-menu-edit = Editar
newtab-widget-lists-name-label-default =
    .label = Lista de tareas
newtab-widget-lists-name-placeholder-default =
    .placeholder = Lista de tareas
# The placeholder value of the name field for a newly created list
newtab-widget-lists-name-placeholder-new =
    .placeholder = Nueva lista

## Strings for timer productivity widget
## When the timer ends, a system notification may be shown. Depending on which mode the timer is in, that message would be shown

newtab-widget-timer-notification-title = Temporizador
newtab-widget-timer-notification-focus = Se terminó el tiempo de enfocarse. Buen trabajo. ¿Necesita un descanso?
newtab-widget-timer-notification-break = Se terminó el descanso. ¿Listo para enfocarse?
newtab-widget-timer-notification-warning = Las notificaciones están desactivadas
newtab-widget-timer-mode-break =
    .label = Descanso
newtab-widget-timer-pause =
    .title = Pausa
newtab-widget-timer-reset =
    .title = Restablecer
newtab-widget-timer-menu-notifications = Desactivar notificaciones
newtab-widget-timer-menu-notifications-on = Activar notificaciones
newtab-widget-timer-menu-hide = Ocultar temporizador
newtab-widget-timer-menu-learn-more = Conocer más
# Message that appears when widgets are full-height. This reminds users that there is more New Tab content to see if they scroll
newtab-widget-keep-scrolling = Deslizar para ver más
newtab-widget-message-title = Manténgase enfocado con listas y un temporizador incorporado
# to-dos stands for "things to do".
newtab-widget-message-copy = Desde recordatorios rápidos hasta tareas diarias, sesiones de enfoque y descansos prolongados: manténgase concentrado en la tarea y a tiempo.
newtab-promo-card-title = Ayudar a { -brand-product-name }
newtab-promo-card-body = Nuestros patrocinadores apoyan nuestra misión de construir una web mejor
newtab-promo-card-cta = Conocer más
newtab-promo-card-dismiss-button =
    .title = Descartar
    .aria-label = Descartar
