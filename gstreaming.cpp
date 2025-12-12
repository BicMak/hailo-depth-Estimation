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
#include "gstreaming.hpp" 

#include <fstream>
static const bool MONITORING = FALSE;


/**
 * @brief GStreamer bus message handler callback
 * 
 * Processes pipeline messages including errors, warnings, and EOS (End of Stream).
 * Terminates the main loop on error or stream completion.
 * 
 * @param[in] bus GStreamer message bus that delivers messages from pipeline elements
 * @param[in] message GStreamer message object containing event type and details
 * @param[in] data User data pointer, cast to GMainLoop* for loop control
 * 
 * @return gboolean Always returns TRUE to keep the callback active
 *                  (Returning FALSE would remove the callback from the bus)
 */
gboolean on_message(GstBus *bus, GstMessage *message, gpointer data) {
    GMainLoop *loop = (GMainLoop*)data;
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            // 창 닫힘은 정상 종료로 처리
            if (strstr(err->message, "Output window was closed")) {
                std::cout << "영상 창 닫힘. 파일 저장 중..." << std::endl;
            } else {
                std::cerr << "에러: " << err->message << std::endl;
                std::cerr << "디버그: " << debug << std::endl;
            }
            
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
        case GST_MESSAGE_ELEMENT: {
            const GstStructure *s = gst_message_get_structure(message);
            
            // navigation 이벤트 처리 (키보드 입력)
            if (gst_structure_has_name(s, "application/x-gst-navigation")) {
                const gchar *event = gst_structure_get_string(s, "event");
                const gchar *key = gst_structure_get_string(s, "key");
                
                if (event && key && strcmp(event, "key-press") == 0) {
                    if (strcmp(key, "q") == 0 || strcmp(key, "Q") == 0) {
                        std::cout << "q 키 눌림. 종료 중..." << std::endl;
                        g_main_loop_quit(loop);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    
    return TRUE;
}

/**
 * @brief create videoInput to appSink stream Gstreamer pipeline 
 * 
 * @param[out] pipeline Gsteamer inputpipe line 
 * @param[in] config Configuration containing videoInput stream
 */
void makeSinkpipeline(GstElement* pipeline, const Config& config){
   
    if (!gst_is_initialized()) {
        std::cerr << "GStreamer 초기화 실패" << std::endl;
    }
    
    std::cout << "GStreamer 초기화 성공" << std::endl;
    
    // 엘리먼트 생성
    GstElement *source = gst_element_factory_make("v4l2src", "source");
    GstElement *videoconvert1 = gst_element_factory_make("videoconvert", "convert1");
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
        source, videoconvert1, scaler, queue1, NULL);

    // Property 설정
    g_object_set(source, "device", config.device.c_str(), NULL);

    // Part 1 연결: source → ... → appsink
    std::string caps_str = "video/x-raw,format=RGB,width=" + 
                          std::to_string(config.video_inWidth) + 
                          ",height=" + std::to_string(config.video_inHeight);
    GstCaps *caps1 = gst_caps_from_string(caps_str.c_str());

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

/**
 * @brief create appSrc to VideoOut stream Gstreamer pipeline 
 * 
 * @param[out] pipeline Gsteamer output pipeline 
 * @param[in] config Configuration containing videoOut stream
 */
GstElement* makeSrcPipeline(GstElement* pipeline, const Config& config) {
    // 엘리먼트 생성
    GstElement *appsrc = gst_element_factory_make("appsrc", "app_src");
    GstElement *videoconvert = gst_element_factory_make("videoconvert", "convert_src");
    GstElement *tee = gst_element_factory_make("tee", "tee");
    
    // 화면 출력 브랜치
    GstElement *queue1 = gst_element_factory_make("queue", "queue_display");
    GstElement *sink = gst_element_factory_make("autovideosink", "video_sink");
    
    // 파일 저장 브랜치
    GstElement *queue2 = gst_element_factory_make("queue", "queue_file");
    GstElement *encoder = gst_element_factory_make("x264enc", "encoder");
    GstElement *muxer = gst_element_factory_make("mp4mux", "muxer");
    GstElement *filesink = gst_element_factory_make("filesink", "file_sink");
    
    // NULL 체크
    if (!appsrc || !videoconvert || !tee || !queue1 || !sink || 
        !queue2 || !encoder || !muxer || !filesink) {
        std::cerr << "Src 파이프라인 엘리먼트 생성 실패!" << std::endl;
        return nullptr;
    }
    
    // 파이프라인에 추가
    gst_bin_add_many(GST_BIN(pipeline), 
                     appsrc, videoconvert, tee,
                     queue1, sink,
                     queue2, encoder, muxer, filesink,
                     NULL);
    
    // appsrc 설정
    std::string caps_str = "video/x-raw,format=RGB,width=" + 
                          std::to_string(config.video_outWidth) + 
                          ",height=" + std::to_string(config.video_outHeight) +
                          ",framerate=" + std::to_string(config.frame_rate) + "/1";
    
    GstCaps *caps = gst_caps_from_string(caps_str.c_str());
    g_object_set(appsrc,
                 "caps", caps,
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 NULL);
    gst_caps_unref(caps);
    
    // filesink 설정
    g_object_set(filesink, 
                 "location", config.output_name.c_str(),
                 NULL);
    
    // encoder 설정 - tune을 int flags 값으로 설정
    g_object_set(encoder,
                 "speed-preset", config.encode_speed,
                 "tune", config.tune,  // int flags 값
                 NULL);
    
    // 메인 라인 링크
    if (!gst_element_link_many(appsrc, videoconvert, tee, NULL)) {
        std::cerr << "메인 파이프라인 링크 실패" << std::endl;
        return nullptr;
    }
    
    // 화면 출력 브랜치 링크
    if (!gst_element_link_many(queue1, sink, NULL)) {
        std::cerr << "화면 출력 브랜치 링크 실패" << std::endl;
        return nullptr;
    }
    
    // 파일 저장 브랜치 링크
    if (!gst_element_link_many(queue2, encoder, muxer, filesink, NULL)) {
        std::cerr << "파일 저장 브랜치 링크 실패" << std::endl;
        return nullptr;
    }
    
    // tee 패드 연결
    GstPad *tee_src1 = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *queue1_sink = gst_element_get_static_pad(queue1, "sink");
    gst_pad_link(tee_src1, queue1_sink);
    
    GstPad *tee_src2 = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *queue2_sink = gst_element_get_static_pad(queue2, "sink");
    gst_pad_link(tee_src2, queue2_sink);
    
    // 패드 언레프
    gst_object_unref(tee_src1);
    gst_object_unref(queue1_sink);
    gst_object_unref(tee_src2);
    gst_object_unref(queue2_sink);
    
    return appsrc;
}


/**
 * @brief GStreamer callback function for processing video frames through NPU inference pipeline
 * 
 * This callback is triggered when a new frame arrives at the appsink. It performs the complete
 * inference pipeline: preprocessing → NPU inference → postprocessing, then pushes the result
 * to appsrc for display.
 * 
 * Processing steps:
 * 1. Pull frame from appsink
 * 2. Preprocessing: Gaussian blur and resize to model input dimensions
 * 3. NPU inference: Depth estimation using Hailo-8
 * 4. Postprocessing: Normalize, apply colormap, resize back, concatenate with original frame
 * 5. Push processed frame to appsrc
 * 6. Log timing metrics (preprocessing, inference, postprocessing, total)
 * 
 * @param[in] sink GStreamer appsink element providing input frames
 * @param[in] user_data Pointer to CallbackData struct containing:
 *                      - infer_pipeline: NPU inference VStreams
 *                      - appsrc: GStreamer appsrc element for output
 *                      - config: Pipeline configuration (dimensions, paths, etc.)
 *                      - log_file: Output stream for performance logging
 *                      - header_written: Flag for CSV header initialization
 * 
 * @return GstFlowReturn status code
 *         - GST_FLOW_OK: Frame processed and pushed successfully
 *         - GST_FLOW_ERROR: Processing failed (empty inference result, buffer allocation error, etc.)
 */
GstFlowReturn new_sample_callback(GstElement *sink, gpointer user_data) {
    auto t_start = std::chrono::high_resolution_clock::now();
    
    // user_data에서 필요한 데이터 꺼내기
    CallbackData* cb_data = static_cast<CallbackData*>(user_data);
    InferVStreams* infer_pipeline = cb_data->infer_pipeline;
    GstElement* appsrc = cb_data->appsrc;
    const Config* config = cb_data->config;
    std::ofstream* log_file = cb_data->log_file;
    bool* header_written = cb_data->header_written;
    
    // 1. appsink에서 sample 가져오기
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    
    // ========== 전처리 시작 ==========
    auto t_preprocess_start = std::chrono::high_resolution_clock::now();
    
    cv::Mat raw_img(config->video_inHeight, config->video_inWidth, CV_8UC3, map.data);
    cv::GaussianBlur(raw_img, raw_img, cv::Size(3, 3), 0);
    cv::Mat input_img;
    cv::resize(raw_img, input_img, 
               cv::Size(config->model_width, config->model_height), 
               0, 0, cv::INTER_LINEAR);
    
    auto t_preprocess_end = std::chrono::high_resolution_clock::now();
    
    // ========== 추론 시작 ==========
    auto t_infer_start = std::chrono::high_resolution_clock::now();    

    if (MONITORING) std::cout << ">>> BEFORE infer() call" << std::endl;
    cv::Mat output_img;
    output_img = infer(*infer_pipeline, input_img, *config);
    if (MONITORING) std::cout << ">>> AFTER infer() call" << std::endl;

    // 반환값 검증
    if (output_img.empty()) {
        std::cerr << "❌ infer() returned empty Mat!" << std::endl;
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    if (MONITORING) std::cout << "✅ infer() returned valid Mat: " << output_img.size() << std::endl;

    auto t_infer_end = std::chrono::high_resolution_clock::now();
    
    // ========== 후처리 시작 ==========
    auto t_postprocess_start = std::chrono::high_resolution_clock::now();

    if (MONITORING) std::cout << ">>> [POST-1] Starting normalize..." << std::endl;
    cv::Mat depth_normalized;
    cv::normalize(output_img, depth_normalized, 0, 255, cv::NORM_MINMAX);
    if (MONITORING) std::cout << "    ✓ normalize done: " << depth_normalized.size() << std::endl;

    if (MONITORING) std::cout << ">>> [POST-2] Starting applyColorMap..." << std::endl;
    cv::Mat depth_colormap;
    cv::applyColorMap(depth_normalized, depth_colormap, cv::COLORMAP_MAGMA);
    if (MONITORING) std::cout << "    ✓ colormap done: " << depth_colormap.size() << std::endl;

    if (MONITORING) std::cout << ">>> [POST-3] Starting cvtColor..." << std::endl;
    cv::cvtColor(depth_colormap, depth_colormap, cv::COLOR_RGB2BGR);
    if (MONITORING) std::cout << "    ✓ cvtColor done" << std::endl;

    if (MONITORING) {
        std::cout << ">>> [POST-4] Starting resize to " 
                  << config->video_inWidth << "x" << config->video_inHeight << "..." << std::endl;
    }
    cv::Mat depth_resized;
    cv::resize(depth_colormap, depth_resized, 
               cv::Size(config->video_inWidth, config->video_inHeight), 
               0, 0, cv::INTER_LINEAR);
    if (MONITORING) std::cout << "    ✓ resize done: " << depth_resized.size() << std::endl;

    if (MONITORING) std::cout << ">>> [POST-5] Starting convertTo..." << std::endl;
    depth_resized.convertTo(depth_resized, CV_8UC3);
    if (MONITORING) std::cout << "    ✓ convertTo done" << std::endl;

    if (MONITORING) {
        // hconcat 전에 이것들 확인:
        std::cout << ">>> Checking raw_img..." << std::endl;
        std::cout << "    - empty: " << raw_img.empty() << std::endl;
        std::cout << "    - isContinuous: " << raw_img.isContinuous() << std::endl;
        std::cout << "    - data ptr: " << (void*)raw_img.data << std::endl;
        std::cout << "    - type: " << raw_img.type() << " (CV_8UC3=" << CV_8UC3 << ")" << std::endl;

        std::cout << ">>> Checking depth_resized..." << std::endl;
        std::cout << "    - empty: " << depth_resized.empty() << std::endl;
        std::cout << "    - isContinuous: " << depth_resized.isContinuous() << std::endl;
        std::cout << "    - data ptr: " << (void*)depth_resized.data << std::endl;
        std::cout << "    - type: " << depth_resized.type() << " (CV_8UC3=" << CV_8UC3 << ")" << std::endl;

        // 메모리 접근 테스트
        try {
            uchar test1 = raw_img.at<cv::Vec3b>(0, 0)[0];
            uchar test2 = depth_resized.at<cv::Vec3b>(0, 0)[0];
            std::cout << "    ✓ Memory access OK" << std::endl;
        } catch (...) {
            std::cerr << "    ✗ Memory access FAILED!" << std::endl;
        }
    }

    if (MONITORING) {
        std::cout << ">>> [POST-6] Starting hconcat (raw_img: " 
                  << raw_img.size() << ", depth_resized: " << depth_resized.size() << ")..." << std::endl;
    }
    cv::Mat result;
    cv::hconcat(raw_img, depth_resized, result);
    if (MONITORING) std::cout << "    ✓ hconcat done: " << result.size() << std::endl;
    auto t_postprocess_end = std::chrono::high_resolution_clock::now();

    // ===== appsrc로 push =====
    if (MONITORING) {
        std::cout << ">>> [GST-1] Calculating buffer size..." << std::endl;
    }
    gsize size = result.total() * result.elemSize();
    if (MONITORING) {
        std::cout << "    - Size: " << size << " bytes (" << size/1024/1024.0 << " MB)" << std::endl;
        std::cout << "    - result.data: " << (void*)result.data << std::endl;
        std::cout << "    - result.isContinuous: " << result.isContinuous() << std::endl;
    }

    if (MONITORING) std::cout << ">>> [GST-2] Allocating GstBuffer..." << std::endl;
    GstBuffer *out_buffer = gst_buffer_new_allocate(NULL, size, NULL);
    if (!out_buffer) {
        std::cerr << "    ✗ gst_buffer_new_allocate FAILED!" << std::endl;
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    if (MONITORING) std::cout << "    ✓ GstBuffer allocated" << std::endl;

    if (MONITORING) std::cout << ">>> [GST-3] Mapping GstBuffer..." << std::endl;
    GstMapInfo out_map;
    if (!gst_buffer_map(out_buffer, &out_map, GST_MAP_WRITE)) {
        std::cerr << "    ✗ gst_buffer_map FAILED!" << std::endl;
        gst_buffer_unref(out_buffer);
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    if (MONITORING) std::cout << "    ✓ GstBuffer mapped, out_map.data: " << (void*)out_map.data << std::endl;

    if (MONITORING) std::cout << ">>> [GST-4] Copying data (memcpy " << size << " bytes)..." << std::endl;
    memcpy(out_map.data, result.data, size);
    if (MONITORING) std::cout << "    ✓ memcpy done" << std::endl;

    if (MONITORING) std::cout << ">>> [GST-5] Unmapping buffer..." << std::endl;
    gst_buffer_unmap(out_buffer, &out_map);
    if (MONITORING) std::cout << "    ✓ Unmapped" << std::endl;

    if (MONITORING) std::cout << ">>> [GST-6] Pushing to appsrc..." << std::endl;
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), out_buffer);
    if (MONITORING) std::cout << "    ✓ Push complete, return: " << ret << std::endl;
    auto t_end = std::chrono::high_resolution_clock::now();

    // ========== 시간 계산 및 출력 ==========
    auto preprocess_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_preprocess_end - t_preprocess_start).count();
    auto infer_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_infer_end - t_infer_start).count();
    auto postprocess_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_postprocess_end - t_postprocess_start).count();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    
    if (!(*header_written)) {
        (*log_file) << "Timestamp(ms),Preprocess(ms),Infer(ms),Postprocess(ms),Total(ms)\n";
        *header_written = true;
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    (*log_file) << timestamp << ","
                << preprocess_time << ","
                << infer_time << ","
                << postprocess_time << ","
                << total_time << "\n";
    
    // 성능 측정 결과는 항상 출력
    std::cout << "⏱️  전처리: " << preprocess_time << "ms | "
              << "추론: " << infer_time << "ms | "
              << "후처리: " << postprocess_time << "ms | "
              << "전체: " << total_time << "ms" << std::endl;
    
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    
    return GST_FLOW_OK;
}