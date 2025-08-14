/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ContentSection } from "content-src/components/CustomizeMenu/ContentSection/ContentSection";
import { connect } from "react-redux";
import React from "react";
// eslint-disable-next-line no-shadow
import { CSSTransition } from "react-transition-group";

export class _CustomizeMenu extends React.PureComponent {
  constructor(props) {
    super(props);
    this.onEntered = this.onEntered.bind(this);
    this.onExited = this.onExited.bind(this);
    this.state = {
      exitEventFired: false,
    };
  }

  onEntered() {
    this.setState({ exitEventFired: false });
    if (this.closeButton) {
      this.closeButton.focus();
    }
  }

  onExited() {
    this.setState({ exitEventFired: true });
    if (this.openButton) {
      this.openButton.focus();
    }
  }

  render() {
    return (
      <span>
        <CSSTransition
          timeout={300}
          classNames="personalize-animate"
          in={!this.props.showing}
          appear={true}
        >
          <button
            className="personalize-button"
            data-l10n-id="newtab-customize-panel-icon-button"
            onClick={() => this.props.onOpen()}
            onKeyDown={e => {
              if (e.key === "Enter") {
                this.props.onOpen();
              }
            }}
            ref={c => (this.openButton = c)}
          >
            <div>
              <img
                role="presentation"
                src="chrome://global/skin/icons/edit-outline.svg"
              />
            </div>
            <label data-l10n-id="newtab-customize-panel-icon-button-label" />
          </button>
        </CSSTransition>
        <CSSTransition
          timeout={250}
          classNames="customize-animate"
          in={this.props.showing}
          onEntered={this.onEntered}
          onExited={this.onExited}
          appear={true}
        >
          <div
            className="customize-menu"
            role="dialog"
            data-l10n-id="newtab-settings-dialog-label"
          >
            <div className="close-button-wrapper">
              <moz-button
                onClick={() => this.props.onClose()}
                id="close-button"
                type="icon ghost"
                data-l10n-id="newtab-custom-close-menu-button"
                iconsrc="chrome://global/skin/icons/close.svg"
                ref={c => (this.closeButton = c)}
              ></moz-button>
            </div>
            <ContentSection
              openPreferences={this.props.openPreferences}
              setPref={this.props.setPref}
              enabledSections={this.props.enabledSections}
              enabledWidgets={this.props.enabledWidgets}
              wallpapersEnabled={this.props.wallpapersEnabled}
              activeWallpaper={this.props.activeWallpaper}
              pocketRegion={this.props.pocketRegion}
              mayHaveTopicSections={this.props.mayHaveTopicSections}
              mayHaveInferredPersonalization={
                this.props.mayHaveInferredPersonalization
              }
              mayHaveRecentSaves={this.props.DiscoveryStream.recentSavesEnabled}
              mayHaveWeather={this.props.mayHaveWeather}
              mayHaveTrendingSearch={this.props.mayHaveTrendingSearch}
              mayHaveWidgets={this.props.mayHaveWidgets}
              mayHaveTimerWidget={this.props.mayHaveTimerWidget}
              mayHaveListsWidget={this.props.mayHaveListsWidget}
              dispatch={this.props.dispatch}
              exitEventFired={this.state.exitEventFired}
            />
          </div>
        </CSSTransition>
      </span>
    );
  }
}

export const CustomizeMenu = connect(state => ({
  DiscoveryStream: state.DiscoveryStream,
}))(_CustomizeMenu);
