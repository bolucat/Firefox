/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BasePromiseWorker: "resource://gre/modules/PromiseWorker.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
});

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";

const PREF_WALLPAPERS_ENABLED =
  "browser.newtabpage.activity-stream.newtabWallpapers.enabled";

const PREF_WALLPAPERS_HIGHLIGHT_SEEN_COUNTER =
  "browser.newtabpage.activity-stream.newtabWallpapers.highlightSeenCounter";

const WALLPAPER_REMOTE_SETTINGS_COLLECTION_V2 = "newtab-wallpapers-v2";

const PREF_WALLPAPERS_CUSTOM_WALLPAPER_ENABLED =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.enabled";

const PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.uuid";

const PREF_SELECTED_WALLPAPER =
  "browser.newtabpage.activity-stream.newtabWallpapers.wallpaper";

export class WallpaperFeed {
  #customBackgroundObjectURL = null;

  constructor() {
    this.loaded = false;
    this.wallpaperClient = null;
    this._onSync = this.onSync.bind(this);
  }

  /**
   * This thin wrapper around global.fetch makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   */
  fetch(...args) {
    return fetch(...args);
  }

  /**
   * This thin wrapper around lazy.RemoteSettings makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   */
  RemoteSettings(...args) {
    return lazy.RemoteSettings(...args);
  }

  /**
   * This thin wrapper around lazy.BasePromiseWorker makes it easier for us to write
   * automated tests
   */
  BasePromiseWorker(...args) {
    return new lazy.BasePromiseWorker(...args);
  }

  async wallpaperSetup(isStartup = false) {
    const wallpapersEnabled = Services.prefs.getBoolPref(
      PREF_WALLPAPERS_ENABLED
    );

    if (wallpapersEnabled) {
      if (!this.wallpaperClient) {
        // getting collection
        this.wallpaperClient = this.RemoteSettings(
          WALLPAPER_REMOTE_SETTINGS_COLLECTION_V2
        );
      }

      this.wallpaperClient.on("sync", this._onSync);
      this.updateWallpapers(isStartup);
    }
  }

  async wallpaperTeardown() {
    if (this._onSync) {
      this.wallpaperClient?.off("sync", this._onSync);
    }
    this.loaded = false;
    this.wallpaperClient = null;
  }

  async onSync() {
    this.wallpaperTeardown();
    await this.wallpaperSetup(false /* isStartup */);
  }

  async updateWallpapers(isStartup = false) {
    let uuid = Services.prefs.getStringPref(
      PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
      ""
    );

    const selectedWallpaper = Services.prefs.getStringPref(
      PREF_SELECTED_WALLPAPER,
      ""
    );

    if (this.#customBackgroundObjectURL) {
      URL.revokeObjectURL(this.#customBackgroundObjectURL);
      this.#customBackgroundObjectURL = null;
    }

    if (uuid && selectedWallpaper === "custom") {
      const wallpaperDir = PathUtils.join(PathUtils.profileDir, "wallpaper");
      const filePath = PathUtils.join(wallpaperDir, uuid);

      try {
        let testFile = await IOUtils.getFile(filePath);

        if (!testFile) {
          throw new Error("File does not exist");
        }

        let imageFile = await File.createFromNsIFile(testFile);
        this.#customBackgroundObjectURL = URL.createObjectURL(imageFile);

        this.store.dispatch(
          ac.BroadcastToContent({
            type: at.WALLPAPERS_CUSTOM_SET,
            data: this.#customBackgroundObjectURL,
          })
        );
      } catch (error) {
        console.warn(`Wallpaper file not found: ${error.message}`);
        Services.prefs.clearUserPref(PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID);
        return;
      }
    } else {
      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.WALLPAPERS_CUSTOM_SET,
          data: null,
        })
      );
    }

    // retrieving all records in collection
    const records = await this.wallpaperClient.get();
    if (!records?.length) {
      return;
    }

    const customWallpaperEnabled = Services.prefs.getBoolPref(
      PREF_WALLPAPERS_CUSTOM_WALLPAPER_ENABLED
    );

    const baseAttachmentURL = await lazy.Utils.baseAttachmentsURL();

    const wallpapers = [
      ...records.map(record => {
        return {
          ...record,
          ...(record.attachment
            ? {
                wallpaperUrl: `${baseAttachmentURL}${record.attachment.location}`,
              }
            : {}),
          category: record.category || "",
        };
      }),
    ];

    const categories = [
      ...new Set(
        wallpapers.map(wallpaper => wallpaper.category).filter(Boolean)
      ),
      ...(customWallpaperEnabled ? ["custom-wallpaper"] : []), // Conditionally add custom wallpaper input
    ];

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_SET,
        data: wallpapers,
        meta: {
          isStartup,
        },
      })
    );

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WALLPAPERS_CATEGORY_SET,
        data: categories,
        meta: {
          isStartup,
        },
      })
    );
  }

  initHighlightCounter() {
    let counter = Services.prefs.getIntPref(
      PREF_WALLPAPERS_HIGHLIGHT_SEEN_COUNTER
    );

    this.store.dispatch(
      ac.AlsoToPreloaded({
        type: at.WALLPAPERS_FEATURE_HIGHLIGHT_COUNTER_INCREMENT,
        data: {
          value: counter,
        },
      })
    );
  }

  wallpaperSeenEvent() {
    let counter = Services.prefs.getIntPref(
      PREF_WALLPAPERS_HIGHLIGHT_SEEN_COUNTER
    );

    const newCount = counter + 1;

    this.store.dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: {
          name: "newtabWallpapers.highlightSeenCounter",
          value: newCount,
        },
      })
    );

    this.store.dispatch(
      ac.AlsoToPreloaded({
        type: at.WALLPAPERS_FEATURE_HIGHLIGHT_COUNTER_INCREMENT,
        data: {
          value: newCount,
        },
      })
    );
  }

  async wallpaperUpload(file) {
    try {
      const customWallpaperThemeWorker = this.BasePromiseWorker(
        "resource://newtab/lib/Wallpapers/WallpaperTheme.worker.mjs",
        { type: "module" }
      );
      const wallpaperTheme = await customWallpaperThemeWorker.post(
        "calculateTheme",
        [file]
      );
      customWallpaperThemeWorker.terminate();
      const wallpaperDir = PathUtils.join(PathUtils.profileDir, "wallpaper");

      // create wallpaper directory if it does not exist
      await IOUtils.makeDirectory(wallpaperDir, { ignoreExisting: true });

      let uuid = Services.uuid.generateUUID().toString().slice(1, -1);
      Services.prefs.setStringPref(PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID, uuid);

      const filePath = PathUtils.join(wallpaperDir, uuid);

      // convert to Uint8Array for IOUtils
      const arrayBuffer = await file.arrayBuffer();
      const uint8Array = new Uint8Array(arrayBuffer);

      await IOUtils.write(filePath, uint8Array, { tmpPath: `${filePath}.tmp` });

      if (this.#customBackgroundObjectURL) {
        URL.revokeObjectURL(this.#customBackgroundObjectURL);
        this.#customBackgroundObjectURL = null;
      }

      this.#customBackgroundObjectURL = URL.createObjectURL(file);

      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.WALLPAPERS_CUSTOM_SET,
          data: this.#customBackgroundObjectURL,
        })
      );

      this.store.dispatch(
        ac.SetPref("newtabWallpapers.customWallpaper.theme", wallpaperTheme)
      );

      return filePath;
    } catch (error) {
      console.error("Error saving wallpaper:", error);
      return null;
    }
  }

  async removeCustomWallpaper() {
    try {
      let uuid = Services.prefs.getStringPref(
        PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID,
        ""
      );

      if (!uuid) {
        return;
      }

      const wallpaperDir = PathUtils.join(PathUtils.profileDir, "wallpaper");
      const filePath = PathUtils.join(wallpaperDir, uuid);

      await IOUtils.remove(filePath, { ignoreAbsent: true });

      Services.prefs.clearUserPref(PREF_WALLPAPERS_CUSTOM_WALLPAPER_UUID);

      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.WALLPAPERS_CUSTOM_SET,
          data: null,
        })
      );
    } catch (error) {
      console.error("Failed to remove custom wallpaper:", error);
    }
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        await this.wallpaperSetup(true /* isStartup */);
        this.initHighlightCounter();
        break;
      case at.UNINIT:
        break;
      case at.SYSTEM_TICK:
        break;
      case at.PREF_CHANGED:
        if (
          action.data.name ===
            "newtabWallpapers.newtabWallpapers.customColor.enabled" ||
          action.data.name === "newtabWallpapers.customWallpaper.enabled" ||
          action.data.name === "newtabWallpapers.enabled"
        ) {
          this.wallpaperTeardown();
          await this.wallpaperSetup(false /* isStartup */);
        }
        if (action.data.name === "newtabWallpapers.highlightSeenCounter") {
          // Reset redux highlight counter to pref
          this.initHighlightCounter();
        }
        break;
      case at.WALLPAPERS_SET:
        break;
      case at.WALLPAPERS_FEATURE_HIGHLIGHT_SEEN:
        this.wallpaperSeenEvent();
        break;
      case at.WALLPAPER_UPLOAD:
        this.wallpaperUpload(action.data.file);
        break;
      case at.WALLPAPER_REMOVE_UPLOAD:
        await this.removeCustomWallpaper();
        break;
    }
  }
}
