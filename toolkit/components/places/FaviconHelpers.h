/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "nsIFaviconService.h"
#include "nsIChannelEventSink.h"
#include "nsIInterfaceRequestor.h"
#include "nsIStreamListener.h"
#include "mozIPlacesPendingOperation.h"
#include "nsThreadUtils.h"
#include "nsProxyRelease.h"
#include "imgLoader.h"
#include "ConcurrentConnection.h"

class nsIPrincipal;

#include "Database.h"
#include "mozilla/storage.h"
#include "mozilla/ipc/IPCCore.h"

#define ICON_STATUS_UNKNOWN 0
#define ICON_STATUS_CHANGED 1 << 0
#define ICON_STATUS_SAVED 1 << 1
#define ICON_STATUS_ASSOCIATED 1 << 2
#define ICON_STATUS_CACHED 1 << 3

#define TO_CHARBUFFER(_buffer) \
  reinterpret_cast<char*>(const_cast<uint8_t*>(_buffer))
#define TO_INTBUFFER(_string) \
  reinterpret_cast<uint8_t*>(const_cast<char*>(_string.get()))

#define PNG_MIME_TYPE "image/png"
#define SVG_MIME_TYPE "image/svg+xml"

// Always ensure a minimum expiration time, so icons are not already expired
// on addition.
#define MIN_FAVICON_EXPIRATION ((PRTime)1 * 24 * 60 * 60 * PR_USEC_PER_SEC)
// The maximum time we will keep a favicon around.  We always ask the cache
// first and default to this value if we can't get a time, or the time we get
// is far in the future.
#define MAX_FAVICON_EXPIRATION ((PRTime)7 * 24 * 60 * 60 * PR_USEC_PER_SEC)

namespace mozilla {
namespace places {

/**
 * Represents one of the payloads (frames) of an icon entry.
 */
struct IconPayload {
  IconPayload() : id(0), width(0) {
    data.SetIsVoid(true);
    mimeType.SetIsVoid(true);
  }

  int64_t id;
  uint16_t width;
  nsCString data;
  nsCString mimeType;
};

/**
 * Represents an icon entry.
 */
struct IconData {
  IconData()
      : expiration(0), status(ICON_STATUS_UNKNOWN), rootIcon(0), flags(0) {}

  nsCString spec;
  nsCString host;
  PRTime expiration;
  uint16_t status;  // This is a bitset, see ICON_STATUS_* defines above.
  uint8_t rootIcon;
  CopyableTArray<IconPayload> payloads;
  uint16_t flags;  // This is a bitset, see ICONDATA_FLAGS_* defines
                   // in toolkit/components/places/nsIFaviconService.idl.
};

/**
 * Data cache for a page entry.
 */
struct PageData {
  PageData() : id(0), placeId(0), canAddToHistory(true) {
    guid.SetIsVoid(true);
  }

  int64_t id;       // This is the moz_pages_w_icons id.
  int64_t placeId;  // This is the moz_places page id.
  nsCString spec;
  nsCString host;
  nsCString bookmarkedSpec;
  bool canAddToHistory;  // False for disabled history and unsupported schemas.
  nsCString guid;
};

/**
 * Info for a frame.
 */
struct FrameData {
  FrameData(uint16_t aIndex, uint16_t aWidth) : index(aIndex), width(aWidth) {}

  uint16_t index;
  uint16_t width;
};

/**
 * Associates the icon to the required page, finally dispatches an event to the
 * main thread to notify the change to observers.
 */
class AsyncAssociateIconToPage final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aIcon
   *        Icon to be associated.
   * @param aPage
   *        Page to which associate the icon.
   */
  AsyncAssociateIconToPage(const IconData& aIcon, const PageData& aPage);

 private:
  IconData mIcon;
  PageData mPage;
};

/**
 * Set favicon for the page, finally dispatches an event to the
 * main thread to notify the change to observers.
 */
class AsyncSetIconForPage final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aIcon
   *        Icon to be associated.
   * @param aPage
   *        Page to which associate the icon.
   * @param aPromise
   *        Promise that returns the result.
   */
  AsyncSetIconForPage(const IconData& aIcon, const PageData& aPage,
                      dom::Promise* aPromise);

 private:
  nsMainThreadPtrHandle<dom::Promise> mPromise;
  IconData mIcon;
  PageData mPage;
};

using FaviconPromise =
    mozilla::MozPromise<nsCOMPtr<nsIFavicon>, nsresult, true>;
using BoolPromise = mozilla::MozPromise<bool, nsresult, true>;

/**
 * Asynchronously tries to get the URL and data of a page's favicon, then
 * resolve given promise with the result.
 */
class AsyncGetFaviconForPageRunnable final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aPageURI
   *        URI of the page whose favicon's URL we're fetching
   * @param aPreferredWidth
   *        The preferred size of the icon.  We will try to return an icon close
   *        to this size.
   * @param aPromise
   *        Promise that returns the result.
   */
  AsyncGetFaviconForPageRunnable(
      const nsCOMPtr<nsIURI>& aPageURI, uint16_t aPreferredWidth,
      const RefPtr<FaviconPromise::Private>& aPromise, bool aOnConcurrentConn);

 private:
  ~AsyncGetFaviconForPageRunnable();

  nsCOMPtr<nsIURI> mPageURI;
  uint16_t mPreferredWidth;
  nsMainThreadPtrHandle<FaviconPromise::Private> mPromise;
  bool mOnConcurrentConn;
};

/**
 * Notifies the icon change to favicon observers.
 */
class NotifyIconObservers final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aIcon
   *        Icon information. Can be empty if no icon is associated to the page.
   * @param aPage
   *        Page to which the icon information applies.
   */
  NotifyIconObservers(const IconData& aIcon, const PageData& aPage);

 private:
  IconData mIcon;
  PageData mPage;
};

/**
 * Asynchronously tries to copy the favicons asociated to the URL.
 */
class AsyncTryCopyFaviconsRunnable final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aFromPageURI
   *        The originating URI.
   * @param aToPageURI
   *        The destination URI.
   * @param aCanAddToHistoryForToPage
   *        Whether or not can add history to aToPageURI.
   * @param aPromise
   *        Promise that returns the result.
   */
  AsyncTryCopyFaviconsRunnable(const nsCOMPtr<nsIURI>& aFromPageURI,
                               const nsCOMPtr<nsIURI>& aToPageURI,
                               const bool aCanAddToHistoryForToPage,
                               const RefPtr<BoolPromise::Private>& aPromise);

 private:
  nsCOMPtr<nsIURI> mFromPageURI;
  nsCOMPtr<nsIURI> mToPageURI;
  bool mCanAddToHistoryForToPage;
  nsMainThreadPtrHandle<BoolPromise::Private> mPromise;
};

/**
 * Provides a uniform way to obtain statements from either the
 * main Places Database or a ConcurrentConnection.
 */
class ConnectionAdapter {
 public:
  /**
   * Constructor.
   *
   * @param aDB
   *  The main Database object.
   */
  explicit ConnectionAdapter(const RefPtr<Database>& aDB)
      : mDatabase(aDB), mConcurrentConnection(nullptr) {}

  /**
   * Constructor.
   *
   * @param aConn
   *  The read-only ConcurrentConnection.
   */
  explicit ConnectionAdapter(const RefPtr<ConcurrentConnection>& aConn)
      : mDatabase(nullptr), mConcurrentConnection(aConn) {}

  already_AddRefed<mozIStorageStatement> GetStatement(

      const nsCString& aQuery) const {
    MOZ_ASSERT(!NS_IsMainThread(), "Must be on helper thread");

    if (mDatabase) {
      return mDatabase->GetStatement(aQuery);
    } else if (mConcurrentConnection) {
      auto conn = mConcurrentConnection.get();
      if (conn) {
        return conn->GetStatementOnHelperThread(aQuery);
      }
    }
    return nullptr;
  }

  explicit operator bool() const {
    return mDatabase || mConcurrentConnection.get();
  }

 private:
  RefPtr<Database> mDatabase;
  RefPtr<ConcurrentConnection> mConcurrentConnection;
};

}  // namespace places
}  // namespace mozilla
