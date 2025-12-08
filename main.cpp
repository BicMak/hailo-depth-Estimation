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

struct Config {
    std::string device;
    std::string hef_path;
    int model_width, model_height;

    int video_inWidth, video_inHeight;
    int video_outWidth, video_outHeight;

    std::string output_name; 
    int frame_rate;
    int encode_speed;
    int tune;
    std::string timing_log; 
} g_config; 

int main(int argc, char *argv[]){
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
