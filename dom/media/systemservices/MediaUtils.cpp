/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaUtils.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "nsNetUtil.h"

namespace mozilla::media {

bool HostnameInPref(const char* aPref, const nsCString& aHostName) {
  auto HostInDomain = [](const nsCString& aHost, const nsCString& aPattern) {
    int32_t patternOffset = 0;
    int32_t hostOffset = 0;

    // Act on '*.' wildcard in the left-most position in a domain pattern.
    if (StringBeginsWith(aPattern, nsCString("*."))) {
      patternOffset = 2;

      // Ignore the lowest level sub-domain for the hostname.
      hostOffset = aHost.FindChar('.') + 1;

      if (hostOffset <= 1) {
        // Reject a match between a wildcard and a TLD or '.foo' form.
        return false;
      }
    }

    nsDependentCString hostRoot(aHost, hostOffset);
    return hostRoot.EqualsIgnoreCase(aPattern.BeginReading() + patternOffset);
  };

  nsCString domainList;
  nsresult rv = Preferences::GetCString(aPref, domainList);

  if (NS_FAILED(rv)) {
    return false;
  }

  domainList.StripWhitespace();

  if (domainList.IsEmpty() || aHostName.IsEmpty()) {
    return false;
  }

  // Test each domain name in the comma separated list
  // after converting from UTF8 to ASCII. Each domain
  // must match exactly or have a single leading '*.' wildcard.
  for (const nsACString& each : domainList.Split(',')) {
    nsCString domainPattern;
    rv = NS_DomainToASCIIAllowAnyGlyphfulASCII(each, domainPattern);
    if (NS_SUCCEEDED(rv)) {
      if (HostInDomain(aHostName, domainPattern)) {
        return true;
      }
    } else {
      NS_WARNING("Failed to convert UTF-8 host to ASCII");
    }
  }
  return false;
}

nsCOMPtr<nsIAsyncShutdownClient> GetShutdownBarrier() {
  nsCOMPtr<nsIAsyncShutdownService> svc = services::GetAsyncShutdownService();
  if (!svc) {
    // We can fail to get the shutdown service if we're already shutting down.
    return nullptr;
  }

  nsCOMPtr<nsIAsyncShutdownClient> barrier;
  nsresult rv = svc->GetProfileBeforeChange(getter_AddRefs(barrier));
  if (!barrier) {
    // We are probably in a content process. We need to do cleanup at
    // XPCOM shutdown in leakchecking builds.
    rv = svc->GetXpcomWillShutdown(getter_AddRefs(barrier));
  }
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  MOZ_RELEASE_ASSERT(barrier);
  return barrier;
}

nsCOMPtr<nsIAsyncShutdownClient> MustGetShutdownBarrier() {
  nsCOMPtr<nsIAsyncShutdownClient> barrier = GetShutdownBarrier();
  MOZ_RELEASE_ASSERT(barrier);
  return barrier;
}

NS_IMPL_ISUPPORTS(ShutdownBlocker, nsIAsyncShutdownBlocker)

namespace {
class TicketBlocker : public ShutdownBlocker {
  using ShutdownMozPromise = ShutdownBlockingTicket::ShutdownMozPromise;

 public:
  explicit TicketBlocker(const nsAString& aName)
      : ShutdownBlocker(aName), mPromise(mHolder.Ensure(__func__)) {}

  NS_IMETHOD
  BlockShutdown(nsIAsyncShutdownClient* aProfileBeforeChange) override {
    mHolder.Resolve(true, __func__);
    return NS_OK;
  }

  void RejectIfExists() { mHolder.RejectIfExists(false, __func__); }

  ShutdownMozPromise* ShutdownPromise() { return mPromise; }

 private:
  ~TicketBlocker() = default;

  MozPromiseHolder<ShutdownMozPromise> mHolder;
  const RefPtr<ShutdownMozPromise> mPromise;
};

class ShutdownBlockingTicketImpl : public ShutdownBlockingTicket {
 private:
  RefPtr<TicketBlocker> mBlocker;

 public:
  explicit ShutdownBlockingTicketImpl(RefPtr<TicketBlocker> aBlocker)
      : mBlocker(std::move(aBlocker)) {}

  static UniquePtr<ShutdownBlockingTicket> Create(const nsAString& aName,
                                                  const nsAString& aFileName,
                                                  int32_t aLineNr) {
    auto blocker = MakeRefPtr<TicketBlocker>(aName);
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "ShutdownBlockingTicketImpl::AddBlocker",
        [blocker, file = nsString(aFileName), aLineNr] {
          MustGetShutdownBarrier()->AddBlocker(blocker, file, aLineNr, u""_ns);
        }));
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdown)) {
      // Adding a blocker is not guaranteed to succeed. Remove the blocker in
      // case it succeeded anyway, and bail.
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ShutdownBlockingTicketImpl::RemoveBlocker", [blocker] {
            MustGetShutdownBarrier()->RemoveBlocker(blocker);
            blocker->RejectIfExists();
          }));
      return nullptr;
    }

    // Adding a blocker is now guaranteed to succeed:
    // - If AppShutdown::IsInOrBeyond(AppShutdown) returned false,
    // - then the AddBlocker main thread task was queued before AppShutdown's
    //   sCurrentShutdownPhase is set to ShutdownPhase::AppShutdown,
    // - which is before AppShutdown will drain the (main thread) event queue to
    //   run the AddBlocker task, if not already run,
    // - which is before profile-before-change (the earliest barrier we'd add a
    //   blocker to, see GetShutdownBarrier()) is notified,
    // - which is when AsyncShutdown prevents further conditions (blockers)
    //   being added to the profile-before-change barrier.
    return MakeUnique<ShutdownBlockingTicketImpl>(std::move(blocker));
  }

  ~ShutdownBlockingTicketImpl() {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
        NS_NewRunnableFunction(__func__, [blocker = std::move(mBlocker)] {
          GetShutdownBarrier()->RemoveBlocker(blocker);
          blocker->RejectIfExists();
        })));
  }

  ShutdownMozPromise* ShutdownPromise() override {
    return mBlocker->ShutdownPromise();
  }
};
}  // namespace

UniquePtr<ShutdownBlockingTicket> ShutdownBlockingTicket::Create(
    const nsAString& aName, const nsAString& aFileName, int32_t aLineNr) {
  return ShutdownBlockingTicketImpl::Create(aName, aFileName, aLineNr);
}

}  // namespace mozilla::media
