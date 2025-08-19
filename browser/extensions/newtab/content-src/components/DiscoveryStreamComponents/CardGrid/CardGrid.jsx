/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DSCard, PlaceholderDSCard } from "../DSCard/DSCard.jsx";
import { DSEmptyState } from "../DSEmptyState/DSEmptyState.jsx";
import { TopicsWidget } from "../TopicsWidget/TopicsWidget.jsx";
import { ListFeed } from "../ListFeed/ListFeed.jsx";
import { AdBanner } from "../AdBanner/AdBanner.jsx";
import { FluentOrText } from "../../FluentOrText/FluentOrText.jsx";
import React, { useEffect, useRef } from "react";
import { connect } from "react-redux";
import { TrendingSearches } from "../TrendingSearches/TrendingSearches.jsx";
const PREF_SECTIONS_CARDS_ENABLED = "discoverystream.sections.cards.enabled";
const PREF_THUMBS_UP_DOWN_ENABLED = "discoverystream.thumbsUpDown.enabled";
const PREF_TOPICS_ENABLED = "discoverystream.topicLabels.enabled";
const PREF_TOPICS_SELECTED = "discoverystream.topicSelection.selectedTopics";
const PREF_TOPICS_AVAILABLE = "discoverystream.topicSelection.topics";
const PREF_SPOCS_STARTUPCACHE_ENABLED =
  "discoverystream.spocs.startupCache.enabled";
const PREF_LIST_FEED_ENABLED = "discoverystream.contextualContent.enabled";
const PREF_LIST_FEED_SELECTED_FEED =
  "discoverystream.contextualContent.selectedFeed";
const PREF_FAKESPOT_ENABLED =
  "discoverystream.contextualContent.fakespot.enabled";
const PREF_BILLBOARD_ENABLED = "newtabAdSize.billboard";
const PREF_BILLBOARD_POSITION = "newtabAdSize.billboard.position";
const PREF_LEADERBOARD_ENABLED = "newtabAdSize.leaderboard";
const PREF_LEADERBOARD_POSITION = "newtabAdSize.leaderboard.position";
const PREF_TRENDING_SEARCH = "trendingSearch.enabled";
const PREF_TRENDING_SEARCH_SYSTEM = "system.trendingSearch.enabled";
const PREF_SEARCH_ENGINE = "trendingSearch.defaultSearchEngine";
const PREF_TRENDING_SEARCH_VARIANT = "trendingSearch.variant";
const WIDGET_IDS = {
  TOPICS: 1,
};

export function DSSubHeader({ children }) {
  return (
    <div className="section-top-bar ds-sub-header">
      <h3 className="section-title-container">{children}</h3>
    </div>
  );
}

// eslint-disable-next-line no-shadow
export function IntersectionObserver({
  children,
  windowObj = window,
  onIntersecting,
}) {
  const intersectionElement = useRef(null);

  useEffect(() => {
    let observer;
    if (!observer && onIntersecting && intersectionElement.current) {
      observer = new windowObj.IntersectionObserver(entries => {
        const entry = entries.find(e => e.isIntersecting);

        if (entry) {
          // Stop observing since element has been seen
          if (observer && intersectionElement.current) {
            observer.unobserve(intersectionElement.current);
          }

          onIntersecting();
        }
      });
      observer.observe(intersectionElement.current);
    }
    // Cleanup
    return () => observer?.disconnect();
  }, [windowObj, onIntersecting]);

  return <div ref={intersectionElement}>{children}</div>;
}

export class _CardGrid extends React.PureComponent {
  // eslint-disable-next-line max-statements
  renderCards() {
    const prefs = this.props.Prefs.values;
    const {
      items,
      ctaButtonSponsors,
      ctaButtonVariant,
      spocMessageVariant,
      widgets,
      DiscoveryStream,
    } = this.props;

    const { topicsLoading } = DiscoveryStream;
    const mayHaveSectionsCards = prefs[PREF_SECTIONS_CARDS_ENABLED];
    const mayHaveThumbsUpDown = prefs[PREF_THUMBS_UP_DOWN_ENABLED];
    const showTopics = prefs[PREF_TOPICS_ENABLED];
    const selectedTopics = prefs[PREF_TOPICS_SELECTED];
    const availableTopics = prefs[PREF_TOPICS_AVAILABLE];
    const spocsStartupCacheEnabled = prefs[PREF_SPOCS_STARTUPCACHE_ENABLED];
    const listFeedEnabled = prefs[PREF_LIST_FEED_ENABLED];
    const listFeedSelectedFeed = prefs[PREF_LIST_FEED_SELECTED_FEED];
    const billboardEnabled = prefs[PREF_BILLBOARD_ENABLED];
    const leaderboardEnabled = prefs[PREF_LEADERBOARD_ENABLED];
    const trendingEnabled =
      prefs[PREF_TRENDING_SEARCH] &&
      prefs[PREF_TRENDING_SEARCH_SYSTEM] &&
      prefs[PREF_SEARCH_ENGINE]?.toLowerCase() === "google";
    const trendingVariant = prefs[PREF_TRENDING_SEARCH_VARIANT];

    // filter out recs that should be in ListFeed
    const recs = this.props.data.recommendations
      .filter(item => !item.feedName)
      .slice(0, items);
    const cards = [];

    for (let index = 0; index < items; index++) {
      const rec = recs[index];
      cards.push(
        topicsLoading ||
          !rec ||
          rec.placeholder ||
          (rec.flight_id &&
            !spocsStartupCacheEnabled &&
            this.props.App.isForStartupCache.DiscoveryStream) ? (
          <PlaceholderDSCard key={`dscard-${index}`} />
        ) : (
          <DSCard
            key={`dscard-${rec.id}`}
            pos={rec.pos}
            flightId={rec.flight_id}
            image_src={rec.image_src}
            raw_image_src={rec.raw_image_src}
            icon_src={rec.icon_src}
            word_count={rec.word_count}
            time_to_read={rec.time_to_read}
            title={rec.title}
            topic={rec.topic}
            features={rec.features}
            showTopics={showTopics}
            selectedTopics={selectedTopics}
            excerpt={rec.excerpt}
            availableTopics={availableTopics}
            url={rec.url}
            id={rec.id}
            shim={rec.shim}
            fetchTimestamp={rec.fetchTimestamp}
            type={this.props.type}
            context={rec.context}
            sponsor={rec.sponsor}
            sponsored_by_override={rec.sponsored_by_override}
            dispatch={this.props.dispatch}
            source={rec.domain}
            publisher={rec.publisher}
            pocket_id={rec.pocket_id}
            context_type={rec.context_type}
            bookmarkGuid={rec.bookmarkGuid}
            ctaButtonSponsors={ctaButtonSponsors}
            ctaButtonVariant={ctaButtonVariant}
            spocMessageVariant={spocMessageVariant}
            recommendation_id={rec.recommendation_id}
            firstVisibleTimestamp={this.props.firstVisibleTimestamp}
            mayHaveThumbsUpDown={mayHaveThumbsUpDown}
            mayHaveSectionsCards={mayHaveSectionsCards}
            corpus_item_id={rec.corpus_item_id}
            scheduled_corpus_item_id={rec.scheduled_corpus_item_id}
            recommended_at={rec.recommended_at}
            received_rank={rec.received_rank}
            format={rec.format}
            alt_text={rec.alt_text}
            isTimeSensitive={rec.isTimeSensitive}
          />
        )
      );
    }

    if (widgets?.positions?.length && widgets?.data?.length) {
      let positionIndex = 0;
      const source = "CARDGRID_WIDGET";

      for (const widget of widgets.data) {
        let widgetComponent = null;
        const position = widgets.positions[positionIndex];

        // Stop if we run out of positions to place widgets.
        if (!position) {
          break;
        }

        switch (widget?.type) {
          case "TopicsWidget":
            widgetComponent = (
              <TopicsWidget
                position={position.index}
                dispatch={this.props.dispatch}
                source={source}
                id={WIDGET_IDS.TOPICS}
              />
            );
            break;
        }

        if (widgetComponent) {
          // We found a widget, so up the position for next try.
          positionIndex++;
          // We replace an existing card with the widget.
          cards.splice(position.index, 1, widgetComponent);
        }
      }
    }
    if (listFeedEnabled) {
      const isFakespot = listFeedSelectedFeed === "fakespot";
      const fakespotEnabled = prefs[PREF_FAKESPOT_ENABLED];
      if (!isFakespot || (isFakespot && fakespotEnabled)) {
        // Place the list feed as the 3rd element in the card grid
        cards.splice(
          2,
          1,
          this.renderListFeed(
            this.props.data.recommendations,
            listFeedSelectedFeed
          )
        );
      }
    }
    if (trendingEnabled && trendingVariant === "b") {
      const firstSpocPosition = this.props.spocPositions[0]?.index;
      // double check that a spoc/mrec is actually in the index it should be in
      const format = cards[firstSpocPosition]?.props?.format;
      const isSpoc = format === "spoc" || format === "rectangle";
      // if the spoc is not in its position, place TrendingSearches in the 3rd position
      cards.splice(isSpoc ? firstSpocPosition + 1 : 2, 1, <TrendingSearches />);
    }

    // if a banner ad is enabled and we have any available, place them in the grid
    const { spocs } = this.props.DiscoveryStream;

    if (
      (billboardEnabled || leaderboardEnabled) &&
      spocs?.data?.newtab_spocs?.items
    ) {
      // Only render one AdBanner in the grid -
      // Prioritize rendering a leaderboard if it exists,
      // otherwise render a billboard
      const spocToRender =
        spocs.data.newtab_spocs.items.find(
          ({ format }) => format === "leaderboard" && leaderboardEnabled
        ) ||
        spocs.data.newtab_spocs.items.find(
          ({ format }) => format === "billboard" && billboardEnabled
        );

      if (spocToRender && !spocs.blocked.includes(spocToRender.url)) {
        const row =
          spocToRender.format === "leaderboard"
            ? prefs[PREF_LEADERBOARD_POSITION]
            : prefs[PREF_BILLBOARD_POSITION];

        function displayCardsPerRow() {
          // Determines the number of cards per row based on the window width:
          // width <= 1122px: 2 cards per row
          // width 1123px to 1697px: 3 cards per row
          // width >= 1698px: 4 cards per row
          if (window.innerWidth <= 1122) {
            return 2;
          } else if (window.innerWidth > 1122 && window.innerWidth < 1698) {
            return 3;
          }
          return 4;
        }

        const injectAdBanner = bannerIndex => {
          // .splice() inserts the AdBanner at the desired index, ensuring correct DOM order for accessibility and keyboard navigation.
          // .push() would place it at the end, which is visually incorrect even if adjusted with CSS.
          cards.splice(
            bannerIndex,
            0,
            <AdBanner
              spoc={spocToRender}
              key={`dscard-${spocToRender.id}`}
              dispatch={this.props.dispatch}
              type={this.props.type}
              firstVisibleTimestamp={this.props.firstVisibleTimestamp}
              row={row}
              prefs={prefs}
            />
          );
        };

        const getBannerIndex = () => {
          // Calculate the index for where the AdBanner should be added, depending on number of cards per row on the grid
          const cardsPerRow = displayCardsPerRow();
          let bannerIndex = (row - 1) * cardsPerRow;
          return bannerIndex;
        };

        injectAdBanner(getBannerIndex());
      }
    }

    const gridClassName = this.renderGridClassName();

    return (
      <>{cards?.length > 0 && <div className={gridClassName}>{cards}</div>}</>
    );
  }

  renderListFeed(recommendations, selectedFeed) {
    const recs = recommendations.filter(item => item.feedName === selectedFeed);
    const isFakespot = selectedFeed === "fakespot";
    // remove duplicates from category list
    const categories = [...new Set(recs.map(({ category }) => category))];
    const listFeed = (
      <ListFeed
        // only display recs that match selectedFeed for ListFeed
        recs={recs}
        categories={isFakespot ? categories : []}
        firstVisibleTimestamp={this.props.firstVisibleTimestamp}
        type={this.props.type}
        dispatch={this.props.dispatch}
      />
    );
    return listFeed;
  }

  renderGridClassName() {
    const {
      hybridLayout,
      hideCardBackground,
      fourCardLayout,
      compactGrid,
      hideDescriptions,
    } = this.props;

    const hideCardBackgroundClass = hideCardBackground
      ? `ds-card-grid-hide-background`
      : ``;
    const fourCardLayoutClass = fourCardLayout
      ? `ds-card-grid-four-card-variant`
      : ``;
    const hideDescriptionsClassName = !hideDescriptions
      ? `ds-card-grid-include-descriptions`
      : ``;
    const compactGridClassName = compactGrid ? `ds-card-grid-compact` : ``;
    const hybridLayoutClassName = hybridLayout
      ? `ds-card-grid-hybrid-layout`
      : ``;

    const gridClassName = `ds-card-grid ${hybridLayoutClassName} ${hideCardBackgroundClass} ${fourCardLayoutClass} ${hideDescriptionsClassName} ${compactGridClassName}`;
    return gridClassName;
  }

  render() {
    const { data } = this.props;

    // Handle a render before feed has been fetched by displaying nothing
    if (!data) {
      return null;
    }

    // Handle the case where a user has dismissed all recommendations
    const isEmpty = data.recommendations.length === 0;

    return (
      <div>
        {this.props.title && (
          <div className="ds-header">
            <div className="title">{this.props.title}</div>
            {this.props.context && (
              <FluentOrText message={this.props.context}>
                <div className="ds-context" />
              </FluentOrText>
            )}
          </div>
        )}
        {isEmpty ? (
          <div className="ds-card-grid empty">
            <DSEmptyState
              status={data.status}
              dispatch={this.props.dispatch}
              feed={this.props.feed}
            />
          </div>
        ) : (
          this.renderCards()
        )}
      </div>
    );
  }
}

_CardGrid.defaultProps = {
  items: 4, // Number of stories to display
};

export const CardGrid = connect(state => ({
  Prefs: state.Prefs,
  App: state.App,
  DiscoveryStream: state.DiscoveryStream,
}))(_CardGrid);
