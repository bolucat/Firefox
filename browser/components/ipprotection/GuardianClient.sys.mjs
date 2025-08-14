/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () =>
  ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton()
);
ChromeUtils.defineLazyGetter(
  lazy,
  "hiddenBrowserManager",
  () =>
    ChromeUtils.importESModule("resource://gre/modules/HiddenFrame.sys.mjs")
      .HiddenBrowserManager
);

if (Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT) {
  throw new Error("Guardian.sys.mjs should only run in the parent process");
}

/**
 * An HTTP Client to talk to the Guardian service.
 * Allows to enroll FxA users to the proxy service,
 * fetch a proxy pass and check if the user is a proxy user.
 *
 */
export class GuardianClient {
  /**
   */
  constructor(config = gConfig) {
    this.guardianEndpoint = config.guardianEndpoint;
    this.fxaOrigin = config.fxaOrigin;
    this.withToken = config.withToken;
  }
  /**
   * Checks the current user's FxA account to see if it is linked to the Guardian service.
   * This should be used before attempting to check Entitlement info.
   *
   * @returns {Promise<boolean>}
   *  - True: The user is linked to the Guardian service, they might be a proxy user or have/had a VPN-Subscription.
   *          This needs to be followed up with a call to `fetchUserInfo()` to check if they are a proxy user.
   *  - False: The user is not linked to the Guardian service, they cannot be a proxy user.
   */
  async isLinkedToGuardian() {
    const guardian_clientId = CLIENT_ID_MAP[this.#successURL.origin];
    if (!guardian_clientId) {
      throw new Error(
        `No client_id found for Guardian origin: ${this.#successURL.origin}`
      );
    }
    const cached_clients = await lazy.fxAccounts.listAttachedOAuthClients();
    if (cached_clients.some(client => client.id === guardian_clientId)) {
      return true;
    }
    // If we don't have the client in the cache, we refresh it, just to be sure.
    const refreshed_clients =
      await lazy.fxAccounts.listAttachedOAuthClients(true);
    if (refreshed_clients.some(client => client.id === guardian_clientId)) {
      return true;
    }
    return false;
  }

  /**
   * Tries to enroll the user to the proxy service.
   * It will silently try to sign in the user into guardian using their FxA account.
   * If the user already has a proxy entitlement, nothing will happen.
   *
   * @param { AbortSignal | null } aAbortSignal - An AbortSignal to cancel the operation.
   * @returns {Promise<{error?: string, ok?: boolean}>} Re
   */
  async enroll(aAbortSignal) {
    // We abort loading the page if the origion is not allowed.
    const allowedOrigins = [
      new URL(this.guardianEndpoint).origin,
      new URL(this.fxaOrigin).origin,
    ];
    // If the browser is redirected to one of those urls
    // we know we're done with the browser.
    const finalizerURLs = [this.#successURL, this.#enrollmentError];
    return await lazy.hiddenBrowserManager.withHiddenBrowser(async browser => {
      aAbortSignal?.addEventListener("abort", () => {
        browser.stop();
        browser.remove();
        throw new Error("aborted");
      });
      const finalEndpoint = waitUntilURL(browser, url => {
        const urlObj = new URL(url);
        if (!allowedOrigins.includes(urlObj.origin)) {
          browser.stop();
          browser.remove();
          throw new Error(`URL origin ${urlObj.origin} is not allowed.`);
        }
        if (
          finalizerURLs.some(
            finalizer =>
              urlObj.pathname === finalizer.pathname &&
              urlObj.origin === finalizer.origin
          )
        ) {
          return true;
        }
        return false;
      });
      browser.loadURI(Services.io.newURI(this.#loginURL.href), {
        // TODO: Make sure this is the right principal to use?
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });

      const result = await finalEndpoint;
      return GuardianClient._parseGuardianSuccessURL(result);
    });
  }

  static _parseGuardianSuccessURL(aUrl) {
    if (!aUrl) {
      return { error: "timeout", ok: false };
    }
    const url = new URL(aUrl);
    const params = new URLSearchParams(url.search);
    const error = params.get("error");
    if (error) {
      return { error, ok: false };
    }
    // Otherwise we should have:
    // - a code in the URL query
    if (!params.has("code")) {
      return { error: "missing_code", ok: false };
    }
    return { ok: true };
  }

  /**
   * Fetches a proxy pass from the Guardian service.
   *
   * @returns {Promise<{error?: string, status?:number, pass?: ProxyPass}>} Resolves with an object containing either an error string or the proxy pass data and a status code.
   * Status codes to watch for:
   * - 200: User is a proxy user and a new pass was fetched
   * - 403: The FxA was valid but the user is not a proxy user.
   * - 401: The FxA token was rejected.
   * - 5xx: Internal guardian error.
   */
  async fetchProxyPass() {
    const response = await this.withToken(async token => {
      return await fetch(this.#tokenURL, {
        method: "GET",
        headers: {
          Authorization: `Bearer ${token}`,
          "Content-Type": "application/json",
        },
      });
    });
    if (!response) {
      return { error: "login_needed" };
    }
    const status = response.status;
    try {
      const pass = await ProxyPass.fromResponse(response);
      if (!pass) {
        return { status, error: "invalid_response" };
      }
      return { pass, status };
    } catch (error) {
      console.error("Error creating ProxyPass:", error);
      return { status, error: "parse_error" };
    }
  }
  /**
   * Fetches the user's entitlement information.
   *
   * @returns {Promise<{status?: number, entitlement?: Entitlement|null, error?:string}>} A promise that resolves to an object containing the HTTP status code and the user's entitlement information.
   *
   * Status codes to watch for:
   * - 200: User is a proxy user and the entitlement information is available.
   * - 404: User is not a proxy user, no entitlement information available.
   * - 401: The FxA token was rejected, probably guardian and fxa mismatch. (i.e guardian-stage and fxa-prod)
   */
  async fetchUserInfo() {
    const response = await this.withToken(async token => {
      return fetch(this.#statusURL, {
        method: "GET",
        headers: {
          Authorization: `Bearer ${token}`,
          "Content-Type": "application/json",
        },
        cache: "no-cache",
      });
    });
    if (!response) {
      return { error: "login_needed" };
    }
    const status = response.status;
    try {
      const entitlement = await Entitlement.fromResponse(response);
      if (!entitlement) {
        return { status, error: "invalid_response" };
      }
      return {
        status,
        entitlement,
      };
    } catch (error) {
      return { status, error: "parse_error" };
    }
  }

  /** This is the URL that will be used to fetch the proxy pass. */
  get #tokenURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/token";
    return url;
  }
  /** This is the URL that will be used to log in to the Guardian service. */
  get #loginURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/auth";
    return url;
  }
  /** This is the URL that the user will be redirected to after a successful enrollment. */
  get #successURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/oauth/success";
    return url;
  }
  /** This is the URL that the user will be redirected to after a rejected/failed enrollment.
   *  The url will contain an error query parameter with the error message.
   */
  get #enrollmentError() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/error";
    return url;
  }
  /** This is the URL that will be used to check the user's proxy status. */
  get #statusURL() {
    const url = new URL(this.guardianEndpoint);
    url.pathname = "/api/v1/fpn/status";
    return url;
  }
  guardianEndpoint = "";
}

/**
 * A ProxyPass contains a JWT token that can be used to authenticate the proxy service.
 * It also contains the timestamp until which the token is valid.
 * The Proxy will reject new connections if the token is not valid anymore.
 *
 * Immutable after creation.
 */
class ProxyPass {
  /**
   * @param {string} token - The JWT to use for authentication.
   * @param {number} until - The timestamp until which the token is valid.
   */
  constructor(token, until) {
    if (typeof token !== "string" || typeof until !== "number") {
      throw new TypeError("Invalid arguments for ProxyPass constructor");
    }
    this.token = token;
    this.until = until;
    const [header, body] = this.token.split(".");
    try {
      const parses = [header, body].every(json =>
        JSON.parse(atob(json) != null)
      );
      if (!parses) {
        throw new TypeError("Invalid token format");
      }
    } catch (error) {
      throw new TypeError("Invalid token format: " + error.message);
    }
    Object.freeze(this);
  }
  isValid() {
    const now = Date.now();
    return this.until > now;
  }
  /**
   * Parses a ProxyPass from a Response object.
   *
   * @param {Response} response
   * @returns {Promise<ProxyPass|null>} A promise that resolves to a ProxyPass instance or null if the response is invalid.
   */
  static async fromResponse(response) {
    // if the response is not 200 return null
    if (!response.ok) {
      console.error(
        `Failed to fetch proxy pass: ${response.status} ${response.statusText}`
      );
      return null;
    }

    try {
      // Get cache_control max-age value
      const cache_control = response.headers
        .get("cache-control")
        ?.match(/max-age=(\d+)/)?.[1];

      if (!cache_control) {
        console.error("Missing or invalid Cache-Control header");
        return null;
      }

      const max_age = parseInt(cache_control, 10);
      if (isNaN(max_age)) {
        console.error("Invalid max-age value in Cache-Control header");
        return null;
      }

      const until = Date.now() + max_age * 1000;

      // Parse JSON response
      const responseData = await response.json();
      const token = responseData?.token;

      if (!token || typeof token !== "string") {
        console.error("Missing or invalid token in response");
        return null;
      }

      return new ProxyPass(token, until);
    } catch (error) {
      console.error("Error parsing proxy pass response:", error);
      return null;
    }
  }
  asBearerToken() {
    return `Bearer ${this.token}`;
  }
}

/**
 * Represents a user's Entitlement for the Proxy Service of Guardian.
 *
 * Right now any FxA user can have one entitlement.
 * If a user has an entitlement, they may access the proxy service.
 *
 * Immutable after creation.
 */
export class Entitlement {
  /** True if the User has any valid subscription plan to Mozilla VPN */
  subscribed = false;
  /** The Guardian User ID */
  uid = 0;
  /** The date the entitlement was added to the user */
  created_at = new Date();

  constructor(subscribed, uid, created_at) {
    if (
      typeof subscribed !== "boolean" ||
      typeof uid !== "number" ||
      typeof created_at !== "string"
    ) {
      throw new TypeError("Invalid arguments for Entitlement constructor");
    }
    // Assert ISO 8601 format: YYYY-MM-DDTHH:mm:ss.sssZ
    const iso8601Regex = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/;
    if (!iso8601Regex.test(created_at)) {
      throw new TypeError(
        "entitlementDate must be in ISO 8601 format: YYYY-MM-DDTHH:mm:ss.sssZ"
      );
    }
    // Ensure it parses to a valid date
    const parsed = Date.parse(created_at);
    if (isNaN(parsed)) {
      throw new TypeError("entitlementDate is not a valid date string");
    }
    this.subscribed = subscribed;
    this.uid = uid;
    this.created_at = parsed;
    Object.freeze(this);
  }
  static fromResponse(response) {
    // if the response is not 200 return null
    if (!response.ok) {
      return null;
    }
    return response.json().then(data => {
      return new Entitlement(data.subscribed, data.uid, data.created_at);
    });
  }
}

/**
 * Maps the Guardian service endpoint to the public OAuth client ID.
 */
const CLIENT_ID_MAP = {
  "http://localhost:3000": "6089c54fdc970aed",
  "https://guardian-dev.herokuapp.com": "64ef9b544a31bca8",
  "https://stage.guardian.nonprod.cloudops.mozgcp.net": "e6eb0d1e856335fc",
  "https://fpn.firefox.com": "e6eb0d1e856335fc",
  "https://vpn.mozilla.org": "e6eb0d1e856335fc",
};

/**
 * Waits for a specific URL to be loaded in the browser.
 *
 * @param {*} browser - The browser instance to listen for URL changes.
 * @param {(location: string) => boolean} predicate - A function that returns true if the location matches the desired URL.
 * @returns {Promise<string>} A promise that resolves to the matching URL.
 */
async function waitUntilURL(browser, predicate) {
  const prom = Promise.withResolvers();
  const wp = browser.webProgress; // nsIWebProgress
  const done = false;
  const check = arg => {
    if (done) {
      return;
    }
    if (predicate(arg)) {
      wp.removeProgressListener(listener);
      prom.resolve(arg);
    }
  };
  const listener = {
    QueryInterface: ChromeUtils.generateQI([
      "nsIWebProgressListener",
      "nsISupportsWeakReference",
    ]),

    // Fires for every redirect/location change (before the document load).
    onLocationChange(_, __, location) {
      check(location.spec);
    },

    // Unused callbacks we still need to implement:
    onStateChange() {},
    onProgressChange() {},
    onStatusChange(_, request) {
      try {
        const url = request.QueryInterface(Ci.nsIChannel).URI.spec;
        check(url);
      } catch (ex) {}
    },
    onSecurityChange() {},
    onContentBlockingEvent() {},
  };
  wp.addProgressListener(listener, Ci.nsIWebProgress.NOTIFY_ALL);
  const url = await prom.promise;

  return url;
}

let gConfig = {
  /**
   * Executes the callback with an FxA token and returns its result.
   * Destroys the token after use.
   *
   * @template T
   * @param {(token: string) => T|Promise<T>} cb
   * @returns {Promise<T|null>}
   */
  withToken: async cb => {
    const token = await lazy.fxAccounts.getOAuthToken({
      scope: ["profile", "https://identity.mozilla.com/apps/vpn"],
    });
    if (!token) {
      return null;
    }
    const res = await cb(token);
    lazy.fxAccounts.removeCachedOAuthToken({
      token,
    });
    return res;
  },
  guardianEndpoint: "",
  fxaOrigin: "",
};
XPCOMUtils.defineLazyPreferenceGetter(
  gConfig,
  "guardianEndpoint",
  "browser.ipProtection.guardian.endpoint",
  "https://vpn.mozilla.com"
);
XPCOMUtils.defineLazyPreferenceGetter(
  gConfig,
  "fxaOrigin",
  "identity.fxaccounts.remote.root"
);
