#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

struct HomeActivity::Layout {
  Rect cover{};
  Rect menu{};
  int rowHeight;
  int firstVisibleRow;
  int visibleRows;
  int menuItemCount;
  bool showCover;
  bool recentsInMenu;
};

HomeActivity::Layout HomeActivity::getLayout() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int safeRight = std::min(safeArea.x + safeArea.width, renderer.getScreenWidth() - right);
  const int safeBottom = std::min(safeArea.y + safeArea.height, renderer.getScreenHeight() - bottom);
  safeArea.x = std::max(safeArea.x, left);
  safeArea.y = std::max(safeArea.y, top);
  safeArea.width = safeRight - safeArea.x;

  Layout layout{};
  layout.rowHeight = GUI.getMenuRowHeight(renderer);
  layout.cover = Rect{safeArea.x, safeArea.y + metrics.homeTopPadding, safeArea.width, metrics.homeCoverTileHeight};
  const int fullMenuTop = layout.cover.y + layout.cover.height + metrics.homeMenuTopOffset;
  const int minimumMenuHeight = 3 * layout.rowHeight + 2 * metrics.menuSpacing;
  layout.showCover = safeBottom - fullMenuTop >= minimumMenuHeight;
  layout.recentsInMenu = metrics.homeContinueReadingInMenu || !layout.showCover;
  const int menuTop = layout.showCover ? fullMenuTop : layout.cover.y;
  layout.menu = Rect{safeArea.x, menuTop, safeArea.width, std::max(0, safeBottom - menuTop)};
  layout.menuItemCount = getMenuItemCount() - (layout.recentsInMenu ? 0 : static_cast<int>(recentBooks.size()));
  const int pageSize =
      std::max(1, (layout.menu.height + metrics.menuSpacing) / (layout.rowHeight + metrics.menuSpacing));
  const int selectedRow = selectorIndex - (layout.recentsInMenu ? 0 : static_cast<int>(recentBooks.size()));
  layout.firstVisibleRow = std::max(0, selectedRow) / pageSize * pageSize;
  layout.visibleRows = std::min(pageSize, layout.menuItemCount - layout.firstVisibleRow);
  return layout;
}

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Library, File transfer, Settings, Apps
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      case HomeMenuItem::APPS:
        onAppsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  const auto layout = getLayout();
  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = layout.showCover ? std::min(static_cast<int>(recentBooks.size()), coverColumnCount) : 0;
  const int coverColumnWidth = (layout.cover.width - 2 * metrics.contentSidePadding) / coverColumnCount;
  int touchedBook = -1;
  const auto coverTouch =
      mappedInput.colTouch(touchedBook, layout.cover.x + metrics.contentSidePadding, coverColumnWidth, recentCount,
                           layout.cover.y, layout.cover.y + layout.cover.height, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      activateSelection();
    }
    return;
  }

  int menuRow = -1;
  const auto menuTouch =
      mappedInput.rowTouch(menuRow, layout.menu.y, layout.rowHeight + metrics.menuSpacing, layout.visibleRows,
                           layout.menu.x, layout.menu.x + layout.menu.width, layout.rowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        layout.firstVisibleRow + menuRow + (layout.recentsInMenu ? 0 : static_cast<int>(recentBooks.size()));
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = getLayout();

  renderer.clearScreen();
  if (!layout.showCover || coverRectX != layout.cover.x || coverRectY != layout.cover.y ||
      coverRectW != layout.cover.width || coverRectH != layout.cover.height) {
    freeCoverBuffer();
    coverRendered = false;
  }
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer,
                 Rect{layout.cover.x, layout.cover.y - metrics.homeTopPadding + metrics.topPadding, layout.cover.width,
                      metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = layout.cover.x;
  coverRectY = layout.cover.y;
  coverRectW = layout.cover.width;
  coverRectH = layout.cover.height;

  if (layout.showCover) {
    GUI.drawRecentBookCover(renderer, layout.cover, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                            bufferRestored, std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  constexpr int MAX_MENU_ITEMS = 9;
  std::array<const char*, MAX_MENU_ITEMS> menuItems{};
  std::array<UIIcon, MAX_MENU_ITEMS> menuIcons{};
  int menuItemCount = 0;
  const auto addMenuItem = [&](const char* label, UIIcon icon) {
    menuItems[menuItemCount] = label;
    menuIcons[menuItemCount++] = icon;
  };

  if (layout.recentsInMenu) {
    for (const auto& book : recentBooks) {
      addMenuItem(layout.showCover ? tr(STR_CONTINUE_READING) : book.title.c_str(), Book);
    }
  }
  addMenuItem(tr(STR_BROWSE_FILES), Folder);
  addMenuItem(tr(STR_LIBRARY), Library);
  if (hasOpdsServers) {
    addMenuItem(tr(STR_OPDS_BROWSER), Blocks);
  }
  addMenuItem(tr(STR_FILE_TRANSFER), Transfer);
  addMenuItem(tr(STR_SETTINGS_TITLE), Settings);
  addMenuItem(tr(STR_APPS), Blocks);

  const int selectedRow = selectorIndex - (layout.recentsInMenu ? 0 : static_cast<int>(recentBooks.size()));
  GUI.drawButtonMenu(
      renderer, layout.menu, layout.visibleRows, selectedRow - layout.firstVisibleRow,
      [&](int index) { return std::string(menuItems[layout.firstVisibleRow + index]); },
      [&](int index) { return menuIcons[layout.firstVisibleRow + index]; });
  GUI.drawMenuScrollBar(renderer, layout.menu, menuItemCount, layout.firstVisibleRow, layout.visibleRows);

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (layout.showCover && !recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onAppsOpen() { activityManager.goToApps(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
