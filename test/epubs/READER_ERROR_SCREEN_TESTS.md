# Reader error-screen simulator tests

These are valid EPUB fixtures for checking the two reader error screens that
display the book title in the header.

## Run

From the repository root, copy both fixtures into the simulator SD-card folder:

```bash
mkdir -p fs_
cp test/epubs/reader_error_out_of_bounds.epub fs_/
cp test/epubs/reader_error_page_load.epub fs_/
```

Then build and launch the simulator:

```bash
pio run -e simulator && ./.pio/build/simulator/program
```

In the simulator's file browser, open either EPUB:

- `reader_error_out_of_bounds.epub` opens the **Page out of bounds** screen.
- `reader_error_page_load.epub` opens the **Page load error** screen.

Press Back to leave either screen.

## Notes

- The simulator test hooks are guarded by `SIMULATOR`; normal X4 firmware
  builds do not include them.
- Both screens are rendered by the existing `EpubReaderActivity` error
  handling code. The fixtures do not use a lookalike or a separate mock screen.
- The page-load fixture forces the normal page-load failure branch until its
  retry limit is reached. The out-of-bounds fixture sets an invalid page index
  after normal saved-position clamping, immediately before the existing
  bounds check.
