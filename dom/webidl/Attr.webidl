/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.w3.org/TR/2012/WD-dom-20120105/
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

[Exposed=Window]
interface Attr : Node {
  readonly attribute DOMString localName;
           [CEReactions, SetterNeedsSubjectPrincipal=NonSystem, SetterThrows]
           attribute DOMString value;

  // Rename binary names of nodeValue and textContent from the Node interface.
  [CEReactions, SetterThrows, Pure,
   BinaryName="nodeValueWithTrustedTypeCheck"]
           attribute DOMString? nodeValue;
  [CEReactions, SetterThrows, GetterCanOOM,
   SetterNeedsSubjectPrincipal=NonSystem, Pure,
   BinaryName="textContentWithTrustedTypeCheck"]
           attribute DOMString? textContent;

  [Constant]
  readonly attribute DOMString name;
  [Constant]
  readonly attribute DOMString? namespaceURI;
  [Constant]
  readonly attribute DOMString? prefix;

  readonly attribute boolean specified;
};

// Mozilla extensions

partial interface Attr {
  readonly attribute Element? ownerElement;
};
