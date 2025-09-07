/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  when,
  ifDefined,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

class LoginLine extends MozLitElement {
  static properties = {
    value: { type: String },
    labelL10nId: { type: String },
    lineType: { type: String },
    inputType: { type: String },
    favIcon: { type: String },
    alert: { type: Boolean },
    onLineClick: { type: Function },
  };

  #copyTimeoutID;

  static get queries() {
    return {
      lineContainer: ".line-container",
    };
  }

  static lineTypeIdMap = {
    password: "contextual-manager-check-icon-password",
    username: "contextual-manager-check-icon-username",
  };

  #canCopy() {
    return this.lineType !== "origin";
  }

  #addCopyAttr() {
    return ifDefined(this.#canCopy() ? "data-after" : undefined);
  }

  #handleCopyAnimation() {
    if (!this.lineContainer.classList.contains("copied")) {
      this.lineContainer.classList.add("copied");
      this.#copyTimeoutID = setTimeout(() => {
        this.lineContainer.classList.remove("copied");
        this.#copyTimeoutID = null;
      }, 4000);
    }
  }

  async #onClick() {
    const isAuthorized = await this.onLineClick();
    if (!isAuthorized || !this.#canCopy()) {
      return;
    }
    this.#handleCopyAnimation();
  }

  connectedCallback() {
    super.connectedCallback();

    this.addEventListener("click", this.#onClick);
    this.addEventListener("keypress", async e => {
      if (e.code === "Enter" || e.code === "Space") {
        e.preventDefault();
        await this.#onClick();
      }
    });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (this.#copyTimeoutID) {
      clearTimeout(this.#copyTimeoutID);
    }
  }

  constructor() {
    super();
    this.favIcon = "";
    this.#copyTimeoutID = null;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/megalist/components/login-line/login-line.css"
      />
      <div class="line-container">
        <div class="input-container">
          <div class="label-container">
            <label
              data-l10n-id=${this.labelL10nId}
              data-l10n-attrs=${this.#addCopyAttr()}
              class="input-label"
              for="login-line-input"
            ></label>
            ${when(
              this.alert,
              () =>
                html` <img
                  data-l10n-id="contextual-manager-alert-icon"
                  class="alert-icon"
                  src="chrome://global/skin/icons/warning-fill-12.svg"
                />`
            )}
          </div>
          <div class="value-container">
            ${when(
              this.favIcon,
              () =>
                html` <img
                  data-l10n-id="contextual-manager-website-icon"
                  class="fav-icon"
                  src=${this.favIcon}
                />`
            )}
            <input
              class="input-field"
              id="login-line-input"
              .value=${this.value}
              type=${this.inputType}
              readonly
            />
          </div>
        </div>
        ${when(this.#canCopy(), () => {
          return html`
            <div class="copy-container">
              <img
                data-l10n-id="contextual-manager-copy-icon"
                aria-labelledby="contextual-manager-copy-icon"
                class="copy-icon"
                src="chrome://global/skin/icons/edit-copy.svg"
              />
              <img
                data-l10n-id=${ifDefined(
                  LoginLine.lineTypeIdMap[this.lineType]
                )}
                class="check-icon"
                src="chrome://global/skin/icons/check-filled.svg"
              />
            </div>
          `;
        })}
      </div>
    `;
  }
}

class ConcealedLoginLine extends MozLitElement {
  static properties = {
    value: { type: String },
    labelL10nId: { type: String },
    alert: { type: Boolean },
    visible: { type: Boolean },
    onLineClick: { type: Function },
    onButtonClick: { type: Function },
  };

  static CONCEALED_VALUE_TEXT = " ".repeat(8);

  static get queries() {
    return {
      loginLine: "login-line",
      revealBtn: "moz-button",
    };
  }

  get #inputType() {
    return !this.visible ? "password" : "text";
  }

  get #displayValue() {
    return !this.visible ? ConcealedLoginLine.CONCEALED_VALUE_TEXT : this.value;
  }

  get #revealBtnLabel() {
    return !this.visible
      ? "contextual-manager-show-password-button"
      : "contextual-manager-hide-password-button";
  }

  #revealIconSrc() {
    return this.visible
      ? "chrome://global/skin/icons/eye-slash.svg"
      : "chrome://global/skin/icons/eye.svg";
  }

  async #onRevealButtonClick() {
    const isAuthorized = await this.onButtonClick();
    if (!isAuthorized) {
      return;
    }

    const l10nAriaId = this.#revealBtnLabel;
    this.revealBtn.setAttribute("data-l10n-id", l10nAriaId);
    this.revealBtn.setAttribute("aria-labelledby", l10nAriaId);
  }

  render() {
    const dataL10nId = this.alert
      ? "contextual-manager-password-login-line-with-alert"
      : "contextual-manager-password-login-line";
    const l10nAriaId = this.#revealBtnLabel;
    return html` <link
        rel="stylesheet"
        href="chrome://global/content/megalist/components/login-line/login-line.css"
      />
      <login-line
        role="option"
        tabindex="-1"
        data-l10n-id=${dataL10nId}
        lineType="password"
        inputType=${this.#inputType}
        labelL10nId=${this.labelL10nId}
        .value=${this.#displayValue}
        ?alert=${this.alert}
        .onLineClick=${this.onLineClick}
        }
      >
      </login-line>
      <div class="reveal-button-container">
        <moz-button
          role="option"
          class="reveal-button"
          type="icon ghost"
          tabindex="-1"
          data-l10n-id=${l10nAriaId}
          aria-labelledby=${l10nAriaId}
          iconSrc=${this.#revealIconSrc()}
          @keypress=${async e => {
            if (e.code === "Enter") {
              await this.#onRevealButtonClick();
            }
          }}
          @click=${this.#onRevealButtonClick}
        ></moz-button>
      </div>`;
  }
}

customElements.define("login-line", LoginLine);
customElements.define("concealed-login-line", ConcealedLoginLine);
