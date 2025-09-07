/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { NetUtil } from "resource://gre/modules/NetUtil.sys.mjs";

export function MozProtocolHandler() {
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "urlToLoad",
    "toolkit.mozprotocol.url",
    "https://www.mozilla.org/about/manifesto/"
  );
}

MozProtocolHandler.prototype = {
  scheme: "moz",
  defaultPort: -1,
  protocolFlags: Ci.nsIProtocolHandler.URI_DANGEROUS_TO_LOAD,

  newChannel(uri, loadInfo) {
    const kCanada = "https://community.mozilla.org/groups/?country=CA";
    let realURL = NetUtil.newURI(
      uri && uri.spec == "moz://eh" ? kCanada : this.urlToLoad
    );
    let channel = Services.io.newChannelFromURIWithLoadInfo(realURL, loadInfo);
    loadInfo.resultPrincipalURI = realURL;
    return channel;
  },

  QueryInterface: ChromeUtils.generateQI(["nsIProtocolHandler"]),
};
