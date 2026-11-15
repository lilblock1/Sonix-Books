# Named bookmarks and notes — item 7

Development branch: `codex/named-bookmarks-notes`, based on the released
`v0.2.0.5-test` source. No new R1 firmware is published for this step.

In an open book, open the reading menu with Play/Pause or a centre long press,
then choose **Bookmark / note**. The **Name** and **Note** tabs share the
on-screen keyboard; **New line** inserts a line break in the note. **Save** adds the bookmark; **Cancel** or Play/Pause
discards the edit. Opening the editor at a position already bookmarked edits
that bookmark rather than creating a duplicate. Blank name and note fields
save an ordinary bookmark. Double tapping the top bookmark zone still saves
an ordinary bookmark immediately.

From the Books bookmark list, open a book's marks. Tap a mark to resume at its
saved position, or hold it and choose **Edit name / note** or **Delete bookmark**.
Names appear above the original excerpt and notes below it. Existing unnamed
bookmarks retain their excerpts and positions. Deleting a mark moves its
entire annotation with the remaining marks; deleting the final mark shows an
empty-state message.

Names allow 48 UTF-8 bytes and notes 240 UTF-8 bytes. Notes support line breaks.
The keyboard's character limit is an upper bound; text containing multibyte
characters can reach the byte limit earlier, in which case Save asks for
shorter text and leaves the edit open. There are still at most 64 marks per
book. A full shared settings store or a failed write is reported instead of
showing a successful save. Unsaved text stays available for retry or cancel.

The existing `bookmarks` position/excerpt keys remain readable by older
firmware. Optional names and notes use separate, hex-encoded keys in the same
ebook settings file, preserving delimiters, leading/trailing spaces, Unicode,
and newlines without injecting INI records. A position marker prevents
annotations being attached to a different mark if an older firmware shifts
the legacy entries. Missing or corrupt annotations fall back to a plain mark.
Saving, editing and deleting apply all this book's keys in one checked
transaction, with an in-memory rollback if the atomic file replacement fails.
Reading positions, history, font choices and other books' bookmarks are retained.

Temporary arrays are bounded to 64 marks and 449 key changes and freed after
each explicit save/delete. No chapter parsing, extra background worker or
resident annotation cache is introduced. One lazy keyboard overlay is reused
by both callers. Leaving its owning screen discards the edit; reader page
presses cannot turn the page behind it.

Regression checks cover legacy bookmarks, Unicode and multiline text,
separator/INI-looking text, byte boundaries, duplicate positions, immutable
positions during edits, book isolation, deletion/compaction, metadata cleanup,
malformed records, maximum positions, full-book/full-store behavior, and
failed-save rollback. R1 touch layout, keyboard entry, physical-key timing,
power-cycle persistence and memory use still need on-device validation in a
future combined firmware build.
