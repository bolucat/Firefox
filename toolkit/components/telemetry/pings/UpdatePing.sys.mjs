/* -*- js-indent-level: 2; indent-tabs-mode: nil -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Log } from "resource://gre/modules/Log.sys.mjs";

import { TelemetryUtils } from "resource://gre/modules/TelemetryUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
});

const LOGGER_NAME = "Toolkit.Telemetry";
const PING_TYPE = "update";
const UPDATE_DOWNLOADED_TOPIC = "update-downloaded";
const UPDATE_STAGED_TOPIC = "update-staged";

/**
 * This module is responsible for listening to all the relevant update
 * signals, gathering the needed information and assembling the "update"
 * ping.
 */
export var UpdatePing = {
  _enabled: false,

  earlyInit() {
    this._log = Log.repository.getLoggerWithMessagePrefix(
      LOGGER_NAME,
      "UpdatePing::"
    );
    this._enabled = Services.prefs.getBoolPref(
      TelemetryUtils.Preferences.UpdatePing,
      false
    );

    this._log.trace("init - enabled: " + this._enabled);

    if (!this._enabled) {
      return;
    }

    Services.obs.addObserver(this, UPDATE_DOWNLOADED_TOPIC);
    Services.obs.addObserver(this, UPDATE_STAGED_TOPIC);
  },

  /**
   * Generate an "update" ping with reason "success" and dispatch it
   * to the Telemetry system.
   *
   * @param {String} aPreviousVersion The browser version we updated from.
   * @param {String} aPreviousBuildId The browser build id we updated from.
   * @param {String} progress An object to measure the progress of handleUpdateSuccess
   *                          to provide to the shutdown blocker (Bug 1917651)
   */
  async handleUpdateSuccess(aPreviousVersion, aPreviousBuildId, progress) {
    if (!this._enabled) {
      return;
    }

    this._log.trace("handleUpdateSuccess");

    // An update could potentially change the update channel. Moreover,
    // updates can only be applied if the update's channel matches with the build channel.
    // There's no way to pass this information from the caller nor the environment as,
    // in that case, the environment would report the "new" channel. However, the
    // update manager should still have information about the active update: given the
    // previous assumptions, we can simply get the channel from the update and assume
    // it matches with the state previous to the update.
    let updateManager = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    let update = updateManager
      ? await updateManager.updateInstalledAtStartup()
      : null;

    progress.updateFetched = true;

    const payload = {
      reason: "success",
      previousChannel: update ? update.channel : null,
      previousVersion: aPreviousVersion,
      previousBuildId: aPreviousBuildId,
    };

    const options = {
      addClientId: true,
      addEnvironment: true,
      usePingSender: true,
    };

    progress.payloadCreated = true;

    lazy.TelemetryController.submitExternalPing(
      PING_TYPE,
      payload,
      options
    ).catch(e => {
      progress.pingFailed = true;
      this._log.error("handleUpdateSuccess - failed to submit update ping", e);
    });

    if (update) {
      Glean.update.previousChannel.set(update.channel);
    }
    Glean.update.previousVersion.set(aPreviousVersion);
    Glean.update.previousBuildId.set(aPreviousBuildId);
    GleanPings.update.submit("success");
  },

  /**
   * Generate an "update" ping with reason "ready" and dispatch it
   * to the Telemetry system.
   *
   * @param {String} aUpdateState The state of the downloaded patch. See
   *        nsIUpdateService.idl for a list of possible values.
   */
  async _handleUpdateReady(aUpdateState) {
    const ALLOWED_STATES = [
      "applied",
      "applied-service",
      "pending",
      "pending-service",
      "pending-elevate",
    ];
    if (!ALLOWED_STATES.includes(aUpdateState)) {
      this._log.trace("Unexpected update state: " + aUpdateState);
      return;
    }

    // Get the information about the update we're going to apply from the
    // update manager.
    let updateManager = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    let update = updateManager ? await updateManager.getReadyUpdate() : null;
    if (!update) {
      this._log.trace(
        "Cannot get the update manager or no update is currently active."
      );
      return;
    }

    const payload = {
      reason: "ready",
      targetChannel: update.channel,
      targetVersion: update.appVersion,
      targetBuildId: update.buildID,
      targetDisplayVersion: update.displayVersion,
    };

    const options = {
      addClientId: true,
      addEnvironment: true,
      usePingSender: true,
    };

    lazy.TelemetryController.submitExternalPing(
      PING_TYPE,
      payload,
      options
    ).catch(e =>
      this._log.error("_handleUpdateReady - failed to submit update ping", e)
    );

    Glean.update.targetChannel.set(update.channel);
    Glean.update.targetVersion.set(update.appVersion);
    Glean.update.targetBuildId.set(update.buildID);
    Glean.update.targetDisplayVersion.set(update.displayVersion);
    GleanPings.update.submit("ready");
  },

  /**
   * The notifications handler.
   */
  async observe(aSubject, aTopic, aData) {
    this._log.trace("observe - aTopic: " + aTopic);
    if (aTopic == UPDATE_DOWNLOADED_TOPIC || aTopic == UPDATE_STAGED_TOPIC) {
      await this._handleUpdateReady(aData);
    }
  },

  shutdown() {
    if (!this._enabled) {
      return;
    }
    Services.obs.removeObserver(this, UPDATE_DOWNLOADED_TOPIC);
    Services.obs.removeObserver(this, UPDATE_STAGED_TOPIC);
  },
};
