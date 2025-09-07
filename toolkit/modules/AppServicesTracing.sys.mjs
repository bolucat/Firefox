/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A connection between app-services tracing-support library and Firefox.
 *
 * Used by logging and other diagnostic support from app-services Rust components.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  // eslint-disable-next-line mozilla/use-console-createInstance
  Log: "resource://gre/modules/Log.sys.mjs",
});

import {
  registerMinLevelEventSink,
  registerEventSink,
  unregisterMinLevelEventSink,
  unregisterEventSink,
  EventSink,
  TracingLevel,
} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustTracing.sys.mjs";

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "AppServices",
    maxLogLevelPref: "toolkit.rust-components.logging.internal-level",
  })
);

/**
 * List of callback/level pairs to send events to
 */
class CallbackList {
  items = [];

  /**
   * Get the maximum level in the callback list
   *
   * Retuns -Infinity when no items are in the list
   */
  maxLevel() {
    return Math.max(...this.items.map(i => i.level));
  }

  /**
   * Add a callback with a given level
   *
   * If the callback is already in the list, then the level will be updated rather than adding a new
   * item.
   *
   * If this changes the max level for the list, this returns the new max level otherwise it returns
   * undefined;
   */
  add(level, callback) {
    const oldMaxLevel = this.maxLevel();
    const index = this.items.findIndex(item => item.callback === callback);
    if (index == -1) {
      this.items.push({ level, callback });
    } else {
      this.items[index].level = level;
    }
    const newMaxLevel = this.maxLevel();
    if (newMaxLevel != oldMaxLevel) {
      return newMaxLevel;
    }
    return undefined;
  }

  /**
   * Remove a callback from the list, returning the level for that callback
   *
   * If this changes the max level for the list, this returns the new max level otherwise it returns
   * undefined;
   */
  remove(callback) {
    const index = this.items.find(i => i.callback === callback);
    if (index === undefined) {
      lazy.console.trace(
        "ignoring attempt to remove an event handler that's not registered"
      );
      return undefined;
    }
    const oldMaxLevel = this.maxLevel();
    this.items.splice(index, 1);
    const newMaxLevel = this.maxLevel();
    if (newMaxLevel != oldMaxLevel) {
      return newMaxLevel;
    }
    return undefined;
  }

  /**
   * Process an event using all items in this CallbackList.
   *
   * The callbacks for item >= `event.level` will be called.
   */
  processEvent(event) {
    for (const item of this.items) {
      if (item.level >= event.level) {
        try {
          item.callback(event);
        } catch (e) {
          lazy.console.error("tracing callback failed", item.callback, e);
        }
      }
    }
  }
}

/** A singleton uniffi callback interface. */
class TracingEventHandler extends EventSink {
  static OBSERVER_NAME = "xpcom-will-shutdown";

  constructor() {
    super();
    // Map targets to CallbackLists
    this.targetCallbackLists = new Map();
    // CallbackList for callbacks registered with registerMinLevelEventSink
    this.minLevelCallbackList = new CallbackList();

    Services.obs.addObserver(this, TracingEventHandler.OBSERVER_NAME);
  }

  register(target, level, callback) {
    if (this.targetCallbackLists === null) {
      lazy.console.trace(
        "ignoring attempt to register event handler after shutdown has commenced"
      );
      return;
    }
    const callbackList = this._getTargetList(target);
    const newMaxLevel = callbackList.add(level, callback);
    if (newMaxLevel !== undefined) {
      lazy.console.trace(
        `calling registerEventSink (${target} ${newMaxLevel})`
      );
      registerEventSink(target, newMaxLevel, this);
    }
  }

  deregister(target, callback) {
    if (this.targetCallbackLists === null) {
      lazy.console.trace(
        "ignoring attempt to register event handler after shutdown has commenced"
      );
      return;
    }
    const callbackList = this._getTargetList(target);
    const newMaxLevel = callbackList.remove(callback);
    if (newMaxLevel !== undefined) {
      if (newMaxLevel == -Infinity) {
        lazy.console.trace(`calling unregisterEventSink (${target})`);
        unregisterEventSink(target);
        this.targetCallbackLists.delete(target);
      } else {
        lazy.console.trace(
          `calling registerEventSink (${target} ${newMaxLevel})`
        );
        registerEventSink(target, newMaxLevel, this);
      }
    }
  }

  registerMinLevelEventSink(level, callback) {
    if (this.minLevelCallbackList === null) {
      lazy.console.trace(
        "ignoring attempt to register min-level event handler after shutdown has commenced"
      );
      return;
    }

    const newMaxLevel = this.minLevelCallbackList.add(level, callback);
    if (newMaxLevel !== undefined) {
      lazy.console.trace(`calling registerMinLevelEventSink (${newMaxLevel})`);
      registerMinLevelEventSink(newMaxLevel, this);
    }
  }

  unregisterMinLevelEventSink(callback) {
    if (this.minLevelCallbackList === null) {
      lazy.console.trace(
        "ignoring attempt to unregister min-level event handler after shutdown has commenced"
      );
      return;
    }

    const newMaxLevel = this.minLevelCallbackList.remove(callback);
    if (newMaxLevel !== undefined) {
      if (newMaxLevel == -Infinity) {
        lazy.console.trace(`calling unregisterMinLevelEventSink`);
        unregisterMinLevelEventSink();
      } else {
        lazy.console.trace(
          `calling registerMinLevelEventSink (${newMaxLevel})`
        );
        registerMinLevelEventSink(newMaxLevel, this);
      }
    }
  }

  _getTargetList(target) {
    if (!this.targetCallbackLists.has(target)) {
      this.targetCallbackLists.set(target, new CallbackList());
    }
    return this.targetCallbackLists.get(target);
  }

  onEvent(event) {
    let target = targetRoot(event.target);
    let targetList = this.targetCallbackLists.get(target);
    if (targetList) {
      targetList.processEvent(event);
    }
    this.minLevelCallbackList.processEvent(event);
  }

  observe(_aSubject, aTopic, _aData) {
    if (aTopic == TracingEventHandler.OBSERVER_NAME) {
      for (let target of this.targetCallbackLists.keys()) {
        unregisterEventSink(target);
      }
      unregisterMinLevelEventSink();
      this.targetCallbackLists = null;
      this.minLevelCallbackList = null;
    }
  }
}

// the singleton.
let tracingEventHandler = new TracingEventHandler();

// helper to get the pre '::' part of the target.
function targetRoot(target) {
  let colonIndex = target.indexOf(":");
  if (colonIndex > 0) {
    return target.slice(0, colonIndex);
  }
  return target;
}

/*
 * Log.sys.mjs adaptor.
 * Converts tracing log events to Log calls.
 */

// each target may go to one or more logs.
let targetToLogNames = new Map();

// handles a logging event we previously registered for.
function loggerEventHandler(event) {
  let target = targetRoot(event.target);
  for (const name of targetToLogNames.get(target) || []) {
    let log = lazy.Log.repository.getLogger(name);
    let log_level;
    if (event.level == TracingLevel.DEBUG) {
      log_level = lazy.Log.Level.Debug;
    } else if (event.level == TracingLevel.INFO) {
      log_level = lazy.Log.Level.Info;
    } else if (event.level == TracingLevel.WARN) {
      log_level = lazy.Log.Level.Warn;
    } else {
      log_level = lazy.Log.Level.Error;
    }
    log.log(log_level, event.message);
  }
}

/**
 * send output from a target to a Log.sys.jsm logger.
 */
export function setupLoggerForTarget(target, log) {
  if (typeof log == "string") {
    log = lazy.Log.repository.getLogger(log);
  }
  let log_level = log.level;
  let tracing_level;
  if (log_level == lazy.Log.ERROR) {
    tracing_level = TracingLevel.ERROR;
  } else if (log_level == lazy.Log.WARN) {
    tracing_level = TracingLevel.WARN;
  } else if (log_level == lazy.Log.INFO) {
    tracing_level = TracingLevel.INFO;
  } else if (log_level == lazy.Log.DEBUG) {
    tracing_level = TracingLevel.DEBUG;
  } else {
    tracing_level = TracingLevel.TRACE;
  }
  targetToLogNames.set(target, log.name);
  tracingEventHandler.register(target, tracing_level, loggerEventHandler);
}

class LogForwarder extends EventSink {
  static PREF_CRATES_TO_FORWARD = "toolkit.rust-components.logging.crates";

  registeredTargets = new Set();
  registeredMinLevelSink = false;

  init() {
    Services.prefs.addObserver(LogForwarder.PREF_CRATES_TO_FORWARD, this);
    this.callback = this.onLog.bind(this);
    this.console = console.createInstance({});
    this.setupForwarding();
  }

  /**
   * Read the current preference value and setup forwarding based on it
   */
  setupForwarding() {
    const prefValue = Services.prefs.getStringPref(
      LogForwarder.PREF_CRATES_TO_FORWARD,
      ""
    );
    const prefValueParsed = this.parsePrefValue(prefValue);

    if (prefValueParsed.minLevel != -Infinity) {
      // Register a min-level sink
      tracingEventHandler.registerMinLevelEventSink(
        prefValueParsed.minLevel,
        this.callback
      );
      this.registeredMinLevelSink = true;
    } else if (this.registeredMinLevelSink) {
      tracingEventHandler.unregisterMinLevelEventSink(this.callback);
      this.registeredMinLevelSink = false;
    }

    const oldRegisteredTargets = this.registeredTargets;
    this.registeredTargets = new Set();
    for (const [target, level] of prefValueParsed.targets) {
      oldRegisteredTargets.delete(target);
      this.registeredTargets.add(target);
      tracingEventHandler.register(target, level, this.callback);
    }
    for (const oldTarget of oldRegisteredTargets) {
      tracingEventHandler.deregister(oldTarget, this.callback);
    }
  }

  /**
   * Parse the tracing pref value
   */
  parsePrefValue(prefValue) {
    const parsed = {
      minLevel: -Infinity,
      targets: [],
    };

    if (prefValue == "") {
      parsed.minLevel = TracingLevel.ERROR;
      return parsed;
    }

    for (let spec of prefValue.split(",")) {
      spec = spec.trim();
      if (spec == "") {
        continue;
      }
      const minLevel = this.parseLevel(spec);
      if (minLevel !== undefined) {
        parsed.minLevel = Math.max(parsed.minLevel, minLevel);
      } else {
        parsed.targets.push(this.parseCrateSpec(spec));
      }
    }
    return parsed;
  }

  parseCrateSpec(spec) {
    var [target, level] = spec.split(":");
    if (level === undefined) {
      return [target, TracingLevel.DEBUG];
    }
    return [target, this.parseLevel(level) ?? TracingLevel.DEBUG];
  }

  parseLevel(levelString) {
    levelString = levelString.toLowerCase();
    if (levelString == "error") {
      return TracingLevel.ERROR;
    } else if (levelString == "warn" || levelString == "warning") {
      return TracingLevel.WARN;
    } else if (levelString == "info") {
      return TracingLevel.INFO;
    } else if (levelString == "debug") {
      return TracingLevel.DEBUG;
    } else if (levelString == "trace") {
      return TracingLevel.TRACE;
    }
    return undefined;
  }

  onLog(event) {
    const msg = `${event.target}: ${event.message}`;
    let log_level = event.level;
    if (log_level == TracingLevel.ERROR) {
      this.console.error(msg);
    } else if (log_level == TracingLevel.WARN) {
      this.console.warn(msg);
    } else if (log_level == TracingLevel.INFO) {
      this.console.info(msg);
    } else if (log_level == TracingLevel.DEBUG) {
      this.console.debug(msg);
    } else {
      this.console.trace(msg);
    }
  }

  observe(_subj, topic, data) {
    switch (topic) {
      case "nsPref:changed":
        if (data === LogForwarder.PREF_CRATES_TO_FORWARD) {
          this.setupForwarding();
        }
        break;
    }
  }
}

export const RustLogForwarder = new LogForwarder();
