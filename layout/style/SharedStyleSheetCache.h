/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SharedStyleSheetCache_h__
#define mozilla_SharedStyleSheetCache_h__

// The shared style sheet cache is a cache that allows us to share sheets across
// documents.
//
// It's generally a singleton, but it is different from GlobalStyleSheetCache in
// the sense that:
//
//  * It needs to be cycle-collectable, as it can keep alive style sheets from
//    various documents.
//
//  * It is conceptually a singleton, but given its cycle-collectable nature, we
//    might re-create it.

#include "mozilla/MemoryReporting.h"
#include "mozilla/SharedSubResourceCache.h"
#include "mozilla/css/Loader.h"

namespace mozilla {

class StyleSheet;
class SheetLoadDataHashKey;

namespace css {
class SheetLoadData;
class Loader;
}  // namespace css

struct SharedStyleSheetCacheTraits {
  using Loader = css::Loader;
  using Key = SheetLoadDataHashKey;
  using Value = StyleSheet;
  using LoadingValue = css::SheetLoadData;

  static SheetLoadDataHashKey KeyFromLoadingValue(const LoadingValue& aValue) {
    return SheetLoadDataHashKey(aValue);
  }
};

class SharedStyleSheetCache final
    : public SharedSubResourceCache<SharedStyleSheetCacheTraits,
                                    SharedStyleSheetCache>,
      public nsIMemoryReporter {
 public:
  using Base = SharedSubResourceCache<SharedStyleSheetCacheTraits,
                                      SharedStyleSheetCache>;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTER

  SharedStyleSheetCache();
  void Init();

  // This has to be static because it's also called for loaders that don't have
  // a sheet cache (loaders that are not owned by a document).
  static void LoadCompleted(SharedStyleSheetCache*, css::SheetLoadData&,
                            nsresult);
  using Base::LoadCompleted;
  static void LoadCompletedInternal(SharedStyleSheetCache*, css::SheetLoadData&,
                                    nsTArray<RefPtr<css::SheetLoadData>>&);
  static void Clear(const Maybe<bool>& aChrome = Nothing(),
                    const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal = Nothing(),
                    const Maybe<nsCString>& aSchemelessSite = Nothing(),
                    const Maybe<OriginAttributesPattern>& aPattern = Nothing(),
                    const Maybe<nsCString>& aURL = Nothing());

  void EvictPrincipal(nsIPrincipal* aPrincipal) {
    Base::EvictPrincipal(aPrincipal);
    mInlineSheets.Remove(aPrincipal);
  }

  void ClearInProcess(const Maybe<bool>& aChrome,
                      const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                      const Maybe<nsCString>& aSchemelessSite,
                      const Maybe<OriginAttributesPattern>& aPattern,
                      const Maybe<nsCString>& aURL);

  size_t SizeOfIncludingThis(MallocSizeOf) const;

  auto LookupInline(nsIPrincipal* aPrincipal, const nsAString& aBuffer) {
    auto& principalMap = mInlineSheets.LookupOrInsert(aPrincipal);
    return principalMap.Lookup(aBuffer);
  }

  struct InlineSheetEntry {
    RefPtr<StyleSheet> mSheet;
    bool mWasLoadedAsImage = false;
  };
  using InlineSheetCandidates = nsTArray<InlineSheetEntry>;

  void InsertInline(nsIPrincipal* aPrincipal, const nsAString& aBuffer,
                    InlineSheetEntry&& aEntry) {
    // TODO(emilio): Maybe a better eviction policy for inline sheets, or an
    // expiration tracker or so?
    auto& principalMap = mInlineSheets.LookupOrInsert(aPrincipal);
    principalMap
        .LookupOrInsertWith(aBuffer, [] { return InlineSheetCandidates(); })
        .AppendElement(std::move(aEntry));
  }

 protected:
  void InsertIfNeeded(css::SheetLoadData&);
  nsTHashMap<PrincipalHashKey,
             nsTHashMap<nsStringHashKey, InlineSheetCandidates>>
      mInlineSheets;

  ~SharedStyleSheetCache();
};

}  // namespace mozilla

#endif
