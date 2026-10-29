#pragma once

// A scripted run of the UI for the PC build: NX_AUTOTEST=<script> makes the
// app press buttons, wait, open screens and save screenshots and view-tree
// dumps by itself -- the input goes straight into borealis, never through the
// desktop, so nothing lands in another window. Not compiled for the console.
//
// Script: one command per line, '#' starts a comment.
//   wait <ms>              let the UI and the network settle
//   press <button> [n]     A B X Y LB RB LT RT START BACK UP DOWN LEFT RIGHT
//   shot <name>            <out>/<name>.png, the frame as drawn
//   dump <name>            <out>/<name>.txt: the top screen's views, frames,
//                          texts, focus -- for overflows and clipped text
//   card <movie|tv> <id>   open a TMDB title's card
//   setup                  open the Jackett first-run setup
//   play <url> [title]     play a direct http(s) link (the player alone)
//   focuson <text>         focus the first focusable view showing <text>
//   focus                  log what has the focus
//   search                 open the catalog search
//   log <text>             a line in <out>/run.log
//   quit
// <out> is NX_AUTOTEST_OUT, or ./autotest.
namespace autotest
{

// Reads the script; false when NX_AUTOTEST is not set (the normal case).
bool init();

// Once per main-loop turn: runs whatever is due.
void tick();

} // namespace autotest
