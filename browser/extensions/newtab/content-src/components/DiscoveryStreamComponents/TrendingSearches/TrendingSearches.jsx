/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
import React, { useState, useRef, useCallback } from "react";
import { useDispatch, useSelector } from "react-redux";
import { SafeAnchor } from "../SafeAnchor/SafeAnchor";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { LinkMenu } from "../../LinkMenu/LinkMenu";
import { useIntersectionObserver } from "../../../lib/utils";

const PREF_TRENDING_VARIANT = "trendingSearch.variant";
const PREF_REFINED_CARDS_LAYOUT = "discoverystream.refinedCardsLayout.enabled";

function TrendingSearches() {
  const [showContextMenu, setShowContextMenu] = useState(false);
  // The keyboard access parameter is passed down to LinkMenu component
  // that uses it to focus on the first context menu option for accessibility.
  const [isKeyboardAccess, setIsKeyboardAccess] = useState(false);
  const dispatch = useDispatch();
  const { TrendingSearch, Prefs } = useSelector(state => state);
  const { values: prefs } = Prefs;
  const { suggestions, collapsed } = TrendingSearch;
  const variant = prefs[PREF_TRENDING_VARIANT];
  const refinedCards = prefs[PREF_REFINED_CARDS_LAYOUT];
  let resultRef = useRef([]);
  let contextMenuHost = useRef(null);

  const TRENDING_SEARCH_CONTEXT_MENU_OPTIONS = [
    "TrendingSearchDismiss",
    "TrendingSearchLearnMore",
  ];

  function onArrowClick() {
    dispatch(
      ac.AlsoToMain({
        type: at.TRENDING_SEARCH_TOGGLE_COLLAPSE,
        data: {
          collapsed: !collapsed,
          variant,
        },
      })
    );
  }

  function handleLinkOpen() {
    dispatch(
      ac.AlsoToMain({
        type: at.TRENDING_SEARCH_SUGGESTION_OPEN,
        data: {
          variant,
        },
      })
    );
  }

  // If the window is small, the context menu in variant B will move closer to the card
  // so that it doesn't cut off
  const handleContextMenuShow = () => {
    const host = contextMenuHost.current;
    const isRTL = document.dir === "rtl"; // returns true if page language is right-to-left
    const checkRect = host.getBoundingClientRect();
    const maxBounds = 200;

    // Adds the class of "last-item" if the card is near the edge of the window
    const checkBounds = isRTL
      ? checkRect.left <= maxBounds
      : window.innerWidth - checkRect.right <= maxBounds;

    if (checkBounds) {
      host.classList.add("last-item");
    }
  };

  const handleContextMenuUpdate = () => {
    const host = contextMenuHost.current;
    if (!host) {
      return;
    }

    host.classList.remove("last-item");
  };

  const toggleContextMenu = isKeyBoard => {
    setShowContextMenu(!showContextMenu);
    setIsKeyboardAccess(isKeyBoard);

    if (!showContextMenu) {
      handleContextMenuShow();
    } else {
      handleContextMenuUpdate();
    }
  };

  function onContextMenuClick(e) {
    e.preventDefault();
    toggleContextMenu(false);
  }

  function onContextMenuKeyDown(e) {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      toggleContextMenu(true);
    }
  }

  function onUpdate() {
    setShowContextMenu(!showContextMenu);
  }

  function handleResultKeyDown(event, index) {
    const maxResults = suggestions.length;
    let nextIndex = index;

    if (event.key === "ArrowDown") {
      event.preventDefault();
      if (index < maxResults - 1) {
        nextIndex = index + 1;
      } else {
        return;
      }
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      if (index > 0) {
        nextIndex = index - 1;
      } else {
        return;
      }
    }

    resultRef.current[index].tabIndex = -1;
    resultRef.current[nextIndex].tabIndex = 0;
    resultRef.current[nextIndex].focus();
  }

  const handleIntersection = useCallback(() => {
    dispatch(
      ac.AlsoToMain({
        type: at.TRENDING_SEARCH_IMPRESSION,
        data: {
          variant,
        },
      })
    );
  }, [dispatch, variant]);

  const ref = useIntersectionObserver(handleIntersection);
  if (!suggestions?.length) {
    return null;
  } else if (variant === "a" || variant === "c") {
    return (
      <section
        ref={el => {
          ref.current = [el];
        }}
        // Variant C matches the design of variant A but should only
        // appear on hover
        className={`trending-searches-pill-wrapper ${variant === "c" ? "hover-only" : ""}`}
      >
        <div className="trending-searches-title-wrapper">
          <span className="trending-searches-icon icon icon-arrow-trending"></span>
          <h2
            className="trending-searches-title"
            data-l10n-id="newtab-trending-searches-title"
          ></h2>
          <div className="close-open-trending-searches">
            <moz-button
              iconsrc={`chrome://global/skin/icons/arrow-${collapsed ? "down" : "up"}.svg`}
              onClick={onArrowClick}
              className={`icon icon-arrowhead-up`}
              type="icon ghost"
              data-l10n-id={`newtab-trending-searches-${collapsed ? "show" : "hide"}-trending`}
            ></moz-button>
          </div>
        </div>
        {!collapsed && (
          <ul className="trending-searches-list">
            {suggestions.map((result, index) => {
              return (
                <li
                  key={result.suggestion}
                  className="trending-search-item"
                  onKeyDown={e => handleResultKeyDown(e, index)}
                >
                  <SafeAnchor
                    url={result.searchUrl}
                    onLinkClick={handleLinkOpen}
                    title={result.suggestion}
                    setRef={item => (resultRef.current[index] = item)}
                    tabIndex={index === 0 ? 0 : -1}
                  >
                    {result.lowerCaseSuggestion}
                  </SafeAnchor>
                </li>
              );
            })}
          </ul>
        )}
      </section>
    );
  } else if (variant === "b") {
    return (
      <div
        ref={el => {
          ref.current = [el];
          contextMenuHost.current = el;
        }}
        className="trending-searches-list-view"
      >
        <div className="trending-searches-list-view-header">
          <h3 data-l10n-id="newtab-trending-searches-title"></h3>
          <div className="trending-searches-context-menu-wrapper">
            <div
              className={`trending-searches-context-menu ${showContextMenu ? "context-menu-open" : ""}`}
            >
              <moz-button
                type="icon ghost"
                size="default"
                data-l10n-id="newtab-menu-section-tooltip"
                iconsrc="chrome://global/skin/icons/more.svg"
                onClick={onContextMenuClick}
                onKeyDown={onContextMenuKeyDown}
              />
              {showContextMenu && (
                <LinkMenu
                  onUpdate={onUpdate}
                  dispatch={dispatch}
                  keyboardAccess={isKeyboardAccess}
                  options={TRENDING_SEARCH_CONTEXT_MENU_OPTIONS}
                  shouldSendImpressionStats={true}
                  site={{
                    url: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/trending-searches-new-tab",
                    variant,
                  }}
                />
              )}
            </div>
          </div>
        </div>
        <ul className="trending-searches-list-items">
          {suggestions.slice(0, 6).map((result, index) => {
            return (
              <li
                key={result.suggestion}
                className={`trending-searches-list-item ${refinedCards ? "compact" : ""}`}
                onKeyDown={e => handleResultKeyDown(e, index)}
              >
                <SafeAnchor
                  url={result.searchUrl}
                  onLinkClick={handleLinkOpen}
                  title={result.suggestion}
                  setRef={item => (resultRef.current[index] = item)}
                  tabIndex={index === 0 ? 0 : -1}
                >
                  {result.icon ? (
                    <div className="trending-icon-wrapper">
                      <img src={result.icon} alt="" className="trending-icon" />
                      <div className="trending-info-wrapper">
                        {result.lowerCaseSuggestion}
                        <small>{result.description}</small>
                      </div>
                    </div>
                  ) : (
                    <>
                      <span className="trending-searches-icon icon icon-arrow-trending"></span>
                      {result.lowerCaseSuggestion}
                    </>
                  )}
                </SafeAnchor>
              </li>
            );
          })}
        </ul>
      </div>
    );
  }
}

export { TrendingSearches };
