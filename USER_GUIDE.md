# CrossPoint User Guide

Welcome to the **CrossPoint** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [CrossPoint User Guide](#crosspoint-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
    - [Taking a Screenshot](#taking-a-screenshot)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Recent Books Screen](#34-recent-books-screen)
    - [3.5 File Transfer Screen](#35-file-transfer-screen)
    - [3.5.1 Calibre Wireless Transfers](#351-calibre-wireless-transfers)
      - [Installing the Plugin in Calibre](#installing-the-plugin-in-calibre)
      - [Configuring the CrossPoint Plugin in Calibre](#configuring-the-crosspoint-plugin-in-calibre)
      - [Uploading Books](#uploading-books)
      - [Removing a Book](#removing-a-book)
    - [3.6 Menu](#36-menu)
      - [3.6.1 Hidden Features](#361-hidden-features)
      - [3.6.2 Files](#362-files)
      - [3.6.3 Reading](#363-reading)
      - [3.6.4 Controls](#364-controls)
      - [3.6.5 Display](#365-display)
      - [3.6.6 Network](#366-network)
      - [3.6.7 Web Settings (Wi-Fi)](#367-web-settings-wi-fi)
    - [3.7 Sleep Screen](#37-sleep-screen)
      - [Cover settings](#cover-settings)
      - [Custom images](#custom-images)
    - [3.8 Custom Fonts (SD Card)](#38-custom-fonts-sd-card)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [Auto Page Turn](#auto-page-turn)
    - [Tilt Page Turn (X3 only)](#tilt-page-turn-x3-only)
    - [Endnote Navigation](#endnote-navigation)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Reader Menu](#5-reader-menu)
    - [5.1 Chapter Selection](#51-chapter-selection)
    - [5.2 Highlights](#52-highlights)
  - [6. Current Limitations \& Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues \& Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)

## 1. Hardware Overview

The device utilises the standard buttons on the Xteink X4 (in the same layout as the manufacturer firmware, by default):

### Button Layout

| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Volume Up**, **Volume Down**, **Reset** |

Button layout can be customized in the **[Controls](#364-controls)** group of the Menu.

### Taking a Screenshot

When the Power Button and Volume Down button are pressed at the same time, it will take a screenshot and save it in the folder `screenshots/`.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE]
> On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. From here you can navigate to **[Reading Mode](#4-reading-mode)** with the most recently read book, the **[Browse Files](#33-browse-files-screen)** screen, the **[Recent Books](#34-recent-books-screen)** screen, the **[File Transfer](#35-file-transfer-screen)** screen, or the **[Menu](#36-menu)**.

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen acts as a file and folder browser. The full path to the current directory is shown at the top of the screen. File extensions are displayed alongside each filename, and directories are shown with brackets (e.g. `[folder-name]`). Hidden directories (those beginning with `.`) are also visible.

* **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up and down through folders and books. You can also long-press these buttons to scroll a full page up or down.
* **Open Selection:** Press **Confirm** to open a folder or start reading a selected book. Selecting a `.bmp` file will open the image viewer.
* **Delete Files or Folders:** Hold and release **Confirm** to delete the selected file or folder. You will be given an option to either confirm or cancel. Multiple files can be selected for deletion in a single operation.
* **Rename or Move:** Files can be renamed or moved to a different folder from within the browse screen.

### 3.4 Recent Books Screen

The Recent Books screen lists the most recently opened books in a chronological view, displaying title and author.

### 3.5 File Transfer Screen

The File Transfer screen allows you to upload and manage files on the device. When you enter the screen, choose **Network Mode**, **Hotspot Mode**, or **Calibre Wireless**. The reader then starts the web server for the selected mode.

See the [web server docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

The web interface also supports **WebDAV**, allowing you to mount the device as a network drive and manage files directly from your computer's file manager.

Download links for files already on the device are available in the web interface, so you can retrieve books or screenshots over Wi-Fi without connecting a cable.

A **Wi-Fi signal strength indicator** (dBm) is displayed on-screen during joined-network web server sessions.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [web server docs](./docs/webserver.md) for details.
> [!TIP]
> If your EPUBs have compatibility issues, you can run the built-in **EPUB Optimizer** directly from the device to clean up and reprocess books for better rendering.

### 3.5.1 Calibre Wireless Transfers

CrossPoint supports sending books from Calibre using the CrossPoint Reader device plugin.

#### Installing the Plugin in Calibre

If you don't already have the plugin installed:

1. Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.
2. Download the zip file.
3. Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.
4. Restart Calibre.

#### Configuring the CrossPoint Plugin in Calibre
1. In Calibre select Preferences.
2. In the Preferences dialog select Plugins.
3. In Plugins search for "crosspoint".
4. Click on "Customize plugin".
5. Update the value for "Host" to match the IP for your device.
6. Leave the other settings as they are.
7. [optional] Modify the "Upload path" to point to a subfolder other than the root "/" folder. Enter this as a path relative to the root folder. Example: `/mybooks`
8. Restart Calibre.

<img width="420" height="385" alt="Image" src="https://github.com/user-attachments/assets/01fc7e33-a9a7-48ba-9e26-2e68d1f9daec" />

#### Uploading Books

To upload a book using the CrossPoint plugin in Calibre:

1. On the device: File Transfer -> Calibre Wireless, then join a network.
2. Select one or more books.
3. Right-click on that selection.
4. Select "Send to Device" > "Send to main memory"

The CrossPoint plugin will connect to your device, create a folder for the book's author in the root folder (or the folder you configured for the plugin), then copy the book into that folder.

<img width="783" height="310" alt="Image" src="https://github.com/user-attachments/assets/741b0909-2e1d-4f16-8af0-2c43fbda5ce6" />

#### Removing a Book

Books cannot be removed from your device through Calibre. Use the web interface instead.

### 3.6 Menu

The **Menu** replaces the old Settings screen. It is one list: **Hidden Features** sits at the top in bold, followed by five groups (**Files**, **Reading**, **Controls**, **Display** and **Network**). Press **Confirm** on a group to open or close it. Rows with a value on the right, such as Sleep Screen, change when you press **Confirm**.

#### 3.6.1 Hidden Features

A read-only reference of button holds and shortcuts that are not labelled on screen, grouped by screen: General, Home, Library, Recent Book(s), Reader, Highlight List, Dictionary and Wi-Fi Networks. Only the groups can be opened and closed; the rows themselves cannot be selected.

#### 3.6.2 Files

- **Library**: Browse the files and folders on the SD card (see [Browse Files Screen](#33-browse-files-screen)).
- **SD Card Firmware Update**: Update the firmware without a USB connection by placing a `firmware.bin` file on the SD card.

#### 3.6.3 Reading

- **Recent Book(s)**: Open one of the most recently read books (see [Recent Books Screen](#34-recent-books-screen)).
- **Reader Font**: Browse, download, and manage custom font families. See [Custom Fonts (SD Card)](#38-custom-fonts-sd-card) for more information.

Text size, spacing, orientation and the status bar are changed from the **[Reader Menu](#5-reader-menu)** while reading.

#### 3.6.4 Controls

- **Remap Front Buttons**: A menu for customising the function of each bottom edge button.
- **Reader Side Buttons**: Swap the roles of the up and down volume buttons between "Prev/Next" (default) and "Next/Prev". This only applies while reading.

#### 3.6.5 Display

- **Time To Sleep**: Choose how many minutes of inactivity pass before the device goes to sleep automatically.
- **Sleep Screen**: Which sleep screen to show when the device sleeps. Each press of **Confirm** moves to the next option; see [Sleep Screen](#37-sleep-screen) below for what each one does.
- **Refresh Frequency**: How often the screen does a full refresh while reading to reduce ghosting; options are every 1, 5, 10, 15, or 30 pages.

#### 3.6.6 Network

- **Wi-Fi Networks**: Manage saved Wi-Fi networks without starting a transfer: pick a network to connect, add one, or forget one.
- **File Transfer**: Opens the [File Transfer Screen](#35-file-transfer-screen), where you choose **Network Mode**, **Hotspot Mode**, or **Calibre Wireless**.

#### 3.6.7 Web Settings (Wi-Fi)

While in **File Transfer** mode, the web settings page includes a management card for **Wi-Fi Networks**.

1. On device: open **File Transfer** and connect through **Network Mode** or **Hotspot Mode**.
2. In a browser, open `http://<device-ip>/settings` or `http://crosspoint.local`.
3. In **Wi-Fi Networks**, add, edit, or delete saved network entries (SSID + optional password).

Behavior notes:

- Passwords are never shown back in the web UI after saving.
- Leaving Password blank while editing keeps the existing saved password unchanged.
- The web UI can save hidden-network SSIDs, but connecting to hidden networks still depends on the device-side Wi-Fi connection flow.

### 3.7 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode               | Behavior                                                                                                                     |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| **Dark** (default) | The CrossPoint logo on a dark background.                                                                                    |
| **Light**          | The CrossPoint logo on a white background.                                                                                   |
| **Custom**         | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found.                             |
| **Cover**          | The cover of the currently open book. Falls back to **Dark** if no book is open.                                             |
| **Cover + Custom** | The cover of the currently open book, shown only while actively reading. Falls back to **Custom** behavior when not reading. |
| **Transparent**    | A BMP or PNG overlay drawn over the current screen. Supports PNG and 32-bit BGRA alpha transparency, and treats white as transparent in regular BMPs. Falls back to **Dark** if no valid overlay image is found. |
| **None**           | A blank screen.                                                                                                              |

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom** or **Cover + Custom**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` images inside. One will be randomly selected each time the device sleeps. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. This takes priority over the `.sleep`/`sleep` directories.

#### Transparent overlay images

To use transparent sleep overlays, set the sleep screen mode to **Transparent**, then place BMP or PNG files on the SD card:

- **Multiple Images (recommended):** Create a `.sleep-overlay` directory in the root of the SD card and place any number of valid overlay `.bmp` or `.png` images inside. One will be randomly selected each time the device sleeps. A directory named `sleep-overlay` is also accepted as a fallback.
- **Single Image:** Place `sleep-overlay.bmp` or `sleep-overlay.png` in the root directory. A root BMP takes priority over a root PNG, and both take priority over the `.sleep-overlay`/`sleep-overlay` directories.

Transparent overlay files are intentionally separate from normal sleep images. Regular BMP formats supported by CrossPoint are accepted; white pixels leave the existing screen unchanged. For per-pixel alpha transparency, use a PNG with an alpha channel or a 32-bit BGRA BMP with both visible and non-opaque pixels. Opaque white pixels in alpha images erase the content behind them.

> [!TIP]
> For best results:
> - For non-transparent **Custom** mode, use uncompressed BMP files with 24-bit color depth.
> - For **Transparent** mode, use a PNG or uncompressed 32-bit BGRA BMP for per-pixel alpha, or a regular BMP for white-as-transparent artwork.
> - X4: Use a resolution of 480x800 pixels to match the device's screen resolution.
> - X3: Use a resolution of 528x792 pixels to match the device's screen resolution.

> [!TIP]
> You can set an image as the sleep screen cover directly from the BMP image viewer in the **[Browse Files](#33-browse-files-screen)** screen.

---

### 3.8 Custom Fonts (SD Card)

CrossPoint supports loading additional fonts from the SD card, extending beyond the two built-in families (Noto Serif, Noto Sans). Custom fonts can include extended Unicode coverage, enabling CJK (Chinese, Japanese, Korean) and other scripts.

There are three ways to install fonts:

1. **Download from device (recommended):** Go to **Menu -> Reading -> Reader Font**, browse the available font families, and select one to download over Wi-Fi.
2. **Upload via web interface:** While in **File Transfer** mode, open the web UI in a browser and navigate to the **Fonts** tab to upload `.cpfont` files.
3. **Manual SD card copy:** Download font files from the [Minuta font catalogue](https://github.com/hiitsalice/minuta-fonts) and copy them to `/.fonts/` (preferred) or `/fonts/` on your SD card.

Once installed, custom fonts appear in the font list under **Text Settings** in the **[Reader Menu](#5-reader-menu)** alongside the built-in fonts.

See [docs/sd-card-fonts.md](./docs/sd-card-fonts.md) for full installation details and SD card folder structure.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning

| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Volume Up**    |
| **Next Page**     | Press **Right** _or_ **Volume Down** |

The role of the volume (side) buttons can be swapped in **Menu -> Controls -> Reader Side Buttons** (see [Controls](#364-controls)).

### Chapter Navigation

* **Next Chapter:** Press and **hold** the **Volume Down** (side) button briefly, then release.
* **Previous Chapter:** Press and **hold** the **Volume Up** (side) button briefly, then release.

Holding **Left** or **Right** on the bottom edge starts a tool instead (see [System Navigation](#system-navigation)).

### Auto Page Turn

Auto Page Turn automatically advances pages at a set interval, useful for hands-free reading. This feature can be enabled and configured from the **[Reader Menu](#5-reader-menu)** while reading an EPUB.

### Tilt Page Turn (X3 only)

On the **Xteink X3**, the gyroscope can be used to turn pages by tilting the device.

### Endnote Navigation

When reading an EPUB that contains endnotes, you can navigate to the endnote text by selecting the endnote reference in the book. From the endnote, you can return to your original reading position.

If the device goes to sleep or you close the book while viewing a endnote, the book reopens to your original reading position, not the endnote.

### System Navigation

* **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
* **Main Menu:** Press and **hold** the **Back** button to close the book and go to the main menu.
* **Reader Menu:** Press **Confirm** to open the **[Reader Menu](#5-reader-menu)**, which includes chapter navigation, reading options, and more.
* **Dictionary:** Hold **Confirm** (about 0.7 seconds) to start dictionary word selection on the current page (see [docs/dictionary.md](docs/dictionary.md)).
* **Highlight:** Hold **Left** to start a highlight (see [Highlights](#52-highlights)).
* **Screenshot:** Hold **Right** to save a screenshot of the current page.

These three holds do nothing on the plain message screens the reader shows when a chapter is empty or a page fails to load.

### Supported Languages

CrossPoint renders text using the following Unicode character blocks, enabling support for a wide range of languages:

* **Latin Script (Basic, Supplement, Extended-A/B):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, Catalan, and others.
* **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.
* **Vietnamese:** Supported via extended Latin glyph coverage in the built-in reader fonts.

What is not supported with built-in reader fonts: Chinese, Japanese, Korean, Arabic, Greek, Hebrew, and Farsi. However, **CJK, Hebrew, Greek, and other extended scripts can be enabled by installing custom SD card fonts** — see [Custom Fonts (SD Card)](#38-custom-fonts-sd-card).

---

## 5. Reader Menu

Press **Confirm** while reading to open the Reader Menu. From here you can access reading utilities and navigation options without leaving the book.

The options are grouped into **Navigation**, **Tools** and **Appearance**, which open and close with **Confirm**. **Delete Book Cache** stands alone at the bottom in bold.

**Navigation**

- **Select Chapter** – Open the table of contents to jump to a specific chapter (see [Chapter Selection](#51-chapter-selection) below).
- **Go To Percent** – Jump to a specific position in the book by percentage.
- **Footnotes** – Navigate to the endnotes for the current section.
- **Auto Turn (sec/page)** – Cycle through automatic page turn speed options for hands-free reading.

**Tools**

- **Highlight** – Start a highlight on the current page (see [Highlights](#52-highlights)).
- **Highlight Marker** – Turn the highlight markers on the page on or off.
- **Highlight List** – Browse your saved highlights.
- **Lookup** – Select a word on the current page and show its dictionary definition (see [docs/dictionary.md](docs/dictionary.md)).
- **Set Dictionary** – Choose which installed dictionary is used for lookups.
- **Screenshot** – Save a screenshot of the current page to the `screenshots/` folder.

**Appearance**

- **Status Bar** – Customise the status bar shown while reading.
- **Text Settings** – Font, text size, spacing and other layout options.
- **Reader Orientation** – Cycle through screen orientations without leaving the reader.

**Delete Book Cache** – Clear the cached layout data for the current book, forcing a re-index on next open.

Press **Back** at any time to close the menu and return to your current page.

### 5.1 Chapter Selection

Accessible by selecting **Chapters** from the Reader Menu.

1. Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to highlight the desired chapter.
2. Press **Confirm** to jump to that chapter.
3. *Alternatively, press **Back** to cancel and return to your current page.*

---

### 5.2 Highlights

Highlights let you save a passage you want to come back to.

To start a highlight, hold **Left** for about 0.7 seconds while inside a book, or choose **Highlight** under **Tools** in the Reader Menu. Select the first and last word of the passage, then save it. **Highlight Marker** (under **Tools**) turns the markers on the page on and off.

To browse saved highlights, open **Highlight List** under **Tools**. Highlight a row and press **Confirm** to jump to that place in the book. To delete a highlight, hold **Confirm** on it; there is no confirmation prompt.

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features are **not yet supported** but are planned for future updates:

* **Cover Images:** Large cover images embedded into EPUB require several seconds (~10s for ~2000 pixel tall image) to convert for sleep screen and home screen thumbnail. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.
* **Unsupported Image Formats:** Most JPG and PNG images in EPUBs render correctly. GIFs and progressive JPEGs are not supported and will fall back to an `[Image]` placeholder.
* 
* **Dictionary Lookup:** Inline word lookup is not yet implemented.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Crosspoint, feel free to raise an issue ticket and attach the logs.

**Crash reports on SD card:** After a crash, CrossPoint automatically saves a crash report to the SD card (no USB connection needed). Check the root of the SD card for a crash log file and include it with any bug report.

**Serial monitor logs:** For more detailed debugging, connect the device to a computer and run the custom debugging monitor script (requires Python 3 with `pyserial`, `colorama`, and `matplotlib`; install via `pip3 install pyserial colorama matplotlib`):

```
python3 scripts/debugging_monitor.py
```

The script auto-detects the serial port. You can also specify one explicitly:

```
python3 scripts/debugging_monitor.py /dev/ttyACM0        # Linux
python3 scripts/debugging_monitor.py /dev/tty.usbmodem1  # macOS
python3 scripts/debugging_monitor.py COM7                # Windows
```

**Features:**

- Color-coded log output by category (errors, memory, display, EPUB parsing, etc.)
- Live memory usage graph (free RAM, total RAM, max contiguous allocation) updated every second
- Interactive command prompt — type a command and press Enter to send it to the device
- Screenshot capture — saves the current display to `screenshot.bmp` when triggered by the device

**Options:**

| Option               | Description                                               |
| -------------------- | --------------------------------------------------------- |
| `--baud RATE`        | Baud rate (default: 115200)                               |
| `--filter KEYWORD`   | Show only lines containing the keyword (case-insensitive) |
| `--suppress KEYWORD` | Hide lines containing the keyword (case-insensitive)      |

**Examples:**

```
# Show only memory-related log lines
python3 scripts/debugging_monitor.py --filter MEM

# Hide noisy SD card log lines
python3 scripts/debugging_monitor.py --suppress "[SD]"
```

Press **Ctrl-C** or close the graph window to exit.

If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot to the Home Screen.

There can be issues with broken cache or config. In this case, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.json`, `state.json`, or `epub_*` cache directories in the `.crosspoint/` folder).
