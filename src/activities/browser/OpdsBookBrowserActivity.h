#pragma once
#include <OpdsParser.h>

#include <string>
#include <utility>
#include <vector>

#include "OpdsServerStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

/**
 * Activity for browsing and downloading books from an OPDS server.
 * Supports navigation through catalog hierarchy and downloading EPUBs.
 */
class OpdsBookBrowserActivity final : public Activity, private UiAppHost {
 public:
  enum class BrowserState {
    CHECK_WIFI,
    WIFI_SELECTION,
    LOADING,
    BROWSING,
    DOWNLOADING,
    ERROR,
    SEARCH_INPUT,
    NO_SEARCH
  };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server,
                                   bool returnToParentOnBack = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  BrowserState state = BrowserState::LOADING;
  std::vector<OpdsEntry> entries;
  // Row buffer, built whenever entries changes (fetchFeed()/releaseEntries())
  // so buildBrowsingScreen() reuses it on every repaint instead of rebuilding
  // a ListItem vector per render.
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::string> rowLabels;  // display text, shortened to one line
  void rebuildRowItems();
  std::vector<std::string> navigationHistory;
  std::vector<std::string> pageTrail;                  // pages we came through, for Previous Page
  std::vector<std::vector<std::string>> trailHistory;  // one trail per Back step
  bool hasReturnPage = false;                          // Back from an error returns to returnPath
  std::string returnPath;
  std::vector<std::string> returnTrail;
  std::string currentPath;
  std::string searchTemplate;
  std::string nextPageUrl;  // page rows replace the current page instead of adding a Back step
  std::string prevPageUrl;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  std::string infoMessage;  // text of the info screen (no search, nothing more here)
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  OpdsServer server;  // Copied at construction — safe even if the store changes during browsing
  bool returnToParentOnBack = false;

  // Viewport memory (top/visibleRows) for the browsing list; `selected` is
  // mirrored from selectorIndex at build/move time.
  freeink::ui::ListNav listNav;

  // Single screen fn dispatching on `state`. Browsing rows are built
  // through FreeInkUI; the shared header and fixed-pixel status layouts use GUI.
  static void rootScreen(UiScreen& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildBrowsingScreen(UiScreen& screen);
  void buildDownloadScreen(UiScreen& screen);
  void buildStatusScreen(UiScreen& screen);
  void activateSelected();

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed(const std::string& path);
  // The remaining pages of a section load one per loop pass, so the loading screen keeps updating.
  bool loadingMore = false;
  std::vector<OpdsEntry> loadingEntries;
  std::string loadingCursor;
  int loadingPages = 0;
  unsigned long lastLoadingUpdateMs = 0;
  void stepLoading();
  void finishLoading();

  // Lists we came through, kept (at most 3) so Back restores them instantly instead of loading again.
  struct SavedList {
    std::string path;
    std::vector<OpdsEntry> entries;
    std::string searchTemplate;
    int selectorIndex = 0;
    freeink::ui::ListNav nav;
    bool valid = false;  // false once dropped to free memory
  };
  std::vector<SavedList> savedLists;
  void saveListForBack();
  bool dropOldestSavedList();
  int fetchPage(const std::string& url, std::vector<OpdsEntry>& out, std::string& next, std::string* search,
                bool* truncated);
  void releaseEntries();
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void leaveError();
  void downloadBook(const OpdsEntry& book);
  void launchSearch();
  void performSearch(const std::string& query);
  bool preventAutoSleep() override { return true; }
};
