#include <gst/gst.h>
#include <gst/app/gstappsink.h>  // 추가
#include <gst/app/gstappsrc.h>   // 추가
#include <glib.h>
#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>



// decodebin의 pad-added 콜백 (카메라가 MJPEG일 경우)
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *convert = (GstElement*)data;
    
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (caps) {
        GstStructure *structure = gst_caps_get_structure(caps, 0);
        const gchar *name = gst_structure_get_name(structure);
        
        if (g_str_has_prefix(name, "video")) {
            GstPad *sink_pad = gst_element_get_static_pad(convert, "sink");
            
            if (!gst_pad_is_linked(sink_pad)) {
                GstPadLinkReturn ret = gst_pad_link(pad, sink_pad);
                if (ret != GST_PAD_LINK_OK) {
                    std::cerr << "패드 연결 실패" << std::endl;
                }
            }
            
            gst_object_unref(sink_pad);
        }
        
        gst_caps_unref(caps);
    }
}

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

struct FrameData {
    cv::Mat frame;
};

// 메인 루프에서 실행될 함수
static gboolean show_frame_idle(gpointer user_data) {
    FrameData *data = (FrameData *)user_data;
    
    // imshow 대신 파일 저장
    static int frame_count = 0;
    cv::imwrite("frame_" + std::to_string(frame_count++) + ".png", data->frame);
    std::cout << "Frame saved: " << frame_count << std::endl;
    
    delete data;
    return FALSE;
    }

static GstFlowReturn new_sample_callback(GstElement *sink, gpointer user_data) {
    std::cout << "🔵 Callback called!" << std::endl;  // 맨 첫줄에 추가
    
    // 1. appsink에서 sample 가져오기
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    
    // 2. 여기에 후처리 코드 작00000000000000000000000000000000000성00000000000000000000000000000000000000000
    // map.data: NPU 출력 데이터 (raw bytes)
    // map.size: 데이터 크기
00000000000000000000000000000000000000000000000000000000000000000
    auto buffer_data = map.data;
    auto buffer_size = map.size;
    
    in0000000t width = 256;
    int height = 256;000000000000000000000000000 00000000

    if (width*height == map.size){
        std::cout << "datasize is int8"<<std::endl;
    }
    else{
        std::cout << "datasize is fp32"<<std::endl;
    }
    
    // 1. float32로 읽기 (NPU 출력)
    cv::Mat depth_map(height, width, CV_32F, (float*)map.data);

    // 2. 0-255로 정규화
    cv::Mat depth_normalized;
    cv::normalize(depth_map, depth_normalized, 0, 255, cv::NORM_MINMAX);

    // 3. uint8로 변환
    cv::Mat depth_uint8;
    depth_normalized.convertTo(depth_uint8, CV_8U);

    // 4. 컬러맵 적용 (GRAY → BGR uint8)
    cv::Mat depth_colormap;
    cv::applyColorMap(depth_uint8, depth_colormap, cv::COLORMAP_MAGMA);

    // 5. 640x480 리사이즈
    cv::Mat depth_resized;
    cv::resize(depth_colormap, depth_resized, cv::Size(640, 480), 0, 0, cv::INTER_LINEAR);
    
    std::cout << "Resized: " << depth_resized.cols << "x" << depth_resized.rows 
              << " channels=" << depth_resized.channels() << std::endl;
    
    
    // 6. appsrc로 전송
    FrameData *frame_data = new FrameData();
    frame_data->frame = depth_resized.clone(); 
    g_idle_add(show_frame_idle, frame_data); 

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    
    return GST_FLOW_OK;  // ✅ 여기서 끝!

}

static gboolean opencv_event_handler(gpointer user_data) {
    cv::waitKey(1);  // Qt 이벤트 처리
    return TRUE;  // 계속 반복
}



int main(int argc, char *argv[]) {
    // GStreamer 초기화
    gst_init(&argc, &argv);
    
    if (!gst_is_initialized()) {
        std::cerr << "GStreamer 초기화 실패" << std::endl;
        return -1;
    }
    
    std::cout << "GStreamer 초기화 성공" << std::endl;
    
    // 설정 (환경에 맞게 수정)
    std::string device = "/dev/video0";
    std::string hef_path = "./hefs/Midas_v2_small_model.hef"; // HEF 파일 경로

    // Part 1: 입력 → NPU → appsink
    GstElement *pipeline = gst_pipeline_new("hailo-pipeline");
    if (!pipeline) {
        std::cerr << "파이프라인 생성 실패" << std::endl;
        return -1;
    }

    GstElement *source = gst_element_factory_make("v4l2src", "source");
    GstElement *videoconvert1 = gst_element_factory_make("videoconvert", "convert1");  // 추가!
    GstElement *scaler = gst_element_factory_make("videoscale", "scaler");
    GstElement *queue1 = gst_element_factory_make("queue", "queue1");
    GstElement *hailonet = gst_element_factory_make("hailonet", "hailonet");
    GstElement *appsink = gst_element_factory_make("appsink", "app_sink");



    // 엘리먼트 생성 후 NULL 체크
    if (!source || !videoconvert1 || !scaler || !queue1 || !hailonet || !appsink) {
        std::cerr << "엘리먼트 생성 실패!" << std::endl;
        if (!source) std::cerr << "  - source 실패" << std::endl;
        if (!videoconvert1) std::cerr << "  - videoconvert1 실패" << std::endl;
        if (!scaler) std::cerr << "  - scaler 실패" << std::endl;
        if (!queue1) std::cerr << "  - queue1 실패" << std::endl;
        if (!hailonet) std::cerr << "  - hailonet 실패" << std::endl;
        if (!appsink) std::cerr << "  - appsink 실패" << std::endl;
        gst_object_unref(pipeline); 
        return -1;
    }


    
    gst_bin_add_many(GST_BIN(pipeline), 
        source, videoconvert1, scaler, queue1, hailonet, appsink, NULL); 

    // Property 설정
    g_object_set(source, "device", device.c_str(), NULL);
    g_object_set(hailonet, 
                 "hef-path", hef_path.c_str(),
                 "is-active", TRUE,
                 NULL);

    // appsink 설정 (중요!)
    g_object_set(appsink, 
        "emit-signals", TRUE,
        "sync", FALSE,
        "max-buffers", 1,
        "drop", TRUE,
        NULL);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample_callback), NULL);



    // Part 1 연결: source → ... → appsink
    GstCaps *caps1 = gst_caps_from_string("video/x-raw,format=RGB,width=256,height=256");


    if (!gst_element_link(source, videoconvert1)) {
        std::cerr << "source → videoconvert1 링크 실패" << std::endl;
        return -1;
    }
    if (!gst_element_link(videoconvert1, scaler)) {
        std::cerr << "videoconvert1 → scaler 링크 실패" << std::endl;
        return -1;
    }
    if (!gst_element_link_filtered(scaler, queue1, caps1)) {
        std::cerr << "scaler → queue1 링크 실패" << std::endl;
        return -1;
    }
    if (!gst_element_link(queue1, hailonet)) {
        std::cerr << "queue1 → hailonet 링크 실패" << std::endl;
        return -1;
    }
    if (!gst_element_link(hailonet, appsink)) {
        std::cerr << "hailonet → appsink 링크 실패" << std::endl;
        return -1;
    }
    gst_caps_unref(caps1);



    // 버스 설정
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    gst_bus_add_signal_watch(bus);
    


    // 파이프라인 시작
    std::cout << "파이프라인 시작..." << std::endl;
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "파이프라인 시작 실패" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }
    
    // 메인루프 실행
    g_timeout_add(30, opencv_event_handler, NULL);
    g_main_loop_run(loop);
    
    // 정리
    std::cout << "종료 중..." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    
    return 0;
}