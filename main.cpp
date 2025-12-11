#include <gst/gst.h>
#include <glib.h>
#include <gst/app/gstappsink.h>  // 추가
#include <gst/app/gstappsrc.h>   // 추가

#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>

#include <yaml-cpp/yaml.h>

#include "Hailoinfer.hpp"
#include "gstreaming.hpp"
#include "hailo/hailort.hpp"
#include "hailo/hailort_common.hpp" 


#include <fstream>

static Config load(const std::string& yaml_path) {
        YAML::Node config = YAML::LoadFile(yaml_path);
        Config cfg;
        
        // 카메라
        cfg.device = config["device"].as<std::string>();
        
        // 모델
        cfg.hef_path = config["model"]["hef_path"].as<std::string>();
        cfg.model_width = config["model"]["input_size"]["width"].as<int>();
        cfg.model_height = config["model"]["input_size"]["height"].as<int>();
        
        // 비디오 입력
        cfg.video_inWidth = config["video"]["input"]["width"].as<int>();
        cfg.video_inHeight = config["video"]["input"]["height"].as<int>();
        
        // 비디오 출력
        cfg.video_outWidth = config["video"]["output"]["width"].as<int>();
        cfg.video_outHeight = config["video"]["output"]["height"].as<int>();
        cfg.output_name = config["video"]["output"]["file"].as<std::string>();
        cfg.frame_rate = config["video"]["framerate"].as<int>();
        
        // 인코더
        cfg.encode_speed = config["encoder"]["speed_preset"].as<int>();
        cfg.tune = config["encoder"]["tune"].as<int>();
        
        // 로그
        cfg.timing_log = config["logging"]["timing_log"].as<std::string>();
        
        return cfg;
}

int main(int argc, char *argv[]){
    Config g_config = load("config.yaml");

    // ========== 1. 지역 변수로 로그 파일 열기 ==========
    std::ofstream log_file(g_config.timing_log, std::ios::app);  // 지역 변수!
    bool header_written = false;  // 지역 변수!
    
    if (!log_file.is_open()) {
        std::cerr << "로그 파일 열기 실패: " << g_config.timing_log << std::endl;
        return -1;
    }

    //infer 초기화
    auto vdevice = VDevice::create();
    if (!vdevice) {
        std::cerr << "Failed to create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    auto network_group = configure_network_group(*vdevice.value(),g_config);
    if (!network_group) {
        std::cerr << "Failed to configure network group " << g_config.hef_path << std::endl;
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

    // ========== 2. config 전달 (수정!) ==========
    makeSinkpipeline(sink_pipeline, g_config);
    GstElement *appsrc = makeSrcPipeline(src_pipeline, g_config);

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

    // ========== 3. CallbackData에 config 추가 (수정!) ==========
    CallbackData cb_data;
    cb_data.infer_pipeline = &pipeline.value();
    cb_data.appsrc = appsrc;
    cb_data.config = &g_config;  // ← 추가!
    cb_data.log_file = &log_file;              // ← 추가!
    cb_data.header_written = &header_written;  // ← 추가!

    
    // callback 연결
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample_callback), &cb_data);

    // 버스 설정
    GstBus *sink_bus = gst_pipeline_get_bus(GST_PIPELINE(sink_pipeline));
    GstBus *src_bus = gst_pipeline_get_bus(GST_PIPELINE(src_pipeline));
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    gst_bus_add_signal_watch(sink_bus);
    gst_bus_add_signal_watch(src_bus);
    g_signal_connect(sink_bus, "message", G_CALLBACK(on_message), loop);
    g_signal_connect(src_bus, "message", G_CALLBACK(on_message), loop);

    // 파이프라인 시작
    gst_element_set_state(sink_pipeline, GST_STATE_PLAYING);
    gst_element_set_state(src_pipeline, GST_STATE_PLAYING);
    
    g_main_loop_run(loop);
    
    // 정리
    gst_element_set_state(sink_pipeline, GST_STATE_NULL);
    gst_element_set_state(src_pipeline, GST_STATE_NULL);
    gst_object_unref(sink_bus);
    gst_object_unref(src_bus);
    gst_object_unref(sink_pipeline);
    gst_object_unref(src_pipeline);
    g_main_loop_unref(loop);
    
    // ========== 4. 로그 파일 닫기 (추가!) ==========
    log_file.close();
    
    return 0;
}