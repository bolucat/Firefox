/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DeleteNodeTransaction.h"

#include "EditorBase.h"
#include "EditorDOMAPIWrapper.h"
#include "EditorDOMPoint.h"
#include "HTMLEditUtils.h"
#include "SelectionState.h"  // RangeUpdater
#include "TextEditor.h"

#include "mozilla/Logging.h"
#include "mozilla/ToString.h"

#include "nsDebug.h"
#include "nsError.h"
#include "nsAString.h"

namespace mozilla {

// static
already_AddRefed<DeleteNodeTransaction> DeleteNodeTransaction::MaybeCreate(
    EditorBase& aEditorBase, nsIContent& aContentToDelete) {
  RefPtr<DeleteNodeTransaction> transaction =
      new DeleteNodeTransaction(aEditorBase, aContentToDelete);
  if (NS_WARN_IF(!transaction->CanDoIt())) {
    return nullptr;
  }
  return transaction.forget();
}

DeleteNodeTransaction::DeleteNodeTransaction(EditorBase& aEditorBase,
                                             nsIContent& aContentToDelete)
    : DeleteContentTransactionBase(aEditorBase),
      mContentToDelete(&aContentToDelete),
      mParentNode(aContentToDelete.GetParentNode()) {
  MOZ_DIAGNOSTIC_ASSERT_IF(
      aEditorBase.IsHTMLEditor(),
      HTMLEditUtils::IsRemovableNode(aContentToDelete) ||
          // It's okay to delete text node if it's added by `HTMLEditor` since
          // remaining it may be noisy for the users.
          (aContentToDelete.IsText() &&
           aContentToDelete.HasFlag(NS_MAYBE_MODIFIED_FREQUENTLY)));
  NS_ASSERTION(
      !aEditorBase.IsHTMLEditor() ||
          HTMLEditUtils::IsRemovableNode(aContentToDelete),
      "Deleting non-editable text node, please write a test for this!!");
}

std::ostream& operator<<(std::ostream& aStream,
                         const DeleteNodeTransaction& aTransaction) {
  aStream << "{ mContentToDelete=" << aTransaction.mContentToDelete.get();
  if (aTransaction.mContentToDelete) {
    aStream << " (" << *aTransaction.mContentToDelete << ")";
  }
  aStream << ", mParentNode=" << aTransaction.mParentNode.get();
  if (aTransaction.mParentNode) {
    aStream << " (" << *aTransaction.mParentNode << ")";
  }
  aStream << ", mRefContent=" << aTransaction.mRefContent.get();
  if (aTransaction.mRefContent) {
    aStream << " (" << *aTransaction.mRefContent << ")";
  }
  aStream << ", mEditorBase=" << aTransaction.mEditorBase.get() << " }";
  return aStream;
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(DeleteNodeTransaction,
                                   DeleteContentTransactionBase,
                                   mContentToDelete, mParentNode, mRefContent)

NS_IMPL_ADDREF_INHERITED(DeleteNodeTransaction, DeleteContentTransactionBase)
NS_IMPL_RELEASE_INHERITED(DeleteNodeTransaction, DeleteContentTransactionBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DeleteNodeTransaction)
NS_INTERFACE_MAP_END_INHERITING(DeleteContentTransactionBase)

bool DeleteNodeTransaction::CanDoIt() const {
  if (NS_WARN_IF(!mContentToDelete) || NS_WARN_IF(!mEditorBase) ||
      !mParentNode) {
    return false;
  }
  return mEditorBase->IsTextEditor() ||
         HTMLEditUtils::IsSimplyEditableNode(*mParentNode);
}

NS_IMETHODIMP DeleteNodeTransaction::DoTransaction() {
  MOZ_LOG(GetLogModule(), LogLevel::Info,
          ("%p DeleteNodeTransaction::%s this=%s", this, __FUNCTION__,
           ToString(*this).c_str()));

  if (NS_WARN_IF(!CanDoIt())) {
    return NS_OK;
  }

  MOZ_ASSERT_IF(mEditorBase->IsTextEditor(), !mContentToDelete->IsText());

  // Remember which child mContentToDelete was (by remembering which child was
  // next).  Note that mRefContent can be nullptr.
  mRefContent = mContentToDelete->GetNextSibling();

  // give range updater a chance.  SelAdjDeleteNode() needs to be called
  // *before* we do the action, unlike some of the other RangeItem update
  // methods.
  const OwningNonNull<EditorBase> editorBase = *mEditorBase;
  editorBase->RangeUpdaterRef().SelAdjDeleteNode(*mContentToDelete);

  const OwningNonNull<nsINode> parentNode = *mParentNode;
  const OwningNonNull<nsIContent> contentToDelete = *mContentToDelete;
  AutoNodeAPIWrapper nodeWrapper(editorBase, parentNode);
  nsresult rv = nodeWrapper.RemoveChild(contentToDelete);
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoNodeAPIWrapper::RemoveChild() failed");
    return rv;
  }
  NS_WARNING_ASSERTION(
      nodeWrapper.IsExpectedResult(),
      "Removing a content node caused other mutations, but ignored");
  return NS_OK;
}

EditorDOMPoint DeleteNodeTransaction::SuggestPointToPutCaret() const {
  return EditorDOMPoint();
}

NS_IMETHODIMP DeleteNodeTransaction::UndoTransaction() {
  MOZ_LOG(GetLogModule(), LogLevel::Info,
          ("%p DeleteNodeTransaction::%s this=%s", this, __FUNCTION__,
           ToString(*this).c_str()));

  if (NS_WARN_IF(!CanDoIt())) {
    // This is a legal state, the transaction is a no-op.
    return NS_OK;
  }
  const OwningNonNull<EditorBase> editorBase = *mEditorBase;
  const OwningNonNull<nsINode> parentNode = *mParentNode;
  const OwningNonNull<nsIContent> contentToDelete = *mContentToDelete;
  const nsCOMPtr<nsIContent> refContent = mRefContent;
  // XXX Perhaps, we should check `refContent` is a child of `parentNode`,
  //     and if it's not, we should stop undoing or something.
  AutoNodeAPIWrapper nodeWrapper(editorBase, parentNode);
  nsresult rv = nodeWrapper.InsertBefore(contentToDelete, refContent);
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoNodeAPIWrapper::InsertBefore() failed");
    return rv;
  }
  NS_WARNING_ASSERTION(nodeWrapper.IsExpectedResult(),
                       "Inserting a node caused other mutations, but ignored");
  return NS_OK;
}

NS_IMETHODIMP DeleteNodeTransaction::RedoTransaction() {
  MOZ_LOG(GetLogModule(), LogLevel::Info,
          ("%p DeleteNodeTransaction::%s this=%s", this, __FUNCTION__,
           ToString(*this).c_str()));

  if (NS_WARN_IF(!CanDoIt())) {
    // This is a legal state, the transaction is a no-op.
    return NS_OK;
  }

  const OwningNonNull<EditorBase> editorBase = *mEditorBase;
  editorBase->RangeUpdaterRef().SelAdjDeleteNode(*mContentToDelete);

  const OwningNonNull<nsINode> parentNode = *mParentNode;
  const OwningNonNull<nsIContent> contentToDelete = *mContentToDelete;
  AutoNodeAPIWrapper nodeWrapper(editorBase, parentNode);
  nsresult rv = nodeWrapper.RemoveChild(contentToDelete);
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoNodeAPIWrapper::RemoveChild() failed");
    return rv;
  }
  NS_WARNING_ASSERTION(
      nodeWrapper.IsExpectedResult(),
      "Removing a content node caused other mutations, but ignored");
  return NS_OK;
}

}  // namespace mozilla
