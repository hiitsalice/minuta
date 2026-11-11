#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Activity showing the list of configured OPDS servers.
 * Allows adding new servers and editing/deleting existing ones.
 * When pickerMode is true, selecting a server navigates to the OPDS browser
 * instead of opening the editor (used from the home screen).
 */
class OpdsServerListActivity final : public UiListActivity {
 public:
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool pickerMode = false,
                                  bool returnToParent = false);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return getItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Picker mode backs out to the home menu rather than finishing.
  void onBackButton() override;
  void navigateButtons() override;
  const char* headerTitle() const override;

  bool pickerMode = false;
  bool returnToParent = false;

  // Row structure (labels/actionValue and server rows), rebuilt only when the
  // server list itself reloads (rebuildRowItems(), called from onEnter() and
  // after returning from the editor). Folder/format values refresh in buildScreen().
  std::vector<freeink::ui::ListItem> rowItems_;
  std::string displayedFolderValue_;
  void rebuildRowItems();

  int getItemCount() const;
  void handleSelection();
};
