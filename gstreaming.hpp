#pragma once
#include <gst/gst.h>
#include <glib.h>
#include <gst/app/gstappsink.h>  // 추가
#include <gst/app/gstappsrc.h>   // 추가

#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>

#include <yaml-cpp/yaml.h>

#include "Hailoinfer.hpp"
#include "hailo/hailort.hpp"
#include "hailo/hailort_common.hpp" 

#include <fstream>

constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
using namespace hailort;

struct CallbackData {
    InferVStreams* infer_pipeline;
    GstElement* appsrc;
};

// 선언만 (초기화 없음!)
extern std::ofstream log_file;
extern bool header_written;

// 버스 메시지 콜백
gboolean on_message(GstBus *bus, GstMessage *message, gpointer data);
void makeSinkpipeline(GstElement* pipeline);
GstElement* makeSrcPipeline(GstElement* pipeline);
GstFlowReturn new_sample_callback(GstElement *sink, gpointer user_data);