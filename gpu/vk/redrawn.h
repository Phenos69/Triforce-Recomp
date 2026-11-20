// (Triforce, 11-18) Whether a render target is drawn again frame after frame, for the new-texture budget (vk_texture.cpp
// create_later, new_texture_later, new_texture_waits, the layered ASTC split, upload_defer, upload_again_later). The budget
// leaves a frame's textures for the next frame, the draw samples the dummy (or a texture's last bytes) in their place and the
// present hold hides that frame: right for a target the game draws again in the next frame (the scene), wrong for one it draws
// once and keeps. Ocarina of Time composes each text box once (its CPU-written R8 line textures, 256x46 and 640x46, drawn into a
// 262x104 .. 656x113 R8 target whose result it samples for as long as the text stands): a compose in a frame over the budget kept
// the dummy for the whole session, the title's buttons, the options' values and dialogue boxes empty or partial (#bug-tracker
// "missing text from main menu and settings", "Menu buttons don't appear unless selected", "Title Screen invisible buttons",
// "Text is invisible sometimes"; found and tested by Kaefergeneral). So a draw may leave a texture for later only when every
// target it writes was written in the frame before too and its program writes no memory (vk_draw.cpp draw: draw_may_defer).
//
// A target's frames: last_written, the frame the GPU last wrote it, and prev_written, the frame it was written in before the
// frame of last_written (0: none, or its contents began anew then). begin_pass marks a pass's targets written before the second
// and later draws into them build their descriptors, so last_written alone would make a box composed for the first time look
// drawn before from its second draw on: prev_written tells them apart.
#pragma once
#include <cstdint>

namespace redrawn {
/// prev_written after a write in `frame` (live: the target held the GPU's contents before this write, not released since)
inline uint64_t prev_on_write(uint64_t prev, uint64_t last, bool live, uint64_t frame) {
    if (last == frame) return prev;   // (written already in this frame: the frame before stays what it was)
    return live ? last : 0;
}
/// Whether a target was written in the frame before `frame` (live: as above, now)
inline bool drawn_before(uint64_t prev, uint64_t last, bool live, uint64_t frame) {
    if (!live || frame == 0) return false;
    if (last == frame) return prev != 0 && prev + 1 == frame;   // (already marked in this frame: the run before it)
    return last + 1 == frame;
}
}  // namespace redrawn
