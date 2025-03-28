/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDataHandler_h___
#define nsDataHandler_h___

#include "mozilla/dom/MimeType.h"
#include "nsIProtocolHandler.h"
#include "nsWeakReference.h"

class nsDataHandler : public nsIProtocolHandler,
                      public nsSupportsWeakReference {
  virtual ~nsDataHandler() = default;

 public:
  NS_DECL_ISUPPORTS

  // nsIProtocolHandler methods:
  NS_DECL_NSIPROTOCOLHANDLER

  // nsDataHandler methods:
  nsDataHandler() = default;

  static nsresult CreateNewURI(const nsACString& aSpec, const char* aCharset,
                               nsIURI* aBaseURI, nsIURI** result);

  // Define a Create method to be used with a factory:
  [[nodiscard]] static nsresult Create(const nsIID& aIID, void** aResult);

  // Parse the full spec of a data: URI and return the individual parts.
  //
  // @arg aSpec The spec of the data URI.
  // @arg aContentType Out param, will hold the parsed content type.
  // @arg aContentCharset Optional, will hold the charset if specified.
  // @arg aIsBase64 Out param, indicates if the data is base64 encoded.
  // @arg aDataBuffer Optional, will reference the substring in |aPath| that
  //  contains the data portion of the path. No copy is made.
  // @arg aMimeType Optional, will be a CMimeType for the data in |aPath|.
  [[nodiscard]] static nsresult ParseURI(
      const nsACString& aSpec, nsCString& aContentType,
      nsCString* aContentCharset, bool& aIsBase64,
      nsDependentCSubstring* aDataBuffer = nullptr,
      RefPtr<CMimeType>* aMimeType = nullptr);
};

#endif /* nsDataHandler_h___ */
