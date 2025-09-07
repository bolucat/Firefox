/* vim: se cin sw=2 ts=2 et filetype=javascript :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const kStorageVersion = 1;

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  EventEmitter: "resource://gre/modules/EventEmitter.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

/**
 * Returns a JSON schema validator for Taskbar Tabs persistent storage.
 *
 * @returns {Promise<Validator>} Resolves to JSON schema validator for Taskbar Tab's persistent storage.
 */
async function getJsonSchema() {
  const kJsonSchema =
    "chrome://browser/content/taskbartabs/TaskbarTabs.1.schema.json";
  let res = await fetch(kJsonSchema);
  let obj = await res.json();
  return new lazy.JsonSchema.Validator(obj);
}

/**
 * Storage class for a single Taskbar Tab's persistent storage.
 */
class TaskbarTab {
  // Unique identifier for the Taskbar Tab.
  #id;
  // List of hosts associated with this Taskbar tab.
  #scopes = [];
  // Container the Taskbar Tab is opened in when opened from the Taskbar.
  #userContextId;
  // URL opened when a Taskbar Tab is opened from the Taskbar.
  #startUrl;
  // Human-readable name of this Taskbar Tab.
  #name;
  // The path to the shortcut associated with this Taskbar Tab, *relative
  // to the `Start Menu\Programs` folder.*
  #shortcutRelativePath;

  constructor({
    id,
    scopes,
    startUrl,
    name,
    userContextId,
    shortcutRelativePath,
  }) {
    this.#id = id;
    this.#scopes = scopes;
    this.#userContextId = userContextId;
    this.#startUrl = startUrl;
    this.#name = name;

    this.#shortcutRelativePath = shortcutRelativePath ?? null;
  }

  get id() {
    return this.#id;
  }

  get scopes() {
    return [...this.#scopes];
  }

  get userContextId() {
    return this.#userContextId;
  }

  get startUrl() {
    return this.#startUrl;
  }

  get name() {
    return this.#name;
  }

  get shortcutRelativePath() {
    return this.#shortcutRelativePath;
  }

  /**
   * Whether the provided URL is navigable from the Taskbar Tab.
   *
   * @param {nsIURI} aUrl - The URL to navigate to.
   * @returns {boolean} `true` if the URL is navigable from the Taskbar Tab associated to the ID.
   * @throws {Error} If `aId` is not a valid Taskbar Tabs ID.
   */
  isScopeNavigable(aUrl) {
    let baseDomain = Services.eTLD.getBaseDomain(aUrl);

    for (const scope of this.#scopes) {
      let scopeBaseDomain = Services.eTLD.getBaseDomainFromHost(scope.hostname);

      // Domains in the same base domain are valid navigation targets.
      if (baseDomain === scopeBaseDomain) {
        lazy.logConsole.info(`${aUrl} is navigable for scope ${scope}.`);
        return true;
      }
    }

    lazy.logConsole.info(
      `${aUrl} is not navigable for Taskbar Tab ID ${this.#id}.`
    );
    return false;
  }

  toJSON() {
    const maybe = (self, name) => (self[name] ? { [name]: self[name] } : {});
    return {
      id: this.id,
      scopes: this.scopes,
      userContextId: this.userContextId,
      startUrl: this.startUrl,
      name: this.name,
      ...maybe(this, "shortcutRelativePath"),
    };
  }

  /**
   * Applies mutable fields from aPatch to this object.
   *
   * Always use TaskbarTabsRegistry.patchTaskbarTab instead. Aside
   * from calling into this, it notifies other objects (especially
   * the saver) about the change.
   */
  _applyPatch(aPatch) {
    if ("shortcutRelativePath" in aPatch) {
      this.#shortcutRelativePath = aPatch.shortcutRelativePath;
    }
  }
}

export const kTaskbarTabsRegistryEvents = Object.freeze({
  created: "created",
  patched: "patched",
  removed: "removed",
});

/**
 * Storage class for Taskbar Tabs feature's persistent storage.
 */
export class TaskbarTabsRegistry {
  // List of registered Taskbar Tabs.
  #taskbarTabs = [];
  // Signals when Taskbar Tabs have been created or removed.
  #emitter = new lazy.EventEmitter();

  static get events() {
    return kTaskbarTabsRegistryEvents;
  }

  /**
   * Initializes a Taskbar Tabs Registry, optionally loading from a file.
   *
   * @param {object} [init] - Initialization context.
   * @param {nsIFile} [init.loadFile] - Optional file to load.
   */
  static async create({ loadFile } = {}) {
    let registry = new TaskbarTabsRegistry();
    if (loadFile) {
      await registry.#load(loadFile);
    }

    return registry;
  }

  /**
   * Loads the stored Taskbar Tabs.
   *
   * @param {nsIFile} aFile - File to load from.
   */
  async #load(aFile) {
    if (!aFile.exists()) {
      lazy.logConsole.error(`File ${aFile.path} does not exist.`);
      return;
    }

    lazy.logConsole.info(`Loading file ${aFile.path} for Taskbar Tabs.`);

    const [schema, jsonObject] = await Promise.all([
      getJsonSchema(),
      IOUtils.readJSON(aFile.path),
    ]);

    if (!schema.validate(jsonObject).valid) {
      throw new Error(
        `JSON from file ${aFile.path} is invalid for the Taskbar Tabs Schema.`
      );
    }
    if (jsonObject.version > kStorageVersion) {
      throw new Error(`File ${aFile.path} has an unrecognized version.
          Current Version: ${kStorageVersion}
          File Version: ${jsonObject.version}`);
    }
    this.#taskbarTabs = jsonObject.taskbarTabs.map(
      tt => new TaskbarTab(migrateStoredTaskbarTab(tt))
    );
  }

  toJSON() {
    return {
      version: kStorageVersion,
      taskbarTabs: this.#taskbarTabs.map(tt => {
        return tt.toJSON();
      }),
    };
  }

  /**
   * Finds or creates a Taskbar Tab based on the provided URL and container.
   *
   * @param {nsIURI} aUrl - The URL to match or derive the scope and start URL from.
   * @param {number} aUserContextId - The container to start a Taskbar Tab in.
   * @param {object} aDetails - Additional options to use if it needs to be
   * created.
   * @param {object} aDetails.manifest - The Web app manifest that should be
   * associated with this Taskbar Tab.
   * @returns {TaskbarTab} The matching or created Taskbar Tab.
   */
  findOrCreateTaskbarTab(aUrl, aUserContextId, { manifest = {} } = {}) {
    let taskbarTab = this.findTaskbarTab(aUrl, aUserContextId);
    if (taskbarTab) {
      return taskbarTab;
    }

    let id = Services.uuid.generateUUID().toString().slice(1, -1);
    taskbarTab = new TaskbarTab({
      id,
      scopes: [{ hostname: aUrl.host }],
      userContextId: aUserContextId,
      name: manifest.name ?? generateName(aUrl),
      startUrl: manifest.start_url ?? aUrl.prePath,
    });
    this.#taskbarTabs.push(taskbarTab);

    lazy.logConsole.info(`Created Taskbar Tab with ID ${id}`);

    Glean.webApp.install.record({});
    this.#emitter.emit(kTaskbarTabsRegistryEvents.created, taskbarTab);

    return taskbarTab;
  }

  /**
   * Removes a Taskbar Tab.
   *
   * @param {string} aId - The ID of the TaskbarTab to remove.
   */
  removeTaskbarTab(aId) {
    let tts = this.#taskbarTabs;
    const i = tts.findIndex(tt => {
      return tt.id === aId;
    });

    if (i > -1) {
      lazy.logConsole.info(`Removing Taskbar Tab Id ${tts[i].id}`);
      let removed = tts.splice(i, 1);

      Glean.webApp.uninstall.record({});
      this.#emitter.emit(kTaskbarTabsRegistryEvents.removed, removed[0]);
    } else {
      lazy.logConsole.error(`Taskbar Tab ID ${aId} not found.`);
    }
  }

  /**
   * Searches for an existing Taskbar Tab matching the URL and Container.
   *
   * @param {nsIURI} aUrl - The URL to match.
   * @param {number} aUserContextId - The container to match.
   * @returns {TaskbarTab|null} The matching Taskbar Tab, or null if none match.
   */
  findTaskbarTab(aUrl, aUserContextId) {
    // Could be used in contexts reading from the command line, so validate
    // input to guard against passing in strings.
    if (!(aUrl instanceof Ci.nsIURI)) {
      throw new TypeError(
        "Invalid argument, `aUrl` should be instance of `nsIURI`"
      );
    }
    if (typeof aUserContextId !== "number") {
      throw new TypeError(
        "Invalid argument, `aUserContextId` should be type of `number`"
      );
    }

    for (const tt of this.#taskbarTabs) {
      for (const scope of tt.scopes) {
        if (aUrl.host === scope.hostname) {
          if (aUserContextId !== tt.userContextId) {
            lazy.logConsole.info(
              `Matched TaskbarTab for URL ${aUrl.host} to ${scope.hostname}, but container ${aUserContextId} mismatched ${tt.userContextId}.`
            );
          } else {
            lazy.logConsole.info(
              `Matched TaskbarTab for URL ${aUrl.host} to ${scope.hostname} with container ${aUserContextId}.`
            );
            return tt;
          }
        }
      }
    }

    lazy.logConsole.info(
      `No matching TaskbarTab found for URL ${aUrl.host} and container ${aUserContextId}.`
    );
    return null;
  }

  /**
   * Retrieves the Taskbar Tab matching the ID.
   *
   * @param {string} aId - The ID of the Taskbar Tab.
   * @returns {TaskbarTab} The matching Taskbar Tab.
   * @throws {Error} If `aId` is not a valid Taskbar Tab ID.
   */
  getTaskbarTab(aId) {
    const tt = this.#taskbarTabs.find(aTaskbarTab => {
      return aTaskbarTab.id === aId;
    });
    if (!tt) {
      lazy.logConsole.error(`Taskbar Tab Id ${aId} not found.`);
      throw new Error(`Taskbar Tab Id ${aId} is invalid.`);
    }

    return tt;
  }

  /**
   * Updates properties within the provided Taskbar Tab.
   *
   * All fields from aPatch will be assigned to aTaskbarTab, except
   * for the ID.
   *
   * @param {TaskbarTab} aTaskbarTab - The taskbar tab to update.
   * @param {object} aPatch - An object with properties to change.
   * @throws {Error} If any taskbar tab in aTaskbarTabs is unknown.
   */
  patchTaskbarTab(aTaskbarTab, aPatch) {
    // This is done from the registry to make it more clear that an event
    // will fire, and thus that I/O might be possible.
    aTaskbarTab._applyPatch(aPatch);
    this.#emitter.emit(kTaskbarTabsRegistryEvents.patched, aTaskbarTab);
  }

  /**
   * Passthrough to `EventEmitter.on`.
   *
   * @param  {...any} args - Same as `EventEmitter.on`.
   */
  on(...args) {
    return this.#emitter.on(...args);
  }

  /**
   * Passthrough to `EventEmitter.off`
   *
   * @param  {...any} args - Same as `EventEmitter.off`
   */
  off(...args) {
    return this.#emitter.off(...args);
  }

  /**
   * Resets the in-memory Taskbar Tabs state for tests.
   */
  resetForTests() {
    this.#taskbarTabs = [];
  }
}

/**
 * Monitor for the Taskbar Tabs Registry that updates the save file as it
 * changes.
 *
 * Note: this intentionally does not save on schema updates to allow for
 * gracefall rollback to an earlier version of Firefox where possible. This is
 * desirable in cases where a user has unintentioally opened a profile on a
 * newer version of Firefox, or has reverted an update.
 */
export class TaskbarTabsRegistryStorage {
  // The registry to save.
  #registry;
  // The file saved to.
  #saveFile;
  // Promise queue to ensure that async writes don't occur out of order.
  #saveQueue = Promise.resolve();

  /**
   * @param {TaskbarTabsRegistry} aRegistry - The registry to serialize.
   * @param {nsIFile} aSaveFile - The save file to update.
   */
  constructor(aRegistry, aSaveFile) {
    this.#registry = aRegistry;
    this.#saveFile = aSaveFile;
  }

  /**
   * Serializes the Taskbar Tabs Registry into a JSON file.
   *
   * Note: file writes are strictly ordered, ensuring the sequence of serialized
   * object writes reflects the latest state even if any individual write
   * serializes the registry in a newer state than when it's associated event
   * was emitted.
   *
   * @returns {Promise} Resolves once the current save operation completes.
   */
  save() {
    this.#saveQueue = this.#saveQueue
      .finally(async () => {
        lazy.logConsole.info(`Updating Taskbar Tabs storage file.`);

        const schema = await getJsonSchema();

        // Copy the JSON object to prevent awaits after validation risking
        // TOCTOU if the registry changes..
        let json = this.#registry.toJSON();

        let result = schema.validate(json);
        if (!result.valid) {
          throw new Error(
            "Generated invalid JSON for the Taskbar Tabs Schema:\n" +
              JSON.stringify(result.errors)
          );
        }

        await IOUtils.makeDirectory(this.#saveFile.parent.path);
        await IOUtils.writeJSON(this.#saveFile.path, json);

        lazy.logConsole.info(`Tasbkar Tabs storage file updated.`);
      })
      .catch(e => {
        lazy.logConsole.error(`Error writing Taskbar Tabs file: ${e}`);
      });

    lazy.AsyncShutdown.profileBeforeChange.addBlocker(
      "Taskbar Tabs: finalizing registry serialization to disk.",
      this.#saveQueue
    );

    return this.#saveQueue;
  }
}

/**
 * Mutates the provided Taskbar Tab object from storage so it contains all
 * current properties.
 *
 * @param {object} aStored - The object stored in the database; this will be
 * mutated as part of migrating it.
 * @returns {object} aStored exactly.
 */
function migrateStoredTaskbarTab(aStored) {
  if (typeof aStored.name !== "string") {
    try {
      aStored.name = generateName(Services.io.newURI(aStored.startUrl));
    } catch (e) {
      lazy.logConsole.warn(`Migrating ${aStored.id} failed:`, e);
    }
  }

  return aStored;
}

/**
 * Generates a name for the Taskbar Tab appropriate for user facing UI.
 *
 * @param {nsIURI} aUri - The URI to derive the name from.
 * @returns {string} A name suitable for user facing UI.
 */
function generateName(aUri) {
  // https://www.subdomain.example.co.uk/test

  // ["www", "subdomain", "example", "co", "uk"]
  let hostParts = aUri.host.split(".");

  // ["subdomain", "example", "co", "uk"]
  if (hostParts[0] === "www") {
    hostParts.shift();
  }

  let suffixDomainCount = Services.eTLD
    .getKnownPublicSuffix(aUri)
    .split(".").length;

  // ["subdomain", "example"]
  hostParts.splice(-suffixDomainCount);

  let name = hostParts
    // ["example", "subdomain"]
    .reverse()
    // ["Example", "Subdomain"]
    .map(s => s.charAt(0).toUpperCase() + s.slice(1))
    // "Example Subdomain"
    .join(" ");

  return name;
}
