#include <gst/gst.h>
#include <glib.h>
#include <gst/app/gstappsink.h>  // 추가
#include <gst/app/gstappsrc.h>   // 추가

#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>

#include "Hailoinfer.hpp"
#include "hailo/hailort.hpp"
#include "hailo/hailort_common.hpp" 

#include <fstream>
static std::ofstream log_file("timing_log.csv", std::ios::app);
static bool header_written = false;

constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;
using namespace hailort;

struct CallbackData {
    InferVStreams* infer_pipeline;
    GstElement* appsrc;
};

// 버스 메시지 콜백
static gboolean on_message(GstBus *bus, GstMessage *message, gpointer data) {
    GMainLoop *loop = (GMainLoop*)data;
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            std::cerr << "에러: " << err->message << std::endl;
            std::cerr << "디버그: " << debug << std::endl;
            g_error_free(err);
            g_free(debug);
            g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "재생 완료" << std::endl;
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_WARNING: {
            GError *warn;
            gchar *debug;
            gst_message_parse_warning(message, &warn, &debug);
            std::cerr << "경고: " << warn->message << std::endl;
            g_error_free(warn);
            g_free(debug);
            break;
        }
        default:
            break;
    }
    
    return TRUE;
}

void makeSinkpipeline(GstElement* pipeline){
    
    if (!gst_is_initialized()) {
        std::cerr << "GStreamer 초기화 실패" << std::endl;
    }
    
    std::cout << "GStreamer 초기화 성공" << std::endl;
    
    // 설정
    std::string device = "/dev/video0";
    std::string hef_path = "./hefs/Midas_v2_small_model.hef";

    // 엘리먼트 생성
    GstElement *source = gst_element_factory_make("v4l2src", "source");
    GstElement *videoconvert1 = gst_element_factory_make("videoconvert", "convert1");  // 추가!
    GstElement *scaler = gst_element_factory_make("videoscale", "scaler");
    GstElement *queue1 = gst_element_factory_make("queue", "queue1");


    // 엘리먼트 생성 후 NULL 체크
    if (!source || !videoconvert1 || !scaler || !queue1) {
        std::cerr << "엘리먼트 생성 실패!" << std::endl;
        if (!source) std::cerr << "  - source 실패" << std::endl;
        if (!videoconvert1) std::cerr << "  - videoconvert1 실패" << std::endl;
        if (!scaler) std::cerr << "  - scaler 실패" << std::endl;
        if (!queue1) std::cerr << "  - queue1 실패" << std::endl;
        gst_object_unref(pipeline); 
    }

    // 파이프라인에 추가
    gst_bin_add_many(GST_BIN(pipeline), 
        source,  videoconvert1, scaler, queue1,  NULL);

    // Property 설정
    g_object_set(source, "device", device.c_str(), NULL);

    // Part 1 연결: source → ... → appsink
    GstCaps *caps1 = gst_caps_from_string("video/x-raw,format=RGB,width=640,height=480");

    if (!gst_element_link(source, videoconvert1)) {
        std::cerr << "source → videoconvert1 링크 실패" << std::endl;
    }
    if (!gst_element_link(videoconvert1, scaler)) {
        std::cerr << "videoconvert1 → scaler 링크 실패" << std::endl;
    }
    if (!gst_element_link_filtered(scaler, queue1, caps1)) {
        std::cerr << "scaler → queue1 링크 실패" << std::endl;
    }
    gst_caps_unref(caps1);

}

GstElement* makeSrcPipeline(GstElement* pipeline) {
    GstElement *appsrc = gst_element_factory_make("appsrc", "app_src");
    GstElement *videoconvert = gst_element_factory_make("videoconvert", "convert_src");
    GstElement *sink = gst_element_factory_make("autovideosink", "video_sink");

    if (!appsrc || !videoconvert || !sink) {
        std::cerr << "Src 파이프라인 엘리먼트 생성 실패!" << std::endl;
        return nullptr;
    }

    gst_bin_add_many(GST_BIN(pipeline), appsrc, videoconvert, sink, NULL);

    GstCaps *caps = gst_caps_from_string(
        "video/x-raw,format=RGB,width=1280,height=480,framerate=30/1");
    g_object_set(appsrc,
        "caps", caps,
        "format", GST_FORMAT_TIME,
        "is-live", TRUE,
        NULL);
    gst_caps_unref(caps);

    if (!gst_element_link_many(appsrc, videoconvert, sink, NULL)) {
        std::cerr << "appsrc 파이프라인 링크 실패" << std::endl;
        return nullptr;
    }

    return appsrc;  // ← 반환!
}

static GstFlowReturn new_sample_callback(GstElement *sink, gpointer user_data) {
    auto t_start = std::chrono::high_resolution_clock::now();


    // user_data에서 pipeline 꺼내기
    CallbackData* cb_data = static_cast<CallbackData*>(user_data);  // ← 수정!
    InferVStreams* infer_pipeline = cb_data->infer_pipeline;
    GstElement* appsrc = cb_data->appsrc;
    // 1. appsink에서 sample 가져오기

    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    
    // 2. 여기에 후처리 코드 작성
    // map.data: NPU 출력 데이터 (raw bytes)
    // map.size: 데이터 크기

    auto buffer_data = map.data;
    auto buffer_size = map.size;
    
    int width = 256;
    int height = 256;

    
    // ========== 전처리 시작 ==========
    auto t_preprocess_start = std::chrono::high_resolution_clock::now();
    cv::Mat raw_img(480, 640, CV_8UC3, map.data);

    cv::Mat input_img;
    cv::resize(raw_img, input_img, cv::Size(256, 256), 0, 0, cv::INTER_LINEAR);
    auto t_preprocess_end = std::chrono::high_resolution_clock::now();
    
    // ========== 추론 시작 ==========
    auto t_infer_start = std::chrono::high_resolution_clock::now();    
    cv::Mat output_img;
    output_img = infer(*infer_pipeline, input_img);
    auto t_infer_end = std::chrono::high_resolution_clock::now();
    
    // ========== 후처리 시작 ==========
    auto t_postprocess_start = std::chrono::high_resolution_clock::now();
    cv::Mat depth_normalized;
    cv::normalize(output_img, depth_normalized, 0, 255, cv::NORM_MINMAX);

    // 4. 컬러맵 적용 (GRAY → BGR uint8)
    cv::Mat depth_colormap;
    cv::applyColorMap(depth_normalized, depth_colormap, cv::COLORMAP_MAGMA);
    cv::cvtColor(depth_colormap, depth_colormap, cv::COLOR_RGB2BGR); 

    // 5. 640x480 리사이즈
    cv::Mat depth_resized;
    cv::resize(depth_colormap, depth_resized, cv::Size(640, 480), 0, 0, cv::INTER_LINEAR);
    depth_resized.convertTo(depth_resized, CV_8UC3);
    
    cv::Mat result;
    cv::hconcat(raw_img, depth_resized, result); 

    // ===== appsrc로 push =====
    gsize size = result.total() * result.elemSize();
    GstBuffer *out_buffer = gst_buffer_new_allocate(NULL, size, NULL);
    
    GstMapInfo out_map;
    gst_buffer_map(out_buffer, &out_map, GST_MAP_WRITE);
    memcpy(out_map.data, result.data, size);
    gst_buffer_unmap(out_buffer, &out_map);
    
    gst_app_src_push_buffer(GST_APP_SRC(appsrc), out_buffer);
    auto t_postprocess_end = std::chrono::high_resolution_clock::now();
    auto t_end = std::chrono::high_resolution_clock::now();
    
    // ========== 시간 계산 및 출력 ==========
    auto preprocess_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_preprocess_end - t_preprocess_start).count();
    auto infer_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_infer_end - t_infer_start).count();
    auto postprocess_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_postprocess_end - t_postprocess_start).count();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    
if (!header_written) {
    log_file << "Timestamp(ms),Preprocess(ms),Infer(ms),Postprocess(ms),Total(ms)\n";
    header_written = true;
}

    // 현재 시각 가져오기
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    log_file << timestamp << ","
            << preprocess_time << ","
            << infer_time << ","
            << postprocess_time << ","
            << total_time << "\n";

    std::cout << "⏱️  전처리: " << preprocess_time << "ms | "
              << "추론: " << infer_time << "ms | "
              << "후처리: " << postprocess_time << "ms | "
              << "전체: " << total_time << "ms" << std::endl;
    
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    
    return GST_FLOW_OK;

}



int main(int argc, char *argv[]) {
    //infer 초기화
    const std::string HEF_FILE = "./hefs/Midas_v2_small_model.hef";
    auto vdevice = VDevice::create();
    if (!vdevice) {
        std::cerr << "Failed to create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    auto network_group = configure_network_group(*vdevice.value(),HEF_FILE);
    if (!network_group) {
        std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
        return network_group.status();
    }

    auto input_params = network_group.value()->make_input_vstream_params({}, FORMAT_TYPE, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!input_params) {
        std::cerr << "Failed make_input_vstream_params " << input_params.status() << std::endl;
        return input_params.status();
    }

    auto output_params = network_group.value()->make_output_vstream_params({}, FORMAT_TYPE, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!output_params) {
        std::cerr << "Failed make_output_vstream_params " << output_params.status() << std::endl;
        return output_params.status();
    }

    auto pipeline = InferVStreams::create(*network_group.value(), input_params.value(), output_params.value());
    if (!pipeline) {
        std::cerr << "Failed to create inference pipeline " << pipeline.status() << std::endl;
        return pipeline.status();
    }

    // GStreamer 초기화
    gst_init(&argc, &argv);
    GstElement *sink_pipeline = gst_pipeline_new("hailo-infersink");
    GstElement *src_pipeline = gst_pipeline_new("source_view");

    makeSinkpipeline(sink_pipeline);
    GstElement *appsrc = makeSrcPipeline(src_pipeline);  // ← 한 번만!

    // appsink 생성 및 링크
    GstElement *appsink = gst_element_factory_make("appsink", "app_sink");
    gst_bin_add(GST_BIN(sink_pipeline), appsink);
    g_object_set(appsink, 
        "emit-signals", TRUE,
        "sync", FALSE,
        "max-buffers", 1,
        "drop", TRUE,
        NULL);

    GstElement *queue1 = gst_bin_get_by_name(GST_BIN(sink_pipeline), "queue1");
    gst_element_link(queue1, appsink);
    gst_object_unref(queue1);

    // CallbackData 초기화
    CallbackData cb_data;
    cb_data.infer_pipeline = &pipeline.value();
    cb_data.appsrc = appsrc;
    
    // callback 연결 (한 번만!)
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample_callback), &cb_data);

    // 버스 설정 (두 파이프라인 모두)
    GstBus *sink_bus = gst_pipeline_get_bus(GST_PIPELINE(sink_pipeline));
    GstBus *src_bus = gst_pipeline_get_bus(GST_PIPELINE(src_pipeline));
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    gst_bus_add_signal_watch(sink_bus);
    gst_bus_add_signal_watch(src_bus);
    g_signal_connect(sink_bus, "message", G_CALLBACK(on_message), loop);
    g_signal_connect(src_bus, "message", G_CALLBACK(on_message), loop);

    // 파이프라인 시작 (둘 다!)
    gst_element_set_state(sink_pipeline, GST_STATE_PLAYING);
    gst_element_set_state(src_pipeline, GST_STATE_PLAYING);
    
    g_main_loop_run(loop);
    
    // 정리 (둘 다!)
    gst_element_set_state(sink_pipeline, GST_STATE_NULL);
    gst_element_set_state(src_pipeline, GST_STATE_NULL);
    gst_object_unref(sink_bus);
    gst_object_unref(src_bus);
    gst_object_unref(sink_pipeline);
    gst_object_unref(src_pipeline);
    g_main_loop_unref(loop);
    
}
