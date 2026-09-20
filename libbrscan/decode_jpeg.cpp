#include "decode_jpeg.h"

#include <algorithm>
#include <csetjmp>
#include <new>
#include <vector>

#include <jpeglib.h>
#include <turbojpeg.h>

namespace brscan {
namespace {

// Dimension ceilings for a decoded page. tjDecompressHeader3 reads width and
// height straight from the JPEG's SOF marker, so a malformed or malicious page
// could declare near-INT_MAX dimensions and force a multi-gigabyte allocation
// (with an uncaught std::bad_alloc) the moment the pixel buffer is sized from
// them. The device's largest real page is ledger/A3 (about 12 x 17 in) at up
// to 1200 dpi, i.e. ~14400 x 20400 px (~294 Mpx); these bound both the
// per-side dimension and the total pixel count well above that but far below
// the runaway range. See L5.
constexpr long kMaxScanDimension = 30000;              // px per side.
constexpr long kMaxScanPixels = 600L * 1000 * 1000;    // 600 Mpx total.

}  // namespace

Status DecodeJpeg(const uint8_t* jpeg, size_t len, Image* out) {
  if (jpeg == nullptr || len == 0 || out == nullptr) {
    return Status::kProtocolError;
  }

  tjhandle handle = tjInitDecompress();
  if (handle == nullptr) return Status::kProtocolError;

  int width = 0;
  int height = 0;
  int subsamp = 0;
  int colorspace = 0;
  if (tjDecompressHeader3(handle, jpeg, static_cast<unsigned long>(len),
                           &width, &height, &subsamp, &colorspace) != 0 ||
      width <= 0 || height <= 0 || width > kMaxScanDimension ||
      height > kMaxScanDimension ||
      static_cast<long>(width) * height > kMaxScanPixels) {
    tjDestroy(handle);
    return Status::kProtocolError;
  }

  // Size the output buffer from the (now bounded) SOF dimensions; still guard
  // the allocation so an unexpectedly large-but-in-bounds page fails cleanly
  // as a protocol error rather than throwing.
  std::vector<uint8_t> pixels;
  try {
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  } catch (const std::bad_alloc&) {
    tjDestroy(handle);
    return Status::kProtocolError;
  }
  const int rc = tjDecompress2(handle, jpeg, static_cast<unsigned long>(len),
                                pixels.data(), width, /*pitch=*/0, height,
                                TJPF_RGB, TJFLAG_ACCURATEDCT);
  // libjpeg-turbo signals two kinds of trouble through the same rc != 0:
  // TJERR_FATAL, where the output is unusable, and TJERR_WARNING, where it
  // recovered and the pixel buffer is still fully populated. Real
  // MFC-J6920DW ADF pages hit the latter: the device streams a baseline
  // JPEG whose final entropy segment is a few MCUs short of the height it
  // declares in its SOF (it omits trailing all-white scan lines of blank
  // paper), so tjDecompress2 returns -1 with "premature end of data
  // segment" after decoding every row -- macOS ImageIO (`sips`) decodes
  // the same bytes without complaint. Rejecting that warning drops an
  // otherwise-perfect page (observed on ADF simplex page 1), so only a
  // fatal error is treated as a decode failure here. tjGetErrorCode must
  // be read before tjDestroy frees the handle.
  //
  // This widens acceptance to ALL libjpeg-turbo recoverable warnings, not
  // only "premature end" -- any warning still yields a fully-written pixel
  // buffer. The caller's structural checks are the backstop: ReadChunkedJpeg
  // only hands over a payload it has confirmed ends at the JPEG EOI marker
  // (ff d9), so a truncated or garbled stream is caught before it reaches
  // here. Requires libjpeg-turbo >= 2.0 for tjGetErrorCode / TJERR_WARNING.
  const bool fatal = rc != 0 && tjGetErrorCode(handle) != TJERR_WARNING;
  tjDestroy(handle);
  if (fatal) return Status::kProtocolError;

  out->width = width;
  out->height = height;
  out->format = PixelFormat::kRgb;
  out->pixels = std::move(pixels);
  return Status::kOk;
}

namespace {

// Byte offset of the first SOF0/1/2 marker's height field, or -1. Walks the
// marker segments from SOI; stops at SOS (image data) or the buffer's end.
long SofHeightOffset(const uint8_t* jpeg, size_t len) {
  if (len < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8) return -1;
  size_t p = 2;
  while (p + 4 <= len) {
    if (jpeg[p] != 0xff) return -1;
    const uint8_t marker = jpeg[p + 1];
    if (marker == 0xff) { ++p; continue; }  // Fill byte.
    if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7)) {
      p += 2;  // Standalone marker, no length.
      continue;
    }
    if (marker == 0xda || marker == 0xd9) return -1;  // SOS / EOI: no SOF.
    const size_t seg_len = (static_cast<size_t>(jpeg[p + 2]) << 8) | jpeg[p + 3];
    if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
      // SOF: length(2) precision(1) height(2) width(2) ...
      if (p + 7 > len) return -1;
      return static_cast<long>(p + 5);
    }
    p += 2 + seg_len;
  }
  return -1;
}

// libjpeg error manager for ResolveUnknownJpegHeight: fatal errors longjmp
// out; the first warning (the entropy decoder hitting EOI / end of data
// early -- JWRN_HIT_MARKER / JWRN_JPEG_EOF) records the scanline count at
// that moment, which is the first row of the MCU row being decoded when the
// data ran out.
struct ProbeErrorMgr {
  struct jpeg_error_mgr pub;
  jmp_buf jump;
  long warned_at_scanline = -1;
};

void ProbeErrorExit(j_common_ptr cinfo) {
  std::longjmp(reinterpret_cast<ProbeErrorMgr*>(cinfo->err)->jump, 1);
}

void ProbeEmitMessage(j_common_ptr cinfo, int msg_level) {
  if (msg_level != -1) return;  // Trace message, not a warning.
  auto* err = reinterpret_cast<ProbeErrorMgr*>(cinfo->err);
  if (err->warned_at_scanline >= 0) return;
  err->warned_at_scanline =
      reinterpret_cast<j_decompress_ptr>(cinfo)->output_scanline;
}

}  // namespace

int JpegSofHeight(const uint8_t* jpeg, size_t len) {
  const long off = SofHeightOffset(jpeg, len);
  if (off < 0) return -1;
  return (static_cast<int>(jpeg[off]) << 8) | jpeg[off + 1];
}

Status ResolveUnknownJpegHeight(std::vector<uint8_t>* jpeg, int* height) {
  if (jpeg == nullptr || height == nullptr) return Status::kProtocolError;
  const long off = SofHeightOffset(jpeg->data(), jpeg->size());
  if (off < 0) return Status::kProtocolError;
  const int declared = (static_cast<int>((*jpeg)[off]) << 8) | (*jpeg)[off + 1];
  if (declared != kJpegHeightUnknown) {
    *height = declared;
    return Status::kOk;
  }

  // libjpeg rejects any dimension above JPEG_MAX_DIMENSION (65500) at the
  // header, so the probe decode runs with the SOF temporarily set to the
  // largest MCU-aligned legal height (65472 = 4091 x 16). It is restored to
  // the resolved count below, or left at the probe value only on failure
  // paths that return kProtocolError anyway.
  constexpr int kProbeHeight = 65472;
  (*jpeg)[off] = static_cast<uint8_t>(kProbeHeight >> 8);
  (*jpeg)[off + 1] = static_cast<uint8_t>(kProbeHeight & 0xff);

  struct jpeg_decompress_struct cinfo {};
  ProbeErrorMgr err{};
  cinfo.err = jpeg_std_error(&err.pub);
  err.pub.error_exit = &ProbeErrorExit;
  err.pub.emit_message = &ProbeEmitMessage;
  std::vector<uint8_t> row;
  if (setjmp(err.jump)) {
    jpeg_destroy_decompress(&cinfo);
    return Status::kProtocolError;
  }
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, jpeg->data(), static_cast<unsigned long>(jpeg->size()));
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    return Status::kProtocolError;
  }
  // Decode at the smallest scale libjpeg offers: only the ROW COUNT matters
  // here, and 1/8 scale still runs the full entropy decode (which is what hits
  // the end of data), at a fraction of the IDCT/color-convert cost. The
  // recorded scanline is in output (scaled) rows; scale it back up below.
  cinfo.scale_num = 1;
  cinfo.scale_denom = 8;
  cinfo.dct_method = JDCT_IFAST;
  cinfo.do_fancy_upsampling = FALSE;
  jpeg_start_decompress(&cinfo);
  row.assign(static_cast<size_t>(cinfo.output_width) * cinfo.output_components,
             0);
  JSAMPROW ptr[1] = {row.data()};
  while (cinfo.output_scanline < cinfo.output_height &&
         err.warned_at_scanline < 0) {
    if (jpeg_read_scanlines(&cinfo, ptr, 1) != 1) break;
  }
  // Whole-MCU-row granularity in FULL-resolution rows. At 1/8 scale an MCU row
  // (16 rows at 4:2:0, 8 at 4:4:4) is 2 or 1 output rows; scale by the
  // decoder's own ratio rather than assuming the subsampling.
  const long scaled = err.warned_at_scanline >= 0
                          ? err.warned_at_scanline
                          : static_cast<long>(cinfo.output_scanline);
  const long full_mcu_rows = static_cast<long>(cinfo.max_v_samp_factor) *
                             DCTSIZE;  // Rows per MCU row at full scale.
  const long scaled_mcu_rows =
      std::max<long>(1, full_mcu_rows * cinfo.output_height /
                            std::max<long>(1, cinfo.image_height));
  long rows = (scaled / scaled_mcu_rows) * full_mcu_rows;
  jpeg_abort_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  if (rows <= 0 || rows >= kJpegHeightUnknown) return Status::kProtocolError;

  (*jpeg)[off] = static_cast<uint8_t>(rows >> 8);
  (*jpeg)[off + 1] = static_cast<uint8_t>(rows & 0xff);
  *height = static_cast<int>(rows);
  return Status::kOk;
}

}  // namespace brscan
