/* GStreamer MPEG audio parser
 * Copyright (C) 2006-2007 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2010 Mark Nauwelaerts <mnauw users sf net>
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-mpegaudioparse
 * @short_description: MPEG audio parser
 * @see_also: #GstAmrParse, #GstAACParse
 *
 * <refsect2>
 * <para>
 * Parses and frames mpeg1 audio streams. Provides seeking.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=test.mp3 ! mp3parse ! mad ! autoaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpegaudioparse.h"
#include <gst/base/gstbytereader.h>

GST_DEBUG_CATEGORY_STATIC (mpeg_audio_parse_debug);
#define GST_CAT_DEFAULT mpeg_audio_parse_debug

#define MPEG_AUDIO_CHANNEL_MODE_UNKNOWN -1
#define MPEG_AUDIO_CHANNEL_MODE_STEREO 0
#define MPEG_AUDIO_CHANNEL_MODE_JOINT_STEREO 1
#define MPEG_AUDIO_CHANNEL_MODE_DUAL_CHANNEL 2
#define MPEG_AUDIO_CHANNEL_MODE_MONO 3

#define CRC_UNKNOWN -1
#define CRC_PROTECTED 0
#define CRC_NOT_PROTECTED 1

#define XING_FRAMES_FLAG     0x0001
#define XING_BYTES_FLAG      0x0002
#define XING_TOC_FLAG        0x0004
#define XING_VBR_SCALE_FLAG  0x0008

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) [ 8000, 48000 ], channels = (int) [ 1, 2 ],"
        "parsed=(boolean) true")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) 1, parsed=(boolean)false")
    );

static void gst_mpeg_audio_parse_finalize (GObject * object);

static gboolean gst_mpeg_audio_parse_start (GstBaseParse * parse);
static gboolean gst_mpeg_audio_parse_stop (GstBaseParse * parse);
static gboolean gst_mpeg_audio_parse_check_valid_frame (GstBaseParse * parse,
    GstBuffer * buffer, guint * size, gint * skipsize);
static GstFlowReturn gst_mpeg_audio_parse_parse_frame (GstBaseParse * parse,
    GstBuffer * buf);
static GstFlowReturn gst_mpeg_audio_parse_pre_push_buffer (GstBaseParse * parse,
    GstBuffer * buf);
static gboolean gst_mpeg_audio_parse_convert (GstBaseParse * parse,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value);

GST_BOILERPLATE (GstMpegAudioParse, gst_mpeg_audio_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

#define GST_TYPE_MPEG_AUDIO_CHANNEL_MODE  \
    (gst_mpeg_audio_channel_mode_get_type())

static const GEnumValue mpeg_audio_channel_mode[] = {
  {MPEG_AUDIO_CHANNEL_MODE_UNKNOWN, "Unknown", "unknown"},
  {MPEG_AUDIO_CHANNEL_MODE_MONO, "Mono", "mono"},
  {MPEG_AUDIO_CHANNEL_MODE_DUAL_CHANNEL, "Dual Channel", "dual-channel"},
  {MPEG_AUDIO_CHANNEL_MODE_JOINT_STEREO, "Joint Stereo", "joint-stereo"},
  {MPEG_AUDIO_CHANNEL_MODE_STEREO, "Stereo", "stereo"},
  {0, NULL, NULL},
};

static GType
gst_mpeg_audio_channel_mode_get_type (void)
{
  static GType mpeg_audio_channel_mode_type = 0;

  if (!mpeg_audio_channel_mode_type) {
    mpeg_audio_channel_mode_type =
        g_enum_register_static ("GstMpegAudioChannelMode",
        mpeg_audio_channel_mode);
  }
  return mpeg_audio_channel_mode_type;
}

static const gchar *
gst_mpeg_audio_channel_mode_get_nick (gint mode)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (mpeg_audio_channel_mode); i++) {
    if (mpeg_audio_channel_mode[i].value == mode)
      return mpeg_audio_channel_mode[i].value_nick;
  }
  return NULL;
}

static void
gst_mpeg_audio_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "MPEG1 Audio Parser",
      "Codec/Parser/Audio",
      "Parses and frames mpeg1 audio streams (levels 1-3), provides seek",
      "Jan Schmidt <thaytan@mad.scientist.com>,"
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");
}

static void
gst_mpeg_audio_parse_class_init (GstMpegAudioParseClass * klass)
{
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mpeg_audio_parse_debug, "mpegaudioparse", 0,
      "MPEG1 audio stream parser");

  object_class->finalize = gst_mpeg_audio_parse_finalize;

  parse_class->start = GST_DEBUG_FUNCPTR (gst_mpeg_audio_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_mpeg_audio_parse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_mpeg_audio_parse_check_valid_frame);
  parse_class->parse_frame =
      GST_DEBUG_FUNCPTR (gst_mpeg_audio_parse_parse_frame);
  parse_class->pre_push_buffer =
      GST_DEBUG_FUNCPTR (gst_mpeg_audio_parse_pre_push_buffer);
  parse_class->convert = GST_DEBUG_FUNCPTR (gst_mpeg_audio_parse_convert);

  /* register tags */
#define GST_TAG_CRC      "has-crc"
#define GST_TAG_MODE     "channel-mode"

  gst_tag_register (GST_TAG_CRC, GST_TAG_FLAG_META, G_TYPE_BOOLEAN,
      "has crc", "Using CRC", NULL);
  gst_tag_register (GST_TAG_MODE, GST_TAG_FLAG_ENCODED, G_TYPE_STRING,
      "channel mode", "MPEG audio channel mode", NULL);

  g_type_class_ref (GST_TYPE_MPEG_AUDIO_CHANNEL_MODE);
}

static void
gst_mpeg_audio_parse_reset (GstMpegAudioParse * mp3parse)
{
  mp3parse->channels = -1;
  mp3parse->rate = -1;
  mp3parse->sent_codec_tag = FALSE;
  mp3parse->last_posted_crc = CRC_UNKNOWN;
  mp3parse->last_posted_channel_mode = MPEG_AUDIO_CHANNEL_MODE_UNKNOWN;

  mp3parse->xing_flags = 0;
  mp3parse->xing_bitrate = 0;
  mp3parse->xing_frames = 0;
  mp3parse->xing_total_time = 0;
  mp3parse->xing_bytes = 0;
  mp3parse->xing_vbr_scale = 0;
  memset (mp3parse->xing_seek_table, 0, 100);
  memset (mp3parse->xing_seek_table_inverse, 0, 256);

  mp3parse->vbri_bitrate = 0;
  mp3parse->vbri_frames = 0;
  mp3parse->vbri_total_time = 0;
  mp3parse->vbri_bytes = 0;
  mp3parse->vbri_seek_points = 0;
  g_free (mp3parse->vbri_seek_table);
  mp3parse->vbri_seek_table = NULL;
}

static void
gst_mpeg_audio_parse_init (GstMpegAudioParse * mp3parse,
    GstMpegAudioParseClass * klass)
{
  gst_mpeg_audio_parse_reset (mp3parse);
}

static void
gst_mpeg_audio_parse_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_mpeg_audio_parse_start (GstBaseParse * parse)
{
  GstMpegAudioParse *mp3parse = GST_MPEG_AUDIO_PARSE (parse);

  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (mp3parse), 1024);
  GST_DEBUG_OBJECT (parse, "starting");

  gst_mpeg_audio_parse_reset (mp3parse);

  return TRUE;
}

static gboolean
gst_mpeg_audio_parse_stop (GstBaseParse * parse)
{
  GstMpegAudioParse *mp3parse = GST_MPEG_AUDIO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stopping");

  gst_mpeg_audio_parse_reset (mp3parse);

  return TRUE;
}

static const guint mp3types_bitrates[2][3][16] = {
  {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}
      },
  {
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}
      },
};

static const guint mp3types_freqs[3][3] = { {44100, 48000, 32000},
{22050, 24000, 16000},
{11025, 12000, 8000}
};

static inline guint
mp3_type_frame_length_from_header (GstMpegAudioParse * mp3parse, guint32 header,
    guint * put_version, guint * put_layer, guint * put_channels,
    guint * put_bitrate, guint * put_samplerate, guint * put_mode,
    guint * put_crc)
{
  guint length;
  gulong mode, samplerate, bitrate, layer, channels, padding, crc;
  gulong version;
  gint lsf, mpg25;

  if (header & (1 << 20)) {
    lsf = (header & (1 << 19)) ? 0 : 1;
    mpg25 = 0;
  } else {
    lsf = 1;
    mpg25 = 1;
  }

  version = 1 + lsf + mpg25;

  layer = 4 - ((header >> 17) & 0x3);

  crc = (header >> 16) & 0x1;

  bitrate = (header >> 12) & 0xF;
  bitrate = mp3types_bitrates[lsf][layer - 1][bitrate] * 1000;
  /* The caller has ensured we have a valid header, so bitrate can't be
     zero here. */
  g_assert (bitrate != 0);

  samplerate = (header >> 10) & 0x3;
  samplerate = mp3types_freqs[lsf + mpg25][samplerate];

  padding = (header >> 9) & 0x1;

  mode = (header >> 6) & 0x3;
  channels = (mode == 3) ? 1 : 2;

  switch (layer) {
    case 1:
      length = 4 * ((bitrate * 12) / samplerate + padding);
      break;
    case 2:
      length = (bitrate * 144) / samplerate + padding;
      break;
    default:
    case 3:
      length = (bitrate * 144) / (samplerate << lsf) + padding;
      break;
  }

  GST_DEBUG_OBJECT (mp3parse, "Calculated mp3 frame length of %u bytes",
      length);
  GST_DEBUG_OBJECT (mp3parse, "samplerate = %lu, bitrate = %lu, version = %lu, "
      "layer = %lu, channels = %lu, mode = %s", samplerate, bitrate, version,
      layer, channels, gst_mpeg_audio_channel_mode_get_nick (mode));

  if (put_version)
    *put_version = version;
  if (put_layer)
    *put_layer = layer;
  if (put_channels)
    *put_channels = channels;
  if (put_bitrate)
    *put_bitrate = bitrate;
  if (put_samplerate)
    *put_samplerate = samplerate;
  if (put_mode)
    *put_mode = mode;
  if (put_crc)
    *put_crc = crc;

  return length;
}

/* Minimum number of consecutive, valid-looking frames to consider
 * for resyncing */
#define MIN_RESYNC_FRAMES 3

/* Perform extended validation to check that subsequent headers match
 * the first header given here in important characteristics, to avoid
 * false sync. We look for a minimum of MIN_RESYNC_FRAMES consecutive
 * frames to match their major characteristics.
 *
 * If at_eos is set to TRUE, we just check that we don't find any invalid
 * frames in whatever data is available, rather than requiring a full
 * MIN_RESYNC_FRAMES of data.
 *
 * Returns TRUE if we've seen enough data to validate or reject the frame.
 * If TRUE is returned, then *valid contains TRUE if it validated, or false
 * if we decided it was false sync.
 * If FALSE is returned, then *valid contains minimum needed data.
 */
static gboolean
gst_mp3parse_validate_extended (GstMpegAudioParse * mp3parse, GstBuffer * buf,
    guint32 header, int bpf, gboolean at_eos, gint * valid)
{
  guint32 next_header;
  const guint8 *data;
  guint available;
  int frames_found = 1;
  int offset = bpf;

  available = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);

  while (frames_found < MIN_RESYNC_FRAMES) {
    /* Check if we have enough data for all these frames, plus the next
       frame header. */
    if (available < offset + 4) {
      if (at_eos) {
        /* Running out of data at EOS is fine; just accept it */
        *valid = TRUE;
        return TRUE;
      } else {
        *valid = offset + 4;
        return FALSE;
      }
    }

    next_header = GST_READ_UINT32_BE (data + offset);
    GST_DEBUG_OBJECT (mp3parse, "At %d: header=%08X, header2=%08X, bpf=%d",
        offset, (unsigned int) header, (unsigned int) next_header, bpf);

/* mask the bits which are allowed to differ between frames */
#define HDRMASK ~((0xF << 12)  /* bitrate */ | \
                  (0x1 <<  9)  /* padding */ | \
                  (0xf <<  4)  /* mode|mode extension */ | \
                  (0xf))        /* copyright|emphasis */

    if ((next_header & HDRMASK) != (header & HDRMASK)) {
      /* If any of the unmasked bits don't match, then it's not valid */
      GST_DEBUG_OBJECT (mp3parse, "next header doesn't match "
          "(header=%08X (%08X), header2=%08X (%08X), bpf=%d)",
          (guint) header, (guint) header & HDRMASK, (guint) next_header,
          (guint) next_header & HDRMASK, bpf);
      *valid = FALSE;
      return TRUE;
    } else if ((((next_header >> 12) & 0xf) == 0) ||
        (((next_header >> 12) & 0xf) == 0xf)) {
      /* The essential parts were the same, but the bitrate held an
         invalid value - also reject */
      GST_DEBUG_OBJECT (mp3parse, "next header invalid (bitrate)");
      *valid = FALSE;
      return TRUE;
    }

    bpf = mp3_type_frame_length_from_header (mp3parse, next_header,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    offset += bpf;
    frames_found++;
  }

  *valid = TRUE;
  return TRUE;
}

static gboolean
gst_mpeg_audio_parse_head_check (GstMpegAudioParse * mp3parse,
    unsigned long head)
{
  GST_DEBUG_OBJECT (mp3parse, "checking mp3 header 0x%08lx", head);
  /* if it's not a valid sync */
  if ((head & 0xffe00000) != 0xffe00000) {
    GST_WARNING_OBJECT (mp3parse, "invalid sync");
    return FALSE;
  }
  /* if it's an invalid MPEG version */
  if (((head >> 19) & 3) == 0x1) {
    GST_WARNING_OBJECT (mp3parse, "invalid MPEG version: 0x%lx",
        (head >> 19) & 3);
    return FALSE;
  }
  /* if it's an invalid layer */
  if (!((head >> 17) & 3)) {
    GST_WARNING_OBJECT (mp3parse, "invalid layer: 0x%lx", (head >> 17) & 3);
    return FALSE;
  }
  /* if it's an invalid bitrate */
  if (((head >> 12) & 0xf) == 0x0) {
    GST_WARNING_OBJECT (mp3parse, "invalid bitrate: 0x%lx."
        "Free format files are not supported yet", (head >> 12) & 0xf);
    return FALSE;
  }
  if (((head >> 12) & 0xf) == 0xf) {
    GST_WARNING_OBJECT (mp3parse, "invalid bitrate: 0x%lx", (head >> 12) & 0xf);
    return FALSE;
  }
  /* if it's an invalid samplerate */
  if (((head >> 10) & 0x3) == 0x3) {
    GST_WARNING_OBJECT (mp3parse, "invalid samplerate: 0x%lx",
        (head >> 10) & 0x3);
    return FALSE;
  }

  if ((head & 0x3) == 0x2) {
    /* Ignore this as there are some files with emphasis 0x2 that can
     * be played fine. See BGO #537235 */
    GST_WARNING_OBJECT (mp3parse, "invalid emphasis: 0x%lx", head & 0x3);
  }

  return TRUE;
}

static gboolean
gst_mpeg_audio_parse_check_valid_frame (GstBaseParse * parse, GstBuffer * buf,
    guint * framesize, gint * skipsize)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buf);
  GstMpegAudioParse *mp3parse = GST_MPEG_AUDIO_PARSE (parse);
  gint off, bpf;
  gboolean sync, drain, valid, caps_change;
  guint32 header;
  guint bitrate, layer, rate, channels, version, mode, crc;

  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < 6))
    return FALSE;

  off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffe00000, 0xffe00000,
      0, GST_BUFFER_SIZE (buf));

  GST_LOG_OBJECT (parse, "possible sync at buffer offset %d", off);

  /* didn't find anything that looks like a sync word, skip */
  if (off < 0) {
    *skipsize = GST_BUFFER_SIZE (buf) - 3;
    return FALSE;
  }

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (off > 0) {
    *skipsize = off;
    return FALSE;
  }

  /* make sure the values in the frame header look sane */
  header = GST_READ_UINT32_BE (GST_BUFFER_DATA (buf));
  if (!gst_mpeg_audio_parse_head_check (mp3parse, header)) {
    *skipsize = 1;
    return FALSE;
  }

  GST_LOG_OBJECT (parse, "got frame");

  bpf = mp3_type_frame_length_from_header (mp3parse, header,
      &version, &layer, &channels, &bitrate, &rate, &mode, &crc);
  g_assert (bpf != 0);

  if (channels != mp3parse->channels || rate != mp3parse->rate ||
      layer != mp3parse->layer || version != mp3parse->version)
    caps_change = TRUE;
  else
    caps_change = FALSE;

  sync = gst_base_parse_get_sync (parse);
  drain = gst_base_parse_get_drain (parse);

  if (!drain && (!sync || caps_change)) {
    if (!gst_mp3parse_validate_extended (mp3parse, buf, header, bpf, drain,
            &valid)) {
      /* not enough data */
      gst_base_parse_set_min_frame_size (parse, valid);
      *skipsize = 0;
      return FALSE;
    } else {
      if (!valid) {
        *skipsize = off + 2;
        return FALSE;
      }
    }
  }

  *framesize = bpf;
  return TRUE;
}

static void
gst_mpeg_audio_parse_handle_first_frame (GstMpegAudioParse * mp3parse,
    GstBuffer * buf)
{
  const guint32 xing_id = 0x58696e67;   /* 'Xing' in hex */
  const guint32 info_id = 0x496e666f;   /* 'Info' in hex - found in LAME CBR files */
  const guint32 vbri_id = 0x56425249;   /* 'VBRI' in hex */
  gint offset;
  guint64 avail;
  gint64 upstream_total_bytes = 0;
  GstFormat fmt = GST_FORMAT_BYTES;
  guint32 read_id;
  const guint8 *data;
  GstBaseParseSeekable seekable;
  guint bitrate;

  if (mp3parse->sent_codec_tag)
    return;

  /* Check first frame for Xing info */
  if (mp3parse->version == 1) { /* MPEG-1 file */
    if (mp3parse->channels == 1)
      offset = 0x11;
    else
      offset = 0x20;
  } else {                      /* MPEG-2 header */
    if (mp3parse->channels == 1)
      offset = 0x09;
    else
      offset = 0x11;
  }
  /* Skip the 4 bytes of the MP3 header too */
  offset += 4;

  /* Check if we have enough data to read the Xing header */
  avail = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);
  if (avail < offset + 8)
    return;

  /* The header starts at the provided offset */
  data += offset;

  /* obtain real upstream total bytes */
  fmt = GST_FORMAT_BYTES;
  if (!gst_pad_query_peer_duration (GST_BASE_PARSE_SINK_PAD (GST_BASE_PARSE
              (mp3parse)), &fmt, &upstream_total_bytes))
    upstream_total_bytes = 0;

  read_id = GST_READ_UINT32_BE (data);
  if (read_id == xing_id || read_id == info_id) {
    guint32 xing_flags;
    guint bytes_needed = offset + 8;
    gint64 total_bytes;
    GstClockTime total_time;

    GST_DEBUG_OBJECT (mp3parse, "Found Xing header marker 0x%x", xing_id);

    /* Read 4 base bytes of flags, big-endian */
    xing_flags = GST_READ_UINT32_BE (data + 4);
    if (xing_flags & XING_FRAMES_FLAG)
      bytes_needed += 4;
    if (xing_flags & XING_BYTES_FLAG)
      bytes_needed += 4;
    if (xing_flags & XING_TOC_FLAG)
      bytes_needed += 100;
    if (xing_flags & XING_VBR_SCALE_FLAG)
      bytes_needed += 4;
    if (avail < bytes_needed) {
      GST_DEBUG_OBJECT (mp3parse,
          "Not enough data to read Xing header (need %d)", bytes_needed);
      return;
    }

    GST_DEBUG_OBJECT (mp3parse, "Reading Xing header");
    mp3parse->xing_flags = xing_flags;

    data = GST_BUFFER_DATA (buf);
    data += offset + 8;

    if (xing_flags & XING_FRAMES_FLAG) {
      mp3parse->xing_frames = GST_READ_UINT32_BE (data);
      if (mp3parse->xing_frames == 0) {
        GST_WARNING_OBJECT (mp3parse,
            "Invalid number of frames in Xing header");
        mp3parse->xing_flags &= ~XING_FRAMES_FLAG;
      } else {
        mp3parse->xing_total_time = gst_util_uint64_scale (GST_SECOND,
            (guint64) (mp3parse->xing_frames) * (mp3parse->spf),
            mp3parse->rate);
      }

      data += 4;
    } else {
      mp3parse->xing_frames = 0;
      mp3parse->xing_total_time = 0;
    }

    if (xing_flags & XING_BYTES_FLAG) {
      mp3parse->xing_bytes = GST_READ_UINT32_BE (data);
      if (mp3parse->xing_bytes == 0) {
        GST_WARNING_OBJECT (mp3parse, "Invalid number of bytes in Xing header");
        mp3parse->xing_flags &= ~XING_BYTES_FLAG;
      }
      data += 4;
    } else {
      mp3parse->xing_bytes = 0;
    }

    /* If we know the upstream size and duration, compute the
     * total bitrate, rounded up to the nearest kbit/sec */
    if ((total_time = mp3parse->xing_total_time) &&
        (total_bytes = mp3parse->xing_bytes)) {
      mp3parse->xing_bitrate = gst_util_uint64_scale (total_bytes,
          8 * GST_SECOND, total_time);
      mp3parse->xing_bitrate += 500;
      mp3parse->xing_bitrate -= mp3parse->xing_bitrate % 1000;
    }

    if (xing_flags & XING_TOC_FLAG) {
      int i, percent = 0;
      guchar *table = mp3parse->xing_seek_table;
      guchar old = 0, new;
      guint first;

      first = data[0];
      GST_DEBUG_OBJECT (mp3parse,
          "Subtracting initial offset of %d bytes from Xing TOC", first);

      /* xing seek table: percent time -> 1/256 bytepos */
      for (i = 0; i < 100; i++) {
        new = data[i] - first;
        if (old > new) {
          GST_WARNING_OBJECT (mp3parse, "Skipping broken Xing TOC");
          mp3parse->xing_flags &= ~XING_TOC_FLAG;
          goto skip_toc;
        }
        mp3parse->xing_seek_table[i] = old = new;
      }

      /* build inverse table: 1/256 bytepos -> 1/100 percent time */
      for (i = 0; i < 256; i++) {
        while (percent < 99 && table[percent + 1] <= i)
          percent++;

        if (table[percent] == i) {
          mp3parse->xing_seek_table_inverse[i] = percent * 100;
        } else if (table[percent] < i && percent < 99) {
          gdouble fa, fb, fx;
          gint a = percent, b = percent + 1;

          fa = table[a];
          fb = table[b];
          fx = (b - a) / (fb - fa) * (i - fa) + a;
          mp3parse->xing_seek_table_inverse[i] = (guint16) (fx * 100);
        } else if (percent == 99) {
          gdouble fa, fb, fx;
          gint a = percent, b = 100;

          fa = table[a];
          fb = 256.0;
          fx = (b - a) / (fb - fa) * (i - fa) + a;
          mp3parse->xing_seek_table_inverse[i] = (guint16) (fx * 100);
        }
      }
    skip_toc:
      data += 100;
    } else {
      memset (mp3parse->xing_seek_table, 0, 100);
      memset (mp3parse->xing_seek_table_inverse, 0, 256);
    }

    if (xing_flags & XING_VBR_SCALE_FLAG) {
      mp3parse->xing_vbr_scale = GST_READ_UINT32_BE (data);
    } else
      mp3parse->xing_vbr_scale = 0;

    GST_DEBUG_OBJECT (mp3parse, "Xing header reported %u frames, time %"
        GST_TIME_FORMAT ", %u bytes, vbr scale %u", mp3parse->xing_frames,
        GST_TIME_ARGS (mp3parse->xing_total_time), mp3parse->xing_bytes,
        mp3parse->xing_vbr_scale);

    /* check for truncated file */
    if (upstream_total_bytes && mp3parse->xing_bytes &&
        mp3parse->xing_bytes * 0.8 > upstream_total_bytes) {
      GST_WARNING_OBJECT (mp3parse, "File appears to have been truncated; "
          "invalidating Xing header duration and size");
      mp3parse->xing_flags &= ~XING_BYTES_FLAG;
      mp3parse->xing_flags &= ~XING_FRAMES_FLAG;
    }
  } else if (read_id == vbri_id) {
    gint64 total_bytes, total_frames;
    GstClockTime total_time;
    guint16 nseek_points;

    GST_DEBUG_OBJECT (mp3parse, "Found VBRI header marker 0x%x", vbri_id);
    if (avail < offset + 26) {
      GST_DEBUG_OBJECT (mp3parse,
          "Not enough data to read VBRI header (need %d)", offset + 26);
      return;
    }

    GST_DEBUG_OBJECT (mp3parse, "Reading VBRI header");
    data = GST_BUFFER_DATA (buf);
    data += offset + 4;

    if (GST_READ_UINT16_BE (data) != 0x0001) {
      GST_WARNING_OBJECT (mp3parse,
          "Unsupported VBRI version 0x%x", GST_READ_UINT16_BE (data));
      return;
    }
    data += 2;

    /* Skip encoder delay */
    data += 2;

    /* Skip quality */
    data += 2;

    total_bytes = GST_READ_UINT32_BE (data);
    if (total_bytes != 0)
      mp3parse->vbri_bytes = total_bytes;
    data += 4;

    total_frames = GST_READ_UINT32_BE (data);
    if (total_frames != 0) {
      mp3parse->vbri_frames = total_frames;
      mp3parse->vbri_total_time = gst_util_uint64_scale (GST_SECOND,
          (guint64) (mp3parse->vbri_frames) * (mp3parse->spf), mp3parse->rate);
    }
    data += 4;

    /* If we know the upstream size and duration, compute the
     * total bitrate, rounded up to the nearest kbit/sec */
    if ((total_time = mp3parse->vbri_total_time) &&
        (total_bytes = mp3parse->vbri_bytes)) {
      mp3parse->vbri_bitrate = gst_util_uint64_scale (total_bytes,
          8 * GST_SECOND, total_time);
      mp3parse->vbri_bitrate += 500;
      mp3parse->vbri_bitrate -= mp3parse->vbri_bitrate % 1000;
    }

    nseek_points = GST_READ_UINT16_BE (data);
    data += 2;

    if (nseek_points > 0) {
      guint scale, seek_bytes, seek_frames;
      gint i;

      mp3parse->vbri_seek_points = nseek_points;

      scale = GST_READ_UINT16_BE (data);
      data += 2;

      seek_bytes = GST_READ_UINT16_BE (data);
      data += 2;

      seek_frames = GST_READ_UINT16_BE (data);

      if (scale == 0 || seek_bytes == 0 || seek_bytes > 4 || seek_frames == 0) {
        GST_WARNING_OBJECT (mp3parse, "Unsupported VBRI seek table");
        goto out_vbri;
      }

      if (avail < offset + 26 + nseek_points * seek_bytes) {
        GST_WARNING_OBJECT (mp3parse,
            "Not enough data to read VBRI seek table (need %d)",
            offset + 26 + nseek_points * seek_bytes);
        goto out_vbri;
      }

      if (seek_frames * nseek_points < total_frames - seek_frames ||
          seek_frames * nseek_points > total_frames + seek_frames) {
        GST_WARNING_OBJECT (mp3parse,
            "VBRI seek table doesn't cover the complete file");
        goto out_vbri;
      }

      if (avail < offset + 26) {
        GST_DEBUG_OBJECT (mp3parse,
            "Not enough data to read VBRI header (need %d)",
            offset + 26 + nseek_points * seek_bytes);
        return;
      }

      data = GST_BUFFER_DATA (buf);
      data += offset + 26;

      /* VBRI seek table: frame/seek_frames -> byte */
      mp3parse->vbri_seek_table = g_new (guint32, nseek_points);
      if (seek_bytes == 4)
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT32_BE (data) * scale;
          data += 4;
      } else if (seek_bytes == 3)
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT24_BE (data) * scale;
          data += 3;
      } else if (seek_bytes == 2)
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT16_BE (data) * scale;
          data += 2;
      } else                    /* seek_bytes == 1 */
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT8 (data) * scale;
          data += 1;
        }
    }
  out_vbri:

    GST_DEBUG_OBJECT (mp3parse, "VBRI header reported %u frames, time %"
        GST_TIME_FORMAT ", bytes %u", mp3parse->vbri_frames,
        GST_TIME_ARGS (mp3parse->vbri_total_time), mp3parse->vbri_bytes);

    /* check for truncated file */
    if (upstream_total_bytes && mp3parse->vbri_bytes &&
        mp3parse->vbri_bytes * 0.8 > upstream_total_bytes) {
      GST_WARNING_OBJECT (mp3parse, "File appears to have been truncated; "
          "invalidating VBRI header duration and size");
      mp3parse->vbri_valid = FALSE;
    } else {
      mp3parse->vbri_valid = TRUE;
    }
  } else {
    GST_DEBUG_OBJECT (mp3parse,
        "Xing, LAME or VBRI header not found in first frame");
  }

  /* set duration if tables provided a valid one */
  if (mp3parse->xing_flags & XING_FRAMES_FLAG) {
    gst_base_parse_set_duration (GST_BASE_PARSE (mp3parse), GST_FORMAT_TIME,
        mp3parse->xing_total_time, 0);
  }
  if (mp3parse->vbri_total_time != 0 && mp3parse->vbri_valid) {
    gst_base_parse_set_duration (GST_BASE_PARSE (mp3parse), GST_FORMAT_TIME,
        mp3parse->vbri_total_time, 0);
  }

  /* tell baseclass how nicely we can seek, and a bitrate if one found */
  seekable = GST_BASE_PARSE_SEEK_DEFAULT;
  if ((mp3parse->xing_flags & XING_TOC_FLAG) && mp3parse->xing_bytes &&
      mp3parse->xing_total_time)
    seekable = GST_BASE_PARSE_SEEK_TABLE;

  if (mp3parse->vbri_seek_table && mp3parse->vbri_bytes &&
      mp3parse->vbri_total_time)
    seekable = GST_BASE_PARSE_SEEK_TABLE;

  if (mp3parse->xing_bitrate)
    bitrate = mp3parse->xing_bitrate;
  else if (mp3parse->vbri_bitrate)
    bitrate = mp3parse->vbri_bitrate;
  else
    bitrate = 0;

  gst_base_parse_set_seek (GST_BASE_PARSE (mp3parse), seekable, bitrate);
}

static GstFlowReturn
gst_mpeg_audio_parse_parse_frame (GstBaseParse * parse, GstBuffer * buf)
{
  GstMpegAudioParse *mp3parse = GST_MPEG_AUDIO_PARSE (parse);
  guint bitrate, layer, rate, channels, version, mode, crc;

  g_return_val_if_fail (GST_BUFFER_SIZE (buf) >= 4, GST_FLOW_ERROR);

  if (!mp3_type_frame_length_from_header (mp3parse,
          GST_READ_UINT32_BE (GST_BUFFER_DATA (buf)),
          &version, &layer, &channels, &bitrate, &rate, &mode, &crc))
    goto broken_header;

  if (G_UNLIKELY (channels != mp3parse->channels || rate != mp3parse->rate ||
          layer != mp3parse->layer || version != mp3parse->version)) {
    GstCaps *caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 1,
        "mpegaudioversion", G_TYPE_INT, version,
        "layer", G_TYPE_INT, layer,
        "rate", G_TYPE_INT, rate,
        "channels", G_TYPE_INT, channels, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
    gst_buffer_set_caps (buf, caps);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);

    mp3parse->rate = rate;
    mp3parse->channels = channels;
    mp3parse->layer = layer;
    mp3parse->version = version;

    /* see http://www.codeproject.com/audio/MPEGAudioInfo.asp */
    if (mp3parse->layer == 1)
      mp3parse->spf = 384;
    else if (mp3parse->layer == 2)
      mp3parse->spf = 1152;
    else if (mp3parse->version == 1) {
      mp3parse->spf = 1152;
    } else {
      /* MPEG-2 or "2.5" */
      mp3parse->spf = 576;
    }

    /* lead_in:
     * We start pushing 9 frames earlier (29 frames for MPEG2) than
     * segment start to be able to decode the first frame we want.
     * 9 (29) frames are the theoretical maximum of frames that contain
     * data for the current frame (bit reservoir).
     *
     * lead_out:
     * Some mp3 streams have an offset in the timestamps, for which we have to
     * push the frame *after* the end position in order for the decoder to be
     * able to decode everything up until the segment.stop position. */
    gst_base_parse_set_frame_props (parse, mp3parse->rate, mp3parse->spf,
        (version == 1) ? 10 : 30, 2);
  }

  /* For first frame; check for seek tables and output a codec tag */
  gst_mpeg_audio_parse_handle_first_frame (mp3parse, buf);

  /* store some frame info for later processing */
  mp3parse->last_crc = crc;
  mp3parse->last_mode = mode;

  return GST_FLOW_OK;

/* ERRORS */
broken_header:
  {
    /* this really shouldn't ever happen */
    GST_ELEMENT_ERROR (parse, STREAM, DECODE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_mpeg_audio_parse_time_to_bytepos (GstMpegAudioParse * mp3parse,
    GstClockTime ts, gint64 * bytepos)
{
  gint64 total_bytes;
  GstClockTime total_time;

  /* If XING seek table exists use this for time->byte conversion */
  if ((mp3parse->xing_flags & XING_TOC_FLAG) &&
      (total_bytes = mp3parse->xing_bytes) &&
      (total_time = mp3parse->xing_total_time)) {
    gdouble fa, fb, fx;
    gdouble percent =
        CLAMP ((100.0 * gst_util_guint64_to_gdouble (ts)) /
        gst_util_guint64_to_gdouble (total_time), 0.0, 100.0);
    gint index = CLAMP (percent, 0, 99);

    fa = mp3parse->xing_seek_table[index];
    if (index < 99)
      fb = mp3parse->xing_seek_table[index + 1];
    else
      fb = 256.0;

    fx = fa + (fb - fa) * (percent - index);

    *bytepos = (1.0 / 256.0) * fx * total_bytes;

    return TRUE;
  }

  if (mp3parse->vbri_seek_table && (total_bytes = mp3parse->vbri_bytes) &&
      (total_time = mp3parse->vbri_total_time)) {
    gint i, j;
    gdouble a, b, fa, fb;

    i = gst_util_uint64_scale (ts, mp3parse->vbri_seek_points - 1, total_time);
    i = CLAMP (i, 0, mp3parse->vbri_seek_points - 1);

    a = gst_guint64_to_gdouble (gst_util_uint64_scale (i, total_time,
            mp3parse->vbri_seek_points));
    fa = 0.0;
    for (j = i; j >= 0; j--)
      fa += mp3parse->vbri_seek_table[j];

    if (i + 1 < mp3parse->vbri_seek_points) {
      b = gst_guint64_to_gdouble (gst_util_uint64_scale (i + 1, total_time,
              mp3parse->vbri_seek_points));
      fb = fa + mp3parse->vbri_seek_table[i + 1];
    } else {
      b = gst_guint64_to_gdouble (total_time);
      fb = total_bytes;
    }

    *bytepos = fa + ((fb - fa) / (b - a)) * (gst_guint64_to_gdouble (ts) - a);

    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_mpeg_audio_parse_bytepos_to_time (GstMpegAudioParse * mp3parse,
    gint64 bytepos, GstClockTime * ts)
{
  gint64 total_bytes;
  GstClockTime total_time;

  /* If XING seek table exists use this for byte->time conversion */
  if ((mp3parse->xing_flags & XING_TOC_FLAG) &&
      (total_bytes = mp3parse->xing_bytes) &&
      (total_time = mp3parse->xing_total_time)) {
    gdouble fa, fb, fx;
    gdouble pos;
    gint index;

    pos = CLAMP ((bytepos * 256.0) / total_bytes, 0.0, 256.0);
    index = CLAMP (pos, 0, 255);
    fa = mp3parse->xing_seek_table_inverse[index];
    if (index < 255)
      fb = mp3parse->xing_seek_table_inverse[index + 1];
    else
      fb = 10000.0;

    fx = fa + (fb - fa) * (pos - index);

    *ts = (1.0 / 10000.0) * fx * gst_util_guint64_to_gdouble (total_time);

    return TRUE;
  }

  if (mp3parse->vbri_seek_table &&
      (total_bytes = mp3parse->vbri_bytes) &&
      (total_time = mp3parse->vbri_total_time)) {
    gint i = 0;
    guint64 sum = 0;
    gdouble a, b, fa, fb;

    do {
      sum += mp3parse->vbri_seek_table[i];
      i++;
    } while (i + 1 < mp3parse->vbri_seek_points
        && sum + mp3parse->vbri_seek_table[i] < bytepos);
    i--;

    a = gst_guint64_to_gdouble (sum);
    fa = gst_guint64_to_gdouble (gst_util_uint64_scale (i, total_time,
            mp3parse->vbri_seek_points));

    if (i + 1 < mp3parse->vbri_seek_points) {
      b = a + mp3parse->vbri_seek_table[i + 1];
      fb = gst_guint64_to_gdouble (gst_util_uint64_scale (i + 1, total_time,
              mp3parse->vbri_seek_points));
    } else {
      b = total_bytes;
      fb = gst_guint64_to_gdouble (total_time);
    }

    *ts = gst_gdouble_to_guint64 (fa + ((fb - fa) / (b - a)) * (bytepos - a));

    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_mpeg_audio_parse_convert (GstBaseParse * parse, GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value)
{
  GstMpegAudioParse *mp3parse = GST_MPEG_AUDIO_PARSE (parse);
  gboolean res = FALSE;

  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES)
    res =
        gst_mpeg_audio_parse_time_to_bytepos (mp3parse, src_value, dest_value);
  else if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME)
    res = gst_mpeg_audio_parse_bytepos_to_time (mp3parse, src_value,
        (GstClockTime *) dest_value);

  /* if no tables, fall back to default estimated rate based conversion */
  if (!res)
    return gst_base_parse_convert_default (parse, src_format, src_value,
        dest_format, dest_value);

  return res;
}

static GstFlowReturn
gst_mpeg_audio_parse_pre_push_buffer (GstBaseParse * parse, GstBuffer * buf)
{
  GstMpegAudioParse *mp3parse = GST_MPEG_AUDIO_PARSE (parse);
  GstTagList *taglist;

  /* tag sending done late enough in hook to ensure pending events
   * have already been sent */

  if (!mp3parse->sent_codec_tag) {
    gchar *codec;

    /* codec tag */
    if (mp3parse->layer == 3) {
      codec = g_strdup_printf ("MPEG %d Audio, Layer %d (MP3)",
          mp3parse->version, mp3parse->layer);
    } else {
      codec = g_strdup_printf ("MPEG %d Audio, Layer %d",
          mp3parse->version, mp3parse->layer);
    }
    taglist = gst_tag_list_new ();
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_AUDIO_CODEC, codec, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (mp3parse),
        GST_BASE_PARSE_SRC_PAD (mp3parse), taglist);
    g_free (codec);

    /* also signals the end of first-frame processing */
    mp3parse->sent_codec_tag = TRUE;
  }

  /* we will create a taglist (if any of the parameters has changed)
   * to add the tags that changed */
  taglist = NULL;
  if (mp3parse->last_posted_crc != mp3parse->last_crc) {
    gboolean using_crc;

    if (!taglist) {
      taglist = gst_tag_list_new ();
    }
    mp3parse->last_posted_crc = mp3parse->last_crc;
    if (mp3parse->last_posted_crc == CRC_PROTECTED) {
      using_crc = TRUE;
    } else {
      using_crc = FALSE;
    }
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_CRC,
        using_crc, NULL);
  }

  if (mp3parse->last_posted_channel_mode != mp3parse->last_mode) {
    if (!taglist) {
      taglist = gst_tag_list_new ();
    }
    mp3parse->last_posted_channel_mode = mp3parse->last_mode;

    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_MODE,
        gst_mpeg_audio_channel_mode_get_nick (mp3parse->last_mode), NULL);
  }

  /* if the taglist exists, we need to send it */
  if (taglist) {
    gst_element_found_tags_for_pad (GST_ELEMENT (mp3parse),
        GST_BASE_PARSE_SRC_PAD (mp3parse), taglist);
  }

  /* usual clipping applies */
  return GST_BASE_PARSE_FLOW_CLIP;
}