# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# As this feature is currently not localized, this brand is temporarily defined
# in this file. It should be moved to toolkit/toolkit/branding/brandings.ftl
# before exposing it to localization
-firefox-vpn-brand-name = Firefox VPN

ipprotection-button =
  .label = { -firefox-vpn-brand-name }
  .tooltiptext = { -firefox-vpn-brand-name }

# The word "Beta" is intended to be uppercase in the experiment label.
ipprotection-experiment-badge =
  .label = BETA

ipprotection-help-button =
  .title = Open { -firefox-vpn-brand-name } support page

ipprotection-title = { -firefox-vpn-brand-name }

## Feature introduction callout

ipprotection-feature-introduction-title = Boost your browser’s privacy with free { -firefox-vpn-brand-name }
ipprotection-feature-introduction-link-text = You’ve been selected for early access to our new, <a data-l10n-name="learn-more-vpn">built-in VPN</a>. Enhance your browser’s protection by hiding your location and encrypting your traffic.
ipprotection-feature-introduction-button-primary = Next
ipprotection-feature-introduction-button-secondary-not-now = Not now
ipprotection-feature-introduction-button-secondary-no-thanks = No thanks

##

# The panel status card has a header and a connection time displayed under it when the VPN is on.
# Variables:
#   $time (String) - The amount of time connected to the proxy as HH:MM:SS (hours, minutes, seconds).
ipprotection-connection-status-on =
  .label = VPN on
  .description = { $time }

ipprotection-connection-status-off =
  .label = VPN off

# When VPN is toggled on
ipprotection-toggle-active =
  .aria-label = Turn VPN off
  .title = Toggle set to VPN on
# When VPN is toggled off
ipprotection-toggle-inactive =
  .aria-label = Turn VPN on
  .title = Toggle set to VPN off

# Location refers to the VPN server geographical position.
ipprotection-location-title =
  .label = Location
  .title = Location selected based on fastest server

upgrade-vpn-title = Get peace of mind with full-device protection
upgrade-vpn-paragraph = Protect yourself beyond the browser with <a data-l10n-name="learn-more-vpn">{ -mozilla-vpn-brand-name }</a>. Customize your VPN location, set site-specific locations, and enjoy enhanced security whether you’re at home or on public Wi-Fi.
upgrade-vpn-button = Upgrade

signed-out-vpn-title = Sign in to boost your browser’s privacy with free { -firefox-vpn-brand-name }
signed-out-vpn-message = You’ve been selected for early access to our new, <a data-l10n-name="learn-more-vpn-signed-out">built-in VPN</a>. Enhance your browser’s protection by hiding your location and encrypting your traffic.
sign-in-vpn = Next

active-subscription-vpn-title = Finish setting up { -mozilla-vpn-brand-name }
active-subscription-vpn-message = Download it and install the extension to unlock your upgrade.
get-vpn-button = Download

## Messages and errors

# TODO: This is placeholder text. Error heading and message need to be finalized.
ipprotection-message-generic-error =
  .heading = Something went wrong
  .message = An error occurred with your VPN connection. Please try again later.

##
