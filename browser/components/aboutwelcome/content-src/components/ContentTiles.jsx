/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useState, useEffect } from "react";
import { Localized } from "./MSLocalized";
import { AddonsPicker } from "./AddonsPicker";
import { SingleSelect } from "./SingleSelect";
import { MobileDownloads } from "./MobileDownloads";
import { MultiSelect } from "./MultiSelect";
import { EmbeddedMigrationWizard } from "./EmbeddedMigrationWizard";
import { ActionChecklist } from "./ActionChecklist";
import { EmbeddedBrowser } from "./EmbeddedBrowser";
import { AboutWelcomeUtils } from "../lib/aboutwelcome-utils.mjs";

const HEADER_STYLES = [
  "backgroundColor",
  "border",
  "padding",
  "margin",
  "width",
  "height",
];

const TILE_STYLES = [
  "marginBlock",
  "marginInline",
  "paddingBlock",
  "paddingInline",
];

const CONTAINER_STYLES = [
  "padding",
  "margin",
  "marginBlock",
  "marginInline",
  "paddingBlock",
  "paddingInline",
  "flexDirection",
  "flexWrap",
  "flexFlow",
  "flexGrow",
  "flexShrink",
  "justifyContent",
  "alignItems",
  "gap",
];

export const ContentTiles = props => {
  const { content } = props;
  const [expandedTileIndex, setExpandedTileIndex] = useState(null);
  // State for header that toggles showing and hiding all tiles, if applicable
  const [tilesHeaderExpanded, setTilesHeaderExpanded] = useState(false);
  const { tiles } = content;
  if (!tiles) {
    return null;
  }

  // eslint-disable-next-line react-hooks/rules-of-hooks
  useEffect(() => {
    // Run once when ContentTiles mounts to prefill activeMultiSelect
    if (!props.activeMultiSelect) {
      const tilesArray = Array.isArray(tiles) ? tiles : [tiles];

      tilesArray.forEach((tile, index) => {
        if (tile.type !== "multiselect" || !tile.data) {
          return;
        }

        const multiSelectId = `tile-${index}`;
        const newActiveMultiSelect = [];

        tile.data.forEach(({ id, defaultValue }) => {
          if (defaultValue && id) {
            newActiveMultiSelect.push(id);
          }
        });

        if (newActiveMultiSelect.length) {
          props.setActiveMultiSelect(newActiveMultiSelect, multiSelectId);
        }
      });
    }
  }, [tiles]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    /**
     * In Spotlight dialogs, the VO cursor can move without changing DOM focus.
     * When a user lands on content tiles, or a tile re-renders, DOM focus often
     * stays on or "snaps back" to the dialog’s first tabbable control by
     * SubDialog’s focus enforcement. Pressing Space/Enter then activates that
     * outside control instead of the VO target.
     *
     * To address this, we remember the last real DOM-focused element inside
     * #content-tiles-container. If focus jumps outside tiles without a recent
     * tab, such as with a VO focus move, restore focus to that element on the
     * next rAF. Tab navigation is unaffected.
     */
    const page =
      document.querySelector(
        "#multi-stage-message-root.onboardingContainer[data-page]"
      )?.dataset.page || document.location.href;

    if (page !== "spotlight") {
      return () => {};
    }

    const tilesEl = document.getElementById("content-tiles-container");
    const dialog = tilesEl?.closest('main[role="alertdialog"]') || null;
    if (!tilesEl || !dialog) {
      return () => {};
    }

    // We use 250ms to tell “intentional tab navigation” from a programmatic
    // snap. It’s long enough to cover a human Tab press (and a quick double-tab
    // / key repeat), but short enough that we don’t delay correcting unintended
    // focus jumps.
    const TAB_GRACE_WINDOW_MS = 250;
    let lastTilesEl = null;
    let lastTabAt = 0;
    let restoring = false;

    function onKeyDown(e) {
      if (e.key === "Tab") {
        lastTabAt = performance.now();
      }
    }

    function onFocusIn(event) {
      const { target } = event;

      // Track true DOM focus inside tiles.
      if (tilesEl.contains(target)) {
        lastTilesEl = target;
        return;
      }

      // If focus left tiles without a recent tab, treat as a programmatic snap.
      const tabRecently = performance.now() - lastTabAt < TAB_GRACE_WINDOW_MS;
      if (
        tabRecently ||
        !lastTilesEl ||
        !document.contains(lastTilesEl) ||
        restoring
      ) {
        return;
      }

      // Restore immediately (before paint) to avoid visible flicker.
      restoring = true;
      try {
        lastTilesEl.focus({ preventScroll: true });
      } finally {
        restoring = false;
      }
    }

    // Preempt other dialog handlers.
    dialog.addEventListener("keydown", onKeyDown, true);
    dialog.addEventListener("focusin", onFocusIn, true);

    return () => {
      dialog.removeEventListener("keydown", onKeyDown, true);
      dialog.removeEventListener("focusin", onFocusIn, true);
    };
  }, []);

  const toggleTile = (index, tile) => {
    const tileId = `${tile.type}${tile.id ? "_" : ""}${tile.id ?? ""}_header`;
    setExpandedTileIndex(prevIndex => (prevIndex === index ? null : index));
    AboutWelcomeUtils.sendActionTelemetry(props.messageId, tileId);
  };

  const toggleTiles = () => {
    setTilesHeaderExpanded(prev => !prev);
    AboutWelcomeUtils.sendActionTelemetry(
      props.messageId,
      "content_tiles_header"
    );
  };

  function getTileMultiSelects(screenMultiSelects, index) {
    return screenMultiSelects?.[`tile-${index}`];
  }

  function getTileActiveMultiSelect(activeMultiSelect, index) {
    return activeMultiSelect?.[`tile-${index}`];
  }

  const renderContentTile = (tile, index = 0) => {
    const isExpanded = expandedTileIndex === index;
    const { header, title, subtitle } = tile;

    return (
      <div
        key={index}
        className={`content-tile ${header ? "has-header" : ""}`}
        style={AboutWelcomeUtils.getTileStyle(tile, TILE_STYLES)}
      >
        {header?.title && (
          <button
            className="tile-header secondary"
            onClick={() => toggleTile(index, tile)}
            aria-expanded={isExpanded}
            aria-controls={`tile-content-${index}`}
            style={AboutWelcomeUtils.getValidStyle(header.style, HEADER_STYLES)}
          >
            <div className="header-text-container">
              <Localized text={header.title}>
                <span className="header-title" />
              </Localized>
              {header.subtitle && (
                <Localized text={header.subtitle}>
                  <span className="header-subtitle" />
                </Localized>
              )}
            </div>
            <div className="arrow-icon"></div>
          </button>
        )}
        {(title || subtitle) && (
          <div
            className="tile-title-container"
            id={`tile-title-container-${index}`}
          >
            {title && (
              <Localized text={title}>
                {/* H1 content is provided by Localized */}
                {/* eslint-disable-next-line jsx-a11y/heading-has-content */}
                <h1 className="tile-title" id={`content-tile-title-${index}`} />
              </Localized>
            )}

            {subtitle && (
              <Localized text={subtitle}>
                <p className="tile-subtitle" />
              </Localized>
            )}
          </div>
        )}
        {isExpanded || !header ? (
          <div className="tile-content" id={`tile-content-${index}`}>
            {tile.type === "addons-picker" && tile.data && (
              <AddonsPicker
                content={{ tiles: tile }}
                installedAddons={props.installedAddons}
                message_id={props.messageId}
                handleAction={props.handleAction}
                layout={content.position}
              />
            )}
            {["theme", "single-select"].includes(tile.type) && tile.data && (
              <SingleSelect
                content={{ tiles: tile }}
                activeTheme={props.activeTheme}
                handleAction={props.handleAction}
                activeSingleSelectSelections={
                  props.activeSingleSelectSelections
                }
                setActiveSingleSelectSelection={
                  props.setActiveSingleSelectSelection
                }
                singleSelectId={`single-select-${index}`}
              />
            )}
            {tile.type === "mobile_downloads" && tile.data && (
              <MobileDownloads
                data={tile.data}
                handleAction={props.handleAction}
              />
            )}
            {tile.type === "multiselect" && tile.data && (
              <MultiSelect
                content={{ tiles: tile }}
                screenMultiSelects={getTileMultiSelects(
                  props.screenMultiSelects,
                  index
                )}
                setScreenMultiSelects={props.setScreenMultiSelects}
                activeMultiSelect={getTileActiveMultiSelect(
                  props.activeMultiSelect,
                  index
                )}
                setActiveMultiSelect={props.setActiveMultiSelect}
                multiSelectId={`tile-${index}`}
              />
            )}
            {tile.type === "migration-wizard" && (
              <EmbeddedMigrationWizard
                handleAction={props.handleAction}
                content={{ tiles: tile }}
              />
            )}
            {tile.type === "action_checklist" && tile.data && (
              <ActionChecklist content={content} message_id={props.messageId} />
            )}
            {tile.type === "embedded_browser" && tile.data?.url && (
              <EmbeddedBrowser url={tile.data.url} style={tile.data.style} />
            )}
          </div>
        ) : null}
      </div>
    );
  };

  const renderContentTiles = () => {
    if (Array.isArray(tiles)) {
      return (
        <div
          id="content-tiles-container"
          style={AboutWelcomeUtils.getValidStyle(
            content?.contentTilesContainer?.style,
            CONTAINER_STYLES
          )}
        >
          {tiles.map((tile, index) => renderContentTile(tile, index))}
        </div>
      );
    }
    // If tiles is not an array render the tile alone without a container
    return renderContentTile(tiles, 0);
  };

  if (content.tiles_header) {
    return (
      <React.Fragment>
        <button
          className="content-tiles-header secondary"
          onClick={toggleTiles}
          aria-expanded={tilesHeaderExpanded}
          aria-controls={`content-tiles-container`}
        >
          <Localized text={content.tiles_header.title}>
            <span className="header-title" />
          </Localized>
          <div className="arrow-icon"></div>
        </button>
        {tilesHeaderExpanded && renderContentTiles()}
      </React.Fragment>
    );
  }
  return renderContentTiles(tiles);
};
