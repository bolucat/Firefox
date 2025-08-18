# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Generative AI (GenAI) Settings section

genai-settings-chat-chatgpt-links = By choosing ChatGPT, you agree to the OpenAI <a data-l10n-name="link1">Terms of Use</a> and <a data-l10n-name="link2">Privacy Policy</a>.
genai-settings-chat-claude-links = By choosing Anthropic Claude, you agree to the Anthropic <a data-l10n-name="link1">Consumer Terms of Service</a>, <a data-l10n-name="link2">Usage Policy</a>, and <a data-l10n-name="link3">Privacy Policy</a>.
genai-settings-chat-copilot-links = By choosing Copilot, you agree to the <a data-l10n-name="link1">Copilot AI Experiences Terms</a> and <a data-l10n-name="link2">Microsoft Privacy Statement</a>.
genai-settings-chat-gemini-links = By choosing Google Gemini, you agree to the <a data-l10n-name="link1">Google Terms of Service</a>, <a data-l10n-name="link2">Generative AI Prohibited Use Policy</a>, and <a data-l10n-name="link3">Gemini Apps Privacy Notice</a>.
genai-settings-chat-huggingchat-links = By choosing HuggingChat, you agree to the <a data-l10n-name="link1">HuggingChat Privacy Notice</a> and <a data-l10n-name="link2">Hugging Face Privacy Policy</a>.
genai-settings-chat-lechat-links = By choosing Le Chat Mistral, you agree to the Mistral AI <a data-l10n-name="link1">Terms of Service</a> and <a data-l10n-name="link2">Privacy Policy</a>.
genai-settings-chat-localhost-links = Bring your own private local chatbot such as <a data-l10n-name="link1">llamafile</a> from { -vendor-short-name }’s Innovation group.

## Chatbot prompts
## Prompts are plain language ‘instructions’ sent to a chatbot.
## These prompts have been made concise and direct in English because some chatbot providers
## have character restrictions and being direct reduces the chance for misinterpretation.
## When localizing, please be concise and direct, but not at the expense of losing meaning.

# This prompt is added to the beginning of selection prompts sent to a chatbot.
# $tabTitle (string) - title of the webpage
# $selection (string) - selected text
genai-prompt-prefix-selection = I’m on page “{ $tabTitle }” with “{ $selection }” selected.

# Prompt purpose: help users understand what a selection covers at a glance
genai-prompts-summarize =
    .label = Summarize
    .value = Please summarize the selection using precise and concise language. Use headers and bulleted lists in the summary, to make it scannable. Maintain the meaning and factual accuracy.
# Prompt purpose: make a selection easier to read
genai-prompts-simplify =
    .label = Simplify language
    .value = Please rewrite the selection using short sentences and simple words. Maintain the meaning and factual accuracy.
# Prompt purpose: test understanding of selection in an interactive way
genai-prompts-quiz =
    .label = Quiz me
    .value = Please quiz me on this selection. Ask me a variety of types of questions, for example multiple choice, true or false, and short answer. Wait for my response before moving on to the next question.
# Prompt purpose: helps users understand words, phrases, concepts
genai-prompts-explain =
    .label = Explain this
    .value = Please explain the key concepts in this selection, using simple words. Also, use examples.
# Prompt purpose: writing tool that helps users with spelling and grammar mistakes and produce a response that identifies errors and rewrites the inputted text correctly
genai-prompts-proofread =
    .label = Proofread
    .value = Please proofread the selection for spelling and grammar errors. Identify any mistakes and provide a corrected version of the text. Maintain the meaning and factual accuracy and output the list of proposed corrections first, followed by the final, corrected version of the text.

## Chatbot menu shortcuts

genai-menu-no-provider-2 =
    .label = Ask an AI Chatbot
    .accesskey = h
genai-menu-choose-chatbot =
    .label = Choose an AI Chatbot
genai-menu-ask-generic-2 =
    .label = Ask AI Chatbot
    .accesskey = h
# $provider (string) - name of the provider
genai-menu-ask-provider-2 =
    .label = Ask { $provider }
    .accesskey = h
genai-menu-open-generic =
    .label = Open AI Chatbot
# $provider (string) - name of the provider
genai-menu-open-provider =
    .label = Open { $provider }
genai-menu-remove-generic =
    .label = Remove AI Chatbot
# $provider (string) - name of the provider
genai-menu-remove-provider =
    .label = Remove { $provider }
genai-menu-remove-sidebar =
    .label = Remove from Sidebar

genai-menu-new-badge = New
genai-menu-summarize-page = Summarize Page

genai-input-ask-generic =
    .placeholder = Ask AI chatbot…
# $provider (string) - name of the provider
genai-input-ask-provider =
    .placeholder = Ask { $provider }…

# $selectionLength (number) - selected text length
# $maxLength (number) - max length of what can be selected
genai-shortcuts-selected-warning-generic =
    .heading = AI chatbot won’t get your full selection
    .message = { $selectionLength ->
        *[other] You’ve selected about { $selectionLength } characters. The number of characters we can send to the AI chatbot is about { $maxLength }.
    }
# $provider (string) - name of the provider
# $selectionLength (number) - selected text length
# $maxLength (number) - max length of what can be selected
genai-shortcuts-selected-warning =
    .heading = { $provider } won’t get your full selection
    .message = { $selectionLength ->
        *[other] You’ve selected about { $selectionLength } characters. The number of characters we can send to { $provider } is about { $maxLength }.
    }
genai-shortcuts-hide =
    .label = Hide chatbot shortcut

## Chatbot header

genai-chatbot-title = AI chatbot
genai-header-provider-menu =
    .title = Choose a chatbot
genai-header-options-button =
    .title = Open menu
genai-header-close-button =
    .title = Close

genai-provider-view-details =
    .label = View chatbot details
genai-options-reload-generic =
    .label = Reload AI chatbot
# $provider (string) - name of the provider
genai-options-reload-provider =
    .label = Reload { $provider }
genai-options-show-shortcut =
    .label = Show shortcut when selecting text
genai-options-hide-shortcut =
    .label = Hide shortcut when selecting text
genai-options-about-chatbot =
    .label = About AI chatbots in { -brand-short-name }

## Chatbot message

genai-page-warning =
    .message = Since the page is long, this is a partial summary.

## Chatbot footer

genai-page-button-summarize = Summarize page

## Chatbot onboarding

genai-chatbot-summarize-title = New! Summarize pages in one click
genai-chatbot-summarize-button = Summarize page

# “Summarize Page” should be consistent with the translation for the string genai-menu-summarize-page
genai-chatbot-summarize-sidebar-provider-subtitle = Right-click on your AI chatbot in the sidebar and choose “Summarize Page”.
# “Summarize Page” should be consistent with the translation for the string genai-menu-summarize-page
genai-chatbot-summarize-sidebar-generic-subtitle = Right-click the sparkles button in the sidebar and choose “Summarize Page”. The first time, you’ll also choose an AI chatbot.

# “Summarize page” should be consistent with the translation for the string genai-page-button-summarize
genai-chatbot-summarize-footer-provider-subtitle = Open your AI chatbot in the sidebar and choose “Summarize page” at the bottom.
genai-chatbot-summarize-footer-generic-subtitle = Add an AI chatbot to the { -brand-short-name } sidebar to quickly summarize pages.

genai-chatbot-contextual-title = Use an AI chatbot without switching tabs
genai-chatbot-contextual-subtitle = Chat and browse side-by-side when you add an AI chatbot in the { -brand-short-name } sidebar.
genai-chatbot-contextual-button = Choose a chatbot

genai-onboarding-header = Summarize, brainstorm, and more as you browse
genai-onboarding-choose-header = Choose an AI chatbot to use in the { -brand-short-name } sidebar
# "Switch anytime" refers to allowing the user to switch to a different chatbot.
genai-onboarding-description = Choose an AI chatbot to use in the { -brand-short-name } sidebar. We’ll show details about each chatbot when you select it. Switch anytime. <a data-l10n-name="learn-more">Learn more</a>
# "Switch anytime" refers to allowing the user to switch to a different chatbot.
genai-onboarding-choose-description = Switch anytime. For help choosing, <a data-l10n-name="learn-more">learn more about each chatbot</a>.
genai-onboarding-primary = Continue
genai-onboarding-secondary = Close
genai-onboarding-claude-tooltip =
    .title = Anthropic Claude
genai-onboarding-claude-learn = Learn more about Claude
genai-onboarding-chatgpt-tooltip =
    .title = ChatGPT
genai-onboarding-chatgpt-learn = Learn more about ChatGPT
genai-onboarding-copilot-tooltip =
    .title = Copilot
genai-onboarding-copilot-learn = Learn more about Copilot
genai-onboarding-gemini-tooltip =
    .title = Google Gemini
genai-onboarding-gemini-learn = Learn more about Gemini
genai-onboarding-huggingchat-tooltip =
    .title = HuggingChat
genai-onboarding-huggingchat-learn = Learn more about HuggingChat
genai-onboarding-lechat-tooltip =
    .title = Le Chat Mistral
genai-onboarding-lechat-learn = Learn more about Le Chat

genai-onboarding-select-header = Select text to see suggestions
genai-onboarding-select-description = When you select text, we’ll suggest prompts you can send to the chatbot. You can also write in your own prompts.
genai-onboarding-select-primary = Start chatting

## Chatbot onboarding choices
## These describe features/capabilities of chatbot providers. These are not buttons/actions.

genai-onboarding-claude-generate = Generate text and code
genai-onboarding-claude-analyze = Analyze documents and images
genai-onboarding-claude-price = Free and paid options; account required
genai-onboarding-chatgpt-generate = Generate text, images, and code
genai-onboarding-chatgpt-analyze = Analyze documents and images
genai-onboarding-chatgpt-price = Free and paid options; account required for some countries and tasks
genai-onboarding-copilot-generate = Generate text, images, and code
genai-onboarding-copilot-analyze = Analyze images
genai-onboarding-copilot-price = Free and paid options; account required for some tasks
genai-onboarding-gemini-generate = Generate text, images, and code
genai-onboarding-gemini-analyze = Analyze images (free) and documents (paid)
genai-onboarding-gemini-price = Free and paid options; account required
genai-onboarding-huggingchat-generate = Generate text and code
genai-onboarding-huggingchat-switch = Switch between a diverse set of open models
genai-onboarding-huggingchat-price-2 = Free; account required after a certain number of requests
genai-onboarding-lechat-generate = Generate text and code
genai-onboarding-lechat-price = Free; account required

## Model Optin Component

genai-model-optin-continue =
  .label = Continue

genai-model-optin-optout =
  .label = Cancel

genai-model-optin-cancel =
  .label = Cancel

## Link previews

# ‘min’ is short for “minute”
# ‘mins’ is short for “minutes”
# An estimate for how long it takes to read an article,
# expressed as a range covering both slow and fast readers.
# Variables:
#   $rangePlural (String): The plural category of the range, using the same set as for numbers.
#   $range (String): The range of minutes as a localised string. Examples: "3-7", "~1".
link-preview-reading-time =
    { $rangePlural ->
        [one] { $range } min reading time
       *[other] { $range } mins reading time
    }

# Error message displayed when a link preview cannot be generated
link-preview-error-message = We can’t preview this link

# Text for the link to visit the original URL when in error state
link-preview-visit-link = Visit link

# Error message when we can't generate key points (summary highlights or main ideas of page content) for a page
link-preview-generation-error-missing-data = We can’t generate key points for this webpage.

# Error message when something went wrong during key point generation
link-preview-generation-error-unexpected = Something went wrong.

# Text for the retry link when generation fails
link-preview-generation-retry = Try again

# Button that opens the Link Preview settings
link-preview-settings-button =
    .title = Link Preview Settings

link-preview-settings-enable =
    .label = Enable link previews
    .description = See the page title, description, and more when you use the shortcut or right-click on a link.
link-preview-settings-key-points =
    .label = Allow AI to read the beginning of the page and generate key points
link-preview-settings-long-press =
    .label = Shortcut: Click and hold the link for 1 second (long press)

# Title that appears when user is shown the opt-in flow for link previews
link-preview-optin-title = See more with AI?

# Message that appears when user is shown the opt-in flow for link previews
link-preview-optin-message = { -brand-short-name } uses AI to read the beginning of the page and generate a few key points. To prioritize your privacy, this happens on your device.

# Onboarding card title for long press
link-preview-onboarding-title-long-press = New: Click and hold any link for a preview

# Onboarding card description for long press
link-preview-onboarding-description-long-press = See a short description, reading time, and more to decide if the link is worth opening. Also available on right-click.

# Header for the key points section
link-preview-key-points-header = Key points

# Disclaimer for AI-generated key points
link-preview-key-points-disclaimer = Key points are AI-generated and may have mistakes.

# Progress message for the first-time setup
# $progress (number) - The percentage value 1-100 indicating the progress of the setup.
link-preview-setup = First-time setup • <strong>{ $progress }%</strong>

# Message indicating faster performance after initial setup
link-preview-setup-faster-next-time = You’ll see key points more quickly next time.

# Onboarding card See a preview button
link-preview-onboarding-button = See a preview

# Onboarding card Close button
link-preview-onboarding-close = Close
