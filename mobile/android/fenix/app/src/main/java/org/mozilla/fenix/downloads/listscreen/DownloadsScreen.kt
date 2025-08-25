/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen

import android.content.Context
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.FloatingActionButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.text.Text
import mozilla.components.lib.state.ext.observeAsState
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.ExpandableListHeader
import org.mozilla.fenix.downloads.listscreen.store.DownloadListItem
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIAction
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState.Mode
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIStore
import org.mozilla.fenix.downloads.listscreen.store.FileItem
import org.mozilla.fenix.downloads.listscreen.store.HeaderItem
import org.mozilla.fenix.downloads.listscreen.store.TimeCategory
import org.mozilla.fenix.downloads.listscreen.ui.DownloadSearchField
import org.mozilla.fenix.downloads.listscreen.ui.FileListItem
import org.mozilla.fenix.downloads.listscreen.ui.Filters
import org.mozilla.fenix.downloads.listscreen.ui.ToolbarConfig
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Downloads screen that displays the list of downloads.
 *
 * @param downloadsStore The [DownloadUIStore] used to manage and access the state of download items.
 * @param onItemClick Callback invoked when a download item is clicked.
 * @param onNavigationIconClick Callback for the back button click in the toolbar.
 */
@Suppress("LongMethod")
@Composable
fun DownloadsScreen(
    downloadsStore: DownloadUIStore,
    onItemClick: (FileItem) -> Unit,
    onNavigationIconClick: () -> Unit,
) {
    val uiState by downloadsStore.observeAsState(initialValue = downloadsStore.state) { it }
    val snackbarHostState = remember { SnackbarHostState() }
    val coroutineScope = rememberCoroutineScope()
    val context = LocalContext.current
    val toolbarConfig = getToolbarConfig(mode = uiState.mode)

    BackHandler(uiState.isBackHandlerEnabled) {
        if (uiState.mode is Mode.Editing) {
            downloadsStore.dispatch(DownloadUIAction.ExitEditMode)
        } else if (uiState.isSearchFieldVisible) {
            downloadsStore.dispatch(DownloadUIAction.SearchBarDismissRequest)
        }
    }

    if (uiState.isDeleteDialogVisible) {
        DeleteDownloadFileDialog(
            onConfirmDelete = {
                downloadsStore.dispatch(DownloadUIAction.UpdateDeleteDialogVisibility(false))
                downloadsStore.dispatch(
                    DownloadUIAction.AddPendingDeletionSet(
                        downloadsStore.state.mode.selectedItems.map { it.id }.toSet(),
                    ),
                )
                showDeleteSnackbar(
                    selectedItems = (downloadsStore.state.mode.selectedItems.toSet()),
                    coroutineScope = coroutineScope,
                    snackbarHostState = snackbarHostState,
                    context = context,
                    undoAction = {
                        downloadsStore.dispatch(
                            DownloadUIAction.UndoPendingDeletion,
                        )
                    },
                )
                downloadsStore.dispatch(DownloadUIAction.ExitEditMode)
            },
            onCancel = {
                downloadsStore.dispatch(DownloadUIAction.UpdateDeleteDialogVisibility(false))
            },
        )
    }

    Scaffold(
        topBar = {
            DownloadsTopAppBar(
                modifier = Modifier.fillMaxWidth(),
                backgroundColor = toolbarConfig.backgroundColor,
                title = {
                    if (uiState.isSearchFieldVisible) {
                        DownloadSearchField(
                            initialText = uiState.searchQuery,
                            onValueChange = {
                                downloadsStore.dispatch(DownloadUIAction.SearchQueryEntered(it))
                            },
                            onSearchDismissRequest = {
                                downloadsStore.dispatch(DownloadUIAction.SearchBarDismissRequest)
                            },
                        )
                    } else {
                        Text(
                            text = toolbarConfig.title,
                            color = toolbarConfig.textColor,
                            style = FirefoxTheme.typography.headline6,
                        )
                    }
                },
                navigationIcon = {
                    if (!uiState.isSearchFieldVisible) {
                        IconButton(onClick = onNavigationIconClick) {
                            Icon(
                                painter = painterResource(R.drawable.mozac_ic_back_24),
                                contentDescription = stringResource(R.string.download_navigate_back_description),
                                tint = toolbarConfig.iconColor,
                            )
                        }
                    }
                },
                actions = {
                    if (uiState.mode is Mode.Editing) {
                        ToolbarEditActions(
                            downloadsStore = downloadsStore,
                            toolbarConfig = toolbarConfig,
                            onItemDeleteClick = { item ->
                                downloadsStore.dispatch(
                                    DownloadUIAction.AddPendingDeletionSet(
                                        setOf(item.id),
                                    ),
                                )
                                showDeleteSnackbar(
                                    selectedItems = setOf(item),
                                    coroutineScope = coroutineScope,
                                    snackbarHostState = snackbarHostState,
                                    context = context,
                                    undoAction = {
                                        downloadsStore.dispatch(
                                            DownloadUIAction.UndoPendingDeletion,
                                        )
                                    },
                                )
                            },
                        )
                    }
                },
            )
        },
        floatingActionButton = {
            if (uiState.isSearchIconVisible) {
                FloatingActionButton(
                    icon = painterResource(R.drawable.mozac_ic_search_24),
                    contentDescription = stringResource(R.string.download_search_placeholder),
                    onClick = { downloadsStore.dispatch(DownloadUIAction.SearchBarVisibilityRequest) },
                )
            }
        },
        containerColor = FirefoxTheme.colors.layer1,
        snackbarHost = {
            SnackbarHost(
                hostState = snackbarHostState,
                modifier = Modifier.imePadding(),
            )
        },
    ) { paddingValues ->
        DownloadsScreenContent(
            uiState = uiState,
            paddingValues = paddingValues,
            onContentTypeSelected = {
                downloadsStore.dispatch(DownloadUIAction.ContentTypeSelected(it))
            },
            onItemClick = onItemClick,
            onSelectionChange = { item, isSelected ->
                if (isSelected) {
                    downloadsStore.dispatch(DownloadUIAction.AddItemForRemoval(item))
                } else {
                    downloadsStore.dispatch(DownloadUIAction.RemoveItemForRemoval(item))
                }
            },
            onPauseClick = {
                downloadsStore.dispatch(
                    DownloadUIAction.PauseDownload(downloadId = it),
                )
            },
            onResumeClick = {
                downloadsStore.dispatch(
                    DownloadUIAction.ResumeDownload(downloadId = it),
                )
            },
            onRetryClick = {
                downloadsStore.dispatch(DownloadUIAction.RetryDownload(downloadId = it))
            },
            onDeleteClick = { item ->
                if (item.status is FileItem.Status.Completed) {
                    downloadsStore.dispatch(
                        DownloadUIAction.AddPendingDeletionSet(
                            setOf(item.id),
                        ),
                    )
                    showDeleteSnackbar(
                        selectedItems = setOf(item),
                        coroutineScope = coroutineScope,
                        snackbarHostState = snackbarHostState,
                        context = context,
                        undoAction = {
                            downloadsStore.dispatch(
                                DownloadUIAction.UndoPendingDeletion,
                            )
                        },
                    )
                } else {
                    downloadsStore.dispatch(DownloadUIAction.CancelDownload(downloadId = item.id))
                }
            },
            onShareUrlClick = { downloadsStore.dispatch(DownloadUIAction.ShareUrlClicked(it.url)) },
            onShareFileClick = {
                downloadsStore.dispatch(
                    DownloadUIAction.ShareFileClicked(
                        it.filePath,
                        it.contentType,
                    ),
                )
            },
        )
    }
}

@Composable
private fun ToolbarEditActions(
    downloadsStore: DownloadUIStore,
    toolbarConfig: ToolbarConfig,
    onItemDeleteClick: (FileItem) -> Unit,
) {
    // IconButton and DropdownMenu in a common parent so the menu position is calculated correctly.
    Row {
        var showMenu by remember { mutableStateOf(false) }

        IconButton(onClick = { showMenu = true }) {
            Icon(
                painter = painterResource(R.drawable.mozac_ic_ellipsis_vertical_24),
                contentDescription = stringResource(
                    R.string.content_description_menu,
                ),
                tint = toolbarConfig.iconColor,
            )
        }

        DropdownMenu(
            expanded = showMenu,
            menuItems = listOf(
                MenuItem.TextItem(
                    text = Text.Resource(R.string.download_select_all_items),
                    level = MenuItem.FixedItem.Level.Default,
                    onClick = { downloadsStore.dispatch(DownloadUIAction.AddAllItemsForRemoval) },
                ),
                MenuItem.TextItem(
                    text = Text.Resource(R.string.download_delete_item),
                    level = MenuItem.FixedItem.Level.Critical,
                    onClick = {
                        when (downloadsStore.state.mode.selectedItems.size) {
                            1 -> {
                                onItemDeleteClick(downloadsStore.state.mode.selectedItems.first())
                                downloadsStore.dispatch(DownloadUIAction.ExitEditMode)
                            }

                            else -> downloadsStore.dispatch(
                                DownloadUIAction.UpdateDeleteDialogVisibility(true),
                            )
                        }
                    },
                ),
            ),
            onDismissRequest = { showMenu = false },
        )
    }
}

/**
 * Content of the screen below the toolbar.
 * @param uiState The UI state of the screen.
 * @param paddingValues The padding values of the screen.
 * @param onContentTypeSelected Callback invoked when a content type filter is selected.
 * @param onItemClick Invoked when a download item is clicked.
 * @param onSelectionChange Invoked when selection state of an item changed.
 * @param onPauseClick Invoked when the pause icon button is clicked.
 * @param onResumeClick Invoked when the resume icon button is clicked.
 * @param onRetryClick Invoked when the retry icon button is clicked.
 * @param onDeleteClick Invoked when delete icon button is clicked.
 * @param onShareUrlClick Invoked when share url button is clicked.
 * @param onShareFileClick Invoked when share file button is clicked.
 */
@SuppressWarnings("LongParameterList")
@Composable
private fun DownloadsScreenContent(
    uiState: DownloadUIState,
    paddingValues: PaddingValues,
    onContentTypeSelected: (FileItem.ContentTypeFilter) -> Unit,
    onItemClick: (FileItem) -> Unit,
    onSelectionChange: (FileItem, Boolean) -> Unit,
    onPauseClick: (id: String) -> Unit,
    onResumeClick: (id: String) -> Unit,
    onRetryClick: (id: String) -> Unit,
    onDeleteClick: (FileItem) -> Unit,
    onShareUrlClick: (FileItem) -> Unit,
    onShareFileClick: (FileItem) -> Unit,
) {
    Column(
        modifier = Modifier
            .padding(paddingValues)
            .imePadding(),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        if (uiState.filtersToDisplay.isNotEmpty()) {
            Filters(
                selectedContentTypeFilter = uiState.selectedContentTypeFilter,
                contentTypeFilters = uiState.filtersToDisplay,
                modifier = Modifier
                    .width(FirefoxTheme.layout.size.containerMaxWidth)
                    .padding(vertical = FirefoxTheme.layout.space.static200),
                onContentTypeSelected = onContentTypeSelected,
            )
        }

        when (uiState.itemsState) {
            is DownloadUIState.ItemsState.NoItems -> EmptyState(modifier = Modifier.fillMaxSize())
            is DownloadUIState.ItemsState.NoSearchResults -> NoSearchResults(
                modifier = Modifier.fillMaxSize(),
            )

            is DownloadUIState.ItemsState.Items -> DownloadsContent(
                items = uiState.itemsState.items,
                mode = uiState.mode,
                onClick = onItemClick,
                onSelectionChange = onSelectionChange,
                onPauseClick = onPauseClick,
                onResumeClick = onResumeClick,
                onRetryClick = onRetryClick,
                onDeleteClick = onDeleteClick,
                onShareUrlClick = onShareUrlClick,
                onShareFileClick = onShareFileClick,
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
@Suppress("LongParameterList")
private fun DownloadsContent(
    items: List<DownloadListItem>,
    mode: Mode,
    modifier: Modifier = Modifier,
    onClick: (FileItem) -> Unit,
    onSelectionChange: (FileItem, Boolean) -> Unit,
    onPauseClick: (id: String) -> Unit,
    onResumeClick: (id: String) -> Unit,
    onRetryClick: (id: String) -> Unit,
    onDeleteClick: (FileItem) -> Unit,
    onShareUrlClick: (FileItem) -> Unit,
    onShareFileClick: (FileItem) -> Unit,
) {
    val haptics = LocalHapticFeedback.current

    LazyColumn(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        itemsIndexed(
            items = items,
            contentType = { _, item -> item::class },
            key = { _, item ->
                when (item) {
                    is HeaderItem -> item.timeCategory
                    is FileItem -> item.id
                }
            },
        ) { index, listItem ->
            when (listItem) {
                is HeaderItem -> {
                    HeaderListItem(
                        headerItem = listItem,
                        modifier = Modifier
                            .animateItem()
                            .width(FirefoxTheme.layout.size.containerMaxWidth),
                    )
                }

                is FileItem -> {
                    FileListItem(
                        fileItem = listItem,
                        isSelected = mode.selectedItems.contains(listItem),
                        areAfterListItemIconsVisible = mode is Mode.Normal ||
                            listItem.status !is FileItem.Status.Completed,
                        onPauseClick = onPauseClick,
                        onDeleteClick = onDeleteClick,
                        onResumeClick = onResumeClick,
                        onRetryClick = onRetryClick,
                        onShareUrlClick = onShareUrlClick,
                        onShareFileClick = onShareFileClick,
                        modifier = Modifier
                            .animateItem()
                            .width(FirefoxTheme.layout.size.containerMaxWidth)
                            .thenConditional(
                                modifier = Modifier
                                    .combinedClickable(
                                        onClick = {
                                            if (mode is Mode.Normal) {
                                                onClick(listItem)
                                            } else {
                                                onSelectionChange(
                                                    listItem,
                                                    !mode.selectedItems.contains(listItem),
                                                )
                                            }
                                        },
                                        onLongClick = {
                                            if (mode is Mode.Normal) {
                                                haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                                onSelectionChange(listItem, true)
                                            }
                                        },
                                    ),
                            ) { listItem.status is FileItem.Status.Completed }
                            .testTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.${listItem.fileName}"),
                    )

                    if (index == items.lastIndex || items[index + 1] is HeaderItem) {
                        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
                    }
                }
            }
        }
    }
}

@Composable
private fun HeaderListItem(
    headerItem: HeaderItem,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier) {
        ExpandableListHeader(
            headerText = stringResource(id = headerItem.timeCategory.stringRes),
        )
    }
}

@Composable
private fun NoSearchResults(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier,
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = stringResource(id = R.string.download_search_no_results_found),
            color = FirefoxTheme.colors.textSecondary,
            style = FirefoxTheme.typography.body2,
        )
    }
}

@Composable
private fun EmptyState(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.padding(all = 16.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Image(
            painter = painterResource(R.drawable.illustration_fox_box_inside_light),
            contentDescription = null,
        )

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = stringResource(id = R.string.download_empty_message_2),
            color = FirefoxTheme.colors.textPrimary,
            style = FirefoxTheme.typography.headline7,
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = stringResource(id = R.string.download_empty_description),
            color = FirefoxTheme.colors.textPrimary,
            style = FirefoxTheme.typography.body2,
        )
    }
}

@Composable
@ReadOnlyComposable
private fun getToolbarConfig(mode: Mode): ToolbarConfig {
    return when (mode) {
        is Mode.Editing -> ToolbarConfig(
            title = stringResource(
                R.string.download_multi_select_title,
                mode.selectedItems.size,
            ),
            backgroundColor = FirefoxTheme.colors.layerAccent,
            textColor = FirefoxTheme.colors.textOnColorPrimary,
            iconColor = FirefoxTheme.colors.iconOnColor,
        )

        is Mode.Normal -> ToolbarConfig(
            title = stringResource(R.string.library_downloads),
            backgroundColor = FirefoxTheme.colors.layer1,
            textColor = FirefoxTheme.colors.textPrimary,
            iconColor = FirefoxTheme.colors.iconPrimary,
        )
    }
}

private fun showDeleteSnackbar(
    selectedItems: Set<FileItem>,
    coroutineScope: CoroutineScope,
    snackbarHostState: SnackbarHostState,
    context: Context,
    undoAction: () -> Unit,
) {
    coroutineScope.launch {
        val snackbarResult = snackbarHostState.showSnackbar(
            message = getDeleteSnackBarMessage(selectedItems, context),
            actionLabel = context.getString(R.string.download_undo_delete_snackbar_action),
            duration = SnackbarDuration.Short,
        )
        if (snackbarResult == SnackbarResult.ActionPerformed) {
            undoAction.invoke()
        }
    }
}

private fun getDeleteSnackBarMessage(fileItems: Set<FileItem>, context: Context): String {
    return if (fileItems.size > 1) {
        context.getString(R.string.download_delete_multiple_items_snackbar_2, fileItems.size)
    } else {
        context.getString(
            R.string.download_delete_single_item_snackbar_2,
            fileItems.first().fileName,
        )
    }
}

private class DownloadsScreenPreviewModelParameterProvider :
    PreviewParameterProvider<DownloadUIState> {
    override val values: Sequence<DownloadUIState>
        get() = sequenceOf(
            DownloadUIState.INITIAL,
            DownloadUIState(
                items = listOf(
                    FileItem(
                        id = "1",
                        fileName = "File 1",
                        url = "https://example.com/file1",
                        description = "1.2 MB • example.com",
                        displayedShortUrl = "example.com",
                        contentType = "application/pdf",
                        status = FileItem.Status.Completed,
                        filePath = "/path/to/file1",
                        timeCategory = TimeCategory.TODAY,
                    ),
                    FileItem(
                        id = "2",
                        fileName = "File 2",
                        url = "https://example.com/file2",
                        description = "2.3 MB • example.com",
                        displayedShortUrl = "example.com",
                        contentType = "image/png",
                        status = FileItem.Status.Completed,
                        filePath = "/path/to/file2",
                        timeCategory = TimeCategory.TODAY,
                    ),
                    FileItem(
                        id = "3",
                        fileName = "File 3",
                        url = "https://example.com/file3",
                        description = "3.4 MB • example.com",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Completed,
                        filePath = "/path/to/file3",
                        timeCategory = TimeCategory.OLDER,
                    ),
                    FileItem(
                        id = "4",
                        fileName = "File 4",
                        url = "https://example.com/file4",
                        description = "5 MB / 10 MB • in 5s",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Downloading(progress = 0.5f),
                        filePath = "/path/to/file4",
                        timeCategory = TimeCategory.IN_PROGRESS,
                    ),
                    FileItem(
                        id = "5",
                        fileName = "File 5",
                        url = "https://example.com/file5",
                        description = "5 MB / 10 MB • pending",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Downloading(progress = 0.5f),
                        filePath = "/path/to/file5",
                        timeCategory = TimeCategory.IN_PROGRESS,
                    ),
                    FileItem(
                        id = "6",
                        fileName = "File 6",
                        url = "https://example.com/file6",
                        description = "5 MB / 10 MB • paused",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Paused(progress = 0.5f),
                        filePath = "/path/to/file6",
                        timeCategory = TimeCategory.IN_PROGRESS,
                    ),
                    FileItem(
                        id = "7",
                        fileName = "File 7",
                        url = "https://example.com/file7",
                        description = "Preparing download…",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Initiated,
                        filePath = "/path/to/file7",
                        timeCategory = TimeCategory.IN_PROGRESS,
                    ),
                    FileItem(
                        id = "8",
                        fileName = "File 8",
                        url = "https://example.com/file8",
                        description = "Download Failed",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Failed,
                        filePath = "/path/to/file8",
                        timeCategory = TimeCategory.IN_PROGRESS,
                    ),
                ),
                mode = Mode.Normal,
                pendingDeletionIds = emptySet(),
                userSelectedContentTypeFilter = FileItem.ContentTypeFilter.All,
            ),
            DownloadUIState(
                items = List(size = 20) { index ->
                    FileItem(
                        id = "$index",
                        fileName = "File $index",
                        url = "https://example.com/file$index",
                        description = "1.2 MB • example.com",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = FileItem.Status.Completed,
                        filePath = "/path/to/file1",
                        timeCategory = TimeCategory.TODAY,
                    )
                },
                mode = Mode.Normal,
                pendingDeletionIds = emptySet(),
                userSelectedContentTypeFilter = FileItem.ContentTypeFilter.All,
            ),
        )
}

@Composable
@FlexibleWindowLightDarkPreview
private fun DownloadsScreenPreviews(
    @PreviewParameter(DownloadsScreenPreviewModelParameterProvider::class) state: DownloadUIState,
) {
    val downloadsStore = remember { DownloadUIStore(initialState = state) }
    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    FirefoxTheme {
        Box {
            DownloadsScreen(
                downloadsStore = downloadsStore,
                onItemClick = {
                    scope.launch {
                        snackbarHostState.showSnackbar(
                            message = "Item ${it.fileName} clicked",
                        )
                    }
                },
                onNavigationIconClick = {
                    scope.launch {
                        snackbarHostState.showSnackbar(
                            message = "Navigation Icon clicked",
                        )
                    }
                },
            )
            SnackbarHost(
                hostState = snackbarHostState,
            )
        }
    }
}
