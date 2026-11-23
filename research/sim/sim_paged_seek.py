#!/usr/bin/env python3
"""Faithful re-implementation of miniaudio's decoded_paged seek/read path.

Mirrors, function for function:
  - ma_paged_audio_buffer_seek_to_pcm_frame   (miniaudio.h ~61031)
  - ma_paged_audio_buffer_read_pcm_frames     (miniaudio.h ~60977)
  - ma_resource_manager_data_buffer_seek/read (miniaudio.h ~71635/71747)
  - ma_resource_manager_data_buffer_node_decode_next_page (~70767)

Purpose: produce independently re-computable numbers for paged-seek-audio.md.
Run with: python3 -I sim_paged_seek.py
"""

# ---- result codes (miniaudio.h 4196+) ----
MA_SUCCESS = 0
MA_BAD_SEEK = -25
MA_BUSY = -19
MA_AT_END = -17

PAGE = 48000          # MA_RESOURCE_MANAGER_PAGE_SIZE_IN_MILLISECONDS=1000 @ 48 kHz
TOTAL_FRAMES = 384000  # 8 pages, last one full


class PagedAudioBuffer:
    """ma_paged_audio_buffer + its ma_paged_audio_buffer_data (single connector)."""

    def __init__(self):
        # linked list of pages, each a (start, size); dummy head at index -1
        self.pages = []           # appended by job thread, in order
        self.p_current = None     # page index currently read; None = dummy head
        self.rel = 0              # relativeCursor
        self.abs = 0              # absoluteCursor

    # ---- ma_paged_audio_buffer_seek_to_pcm_frame ----
    def seek(self, frame_index):
        if frame_index == self.abs:
            return MA_SUCCESS
        if frame_index < self.abs:
            self.p_current = None
            self.abs = 0
            self.rel = 0
        if frame_index > self.abs:
            running = 0
            tail_idx = len(self.pages) - 1
            for i, (start, size) in enumerate(self.pages):
                beg, end = running, running + size
                if frame_index >= beg:
                    if frame_index < end or (frame_index == end and i == tail_idx):
                        self.p_current = i
                        self.abs = frame_index
                        self.rel = frame_index - beg
                        return MA_SUCCESS
                running = end
            return MA_BAD_SEEK
        return MA_SUCCESS

    # ---- ma_paged_audio_buffer_read_pcm_frames ----
    def read(self, frame_count):
        result = MA_SUCCESS
        total = 0
        # caller guarantees connector read only happens with no pending seek
        while total < frame_count:
            start, size = self.pages[self.p_current]
            remaining_in_page = size - self.rel
            this_it = min(remaining_in_page, frame_count - total)
            total += this_it
            self.abs += this_it
            self.rel += this_it
            if self.rel == size:
                if self.p_current + 1 < len(self.pages):
                    self.p_current += 1
                    self.rel = 0
                else:
                    result = MA_AT_END
                    break
        return result, total


class DataBufferNode:
    """Resource-manager side: decoder cursor + decodedFrameCount + page list."""

    def __init__(self):
        self.decoder_cursor = 0
        self.decoded_frames = 0
        self.busy = True                 # node result == MA_BUSY while pages remain

    def decode_next_page(self, pab: PagedAudioBuffer):
        """ma_resource_manager_data_buffer_node_decode_next_page, paged branch."""
        frames_to_try = PAGE
        frames_read = min(frames_to_try, TOTAL_FRAMES - self.decoder_cursor)
        if frames_read == 0:
            self.busy = False
            return MA_AT_END, 0
        pab.pages.append((self.decoder_cursor, frames_read))
        self.decoder_cursor += frames_read
        self.decoded_frames += frames_read
        if self.decoder_cursor >= TOTAL_FRAMES:
            self.busy = False
            return MA_AT_END, frames_read   # normalized to SUCCESS by page job
        return MA_SUCCESS, frames_read      # page job reposts, node stays MA_BUSY


class DataBuffer:
    """ma_resource_manager_data_buffer wrapper (public-API side state)."""

    def __init__(self, pab: PagedAudioBuffer, node: DataBufferNode):
        self.pab = pab
        self.node = node
        self.seek_target = None
        self.pending = False

    # ---- ma_resource_manager_data_buffer_seek_to_pcm_frame ----
    def seek(self, frame_index):
        result = self.pab.seek(frame_index)
        if result != MA_SUCCESS:
            return result          # connector untouched
        self.seek_target = None
        self.pending = False
        return MA_SUCCESS

    # ---- ma_resource_manager_data_buffer_read_pcm_frames (paged branch only) ----
    def read(self, frame_count):
        # scheduled seek must be attempted first
        if self.pending:
            self.pending = False
            result = self.pab.seek(self.seek_target)
            if result == MA_BAD_SEEK:
                self.pending = True
                return MA_BUSY, 0
            if result != MA_SUCCESS:
                return result, 0
        result, frames_read = self.pab.read(frame_count)
        # MA_AT_END while node still loading -> MA_BUSY (line 71726)
        if result == MA_AT_END and self.node.busy:
            result = MA_BUSY
        if result == MA_SUCCESS and frames_read == 0:
            result = MA_AT_END
        return result, frames_read

    def available_frames(self):
        # line 71922: decodedFrameCount - connector cursor
        return max(0, self.node.decoded_frames - self.pab.abs)


def line(t):
    print(t)


def main():
    pab = PagedAudioBuffer()
    node = DataBufferNode()
    db = DataBuffer(pab, node)

    line("== bootstrap: job thread decodes pages 0..3, page 4 in flight ==")
    for _ in range(4):
        r, n = node.decode_next_page(pab)
        line(f"decode job -> result={r:+d} frames={n:6d} decodedFrameCount={node.decoded_frames:6d} pages={len(pab.pages)}")
    line(f"node result = {'MA_BUSY' if node.busy else 'MA_SUCCESS'}")
    line("")

    # playback had consumed into page 1 (frames 48000..95999); set connector cursor
    assert db.seek(70000) == MA_SUCCESS
    line(f"playback cursor before drags: absoluteCursor={pab.abs} (page {pab.p_current}, rel {pab.rel})")
    line("")

    line("== drag #1: seek to 120500 (page 2, already decoded) ==")
    r1 = db.seek(120500)
    line(f"ma_sound_seek -> data_buffer_seek result={r1:+d}  "
         f"connector abs={pab.abs} pCurrent=page{pab.p_current} rel={pab.rel} pending={db.pending}")
    line("UI: ma_sound_get_cursor_in_pcm_frames already returns 120500 (seekTarget atomic, line 79783)")
    line("")

    line("== drag #2 (before old prefetch finished): seek to 220000 (page 4, not decoded) ==")
    r2 = db.seek(220000)
    line(f"ma_sound_seek -> data_buffer_seek result={r2:+d} (MA_BAD_SEEK={MA_BAD_SEEK}); "
         f"connector abs still={pab.abs} (drag#1 position), page{pab.p_current}")
    # NOTE: real engine path schedules via the read path; emulate sound-level behavior:
    db.seek_target = 220000
    db.pending = True
    line("engine read path will schedule: seekTargetInPCMFrames=220000 seekToCursorOnNextRead=MA_TRUE")
    line("")

    line("== mixer reads, 1024 frames each, while page-4 job is still running ==")
    for k in range(1, 4):
        r, n = db.read(1024)
        line(f"read #{k}: result={r:+d} (MA_BUSY={MA_BUSY}) framesRead={n:4d} "
             f"connector abs stays={pab.abs} pending={db.pending} available={db.available_frames()}")
    line("")

    line("== job thread finishes page 4 ==")
    r, n = node.decode_next_page(pab)
    line(f"decode job -> result={r:+d} frames={n} decodedFrameCount={node.decoded_frames} pages={len(pab.pages)}")
    line("")

    line("== next mixer read: pending seek now lands ==")
    r, n = db.read(1024)
    status = {MA_SUCCESS: "MA_SUCCESS", MA_BUSY: "MA_BUSY", MA_AT_END: "MA_AT_END"}[r]
    line(f"read #4: result={r:+d} ({status}) framesRead={n:4d} "
         f"connector abs={pab.abs} (seek first moved it to 220000, then +{n}) "
         f"pCurrent=page{pab.p_current} rel={pab.rel} pending={db.pending}")
    assert pab.abs == 221024 and n == 1024 and r == MA_SUCCESS and db.pending is False
    line("-> first 1024 frames delivered are PCM [220000, 221024): first frame of the last jump")
    line("")

    line("== read straight through the page-4 tail while page 5 not yet decoded ==")
    # drain to end of page 4 (ends at 240000): 240000-221024 = 18976 frames left
    r, n = db.read(18976)
    line(f"read #5 (18976 asked): result={r:+d} framesRead={n:5d} abs={pab.abs} page{pab.p_current} rel={pab.rel}")
    r, n = db.read(1024)
    line(f"read #6 (1024 asked, page 5 missing): result={r:+d} (MA_BUSY) framesRead={n:4d} abs={pab.abs}")
    assert (r, n) == (MA_BUSY, 0)
    line("")

    line("== decode the rest of the file ==")
    while node.busy:
        rr, nn = node.decode_next_page(pab)
        line(f"decode job -> result={rr:+d} frames={nn:5d} decodedFrameCount={node.decoded_frames}")
    line(f"node result = MA_SUCCESS; total pages={len(pab.pages)}")
    line("")

    line("== now the same tail read distinguishes 'waiting' from 'played out' ==")
    # advance near the true end: file ends at 384000
    assert db.seek(383000) == MA_SUCCESS
    r, n = db.read(1024)                 # only 1000 frames exist after cursor
    line(f"read near EOF (1024 asked, 1000 exist): result={r:+d} (MA_AT_END={MA_AT_END}) framesRead={n:4d} abs={pab.abs}")
    assert (r, n) == (MA_AT_END, 1000)
    r, n = db.read(1024)
    line(f"read at EOF again: result={r:+d} framesRead={n:4d}")
    assert (r, n) == (MA_AT_END, 0)
    line("")

    line("== backward jump while a forward prefetch is pending ==")
    # rewind to page 1 (already decoded): backward seek resets to head then walks forward
    r = db.seek(60000)
    line(f"seek 60000 (backward): result={r:+d} abs={pab.abs} page{pab.p_current} rel={pab.rel}")
    assert r == MA_SUCCESS and pab.abs == 60000
    r, n = db.read(512)
    line(f"read after backward seek: result={r:+d} framesRead={n:3d} abs={pab.abs}")
    assert (r, n) == (MA_SUCCESS, 512)


if __name__ == "__main__":
    main()
