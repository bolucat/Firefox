/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"

#include "nsArrayUtils.h"
#include "nsClipboard.h"
#if defined(MOZ_X11)
#  include "nsClipboardX11.h"
#endif
#if defined(MOZ_WAYLAND)
#  include "nsClipboardWayland.h"
#  include "nsWaylandDisplay.h"
#endif
#include "nsGtkUtils.h"
#include "nsIURI.h"
#include "nsIFile.h"
#include "nsNetUtil.h"
#include "nsContentUtils.h"
#include "HeadlessClipboard.h"
#include "nsSupportsPrimitives.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsPrimitiveHelpers.h"
#include "nsImageToPixbuf.h"
#include "nsStringStream.h"
#include "nsIFileURL.h"
#include "nsIObserverService.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/TimeStamp.h"
#include "GRefPtr.h"
#include "WidgetUtilsGtk.h"

#include "imgIContainer.h"

#include <gtk/gtk.h>
#if defined(MOZ_X11)
#  include <gtk/gtkx.h>
#endif

#include "mozilla/Encoding.h"

using namespace mozilla;

// Idle timeout for receiving selection and property notify events (microsec)
// Right now it's set to 1 sec.
const int kClipboardTimeout = 1000000;

// Defines how many event loop iterations will be done without sleep.
// We ususally get data in first 2-3 iterations unless some large object
// (an image for instance) is transferred through clipboard.
const int kClipboardFastIterationNum = 3;

// We add this prefix to HTML markup, so that GetHTMLCharset can correctly
// detect the HTML as UTF-8 encoded.
static const char kHTMLMarkupPrefix[] =
    R"(<meta http-equiv="content-type" content="text/html; charset=utf-8">)";

static const char kURIListMime[] = "text/uri-list";

// MIME to exclude sensitive data (password) from the clipboard history on not
// just KDE.
static const char kKDEPasswordManagerHintMime[] = "x-kde-passwordManagerHint";

MOZ_CONSTINIT ClipboardTargets nsRetrievalContext::sClipboardTargets;
MOZ_CONSTINIT ClipboardTargets nsRetrievalContext::sPrimaryTargets;

// Callback when someone asks us for the data
static void clipboard_get_cb(GtkClipboard* aGtkClipboard,
                             GtkSelectionData* aSelectionData, guint info,
                             gpointer user_data);

// Callback when someone asks us to clear a clipboard
static void clipboard_clear_cb(GtkClipboard* aGtkClipboard, gpointer user_data);

// Callback when owner of clipboard data is changed
static void clipboard_owner_change_cb(GtkClipboard* aGtkClipboard,
                                      GdkEventOwnerChange* aEvent,
                                      gpointer aUserData);

static bool GetHTMLCharset(Span<const char> aData, nsCString& str);

ClipboardTargets ClipboardTargets::Clone() {
  ClipboardTargets ret;
  ret.mCount = mCount;
  if (mCount) {
    ret.mTargets.reset(
        reinterpret_cast<GdkAtom*>(g_malloc(sizeof(GdkAtom) * mCount)));
    memcpy(ret.mTargets.get(), mTargets.get(), sizeof(GdkAtom) * mCount);
  }
  return ret;
}

void ClipboardTargets::Set(ClipboardTargets aTargets) {
  mCount = aTargets.mCount;
  mTargets = std::move(aTargets.mTargets);
}

void ClipboardData::SetData(Span<const uint8_t> aData) {
  mData = nullptr;
  mLength = aData.Length();
  if (mLength) {
    mData.reset(reinterpret_cast<char*>(g_malloc(sizeof(char) * mLength)));
    memcpy(mData.get(), aData.data(), sizeof(char) * mLength);
  }
}

void ClipboardData::SetText(Span<const char> aData) {
  mData = nullptr;
  mLength = aData.Length();
  if (mLength) {
    mData.reset(
        reinterpret_cast<char*>(g_malloc(sizeof(char) * (mLength + 1))));
    memcpy(mData.get(), aData.data(), sizeof(char) * mLength);
    mData.get()[mLength] = '\0';
  }
}

void ClipboardData::SetTargets(ClipboardTargets aTargets) {
  mLength = aTargets.mCount;
  mData.reset(reinterpret_cast<char*>(aTargets.mTargets.release()));
}

ClipboardTargets ClipboardData::ExtractTargets() {
  GUniquePtr<GdkAtom> targets(reinterpret_cast<GdkAtom*>(mData.release()));
  uint32_t length = std::exchange(mLength, 0);
  return ClipboardTargets{std::move(targets), length};
}

GdkAtom GetSelectionAtom(int32_t aWhichClipboard) {
  if (aWhichClipboard == nsIClipboard::kGlobalClipboard)
    return GDK_SELECTION_CLIPBOARD;

  return GDK_SELECTION_PRIMARY;
}

Maybe<nsIClipboard::ClipboardType> GetGeckoClipboardType(
    GtkClipboard* aGtkClipboard) {
  if (aGtkClipboard == gtk_clipboard_get(GDK_SELECTION_PRIMARY)) {
    return Some(nsClipboard::kSelectionClipboard);
  }
  if (aGtkClipboard == gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)) {
    return Some(nsClipboard::kGlobalClipboard);
  }
  return Nothing();  // THAT AIN'T NO CLIPBOARD I EVER HEARD OF
}

void nsRetrievalContext::ClearCachedTargetsClipboard(GtkClipboard* aClipboard,
                                                     GdkEvent* aEvent,
                                                     gpointer data) {
  MOZ_CLIPBOARD_LOG("nsRetrievalContext::ClearCachedTargetsClipboard()");
  sClipboardTargets.Clear();
}

void nsRetrievalContext::ClearCachedTargetsPrimary(GtkClipboard* aClipboard,
                                                   GdkEvent* aEvent,
                                                   gpointer data) {
  MOZ_CLIPBOARD_LOG("nsRetrievalContext::ClearCachedTargetsPrimary()");
  sPrimaryTargets.Clear();
}

ClipboardTargets nsRetrievalContext::GetTargets(int32_t aWhichClipboard) {
  MOZ_CLIPBOARD_LOG("nsRetrievalContext::GetTargets(%s)\n",
                    aWhichClipboard == nsClipboard::kSelectionClipboard
                        ? "primary"
                        : "clipboard");
  ClipboardTargets& storedTargets =
      (aWhichClipboard == nsClipboard::kSelectionClipboard) ? sPrimaryTargets
                                                            : sClipboardTargets;
  if (!storedTargets) {
    MOZ_CLIPBOARD_LOG("  getting targets from system");
    storedTargets.Set(GetTargetsImpl(aWhichClipboard));
  } else {
    MOZ_CLIPBOARD_LOG("  using cached targets");
  }
  return storedTargets.Clone();
}

nsRetrievalContext::~nsRetrievalContext() {
  sClipboardTargets.Clear();
  sPrimaryTargets.Clear();
}

nsClipboard::nsClipboard()
    : nsBaseClipboard(mozilla::dom::ClipboardCapabilities(
#ifdef MOZ_WAYLAND
          widget::GdkIsWaylandDisplay()
              ? widget::WaylandDisplayGet()->IsPrimarySelectionEnabled()
              : true,
#else
          true, /* supportsSelectionClipboard */
#endif
          false /* supportsFindClipboard */,
          false /* supportsSelectionCache */)) {
  g_signal_connect(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "owner-change",
                   G_CALLBACK(clipboard_owner_change_cb), this);
  g_signal_connect(gtk_clipboard_get(GDK_SELECTION_PRIMARY), "owner-change",
                   G_CALLBACK(clipboard_owner_change_cb), this);
}

nsClipboard::~nsClipboard() {
  g_signal_handlers_disconnect_by_func(
      gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
      FuncToGpointer(clipboard_owner_change_cb), this);
  g_signal_handlers_disconnect_by_func(
      gtk_clipboard_get(GDK_SELECTION_PRIMARY),
      FuncToGpointer(clipboard_owner_change_cb), this);
}

NS_IMPL_ISUPPORTS_INHERITED(nsClipboard, nsBaseClipboard, nsIObserver)

nsresult nsClipboard::Init(void) {
#if defined(MOZ_X11)
  if (widget::GdkIsX11Display()) {
    mContext = new nsRetrievalContextX11();
  }
#endif
#if defined(MOZ_WAYLAND)
  if (widget::GdkIsWaylandDisplay()) {
    mContext = new nsRetrievalContextWayland();
  }
#endif

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    os->AddObserver(this, "xpcom-shutdown", false);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsClipboard::Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) {
  // Save global clipboard content to CLIPBOARD_MANAGER.
  // gtk_clipboard_store() can run an event loop, so call from a dedicated
  // runnable.
  return SchedulerGroup::Dispatch(
      NS_NewRunnableFunction("gtk_clipboard_store()", []() {
        MOZ_CLIPBOARD_LOG("nsClipboard storing clipboard content\n");
        gtk_clipboard_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
      }));
}

NS_IMETHODIMP
nsClipboard::SetNativeClipboardData(nsITransferable* aTransferable,
                                    ClipboardType aWhichClipboard) {
  MOZ_DIAGNOSTIC_ASSERT(aTransferable);
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));

  // See if we can short cut
  if ((aWhichClipboard == kGlobalClipboard &&
       aTransferable == mGlobalTransferable.get()) ||
      (aWhichClipboard == kSelectionClipboard &&
       aTransferable == mSelectionTransferable.get())) {
    return NS_OK;
  }

  MOZ_CLIPBOARD_LOG(
      "nsClipboard::SetNativeClipboardData (%s)\n",
      aWhichClipboard == kSelectionClipboard ? "primary" : "clipboard");

  // List of suported targets
  GtkTargetList* list = gtk_target_list_new(nullptr, 0);

  // Get the types of supported flavors
  nsTArray<nsCString> flavors;
  nsresult rv = aTransferable->FlavorsTransferableCanExport(flavors);
  if (NS_FAILED(rv)) {
    MOZ_CLIPBOARD_LOG("    FlavorsTransferableCanExport failed!\n");
    // Fall through.  |gtkTargets| will be null below.
  }

  // Add all the flavors to this widget's supported type.
  bool imagesAdded = false;
  for (uint32_t i = 0; i < flavors.Length(); i++) {
    nsCString& flavorStr = flavors[i];
    MOZ_CLIPBOARD_LOG("    processing target %s\n", flavorStr.get());

    // Special case text/plain since we can handle all of the string types.
    if (flavorStr.EqualsLiteral(kTextMime)) {
      MOZ_CLIPBOARD_LOG("    adding TEXT targets\n");
      gtk_target_list_add_text_targets(list, 0);
      continue;
    }

    if (nsContentUtils::IsFlavorImage(flavorStr)) {
      // Don't bother adding image targets twice
      if (!imagesAdded) {
        // accept any writable image type
        MOZ_CLIPBOARD_LOG("    adding IMAGE targets\n");
        gtk_target_list_add_image_targets(list, 0, TRUE);
        imagesAdded = true;
      }
      continue;
    }

    if (flavorStr.EqualsLiteral(kFileMime)) {
      MOZ_CLIPBOARD_LOG("    adding text/uri-list target\n");
      GdkAtom atom = gdk_atom_intern(kURIListMime, FALSE);
      gtk_target_list_add(list, atom, 0, 0);
      continue;
    }

    // Add this to our list of valid targets
    MOZ_CLIPBOARD_LOG("    adding OTHER target %s\n", flavorStr.get());
    GdkAtom atom = gdk_atom_intern(flavorStr.get(), FALSE);
    gtk_target_list_add(list, atom, 0, 0);
  }

  // Try to exclude private data from clipboard history.
  if (aTransferable->GetIsPrivateData()) {
    GdkAtom atom = gdk_atom_intern(kKDEPasswordManagerHintMime, FALSE);
    gtk_target_list_add(list, atom, 0, 0);
  }

  // Get GTK clipboard (CLIPBOARD or PRIMARY)
  GtkClipboard* gtkClipboard =
      gtk_clipboard_get(GetSelectionAtom(aWhichClipboard));

  gint numTargets = 0;
  GtkTargetEntry* gtkTargets =
      gtk_target_table_new_from_list(list, &numTargets);
  if (!gtkTargets || numTargets == 0) {
    MOZ_CLIPBOARD_LOG(
        "    gtk_target_table_new_from_list() failed or empty list of "
        "targets!\n");
    // Clear references to the any old data and let GTK know that it is no
    // longer available.
    EmptyNativeClipboardData(aWhichClipboard);
    return NS_ERROR_FAILURE;
  }

  ClearCachedTargets(aWhichClipboard);

  // Set getcallback and request to store data after an application exit
  if (gtk_clipboard_set_with_data(gtkClipboard, gtkTargets, numTargets,
                                  clipboard_get_cb, clipboard_clear_cb, this)) {
    // We managed to set-up the clipboard so update internal state
    // We have to set it now because gtk_clipboard_set_with_data() calls
    // clipboard_clear_cb() which reset our internal state
    if (aWhichClipboard == kSelectionClipboard) {
      mSelectionSequenceNumber++;
      mSelectionTransferable = aTransferable;
    } else {
      mGlobalSequenceNumber++;
      mGlobalTransferable = aTransferable;
      gtk_clipboard_set_can_store(gtkClipboard, gtkTargets, numTargets);
    }

    rv = NS_OK;
  } else {
    MOZ_CLIPBOARD_LOG("    gtk_clipboard_set_with_data() failed!\n");
    EmptyNativeClipboardData(aWhichClipboard);
    rv = NS_ERROR_FAILURE;
  }

  gtk_target_table_free(gtkTargets, numTargets);
  gtk_target_list_unref(list);

  return rv;
}

mozilla::Result<int32_t, nsresult>
nsClipboard::GetNativeClipboardSequenceNumber(ClipboardType aWhichClipboard) {
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));
  return aWhichClipboard == kSelectionClipboard ? mSelectionSequenceNumber
                                                : mGlobalSequenceNumber;
}

// When clipboard contains only images, X11/Gtk tries to convert them
// to text when we request text instead of just fail to provide the data.
// So if clipboard contains images only remove text MIME offer.
bool nsClipboard::HasSuitableData(int32_t aWhichClipboard,
                                  const nsACString& aFlavor) {
  MOZ_CLIPBOARD_LOG("%s for %s", __FUNCTION__,
                    PromiseFlatCString(aFlavor).get());

  auto targets = mContext->GetTargets(aWhichClipboard);
  if (!targets) {
    MOZ_CLIPBOARD_LOG("    X11: no targes at clipboard (null), quit.\n");
    // It is possible that clipboard owner doesn't provide TARGETS properly, but
    // the text data is still available.
    return aFlavor.EqualsLiteral(kTextMime);
  }

  for (const auto& atom : targets.AsSpan()) {
    GUniquePtr<gchar> atom_name(gdk_atom_name(atom));
    if (!atom_name) {
      continue;
    }
    // Filter out system MIME types.
    if (strcmp(atom_name.get(), "TARGETS") == 0 ||
        strcmp(atom_name.get(), "TIMESTAMP") == 0 ||
        strcmp(atom_name.get(), "SAVE_TARGETS") == 0 ||
        strcmp(atom_name.get(), "MULTIPLE") == 0) {
      continue;
    }
    // Filter out types which can't be converted to text.
    if (strncmp(atom_name.get(), "image/", 6) == 0 ||
        strncmp(atom_name.get(), "application/", 12) == 0 ||
        strncmp(atom_name.get(), "audio/", 6) == 0 ||
        strncmp(atom_name.get(), "video/", 6) == 0) {
      continue;
    }
    // We have some other MIME type on clipboard which can be hopefully
    // converted to text without any problem.
    // XXX should only return true for text/plain type?
    MOZ_CLIPBOARD_LOG(
        "    X11: text types in clipboard, no need to filter them.\n");
    return true;
  }

  // So make sure we offer only types we have at clipboard.
  for (const auto& atom : targets.AsSpan()) {
    GUniquePtr<gchar> atom_name(gdk_atom_name(atom));
    if (!atom_name) {
      continue;
    }
    if (aFlavor.Equals(atom_name.get())) {
      return true;
    }
  }

  MOZ_CLIPBOARD_LOG("    X11: no suitable data in clipboard, quit.\n");
  return false;
}

static already_AddRefed<nsIFile> GetFileData(const nsACString& aURIList) {
  nsCOMPtr<nsIFile> file;
  nsTArray<nsCString> uris = mozilla::widget::ParseTextURIList(aURIList);
  if (!uris.IsEmpty()) {
    nsCOMPtr<nsIURI> fileURI;
    NS_NewURI(getter_AddRefs(fileURI), uris[0]);
    if (nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(fileURI)) {
      fileURL->GetFile(getter_AddRefs(file));
    }
  }
  return file.forget();
}

static already_AddRefed<nsISupports> GetHTMLData(Span<const char> aData) {
  nsLiteralCString mimeType(kHTMLMime);

  // Convert text/html into our text format
  nsAutoCString charset;
  if (!GetHTMLCharset(aData, charset)) {
    // Fall back to utf-8 in case html/data is missing kHTMLMarkupPrefix.
    MOZ_CLIPBOARD_LOG(
        "Failed to get html/text encoding, fall back to utf-8.\n");
    charset.AssignLiteral("utf-8");
  }

  MOZ_CLIPBOARD_LOG("GetHTMLData: HTML detected charset %s", charset.get());
  // app which use "text/html" to copy&paste
  // get the decoder
  auto encoding = Encoding::ForLabelNoReplacement(charset);
  if (!encoding) {
    MOZ_CLIPBOARD_LOG("GetHTMLData: get unicode decoder error (charset: %s)",
                      charset.get());
    return nullptr;
  }

  // According to spec html UTF-16BE/LE should be switched to UTF-8
  // https://html.spec.whatwg.org/#determining-the-character-encoding:utf-16-encoding-2
  if (encoding == UTF_16LE_ENCODING || encoding == UTF_16BE_ENCODING) {
    encoding = UTF_8_ENCODING;
  }

  // Remove kHTMLMarkupPrefix again, it won't necessarily cause any
  // issues, but might confuse other users.
  const size_t prefixLen = std::size(kHTMLMarkupPrefix) - 1;
  if (aData.Length() >= prefixLen && nsDependentCSubstring(aData.To(prefixLen))
                                         .EqualsLiteral(kHTMLMarkupPrefix)) {
    aData = aData.From(prefixLen);
  }

  nsAutoString unicodeData;
  auto [rv, enc] = encoding->Decode(AsBytes(aData), unicodeData);
#if MOZ_LOGGING
  if (enc != UTF_8_ENCODING && MOZ_CLIPBOARD_LOG_ENABLED()) {
    nsCString decoderName;
    enc->Name(decoderName);
    MOZ_CLIPBOARD_LOG("GetHTMLData: expected UTF-8 decoder but got %s",
                      decoderName.get());
  }
#endif
  if (NS_FAILED(rv)) {
    MOZ_CLIPBOARD_LOG("GetHTMLData: failed to decode HTML");
    return nullptr;
  }

  nsCOMPtr<nsISupports> wrapper;
  nsPrimitiveHelpers::CreatePrimitiveForData(
      mimeType, (const char*)unicodeData.BeginReading(),
      unicodeData.Length() * sizeof(char16_t), getter_AddRefs(wrapper));
  return wrapper.forget();
}

mozilla::Result<nsCOMPtr<nsISupports>, nsresult>
nsClipboard::GetNativeClipboardData(const nsACString& aFlavor,
                                    ClipboardType aWhichClipboard) {
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));

  MOZ_CLIPBOARD_LOG(
      "nsClipboard::GetNativeClipboardData (%s) for %s\n",
      aWhichClipboard == kSelectionClipboard ? "primary" : "clipboard",
      PromiseFlatCString(aFlavor).get());

  // TODO: Ensure we don't re-enter here.
  if (!mContext) {
    return Err(NS_ERROR_FAILURE);
  }

  // Filter out MIME types on X11 to prevent unwanted conversions,
  // see Bug 1611407
  if (widget::GdkIsX11Display() && !HasSuitableData(aWhichClipboard, aFlavor)) {
    MOZ_CLIPBOARD_LOG("    Missing suitable clipboard data, quit.");
    return nsCOMPtr<nsISupports>{};
  }

  if (aFlavor.EqualsLiteral(kJPEGImageMime) ||
      aFlavor.EqualsLiteral(kJPGImageMime) ||
      aFlavor.EqualsLiteral(kPNGImageMime) ||
      aFlavor.EqualsLiteral(kGIFImageMime)) {
    // Emulate support for image/jpg
    nsAutoCString flavor(aFlavor.EqualsLiteral(kJPGImageMime)
                             ? kJPEGImageMime
                             : PromiseFlatCString(aFlavor).get());
    MOZ_CLIPBOARD_LOG("    Getting image %s MIME clipboard data\n",
                      flavor.get());

    auto clipboardData =
        mContext->GetClipboardData(flavor.get(), aWhichClipboard);
    if (!clipboardData) {
      MOZ_CLIPBOARD_LOG("    %s type is missing\n", flavor.get());
      return nsCOMPtr<nsISupports>{};
    }

    nsCOMPtr<nsIInputStream> byteStream;
    NS_NewByteInputStream(getter_AddRefs(byteStream), clipboardData.AsSpan(),
                          NS_ASSIGNMENT_COPY);

    MOZ_CLIPBOARD_LOG("    got %s MIME data\n", flavor.get());
    return nsCOMPtr<nsISupports>(std::move(byteStream));
  }

  // Special case text/plain since we can convert any
  // string into text/plain
  if (aFlavor.EqualsLiteral(kTextMime)) {
    MOZ_CLIPBOARD_LOG("    Getting text %s MIME clipboard data\n",
                      PromiseFlatCString(aFlavor).get());

    auto clipboardData = mContext->GetClipboardText(aWhichClipboard);
    if (!clipboardData) {
      MOZ_CLIPBOARD_LOG("    failed to get text data\n");
      // If the type was text/plain and we couldn't get
      // text off the clipboard, run the next loop
      // iteration.
      return nsCOMPtr<nsISupports>{};
    }

    // Convert utf-8 into our text format.
    NS_ConvertUTF8toUTF16 ucs2string(clipboardData.get());

    nsCOMPtr<nsISupports> wrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(
        aFlavor, (const char*)ucs2string.BeginReading(),
        ucs2string.Length() * 2, getter_AddRefs(wrapper));

    MOZ_CLIPBOARD_LOG("    got text data, length %zd\n", ucs2string.Length());
    return wrapper;
  }

  if (aFlavor.EqualsLiteral(kFileMime)) {
    MOZ_CLIPBOARD_LOG("    Getting file %s MIME clipboard data\n",
                      PromiseFlatCString(aFlavor).get());

    auto clipboardData =
        mContext->GetClipboardData(kURIListMime, aWhichClipboard);
    if (!clipboardData) {
      MOZ_CLIPBOARD_LOG("    text/uri-list type is missing\n");
      return nsCOMPtr<nsISupports>{};
    }

    nsDependentCSubstring fileName(clipboardData.AsSpan());
    if (nsCOMPtr<nsIFile> file = GetFileData(fileName)) {
      MOZ_CLIPBOARD_LOG("    got file data\n");
      return nsCOMPtr<nsISupports>(std::move(file));
    }

    MOZ_CLIPBOARD_LOG("    failed to get file data\n");
    return nsCOMPtr<nsISupports>{};
  }

  MOZ_CLIPBOARD_LOG("    Getting %s MIME clipboard data\n",
                    PromiseFlatCString(aFlavor).get());

  auto clipboardData = mContext->GetClipboardData(
      PromiseFlatCString(aFlavor).get(), aWhichClipboard);
  if (!clipboardData) {
    MOZ_CLIPBOARD_LOG("    failed to get clipboard content.\n");
    return nsCOMPtr<nsISupports>{};
  }

  MOZ_CLIPBOARD_LOG("    got %s mime type data.\n",
                    PromiseFlatCString(aFlavor).get());

  // Special case text/html since we can convert into UCS2
  auto span = clipboardData.AsSpan();
  if (aFlavor.EqualsLiteral(kHTMLMime)) {
    return nsCOMPtr<nsISupports>(GetHTMLData(span));
  }

  nsCOMPtr<nsISupports> wrapper;
  nsPrimitiveHelpers::CreatePrimitiveForData(
      aFlavor, span.data(), span.Length(), getter_AddRefs(wrapper));
  return wrapper;
}

enum DataType {
  DATATYPE_IMAGE,
  DATATYPE_FILE,
  DATATYPE_HTML,
  DATATYPE_RAW,
};

struct DataCallbackHandler {
  nsBaseClipboard::GetNativeDataCallback mDataCallback;
  nsCString mMimeType;
  DataType mDataType;

  DataCallbackHandler(nsBaseClipboard::GetNativeDataCallback&& aDataCallback,
                      const nsACString& aMimeType,
                      DataType aDataType = DATATYPE_RAW)
      : mDataCallback(std::move(aDataCallback)),
        mMimeType(aMimeType),
        mDataType(aDataType) {
    MOZ_COUNT_CTOR(DataCallbackHandler);
    MOZ_CLIPBOARD_LOG("DataCallbackHandler created [%p] MIME %s type %d", this,
                      mMimeType.get(), mDataType);
  }
  ~DataCallbackHandler() {
    MOZ_CLIPBOARD_LOG("DataCallbackHandler deleted [%p]", this);
    MOZ_COUNT_DTOR(DataCallbackHandler);
  }
};

static void AsyncGetTextImpl(
    int32_t aWhichClipboard,
    nsBaseClipboard::GetNativeDataCallback&& aCallback) {
  MOZ_CLIPBOARD_LOG("AsyncGetText() type '%s'",
                    aWhichClipboard == nsClipboard::kSelectionClipboard
                        ? "primary"
                        : "clipboard");

  gtk_clipboard_request_text(
      gtk_clipboard_get(GetSelectionAtom(aWhichClipboard)),
      [](GtkClipboard* aClipboard, const gchar* aText, gpointer aData) -> void {
        UniquePtr<DataCallbackHandler> ref(
            static_cast<DataCallbackHandler*>(aData));
        MOZ_CLIPBOARD_LOG("AsyncGetText async handler of [%p]", aData);

        size_t dataLength = aText ? strlen(aText) : 0;
        if (dataLength <= 0) {
          MOZ_CLIPBOARD_LOG("  quit, text is not available");
          ref->mDataCallback(nsCOMPtr<nsISupports>{});
          return;
        }

        // Convert utf-8 into our unicode format.
        NS_ConvertUTF8toUTF16 utf16string(aText, dataLength);
        nsLiteralCString flavor(kTextMime);

        nsCOMPtr<nsISupports> wrapper;
        nsPrimitiveHelpers::CreatePrimitiveForData(
            flavor, (const char*)utf16string.BeginReading(),
            utf16string.Length() * 2, getter_AddRefs(wrapper));

        MOZ_CLIPBOARD_LOG("  text is set, length = %d", (int)dataLength);
        ref->mDataCallback(wrapper);
      },
      new DataCallbackHandler(std::move(aCallback),
                              nsLiteralCString(kTextMime)));
}

static void AsyncGetDataImpl(
    int32_t aWhichClipboard, const nsACString& aMimeType, DataType aDataType,
    nsBaseClipboard::GetNativeDataCallback&& aCallback) {
  MOZ_CLIPBOARD_LOG("AsyncGetData() type '%s'",
                    aWhichClipboard == nsClipboard::kSelectionClipboard
                        ? "primary"
                        : "clipboard");

  gtk_clipboard_request_contents(
      gtk_clipboard_get(GetSelectionAtom(aWhichClipboard)),
      // Don't ask Gtk for application/x-moz-file.
      gdk_atom_intern((aDataType == DATATYPE_FILE)
                          ? kURIListMime
                          : PromiseFlatCString(aMimeType).get(),
                      FALSE),
      [](GtkClipboard* aClipboard, GtkSelectionData* aSelection,
         gpointer aData) -> void {
        UniquePtr<DataCallbackHandler> ref(
            static_cast<DataCallbackHandler*>(aData));
        MOZ_CLIPBOARD_LOG("AsyncGetData async handler [%p] MIME %s type %d",
                          aData, ref->mMimeType.get(), ref->mDataType);

        int dataLength = gtk_selection_data_get_length(aSelection);
        if (dataLength <= 0) {
          ref->mDataCallback(nsCOMPtr<nsISupports>{});
          return;
        }
        const char* data = (const char*)gtk_selection_data_get_data(aSelection);
        if (!data) {
          ref->mDataCallback(nsCOMPtr<nsISupports>{});
          return;
        }
        switch (ref->mDataType) {
          case DATATYPE_IMAGE: {
            MOZ_CLIPBOARD_LOG("  get image clipboard data");
            nsCOMPtr<nsIInputStream> byteStream;
            NS_NewByteInputStream(getter_AddRefs(byteStream),
                                  Span(data, dataLength), NS_ASSIGNMENT_COPY);
            ref->mDataCallback(nsCOMPtr<nsISupports>(byteStream));
            return;
          }
          case DATATYPE_FILE: {
            MOZ_CLIPBOARD_LOG("  get file clipboard data");
            nsDependentCSubstring uriList(data, dataLength);
            if (nsCOMPtr<nsIFile> file = GetFileData(uriList)) {
              MOZ_CLIPBOARD_LOG("  successfully get file data\n");
              ref->mDataCallback(nsCOMPtr<nsISupports>(file));
              return;
            }
            break;
          }
          case DATATYPE_HTML: {
            MOZ_CLIPBOARD_LOG("  html clipboard data");
            Span dataSpan(data, dataLength);
            if (nsCOMPtr<nsISupports> data = GetHTMLData(dataSpan)) {
              MOZ_CLIPBOARD_LOG("  successfully get HTML data\n");
              ref->mDataCallback(nsCOMPtr<nsISupports>(data));
              return;
            }
            break;
          }
          case DATATYPE_RAW: {
            MOZ_CLIPBOARD_LOG("  raw clipboard data %s", ref->mMimeType.get());

            nsCOMPtr<nsISupports> wrapper;
            nsPrimitiveHelpers::CreatePrimitiveForData(
                ref->mMimeType, data, dataLength, getter_AddRefs(wrapper));
            ref->mDataCallback(nsCOMPtr<nsISupports>(wrapper));
            return;
          }
        }
        ref->mDataCallback(nsCOMPtr<nsISupports>{});
      },
      new DataCallbackHandler(std::move(aCallback), aMimeType, aDataType));
}

static void AsyncGetDataFlavor(
    int32_t aWhichClipboard, const nsACString& aFlavorStr,
    nsBaseClipboard::GetNativeDataCallback&& aCallback) {
  if (aFlavorStr.EqualsLiteral(kJPEGImageMime) ||
      aFlavorStr.EqualsLiteral(kJPGImageMime) ||
      aFlavorStr.EqualsLiteral(kPNGImageMime) ||
      aFlavorStr.EqualsLiteral(kGIFImageMime)) {
    // Emulate support for image/jpg
    nsAutoCString flavor(aFlavorStr.EqualsLiteral(kJPGImageMime)
                             ? kJPEGImageMime
                             : PromiseFlatCString(aFlavorStr).get());
    MOZ_CLIPBOARD_LOG("  Getting image %s MIME clipboard data", flavor.get());
    AsyncGetDataImpl(aWhichClipboard, flavor, DATATYPE_IMAGE,
                     std::move(aCallback));
    return;
  }
  // Special case text/plain since we can convert any
  // string into text/plain
  if (aFlavorStr.EqualsLiteral(kTextMime)) {
    MOZ_CLIPBOARD_LOG("  Getting unicode clipboard data");
    AsyncGetTextImpl(aWhichClipboard, std::move(aCallback));
    return;
  }
  if (aFlavorStr.EqualsLiteral(kFileMime)) {
    MOZ_CLIPBOARD_LOG("  Getting file clipboard data\n");
    AsyncGetDataImpl(aWhichClipboard, aFlavorStr, DATATYPE_FILE,
                     std::move(aCallback));
    return;
  }
  if (aFlavorStr.EqualsLiteral(kHTMLMime)) {
    MOZ_CLIPBOARD_LOG("  Getting HTML clipboard data");
    AsyncGetDataImpl(aWhichClipboard, aFlavorStr, DATATYPE_HTML,
                     std::move(aCallback));
    return;
  }
  MOZ_CLIPBOARD_LOG("  Getting raw %s MIME clipboard data\n",
                    PromiseFlatCString(aFlavorStr).get());
  AsyncGetDataImpl(aWhichClipboard, aFlavorStr, DATATYPE_RAW,
                   std::move(aCallback));
}

void nsClipboard::AsyncGetNativeClipboardData(
    const nsACString& aFlavor, ClipboardType aWhichClipboard,
    GetNativeDataCallback&& aCallback) {
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));

  MOZ_CLIPBOARD_LOG("nsClipboard::AsyncGetNativeClipboardData (%s) for %s",
                    aWhichClipboard == nsClipboard::kSelectionClipboard
                        ? "primary"
                        : "clipboard",
                    PromiseFlatCString(aFlavor).get());

  // Filter out MIME types on X11 to prevent unwanted conversions,
  // see Bug 1611407
  if (widget::GdkIsX11Display()) {
    AsyncHasNativeClipboardDataMatchingFlavors(
        nsTArray<nsCString>{PromiseFlatCString(aFlavor)}, aWhichClipboard,
        [aWhichClipboard,
         callback = std::move(aCallback)](auto aResultOrError) mutable {
          if (aResultOrError.isErr()) {
            callback(Err(aResultOrError.unwrapErr()));
            return;
          }

          nsTArray<nsCString> clipboardFlavors =
              std::move(aResultOrError.unwrap());
          if (!clipboardFlavors.Length()) {
            MOZ_CLIPBOARD_LOG("  no flavors in clipboard, quit.");
            callback(nsCOMPtr<nsISupports>{});
            return;
          }

          AsyncGetDataFlavor(aWhichClipboard, clipboardFlavors[0],
                             std::move(callback));
        });
    return;
  }

  // Read clipboard directly on Wayland
  AsyncGetDataFlavor(aWhichClipboard, aFlavor, std::move(aCallback));
}

nsresult nsClipboard::EmptyNativeClipboardData(ClipboardType aWhichClipboard) {
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));

  MOZ_CLIPBOARD_LOG(
      "nsClipboard::EmptyNativeClipboardData (%s)\n",
      aWhichClipboard == kSelectionClipboard ? "primary" : "clipboard");
  if (aWhichClipboard == kSelectionClipboard) {
    if (mSelectionTransferable) {
      gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_PRIMARY));
      MOZ_ASSERT(!mSelectionTransferable);
    }
  } else {
    if (mGlobalTransferable) {
      gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
      MOZ_ASSERT(!mGlobalTransferable);
    }
  }
  ClearCachedTargets(aWhichClipboard);
  return NS_OK;
}

void nsClipboard::ClearTransferable(int32_t aWhichClipboard) {
  if (aWhichClipboard == kSelectionClipboard) {
    mSelectionSequenceNumber++;
    mSelectionTransferable = nullptr;
  } else {
    mGlobalSequenceNumber++;
    mGlobalTransferable = nullptr;
  }
}

static bool FlavorMatchesTarget(const nsACString& aFlavor, GdkAtom aTarget) {
  GUniquePtr<gchar> atom_name(gdk_atom_name(aTarget));
  if (!atom_name) {
    return false;
  }
  if (aFlavor.Equals(atom_name.get())) {
    return true;
  }
  // X clipboard supports image/jpeg, but we want to emulate support
  // for image/jpg as well
  if (aFlavor.EqualsLiteral(kJPGImageMime) &&
      !strcmp(atom_name.get(), kJPEGImageMime)) {
    return true;
  }
  // application/x-moz-file should be treated like text/uri-list
  if (aFlavor.EqualsLiteral(kFileMime) &&
      !strcmp(atom_name.get(), kURIListMime)) {
    MOZ_CLIPBOARD_LOG(
        "    has text/uri-list treating as application/x-moz-file");
    return true;
  }
  return false;
}

mozilla::Result<bool, nsresult>
nsClipboard::HasNativeClipboardDataMatchingFlavors(
    const nsTArray<nsCString>& aFlavorList, ClipboardType aWhichClipboard) {
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));

  MOZ_CLIPBOARD_LOG(
      "nsClipboard::HasNativeClipboardDataMatchingFlavors (%s)\n",
      aWhichClipboard == kSelectionClipboard ? "primary" : "clipboard");

  if (!mContext) {
    MOZ_CLIPBOARD_LOG("    nsRetrievalContext is not available\n");
    return Err(NS_ERROR_FAILURE);
  }

  auto targets = mContext->GetTargets(aWhichClipboard);
  if (!targets) {
    MOZ_CLIPBOARD_LOG("    no targes at clipboard (null)\n");
    // If TARGETS is not available, fallback to checking for text data directly,
    // as the clipboard owner might not set TARGETS properly, but the text is
    // still available.
    for (auto& flavor : aFlavorList) {
      if (flavor.EqualsLiteral(kTextMime)) {
        MOZ_CLIPBOARD_LOG("    try text data\n");
        if (auto clipboardData = mContext->GetClipboardText(aWhichClipboard)) {
          return true;
        }
        MOZ_CLIPBOARD_LOG("    no text data\n");
      }
    }
    return false;
  }

#ifdef MOZ_LOGGING
  if (MOZ_CLIPBOARD_LOG_ENABLED()) {
    MOZ_CLIPBOARD_LOG("    Clipboard content (target nums %zu):\n",
                      targets.AsSpan().Length());
    for (const auto& target : targets.AsSpan()) {
      GUniquePtr<gchar> atom_name(gdk_atom_name(target));
      if (!atom_name) {
        MOZ_CLIPBOARD_LOG("        failed to get MIME\n");
        continue;
      }
      MOZ_CLIPBOARD_LOG("        MIME %s\n", atom_name.get());
    }
  }
#endif

  // Walk through the provided types and try to match it to a
  // provided type.
  for (auto& flavor : aFlavorList) {
    // We special case text/plain here.
    if (flavor.EqualsLiteral(kTextMime) &&
        gtk_targets_include_text(targets.AsSpan().data(),
                                 targets.AsSpan().Length())) {
      return true;
    }
    for (const auto& target : targets.AsSpan()) {
      if (FlavorMatchesTarget(flavor, target)) {
        return true;
      }
    }
  }

  MOZ_CLIPBOARD_LOG("    no matched targes at clipboard\n");
  return false;
}

struct TragetCallbackHandler {
  TragetCallbackHandler(const nsTArray<nsCString>& aAcceptedFlavorList,
                        nsBaseClipboard::HasMatchingFlavorsCallback&& aCallback)
      : mAcceptedFlavorList(aAcceptedFlavorList.Clone()),
        mCallback(std::move(aCallback)) {
    MOZ_CLIPBOARD_LOG("TragetCallbackHandler(%p) created", this);
  }
  ~TragetCallbackHandler() {
    MOZ_CLIPBOARD_LOG("TragetCallbackHandler(%p) deleted", this);
  }
  nsTArray<nsCString> mAcceptedFlavorList;
  nsBaseClipboard::HasMatchingFlavorsCallback mCallback;
};

void nsClipboard::AsyncHasNativeClipboardDataMatchingFlavors(
    const nsTArray<nsCString>& aFlavorList, ClipboardType aWhichClipboard,
    nsBaseClipboard::HasMatchingFlavorsCallback&& aCallback) {
  MOZ_DIAGNOSTIC_ASSERT(
      nsIClipboard::IsClipboardTypeSupported(aWhichClipboard));

  MOZ_CLIPBOARD_LOG(
      "nsClipboard::AsyncHasNativeClipboardDataMatchingFlavors (%s)",
      aWhichClipboard == kSelectionClipboard ? "primary" : "clipboard");

  gtk_clipboard_request_contents(
      gtk_clipboard_get(GetSelectionAtom(aWhichClipboard)),
      gdk_atom_intern("TARGETS", FALSE),
      [](GtkClipboard* aClipboard, GtkSelectionData* aSelection,
         gpointer aData) -> void {
        MOZ_CLIPBOARD_LOG("gtk_clipboard_request_contents async handler (%p)",
                          aData);
        UniquePtr<TragetCallbackHandler> handler(
            static_cast<TragetCallbackHandler*>(aData));

        if (gtk_selection_data_get_length(aSelection) > 0) {
          GdkAtom* targets = nullptr;
          gint targetsNum = 0;
          gtk_selection_data_get_targets(aSelection, &targets, &targetsNum);

          if (targetsNum > 0) {
#ifdef MOZ_LOGGING
            if (MOZ_CLIPBOARD_LOG_ENABLED()) {
              MOZ_CLIPBOARD_LOG("    Clipboard content (target nums %d):\n",
                                targetsNum);
              for (int i = 0; i < targetsNum; i++) {
                GUniquePtr<gchar> atom_name(gdk_atom_name(targets[i]));
                if (!atom_name) {
                  MOZ_CLIPBOARD_LOG("        failed to get MIME\n");
                  continue;
                }
                MOZ_CLIPBOARD_LOG("        MIME %s\n", atom_name.get());
              }
            }
#endif

            nsTArray<nsCString> results;
            for (auto& flavor : handler->mAcceptedFlavorList) {
              if (flavor.EqualsLiteral(kTextMime) &&
                  gtk_targets_include_text(targets, targetsNum)) {
                results.AppendElement(flavor);
                continue;
              }
              for (int i = 0; i < targetsNum; i++) {
                if (FlavorMatchesTarget(flavor, targets[i])) {
                  results.AppendElement(flavor);
                }
              }
            }
            handler->mCallback(std::move(results));
            return;
          }
        }

        // If TARGETS is not available, fallback to checking for text data
        // directly, as the clipboard owner might not set TARGETS properly, but
        // the text is still available.
        MOZ_CLIPBOARD_LOG("    no targets found\n");
        for (auto& flavor : handler->mAcceptedFlavorList) {
          if (flavor.EqualsLiteral(kTextMime)) {
            MOZ_CLIPBOARD_LOG("    try text data\n");
            gtk_clipboard_request_text(
                aClipboard,
                [](GtkClipboard* aClipboard, const gchar* aText,
                   gpointer aData) -> void {
                  MOZ_CLIPBOARD_LOG(
                      "gtk_clipboard_request_text async handler (%p)", aData);
                  UniquePtr<TragetCallbackHandler> handler(
                      static_cast<TragetCallbackHandler*>(aData));

                  nsTArray<nsCString> results;
                  if (aText) {
                    results.AppendElement(kTextMime);
                  }
                  handler->mCallback(std::move(results));
                },
                handler.release());
            return;
          }
        }

        handler->mCallback(nsTArray<nsCString>{});
      },
      new TragetCallbackHandler(aFlavorList, std::move(aCallback)));
}

nsITransferable* nsClipboard::GetTransferable(int32_t aWhichClipboard) {
  nsITransferable* retval;

  if (aWhichClipboard == kSelectionClipboard)
    retval = mSelectionTransferable.get();
  else
    retval = mGlobalTransferable.get();

  return retval;
}

void nsClipboard::SelectionGetEvent(GtkClipboard* aClipboard,
                                    GtkSelectionData* aSelectionData) {
  // Someone has asked us to hand them something.  The first thing
  // that we want to do is see if that something includes text.  If
  // it does, try to give it text/plain after converting it to
  // utf-8.

  int32_t whichClipboard;

  // which clipboard?
  GdkAtom selection = gtk_selection_data_get_selection(aSelectionData);
  if (selection == GDK_SELECTION_PRIMARY)
    whichClipboard = kSelectionClipboard;
  else if (selection == GDK_SELECTION_CLIPBOARD)
    whichClipboard = kGlobalClipboard;
  else
    return;  // THAT AIN'T NO CLIPBOARD I EVER HEARD OF

  MOZ_CLIPBOARD_LOG(
      "nsClipboard::SelectionGetEvent (%s)\n",
      whichClipboard == kSelectionClipboard ? "primary" : "clipboard");

  nsCOMPtr<nsITransferable> trans = GetTransferable(whichClipboard);
  if (!trans) {
    // We have nothing to serve
    MOZ_CLIPBOARD_LOG(
        "nsClipboard::SelectionGetEvent() - %s clipboard is empty!\n",
        whichClipboard == kSelectionClipboard ? "Primary" : "Clipboard");
    return;
  }

  nsresult rv;
  nsCOMPtr<nsISupports> item;

  GdkAtom selectionTarget = gtk_selection_data_get_target(aSelectionData);
  MOZ_CLIPBOARD_LOG("  selection target %s\n",
                    GUniquePtr<gchar>(gdk_atom_name(selectionTarget)).get());

  // Check to see if the selection data is some text type.
  if (gtk_targets_include_text(&selectionTarget, 1)) {
    MOZ_CLIPBOARD_LOG("  providing text/plain data\n");
    // Try to convert our internal type into a text string.  Get
    // the transferable for this clipboard and try to get the
    // text/plain type for it.
    rv = trans->GetTransferData("text/plain", getter_AddRefs(item));
    if (NS_FAILED(rv) || !item) {
      MOZ_CLIPBOARD_LOG("  GetTransferData() failed to get text/plain!\n");
      return;
    }

    nsCOMPtr<nsISupportsString> wideString;
    wideString = do_QueryInterface(item);
    if (!wideString) return;

    nsAutoString ucs2string;
    wideString->GetData(ucs2string);
    NS_ConvertUTF16toUTF8 utf8string(ucs2string);

    MOZ_CLIPBOARD_LOG("  sent %zd bytes of utf-8 data\n", utf8string.Length());
    if (selectionTarget == gdk_atom_intern("text/plain;charset=utf-8", FALSE)) {
      MOZ_CLIPBOARD_LOG(
          "  using gtk_selection_data_set for 'text/plain;charset=utf-8'\n");
      // Bypass gtk_selection_data_set_text, which will convert \n to \r\n
      // in some versions of GTK.
      gtk_selection_data_set(aSelectionData, selectionTarget, 8,
                             reinterpret_cast<const guchar*>(utf8string.get()),
                             utf8string.Length());
    } else {
      gtk_selection_data_set_text(aSelectionData, utf8string.get(),
                                  utf8string.Length());
    }
    return;
  }

  // Check to see if the selection data is an image type
  if (gtk_targets_include_image(&selectionTarget, 1, TRUE)) {
    MOZ_CLIPBOARD_LOG("  providing image data\n");
    // Look through our transfer data for the image
    static const char* const imageMimeTypes[] = {kNativeImageMime,
                                                 kPNGImageMime, kJPEGImageMime,
                                                 kJPGImageMime, kGIFImageMime};
    nsCOMPtr<nsISupports> imageItem;
    nsCOMPtr<imgIContainer> image;
    for (uint32_t i = 0; i < std::size(imageMimeTypes); i++) {
      rv = trans->GetTransferData(imageMimeTypes[i], getter_AddRefs(imageItem));
      if (NS_FAILED(rv)) {
        MOZ_CLIPBOARD_LOG("    %s is missing at GetTransferData()\n",
                          imageMimeTypes[i]);
        continue;
      }

      image = do_QueryInterface(imageItem);
      if (image) {
        MOZ_CLIPBOARD_LOG("    %s is available at GetTransferData()\n",
                          imageMimeTypes[i]);
        break;
      }
    }

    if (!image) {  // Not getting an image for an image mime type!?
      MOZ_CLIPBOARD_LOG(
          "    Failed to get any image mime from GetTransferData()!\n");
      return;
    }

    RefPtr<GdkPixbuf> pixbuf = nsImageToPixbuf::ImageToPixbuf(image);
    if (!pixbuf) {
      MOZ_CLIPBOARD_LOG("    nsImageToPixbuf::ImageToPixbuf() failed!\n");
      return;
    }

    MOZ_CLIPBOARD_LOG("    Setting pixbuf image data as %s\n",
                      GUniquePtr<gchar>(gdk_atom_name(selectionTarget)).get());
    gtk_selection_data_set_pixbuf(aSelectionData, pixbuf);
    return;
  }

  if (selectionTarget == gdk_atom_intern(kHTMLMime, FALSE)) {
    MOZ_CLIPBOARD_LOG("  providing %s data\n", kHTMLMime);
    rv = trans->GetTransferData(kHTMLMime, getter_AddRefs(item));
    if (NS_FAILED(rv) || !item) {
      MOZ_CLIPBOARD_LOG("  failed to get %s data by GetTransferData()!\n",
                        kHTMLMime);
      return;
    }

    nsCOMPtr<nsISupportsString> wideString;
    wideString = do_QueryInterface(item);
    if (!wideString) {
      MOZ_CLIPBOARD_LOG("  failed to get wideString interface!");
      return;
    }

    nsAutoString ucs2string;
    wideString->GetData(ucs2string);

    nsAutoCString html;
    // Add the prefix so the encoding is correctly detected.
    html.AppendLiteral(kHTMLMarkupPrefix);
    AppendUTF16toUTF8(ucs2string, html);

    MOZ_CLIPBOARD_LOG("  Setting %zd bytes of %s data\n", html.Length(),
                      GUniquePtr<gchar>(gdk_atom_name(selectionTarget)).get());
    gtk_selection_data_set(aSelectionData, selectionTarget, 8,
                           (const guchar*)html.get(), html.Length());
    return;
  }

  // We put kFileMime onto the clipboard as kURIListMime.
  if (selectionTarget == gdk_atom_intern(kURIListMime, FALSE)) {
    MOZ_CLIPBOARD_LOG("  providing %s data\n", kURIListMime);
    rv = trans->GetTransferData(kFileMime, getter_AddRefs(item));
    if (NS_FAILED(rv) || !item) {
      MOZ_CLIPBOARD_LOG("  failed to get %s data by GetTransferData()!\n",
                        kFileMime);
      return;
    }

    nsCOMPtr<nsIFile> file = do_QueryInterface(item);
    if (!file) {
      MOZ_CLIPBOARD_LOG("  failed to get nsIFile interface!");
      return;
    }

    nsCOMPtr<nsIURI> fileURI;
    rv = NS_NewFileURI(getter_AddRefs(fileURI), file);
    if (NS_FAILED(rv)) {
      MOZ_CLIPBOARD_LOG("  failed to get fileURI\n");
      return;
    }

    nsAutoCString uri;
    if (NS_FAILED(fileURI->GetSpec(uri))) {
      MOZ_CLIPBOARD_LOG("  failed to get fileURI spec\n");
      return;
    }

    MOZ_CLIPBOARD_LOG("  Setting %zd bytes of data\n", uri.Length());
    gtk_selection_data_set(aSelectionData, selectionTarget, 8,
                           (const guchar*)uri.get(), uri.Length());
    return;
  }

  if (selectionTarget == gdk_atom_intern(kKDEPasswordManagerHintMime, FALSE)) {
    if (!trans->GetIsPrivateData()) {
      MOZ_CLIPBOARD_LOG(
          "  requested %s, but the data isn't actually private!\n",
          kKDEPasswordManagerHintMime);
      return;
    }

    static const char* kSecret = "secret";
    MOZ_CLIPBOARD_LOG("  Setting data to '%s' for %s\n", kSecret,
                      kKDEPasswordManagerHintMime);
    gtk_selection_data_set(aSelectionData, selectionTarget, 8,
                           (const guchar*)kSecret, strlen(kSecret));
    return;
  }

  MOZ_CLIPBOARD_LOG("  Try if we have anything at GetTransferData() for %s\n",
                    GUniquePtr<gchar>(gdk_atom_name(selectionTarget)).get());

  // Try to match up the selection data target to something our
  // transferable provides.
  GUniquePtr<gchar> target_name(gdk_atom_name(selectionTarget));
  if (!target_name) {
    MOZ_CLIPBOARD_LOG("  Failed to get target name!\n");
    return;
  }

  rv = trans->GetTransferData(target_name.get(), getter_AddRefs(item));
  // nothing found?
  if (NS_FAILED(rv) || !item) {
    MOZ_CLIPBOARD_LOG("  Failed to get anything from GetTransferData()!\n");
    return;
  }

  void* primitive_data = nullptr;
  uint32_t dataLen = 0;
  nsPrimitiveHelpers::CreateDataFromPrimitive(
      nsDependentCString(target_name.get()), item, &primitive_data, &dataLen);
  if (!primitive_data) {
    MOZ_CLIPBOARD_LOG("  Failed to get primitive data!\n");
    return;
  }

  MOZ_CLIPBOARD_LOG("  Setting %s as a primitive data type, %d bytes\n",
                    target_name.get(), dataLen);
  gtk_selection_data_set(aSelectionData, selectionTarget,
                         8, /* 8 bits in a unit */
                         (const guchar*)primitive_data, dataLen);
  free(primitive_data);
}

void nsClipboard::ClearCachedTargets(int32_t aWhichClipboard) {
  if (aWhichClipboard == kSelectionClipboard) {
    nsRetrievalContext::ClearCachedTargetsPrimary(nullptr, nullptr, nullptr);
  } else {
    nsRetrievalContext::ClearCachedTargetsClipboard(nullptr, nullptr, nullptr);
  }
}

void nsClipboard::SelectionClearEvent(GtkClipboard* aGtkClipboard) {
  Maybe<ClipboardType> whichClipboard = GetGeckoClipboardType(aGtkClipboard);
  if (whichClipboard.isNothing()) {
    return;
  }
  MOZ_CLIPBOARD_LOG(
      "nsClipboard::SelectionClearEvent (%s)\n",
      *whichClipboard == kSelectionClipboard ? "primary" : "clipboard");
  ClearCachedTargets(*whichClipboard);
  ClearTransferable(*whichClipboard);
  ClearClipboardCache(*whichClipboard);
}

void nsClipboard::OwnerChangedEvent(GtkClipboard* aGtkClipboard,
                                    GdkEventOwnerChange* aEvent) {
  Maybe<ClipboardType> whichClipboard = GetGeckoClipboardType(aGtkClipboard);
  if (whichClipboard.isNothing()) {
    return;
  }
  MOZ_CLIPBOARD_LOG(
      "nsClipboard::OwnerChangedEvent (%s)\n",
      *whichClipboard == kSelectionClipboard ? "primary" : "clipboard");
  GtkWidget* gtkWidget = [aEvent]() -> GtkWidget* {
    if (!aEvent->owner) {
      return nullptr;
    }
    gpointer user_data = nullptr;
    gdk_window_get_user_data(aEvent->owner, &user_data);
    return GTK_WIDGET(user_data);
  }();
  // If we can get GtkWidget from the current clipboard owner, this
  // owner-changed event must be triggered by ourself via calling
  // gtk_clipboard_set_with_data, the sequence number should already be handled.
  if (!gtkWidget) {
    if (*whichClipboard == kSelectionClipboard) {
      mSelectionSequenceNumber++;
    } else {
      mGlobalSequenceNumber++;
    }
  }

  ClearCachedTargets(*whichClipboard);
}

void clipboard_get_cb(GtkClipboard* aGtkClipboard,
                      GtkSelectionData* aSelectionData, guint info,
                      gpointer user_data) {
  MOZ_CLIPBOARD_LOG("clipboard_get_cb() callback\n");
  nsClipboard* clipboard = static_cast<nsClipboard*>(user_data);
  clipboard->SelectionGetEvent(aGtkClipboard, aSelectionData);
}

void clipboard_clear_cb(GtkClipboard* aGtkClipboard, gpointer user_data) {
  MOZ_CLIPBOARD_LOG("clipboard_clear_cb() callback\n");
  nsClipboard* clipboard = static_cast<nsClipboard*>(user_data);
  clipboard->SelectionClearEvent(aGtkClipboard);
}

void clipboard_owner_change_cb(GtkClipboard* aGtkClipboard,
                               GdkEventOwnerChange* aEvent,
                               gpointer aUserData) {
  MOZ_CLIPBOARD_LOG("clipboard_owner_change_cb() callback\n");
  nsClipboard* clipboard = static_cast<nsClipboard*>(aUserData);
  clipboard->OwnerChangedEvent(aGtkClipboard, aEvent);
}

/*
 * This function extracts the encoding label from the subset of HTML internal
 * encoding declaration syntax that uses the old long form with double quotes
 * and without spaces around the equals sign between the "content" attribute
 * name and the attribute value.
 *
 * This was added for the sake of an ancient version of StarOffice
 * in the pre-UTF-8 era in bug 123389. It is unclear if supporting
 * non-UTF-8 encodings is still necessary and if this function
 * still needs to exist.
 *
 * As of December 2022, both Gecko and LibreOffice emit an UTF-8
 * declaration that this function successfully extracts "UTF-8" from,
 * but that's also the default that we fall back on if this function
 * fails to extract a label.
 */
bool GetHTMLCharset(Span<const char> aData, nsCString& aFoundCharset) {
  // Assume ASCII first to find "charset" info
  const nsDependentCSubstring htmlStr(aData);
  nsACString::const_iterator start, end;
  htmlStr.BeginReading(start);
  htmlStr.EndReading(end);
  nsACString::const_iterator valueStart(start), valueEnd(start);

  if (CaseInsensitiveFindInReadable("CONTENT=\"text/html;"_ns, start, end)) {
    start = end;
    htmlStr.EndReading(end);

    if (CaseInsensitiveFindInReadable("charset="_ns, start, end)) {
      valueStart = end;
      start = end;
      htmlStr.EndReading(end);

      if (FindCharInReadable('"', start, end)) valueEnd = start;
    }
  }
  // find "charset" in HTML
  if (valueStart != valueEnd) {
    aFoundCharset = Substring(valueStart, valueEnd);
    ToUpperCase(aFoundCharset);
    return true;
  }
  return false;
}
