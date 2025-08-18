import { mount } from "enzyme";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { AdBanner } from "content-src/components/DiscoveryStreamComponents/AdBanner/AdBanner";
import { combineReducers, createStore } from "redux";
import { Provider } from "react-redux";
import { actionCreators as ac } from "common/Actions.mjs";
import React from "react";

const DEFAULT_PROPS = {
  prefs: {
    "discoverystream.sections.enabled": false,
  },
  type: "foo",
  firstVisibleTimestamp: new Date("March 21, 2024 10:11:12").getTime(),
  row: 3,
  spoc: {
    format: "billboard",
    id: "12345",
    raw_image_src: "https://picsum.photos/200",
    alt_text: "alt-text",
    url: "https://example.com",
    shim: {
      url: "https://fallback.example.com",
    },
  },
};

// Wrap this around any component that uses useSelector,
// or any mount that uses a child that uses redux.
function WrapWithProvider({ children, state = INITIAL_STATE }) {
  let store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

describe("Discovery Stream <AdBanner>", () => {
  let wrapper;
  let sandbox;
  let dispatch;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    dispatch = sandbox.stub();

    wrapper = mount(
      <WrapWithProvider>
        <AdBanner dispatch={dispatch} {...DEFAULT_PROPS} />
      </WrapWithProvider>
    );
  });

  afterEach(() => {
    sandbox.restore();
  });

  it("should render", () => {
    assert.ok(wrapper.exists());
    assert.ok(wrapper.find(".ad-banner-wrapper").exists());
  });

  it("should have 'sponsored' text visible", () => {
    assert.ok(wrapper.find(".ad-banner-sponsored").exists());
    assert.ok(
      wrapper.find("[data-l10n-id='newtab-label-sponsored-fixed']").exists()
    );
  });

  it("should load API image for the billboard banner with correct dimensions", () => {
    const image = wrapper.find(".ad-banner-content img");
    assert.ok(image.exists());
    assert.equal(image.prop("src"), DEFAULT_PROPS.spoc.raw_image_src);
    assert.equal(image.prop("width"), "970");
    assert.equal(image.prop("height"), "250");
  });

  it("should add alt text from spoc.alt_text property", () => {
    const image = wrapper.find(".ad-banner-content img");
    assert.equal(image.prop("alt"), DEFAULT_PROPS.spoc.alt_text);
  });

  it("should load API image for the leaderboard banner with correct dimensions", () => {
    wrapper.setProps({
      children: (
        <AdBanner
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          spoc={{ ...DEFAULT_PROPS.spoc, format: "leaderboard" }}
        />
      ),
    });
    const image = wrapper.find(".ad-banner-content img");
    assert.ok(image.exists());
    assert.equal(image.prop("src"), DEFAULT_PROPS.spoc.raw_image_src);
    assert.equal(image.prop("width"), "728");
    assert.equal(image.prop("height"), "90");
  });

  it("should set image dimensions to undefined if format is not specified", () => {
    wrapper.setProps({
      children: (
        <AdBanner
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          spoc={{ ...DEFAULT_PROPS.spoc, format: undefined }}
        />
      ),
    });

    const image = wrapper.find(".ad-banner-content img");
    assert.ok(image.exists());
    assert.equal(image.prop("width"), undefined);
    assert.equal(image.prop("height"), undefined);
  });

  it("should pass row prop as `gridRow` to aside element", () => {
    const aside = wrapper.find(".ad-banner-wrapper");
    const clampedRow = Math.max(1, Math.min(9, DEFAULT_PROPS.row));
    assert.deepEqual(aside.prop("style"), { gridRow: clampedRow });
  });

  it("should have the context menu button be visible", () => {
    const dismiss = wrapper.find("moz-button");
    assert.ok(dismiss.exists());

    // The rest of the context menu functionality is now tested in
    // AdBannerContextMenu.test.jsx
  });

  it("should render data-is-sponsored-link='true' on banner link", () => {
    const link = wrapper.find(".ad-banner-link a");
    assert.equal(link.prop("data-is-sponsored-link"), true);
  });

  it("should call onLinkClick when banner is clicked", () => {
    const link = wrapper.find(".ad-banner-link a");
    link.simulate("click");
    assert.calledThrice(dispatch);
    const secondCall = dispatch.getCall(2);
    assert.deepEqual(
      secondCall.args[0],
      ac.DiscoveryStreamUserEvent({
        event: "CLICK",
        source: "FOO",
        // Banner ads don't have a position, but a row number
        action_position: DEFAULT_PROPS.row,
        value: {
          card_type: "spoc",
          tile_id: DEFAULT_PROPS.spoc.id,

          fetchTimestamp: DEFAULT_PROPS.spoc.fetchTimestamp,
          firstVisibleTimestamp: DEFAULT_PROPS.firstVisibleTimestamp,
          format: DEFAULT_PROPS.spoc.format,
        },
      })
    );
  });

  it("should set proper ohttp src if ohttp, contextual, inferred, and sections are true", () => {
    wrapper.setProps({
      children: (
        <AdBanner
          dispatch={dispatch}
          {...DEFAULT_PROPS}
          prefs={{
            ...DEFAULT_PROPS.prefs,
            "discoverystream.sections.enabled": true,
            "unifiedAds.ohttp.enabled": true,
            ohttpImagesConfig: { enabled: true },
            "discoverystream.sections.contextualAds.enabled": true,
            "discoverystream.sections.personalization.inferred.user.enabled": true,
            "discoverystream.sections.personalization.inferred.enabled": true,
          }}
        />
      ),
    });

    const image = wrapper.find(".ad-banner-content img");
    assert.equal(
      image.prop("src"),
      `moz-cached-ohttp://newtab-image/?url=${encodeURIComponent(DEFAULT_PROPS.spoc.raw_image_src)}`
    );
  });
});
