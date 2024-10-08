/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _mozilla_dom_WorkerIPCUtils_h
#define _mozilla_dom_WorkerIPCUtils_h

#include "mozilla/dom/BindingIPCUtils.h"

// For RequestCredentials
#include "mozilla/dom/FetchIPCTypes.h"

// Undo X11/X.h's definition of None
#undef None

#include "mozilla/dom/WorkerBinding.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::WorkerType>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::WorkerType> {};

template <>
struct ParamTraits<mozilla::dom::WorkerOptions> {
  typedef mozilla::dom::WorkerOptions paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mType);
    WriteParam(aWriter, aParam.mCredentials);
    WriteParam(aWriter, aParam.mName);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mType) &&
           ReadParam(aReader, &aResult->mCredentials) &&
           ReadParam(aReader, &aResult->mName);
  }
};

}  // namespace IPC

#endif  // _mozilla_dom_WorkerIPCUtils_h
