/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. Wrap in
// a block to prevent accidentally leaking globals onto `window`.
{
  const { TabMetrics } = ChromeUtils.importESModule(
    "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs"
  );

  class MozTabbrowserTabGroup extends MozXULElement {
    static markup = `
      <vbox class="tab-group-label-container" pack="center">
        <label class="tab-group-label" role="button"/>
      </vbox>
      <html:slot/>
      <vbox class="tab-group-overflow-count-container" pack="center">
        <label class="tab-group-overflow-count" role="button" />
      </vbox>
      `;

    /** @type {string} */
    #defaultGroupName = "";

    /** @type {string} */
    #label;

    /** @type {MozTextLabel} */
    #labelElement;

    /** @type {MozTextLabel} */
    #overflowCountLabel;

    /** @type {MozXULElement} */
    #overflowContainer;

    /** @type {string} */
    #colorCode;

    /** @type {MutationObserver} */
    #tabChangeObserver;

    /** @type {boolean} */
    #wasCreatedByAdoption = false;

    constructor() {
      super();
    }

    static get inheritedAttributes() {
      return {
        ".tab-group-label": "text=label,tooltiptext=data-tooltip",
      };
    }

    connectedCallback() {
      // Always set the mutation observer to listen for tab change events, even
      // if we are already initialized.
      // This is needed to ensure events continue to fire even if the tab group is
      // moved from the horizontal to vertical tab layout or vice-versa, which
      // causes the component to be repositioned in the DOM.
      this.#observeTabChanges();

      // Similar to above, always set up TabSelect listener, as this gets
      // removed in disconnectedCallback
      this.ownerGlobal.addEventListener("TabSelect", this);

      if (this._initialized) {
        return;
      }

      this._initialized = true;
      this.saveOnWindowClose = true;

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();

      Services.obs.addObserver(
        this.resetDefaultGroupName,
        "intl:app-locales-changed"
      );
      window.addEventListener("unload", () => {
        Services.obs.removeObserver(
          this.resetDefaultGroupName,
          "intl:app-locales-changed"
        );
      });

      this.addEventListener("click", this);

      this.#labelElement = this.querySelector(".tab-group-label");
      // Mirroring MozTabbrowserTab
      this.#labelElement.container = gBrowser.tabContainer;
      this.#labelElement.group = this;

      this.#labelElement.addEventListener("mouseover", this);
      this.#labelElement.addEventListener("mouseout", this);
      this.#labelElement.addEventListener("contextmenu", e => {
        e.preventDefault();
        gBrowser.tabGroupMenu.openEditModal(this);
        return false;
      });

      this.#updateLabelAriaAttributes();
      this.#updateCollapsedAriaAttributes();

      this.#overflowContainer = this.querySelector(
        ".tab-group-overflow-count-container"
      );
      this.#overflowCountLabel = this.#overflowContainer.querySelector(
        ".tab-group-overflow-count"
      );

      let tabGroupCreateDetail = this.#wasCreatedByAdoption
        ? { isAdoptingGroup: true }
        : {};
      this.dispatchEvent(
        new CustomEvent("TabGroupCreate", {
          bubbles: true,
          detail: tabGroupCreateDetail,
        })
      );
      // Reset `wasCreatedByAdoption` to default of false so that we only
      // claim that a tab group was created by adoption the first time it
      // mounts after getting created by `Tabbrowser.adoptTabGroup`.
      this.#wasCreatedByAdoption = false;
    }

    resetDefaultGroupName = () => {
      this.#defaultGroupName = "";
      this.#updateLabelAriaAttributes();
      this.#updateTooltip();
    };

    disconnectedCallback() {
      this.ownerGlobal.removeEventListener("TabSelect", this);
      this.#tabChangeObserver?.disconnect();
    }

    appendChild(node) {
      return this.insertBefore(node, this.#overflowContainer);
    }

    #observeTabChanges() {
      if (!this.#tabChangeObserver) {
        this.#tabChangeObserver = new window.MutationObserver(mutations => {
          if (!this.tabs.length) {
            this.dispatchEvent(
              new CustomEvent("TabGroupRemoved", { bubbles: true })
            );
            this.remove();
            Services.obs.notifyObservers(
              this,
              "browser-tabgroup-removed-from-dom"
            );
          } else {
            let tabs = this.tabs;
            let tabCount = tabs.length;
            let hasActiveTab = false;
            tabs.forEach((tab, index) => {
              if (tab.selected) {
                hasActiveTab = true;
              }

              // Renumber tabs so that a11y tools can tell users that a given
              // tab is "2 of 7" in the group, for example.
              tab.setAttribute("aria-posinset", index + 1);
              tab.setAttribute("aria-setsize", tabCount);
            });
            this.hasActiveTab = hasActiveTab;

            // When a group containing the active tab is collapsed,
            // the overflow count displays the number of additional tabs
            // in the group adjacent to the active tab.
            let overflowCountLabel = this.#overflowContainer.querySelector(
              ".tab-group-overflow-count"
            );
            if (tabCount > 1) {
              gBrowser.tabLocalization
                .formatValue("tab-group-overflow-count", {
                  tabCount: tabCount - 1,
                })
                .then(result => (overflowCountLabel.textContent = result));
              gBrowser.tabLocalization
                .formatValue("tab-group-overflow-count-tooltip", {
                  tabCount: tabCount - 1,
                })
                .then(result => {
                  overflowCountLabel.setAttribute("tooltiptext", result);
                  overflowCountLabel.setAttribute("aria-description", result);
                });
              this.toggleAttribute("hasmultipletabs", true);
            } else {
              overflowCountLabel.textContent = "";
              this.toggleAttribute("hasmultipletabs", false);
            }
          }
          for (const mutation of mutations) {
            for (const addedNode of mutation.addedNodes) {
              if (addedNode.tagName == "tab") {
                this.#updateTabAriaHidden(addedNode);
              }
            }
            for (const removedNode of mutation.removedNodes) {
              if (removedNode.tagName == "tab") {
                this.#updateTabAriaHidden(removedNode);
              }
            }
          }
        });
      }
      this.#tabChangeObserver.observe(this, { childList: true });
    }

    get color() {
      return this.#colorCode;
    }

    set color(code) {
      let diff = code !== this.#colorCode;
      this.#colorCode = code;
      this.style.setProperty(
        "--tab-group-color",
        `var(--tab-group-color-${code})`
      );
      this.style.setProperty(
        "--tab-group-color-invert",
        `var(--tab-group-color-${code}-invert)`
      );
      this.style.setProperty(
        "--tab-group-color-pale",
        `var(--tab-group-color-${code}-pale)`
      );
      if (diff) {
        this.dispatchEvent(
          new CustomEvent("TabGroupUpdate", { bubbles: true })
        );
      }
    }

    get defaultGroupName() {
      if (!this.#defaultGroupName) {
        this.#defaultGroupName = gBrowser.tabLocalization.formatValueSync(
          "tab-group-name-default"
        );
      }
      return this.#defaultGroupName;
    }

    get id() {
      return this.getAttribute("id");
    }

    set id(val) {
      this.setAttribute("id", val);
    }

    /**
     * @returns {boolean}
     */
    get hasActiveTab() {
      return this.hasAttribute("hasactivetab");
    }

    /**
     * @param {boolean} val
     */
    set hasActiveTab(val) {
      this.toggleAttribute("hasactivetab", val);
    }

    get label() {
      return this.#label;
    }

    set label(val) {
      let diff = val !== this.#label;
      this.#label = val;

      // If the group name is empty, use a zero width space so we
      // always create a text node and get consistent layout.
      this.setAttribute("label", val || "\u200b");
      this.#updateLabelAriaAttributes();
      this.#updateTooltip();
      if (diff) {
        this.dispatchEvent(
          new CustomEvent("TabGroupUpdate", { bubbles: true })
        );
      }
    }

    // alias for label
    get name() {
      return this.label;
    }

    set name(newName) {
      this.label = newName;
    }

    get collapsed() {
      return this.hasAttribute("collapsed");
    }

    set collapsed(val) {
      if (!!val == this.collapsed) {
        return;
      }
      if (val) {
        for (let tab of this.tabs) {
          // Unlock tab sizes.
          tab.style.maxWidth = "";
        }
      }
      this.toggleAttribute("collapsed", val);
      this.#updateCollapsedAriaAttributes();
      this.#updateTooltip();
      for (const tab of this.tabs) {
        this.#updateTabAriaHidden(tab);
      }
      gBrowser.tabContainer.previewPanel?.deactivate(this, { force: true });
      const eventName = val ? "TabGroupCollapse" : "TabGroupExpand";
      this.dispatchEvent(new CustomEvent(eventName, { bubbles: true }));
    }

    #lastAddedTo = 0;
    get lastSeenActive() {
      return Math.max(
        this.#lastAddedTo,
        ...this.tabs.map(t => t.lastSeenActive)
      );
    }

    async #updateLabelAriaAttributes() {
      let tabGroupName = this.#label || this.defaultGroupName;

      let tabGroupDescription = await gBrowser.tabLocalization.formatValue(
        "tab-group-description",
        {
          tabGroupName,
        }
      );
      this.#labelElement?.setAttribute("aria-label", tabGroupName);
      this.#labelElement?.setAttribute("aria-description", tabGroupDescription);
      this.#labelElement?.setAttribute("aria-level", 1);
    }

    #updateCollapsedAriaAttributes() {
      const ariaExpanded = this.collapsed ? "false" : "true";
      this.#labelElement?.setAttribute("aria-expanded", ariaExpanded);
    }

    async #updateTooltip() {
      let tabGroupName = this.#label || this.defaultGroupName;
      let tooltipKey = this.collapsed
        ? "tab-group-label-tooltip-collapsed"
        : "tab-group-label-tooltip-expanded";
      await gBrowser.tabLocalization
        .formatValue(tooltipKey, {
          tabGroupName,
        })
        .then(result => {
          this.dataset.tooltip = result;
        });
    }

    /**
     * @param {MozTabbrowserTab} tab
     */
    #updateTabAriaHidden(tab) {
      if (tab.group?.collapsed && !tab.selected) {
        tab.setAttribute("aria-hidden", "true");
      } else {
        tab.removeAttribute("aria-hidden");
      }
    }

    /**
     * @returns {MozTabbrowserTab[]}
     */
    get tabs() {
      return Array.from(this.children).filter(node => node.matches("tab"));
    }

    /**
     * @param {MozTabbrowserTab} tab
     * @returns {boolean}
     */
    isTabVisibleInGroup(tab) {
      if (this.isBeingDragged) {
        return false;
      }
      if (this.collapsed && !tab.selected) {
        return false;
      }
      return true;
    }

    /**
     * @returns {MozTextLabel}
     */
    get labelElement() {
      return this.#labelElement;
    }

    get overflowCountLabel() {
      return this.#overflowCountLabel;
    }

    /**
     * @param {boolean} value
     */
    set wasCreatedByAdoption(value) {
      this.#wasCreatedByAdoption = value;
    }

    /**
     * @returns {boolean}
     */
    get isBeingDragged() {
      return this.hasAttribute("movingtabgroup");
    }

    /**
     * @param {boolean} val
     */
    set isBeingDragged(val) {
      this.toggleAttribute("movingtabgroup", val);
    }

    /**
     * add tabs to the group
     *
     * @param {MozTabbrowserTab[]} tabs
     * @param {TabMetricsContext} [metricsContext]
     *   Optional context to record for metrics purposes.
     */
    addTabs(tabs, metricsContext) {
      for (let tab of tabs) {
        if (tab.pinned) {
          tab.ownerGlobal.gBrowser.unpinTab(tab);
        }
        let tabToMove =
          this.ownerGlobal === tab.ownerGlobal
            ? tab
            : gBrowser.adoptTab(tab, {
                tabIndex: gBrowser.tabs.at(-1)._tPos + 1,
                selectTab: tab.selected,
              });
        gBrowser.moveTabToGroup(tabToMove, this, metricsContext);
      }
      this.#lastAddedTo = Date.now();
    }

    /**
     * Remove all tabs from the group and delete the group.
     * @param {TabMetricsContext} [metricsContext]
     */
    ungroupTabs(
      metricsContext = {
        isUserTriggered: false,
        telemetrySource: TabMetrics.METRIC_SOURCE.UNKNOWN,
      }
    ) {
      this.dispatchEvent(
        new CustomEvent("TabGroupUngroup", {
          bubbles: true,
          detail: metricsContext,
        })
      );
      for (let i = this.tabs.length - 1; i >= 0; i--) {
        gBrowser.ungroupTab(this.tabs[i]);
      }
    }

    /**
     * Save group data to session store.
     *
     * @param {object} [options]
     * @param {boolean} [options.isUserTriggered]
     *   Whether or not the save operation was explicitly called by the user.
     *   Used for telemetry. Default is false.
     */
    save({ isUserTriggered = false } = {}) {
      SessionStore.addSavedTabGroup(this);
      this.dispatchEvent(
        new CustomEvent("TabGroupSaved", {
          bubbles: true,
          detail: { isUserTriggered },
        })
      );
    }

    saveAndClose({ isUserTriggered } = {}) {
      this.save({ isUserTriggered });
      gBrowser.removeTabGroup(this);
    }

    /**
     * @param {PointerEvent} event
     */
    on_click(event) {
      let isToggleElement =
        event.target === this.#labelElement ||
        event.target === this.#overflowCountLabel;
      if (isToggleElement && event.button === 0) {
        event.preventDefault();
        this.collapsed = !this.collapsed;
        gBrowser.tabGroupMenu.close();

        /** @type {GleanCounter} */
        let interactionMetric = this.collapsed
          ? Glean.tabgroup.groupInteractions.collapse
          : Glean.tabgroup.groupInteractions.expand;
        interactionMetric.add(1);
      }
    }

    /**
     * @param {CustomEvent} event
     */
    on_mouseover(event) {
      if (event.target === this.#labelElement) {
        this.#labelElement.dispatchEvent(
          new CustomEvent("TabGroupLabelHoverStart", { bubbles: true })
        );
      }
    }

    /**
     * @param {CustomEvent} event
     */
    on_mouseout(event) {
      if (event.target === this.#labelElement) {
        this.#labelElement.dispatchEvent(
          new CustomEvent("TabGroupLabelHoverEnd", { bubbles: true })
        );
      }
    }

    /**
     * @param {CustomEvent} event
     */
    on_TabSelect(event) {
      const { previousTab } = event.detail;
      this.hasActiveTab = event.target.group === this;
      if (this.hasActiveTab) {
        this.#updateTabAriaHidden(event.target);
      }
      if (previousTab.group === this) {
        this.#updateTabAriaHidden(previousTab);
      }
    }

    /**
     * If one of this group's tabs is the selected tab, this will do nothing.
     * Otherwise, it will expand the group if collapsed, and select the first
     * tab in its list.
     */
    select() {
      this.collapsed = false;
      if (gBrowser.selectedTab.group == this) {
        return;
      }
      gBrowser.selectedTab = this.tabs[0];
    }
  }

  customElements.define("tab-group", MozTabbrowserTabGroup);
}
