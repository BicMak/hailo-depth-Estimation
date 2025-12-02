#include <gst/gst.h>
#include <glib.h>
#include <iostream>
#include <string>
#include <cstdio>
#include <memory>
#include <array>

// YouTube URL에서 직접 스트림 URL 추출 (yt-dlp 사용)
std::string get_youtube_stream_url(const std::string& youtube_url) {
    std::string command = "yt-dlp -f best --get-url \"" + youtube_url + "\" 2>/dev/null";
    
    std::array<char, 128> buffer;
    std::string result;
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        std::cerr << "yt-dlp 실행 실패" << std::endl;
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    // 개행 문자 제거
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    return result;
}

// decodebin의 pad-added 콜백
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *convert = (GstElement*)data;
    
    // 패드의 caps 가져오기
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (caps) {
        GstStructure *structure = gst_caps_get_structure(caps, 0);
        const gchar *name = gst_structure_get_name(structure);
        
        // video 타입인지 확인
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

int main(int argc, char *argv[]) {
    // GStreamer 초기화
    gst_init(&argc, &argv);
    
    if (gst_is_initialized()) {
        std::cout << "GStreamer 초기화 성공" << std::endl;
    } else {
        std::cerr << "초기화 실패" << std::endl;
        return -1;
    }
    

    
    // 엘리먼트 생성
    GstElement *pipeline = gst_pipeline_new("video-player");
    GstElement *source = gst_element_factory_make("v4l2src", "source");
    GstElement *decode = gst_element_factory_make("decodebin", "decode");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *scaler = gst_element_factory_make("videoscale", "scaler");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "filter");
    GstElement *sink = gst_element_factory_make("autovideosink", "sink");
    
    // 엘리먼트 생성 확인
    if (!pipeline || !source || !decode || !convert || !scaler || !capsfilter || !sink) {
        std::cerr << "엘리먼트 생성 실패" << std::endl;
        return -1;
    }
    
    // Property 설정
    g_object_set(source, "device", "/dev/video0", NULL);
    
    // Caps 설정
    GstCaps *caps = gst_caps_from_string("video/x-raw,width=256,height=256");
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);
    
    // 파이프라인에 추가
    gst_bin_add_many(GST_BIN(pipeline), source, decode, convert, scaler, capsfilter, sink, NULL);
    
    // 정적 연결
    if (!gst_element_link(source, decode)) {
        std::cerr << "source와 decode 연결 실패" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }
    
    // 동적 연결 (decodebin pad-added 시그널)
    g_signal_connect(decode, "pad-added", G_CALLBACK(on_pad_added), convert);
    
    // 나머지 정적 연결
    if (!gst_element_link_many(convert, scaler, capsfilter, sink, NULL)) {
        std::cerr << "나머지 엘리먼트 연결 실패" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }
    
    // 버스 설정
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", G_CALLBACK(on_message), loop);
    
    // 파이프라인 시작
    std::cout << "재생 시작..." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    // 메인루프 실행
    g_main_loop_run(loop);
    
    // 정리
    std::cout << "종료 중..." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    
    return 0;
}