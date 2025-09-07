/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  ContextIdCallback,
  ContextIdComponent,
} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustContextId.sys.mjs";

const CONTEXT_ID_PREF = "browser.contextual-services.contextId";
const CONTEXT_ID_TIMESTAMP_PREF =
  "browser.contextual-services.contextId.timestamp-in-seconds";
const CONTEXT_ID_ROTATION_DAYS_PREF =
  "browser.contextual-services.contextId.rotation-in-days";
const CONTEXT_ID_RUST_COMPONENT_ENABLED_PREF =
  "browser.contextual-services.contextId.rust-component.enabled";
const SHUTDOWN_TOPIC = "profile-before-change";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "CURRENT_CONTEXT_ID",
  CONTEXT_ID_PREF,
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "UNIFIED_ADS_ENDPOINT",
  "browser.newtabpage.activity-stream.unifiedAds.endpoint",
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "OHTTP_RELAY_URL",
  "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "OHTTP_CONFIG_URL",
  "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
  ""
);

class JsContextIdCallback extends ContextIdCallback {
  constructor(dispatchEvent) {
    super();
    this.dispatchEvent = dispatchEvent;
  }

  persist(newContextId, creationTimestamp) {
    Services.prefs.setCharPref(CONTEXT_ID_PREF, newContextId);
    Services.prefs.setIntPref(CONTEXT_ID_TIMESTAMP_PREF, creationTimestamp);
    this.dispatchEvent(new CustomEvent("ContextId:Persisted"));
  }

  rotated(oldContextId) {
    GleanPings.contextIdDeletionRequest.setEnabled(true);

    Glean.contextualServices.contextId.set(oldContextId);
    GleanPings.contextIdDeletionRequest.submit();
    ContextId.sendMARSDeletionRequest(oldContextId);
  }
}

/**
 * A class that manages and (optionally) rotates the context ID, which is a
 * a unique identifier used by Contextual Services.
 */
export class _ContextId extends EventTarget {
  #comp = null;
  #rotationDays = 0;
  #rustComponentEnabled = false;
  #observer = null;

  constructor() {
    super();

    this.#rustComponentEnabled = Services.prefs.getBoolPref(
      CONTEXT_ID_RUST_COMPONENT_ENABLED_PREF,
      false
    );

    if (this.#rustComponentEnabled) {
      // We intentionally read this once at construction, and cache the result.
      // This is because enabling or disabling rotation may affect external
      // uses of _ContextId which (for example) send the context_id UUID to
      // Shredder in the context-id-deletion-request ping (which we only want to
      // do when rotation is disabled), and that sort of thing tends to get set
      // once during startup.
      this.#rotationDays = Services.prefs.getIntPref(
        CONTEXT_ID_ROTATION_DAYS_PREF,
        0
      );
      // Note that we're setting `running_in_test_automation` to true
      // all of the time. This is because we don't want the ContextID
      // component to be responsible for sending the DELETE request to MARS,
      // since it doesn't know to do it over OHTTP. We'll send the DELETE
      // request ourselves over OHTTP at rotation time.
      this.#comp = ContextIdComponent.init(
        lazy.CURRENT_CONTEXT_ID,
        Services.prefs.getIntPref(CONTEXT_ID_TIMESTAMP_PREF, 0),
        true /* running_in_test_automation */,
        new JsContextIdCallback(this.dispatchEvent.bind(this))
      );
      this.#observer = (subject, topic, data) => {
        this.observe(subject, topic, data);
      };

      Services.obs.addObserver(this.#observer, SHUTDOWN_TOPIC);
    }
  }

  /**
   * nsIObserver implementation.
   *
   * @param {nsISupports} _subject
   * @param {string} topic
   * @param {string} _data
   */
  observe(_subject, topic, _data) {
    if (topic == SHUTDOWN_TOPIC) {
      // Unregister ourselves as the callback to avoid leak assertions.
      this.#comp.unsetCallback();
      Services.obs.removeObserver(this.#observer, SHUTDOWN_TOPIC);
    }
  }

  /**
   * Returns the stored context ID for this profile, if one exists. If one
   * doesn't exist, one is generated and then returned. In the event that
   * context ID rotation is in effect, then this may return a different
   * context ID if we've determined it's time to rotate. This means that
   * consumers _should not_ cache the context ID, but always request it.
   *
   * @returns {Promise<string>}
   *   The context ID for this profile.
   */
  async request() {
    if (this.#rustComponentEnabled) {
      return this.#comp.request(this.#rotationDays);
    }

    // Fallback to the legacy behaviour of just returning the pref, or
    // generating / returning a UUID if the pref is false-y.
    if (!lazy.CURRENT_CONTEXT_ID) {
      let _contextId = Services.uuid.generateUUID().toString();
      Services.prefs.setStringPref(CONTEXT_ID_PREF, _contextId);
    }

    return Promise.resolve(lazy.CURRENT_CONTEXT_ID);
  }

  /**
   * Forces the rotation of the context ID. This should be used by callers when
   * some surface that uses the context ID is disabled. This is only supported
   * with the Rust backend, and is a no-op when the Rust backend is not enabled.
   *
   * @returns {Promise<undefined>}
   */
  async forceRotation() {
    if (this.#rustComponentEnabled) {
      return this.#comp.forceRotation();
    }
    return Promise.resolve();
  }

  /**
   * Returns true if context ID rotation is enabled.
   *
   * @returns {boolean}
   */
  get rotationEnabled() {
    return this.#rustComponentEnabled && this.#rotationDays > 0;
  }

  /**
   * A compatibility shim that only works if rotationEnabled is false which
   * returns the context ID synchronously. This will throw if rotationEnabled
   * is true - so callers should ensure that rotationEnabled is false before
   * using this. This will eventually be removed.
   */
  requestSynchronously() {
    if (this.rotationEnabled) {
      throw new Error(
        "Cannot request context ID synchronously when rotation is enabled."
      );
    }

    return lazy.CURRENT_CONTEXT_ID;
  }

  /**
   * For now, the context_id application-services component does not know how
   * to send the MARS deletion request over OHTTP, so we do it ourselves
   * manually, using the New Tab unified ads preferences. This will eventually
   * go away once the context_id component knows how to use OHTTP itself.
   *
   * @param {string} oldContextId
   *   The old context_id being rotated away from.
   * @returns {Promise<undefined>}
   */
  async sendMARSDeletionRequest(oldContextId) {
    if (
      !lazy.UNIFIED_ADS_ENDPOINT ||
      !lazy.OHTTP_RELAY_URL ||
      !lazy.OHTTP_CONFIG_URL
    ) {
      return;
    }

    const endpoint = `${lazy.UNIFIED_ADS_ENDPOINT}v1/delete_user`;
    const body = {
      context_id: oldContextId,
    };
    const headers = new Headers();
    headers.append("content-type", "application/json");

    const config = await lazy.ObliviousHTTP.getOHTTPConfig(
      lazy.OHTTP_CONFIG_URL
    );

    if (!config) {
      console.error(
        new Error(
          `OHTTP was configured for ${endpoint} but we couldn't fetch a valid config`
        )
      );
    }

    // We don't actually use this AbortController, but ObliviousHTTP wants it.
    const controller = new AbortController();
    const { signal } = controller;

    const response = await lazy.ObliviousHTTP.ohttpRequest(
      lazy.OHTTP_RELAY_URL,
      config,
      endpoint,
      {
        method: "DELETE",
        headers,
        body: JSON.stringify(body),
        credentials: "omit",
        signal,
      }
    );

    if (!response.ok) {
      console.error(new Error(`Unexpected status (${response.status})`));
    }
  }
}

export const ContextId = new _ContextId();
