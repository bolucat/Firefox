/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_StyleSheetInfo_h
#define mozilla_StyleSheetInfo_h

#include "mozilla/CORSMode.h"
#include "mozilla/css/SheetParsingMode.h"
#include "mozilla/dom/SRIMetadata.h"
#include "nsIReferrerInfo.h"

class nsIPrincipal;
class nsIURI;

namespace mozilla {
class StyleSheet;
struct StyleStylesheetContents;
struct URLExtraData;

/**
 * Struct for data common to CSSStyleSheetInner and ServoStyleSheet.
 */
struct StyleSheetInfo final {
  using ReferrerPolicy = dom::ReferrerPolicy;

  StyleSheetInfo(CORSMode aCORSMode, const dom::SRIMetadata& aIntegrity,
                 css::SheetParsingMode aParsingMode);

  // FIXME(emilio): aCopy should be const.
  StyleSheetInfo(StyleSheetInfo& aCopy, StyleSheet* aPrimarySheet);

  ~StyleSheetInfo();

  StyleSheetInfo* CloneFor(StyleSheet* aPrimarySheet);

  void AddSheet(StyleSheet* aSheet);
  void RemoveSheet(StyleSheet* aSheet);

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  // FIXME(emilio): most of this struct should be const, then we can remove the
  // duplication with the UrlExtraData member and such.
  nsCOMPtr<nsIURI> mSheetURI;          // for error reports, etc.
  nsCOMPtr<nsIURI> mOriginalSheetURI;  // for GetHref.  Can be null.
  nsCOMPtr<nsIURI> mBaseURI;           // for resolving relative URIs
  nsCOMPtr<nsIPrincipal> mPrincipal;
  const CORSMode mCORSMode;
  // The ReferrerInfo of a stylesheet is used for its child sheets and loads
  // come from this stylesheet, so it is stored here.
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;
  dom::SRIMetadata mIntegrity;

  // Pointer to the list of child sheets. This is all fundamentally broken,
  // because each of the child sheets has a unique parent... We can only hope
  // (and currently this is the case) that any time page JS can get its hands on
  // a child sheet that means we've already ensured unique infos throughout its
  // parent chain and things are good.
  nsTArray<RefPtr<StyleSheet>> mChildren;

  // If a SourceMap or X-SourceMap response header is seen, this is
  // the value.  If both are seen, SourceMap is preferred.  If neither
  // is seen, this will be an empty string.
  nsCString mSourceMapURL;

  RefPtr<const StyleStylesheetContents> mContents;

  // XXX We already have mSheetURI, mBaseURI, and mPrincipal.
  //
  // Can we somehow replace them with URLExtraData directly? The issue
  // is currently URLExtraData is immutable, but URIs in StyleSheetInfo
  // seems to be mutable, so we probably cannot set them altogether.
  // Also, this is mostly a duplicate reference of the same url data
  // inside RawServoStyleSheet. We may want to just use that instead.
  RefPtr<URLExtraData> mURLData;

  // HACK: This must be the after any member rust accesses in order to not cause
  // issues on i686-android. Bindgen generates an opaque blob of [u64; N] for
  // types it doesn't understand like AutoTArray, but turns out u64 is not
  // 8-byte aligned on this arch (wtf), which would cause other members rust
  // cares about to be misaligned.
  AutoTArray<StyleSheet*, 8> mSheets;

#ifdef DEBUG
  bool mPrincipalSet = false;
#endif
};

}  // namespace mozilla

#endif  // mozilla_StyleSheetInfo_h
