#pragma once

#include <cstddef>
#include <cstdint>

#include "brscan/types.h"

namespace brscan {

// Decodes a complete JPEG buffer (SOI..EOI, the color/CGRAY scan payload
// described in docs/PROTOCOL.md) to an RGB Image, via libturbojpeg.
//
// `jpeg`/`len` must span exactly one baseline JPEG stream, as the scanner
// sends it: the whole payload in one clean buffer, not chunked. A
// truncated buffer (as produced by a cancelled scan with no clean EOI, see
// docs/PROTOCOL.md "Cancellation") fails to decode and returns
// Status::kProtocolError rather than crashing.
//
// Returns Status::kOk and fills `out` on success, Status::kProtocolError
// on any decode failure (malformed header, truncated data, or any other
// libturbojpeg error).
Status DecodeJpeg(const uint8_t* jpeg, size_t len, Image* out);

// The JPEG "height not yet known" convention: an SOF height of 65535 (the
// field's maximum) on a stream whose true height the encoder did not know when
// it wrote the header. The MFC-J5720DW's document feeder emits exactly this
// for an open-ended (A=...,0) job: SOF height 65535, then the entropy data for
// the real sheet, then a normal EOI. (The MFC-J6920DW instead writes the sheet's
// real height.) There is no DNL marker, so the true height is only known once
// the entropy data runs out. Source: live probing of an MFC-J5720DW,
// 2026-09-20 (PROVENANCE.md).
inline constexpr int kJpegHeightUnknown = 65535;

// Reads the height field of the first SOF0/SOF1/SOF2 marker in `jpeg`, or -1
// when no SOF marker is found in the bytes given (a payload can be inspected
// before it is complete; the SOF sits in the first few hundred bytes). Pure.
int JpegSofHeight(const uint8_t* jpeg, size_t len);

// For a JPEG whose SOF height is kJpegHeightUnknown: decodes it until the
// entropy data ends, counts the complete MCU rows that decoded cleanly, and
// rewrites the SOF height field in place to that count, so every downstream
// decoder (DecodeJpeg, the ICA band decoder, ImageIO) sees an ordinary page.
// The partial MCU row the data ended in (at most 16 rows of the sheet's
// bottom margin) is dropped. Returns kOk and sets *height to the new SOF
// height; kProtocolError if the stream has no SOF, is not decodable, or
// decodes to zero complete rows. A JPEG whose SOF height is already known is
// left untouched (kOk, *height = that height).
Status ResolveUnknownJpegHeight(std::vector<uint8_t>* jpeg, int* height);

}  // namespace brscan
