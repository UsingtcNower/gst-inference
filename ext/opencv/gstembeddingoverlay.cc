/*
 * GStreamer
 * Copyright (C) 2019 RidgeRun
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
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstembeddingoverlay.h"
#include "gst/r2inference/gstinferencemeta.h"
#ifdef OCV_VERSION_LT_3_2
#include "opencv2/highgui/highgui.hpp"
#else
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#endif

static const cv::Scalar forest_green = cv::Scalar (11, 102, 35);
static const cv::Scalar chilli_red = cv::Scalar (194, 24, 7);
static const cv::Scalar white = cv::Scalar (255, 255, 255, 255);

GST_DEBUG_CATEGORY_STATIC (gst_embedding_overlay_debug_category);
#define GST_CAT_DEFAULT gst_embedding_overlay_debug_category

/* prototypes */
static GstFlowReturn gst_embedding_overlay_process_meta (GstVideoFrame *
    frame, GstMeta * meta, gdouble font_scale, gint thickness,
    gchar ** labels_list, gint num_labels);

enum
{
  PROP_0
};

struct _GstEmbeddingOverlay
{
  GstInferenceOverlay parent;
};

struct _GstClassificationOverlayClass
{
  GstInferenceOverlay parent;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstEmbeddingOverlay, gst_embedding_overlay,
    GST_TYPE_INFERENCE_OVERLAY,
    GST_DEBUG_CATEGORY_INIT (gst_embedding_overlay_debug_category,
        "embeddingoverlay", 0, "debug category for embedding_overlay element"));

static void
gst_embedding_overlay_class_init (GstEmbeddingOverlayClass * klass)
{
  GstInferenceOverlayClass *io_class = GST_INFERENCE_OVERLAY_CLASS (klass);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "embeddingoverlay", "Filter",
      "Overlays classification metadata on input buffer",
      "Carlos Rodriguez <carlos.rodriguez@ridgerun.com> \n\t\t\t"
      "   Jose Jimenez <jose.jimenez@ridgerun.com> \n\t\t\t"
      "   Michael Gruner <michael.gruner@ridgerun.com> \n\t\t\t"
      "   Carlos Aguero <carlos.aguero@ridgerun.com> \n\t\t\t"
      "   Miguel Taylor <miguel.taylor@ridgerun.com> \n\t\t\t"
      "   Greivin Fallas <greivin.fallas@ridgerun.com>");

  io_class->process_meta =
      GST_DEBUG_FUNCPTR (gst_embedding_overlay_process_meta);
  io_class->meta_type = GST_CLASSIFICATION_META_API_TYPE;
}

static void
gst_embedding_overlay_init (GstEmbeddingOverlay * embedding_overlay)
{
}

static GstFlowReturn
gst_embedding_overlay_process_meta (GstVideoFrame * frame, GstMeta * meta,
    gdouble font_scale, gint thickness, gchar ** labels_list, gint num_labels)
{
  GstClassificationMeta *class_meta;
  gint i, width, height, channels;
  gdouble current, diff;
  cv::Mat cv_mat;
  cv::String str;
  cv::Size size;
  cv::Scalar tmp_color;
  cv::Scalar color = cv::Scalar (0, 0, 0, 0);

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      channels = 3;
      break;
    default:
      channels = 4;
      break;
  }

  width = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0) / channels;
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  class_meta = (GstClassificationMeta *) meta;

  diff = 0.0;
  for (i = 0; i < class_meta->num_labels; ++i) {
    current = class_meta->label_probs[i];
    current = current - atof (labels_list[i]);
    current = current * current;
    diff = diff + current;
  }

  cv_mat = cv::Mat (height, width, CV_MAKETYPE (CV_8U, channels),
      (char *) frame->data[0]);
  if (diff < 1.0) {
    str = cv::format ("Pass");
    tmp_color[0] = forest_green[0];
    tmp_color[1] = forest_green[1];
    tmp_color[2] = forest_green[2];
  } else {
    str = cv::format ("Fail");
    tmp_color[0] = chilli_red[0];
    tmp_color[1] = chilli_red[1];
    tmp_color[2] = chilli_red[2];
  }

  /* Convert color according to colorspace */
  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      color[0] = tmp_color[2];
      color[1] = tmp_color[1];
      color[2] = tmp_color[0];
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      color[1] = tmp_color[0];
      color[2] = tmp_color[1];
      color[3] = tmp_color[2];
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      color[1] = tmp_color[2];
      color[2] = tmp_color[1];
      color[3] = tmp_color[0];
      break;
    default:
      color = tmp_color;
      break;
  }

  /* Put string on screen
   * 10*font_scale+16 aproximates text's rendered size on screen as a
   * lineal function to avoid using cv::getTextSize.
   * 5*thickness adds the border offset
   */
  cv::putText (cv_mat, str, cv::Point (5*thickness, 5*thickness+10*font_scale+16),
      cv::FONT_HERSHEY_PLAIN, font_scale, white, thickness + (thickness*0.5));
  cv::putText (cv_mat, str, cv::Point (5*thickness, 5*thickness+10*font_scale+16),
      cv::FONT_HERSHEY_PLAIN, font_scale, color, thickness);
  cv::rectangle (cv_mat, cv::Point (0, 0), cv::Point (width, height), color,
      10*thickness);

  return GST_FLOW_OK;
}
