#pragma once

#include "hailo/hailort.hpp"
#include <opencv2/opencv.hpp>

using namespace hailort;

/**
 * @brief Configuration structure for depth estimation pipeline
 * 
 * Contains device, model, video I/O, and performance parameters
 */
struct Config {
    std::string device;          ///< Hailo device ID (e.g., "0")
    std::string hef_path;        ///< Path to HEF model file
    
    int model_width;             ///< Model input width in pixels (e.g., 256)
    int model_height;            ///< Model input height in pixels (e.g., 256)
    
    int video_inWidth;           ///< Camera input frame width in pixels
    int video_inHeight;          ///< Camera input frame height in pixels
    
    int video_outWidth;          ///< Display output width in pixels
    int video_outHeight;         ///< Display output height in pixels
    
    std::string output_name;     ///< Output VStream name
    
    int frame_rate;              ///< Target frame rate (FPS)
    int encode_speed;            ///< Batch size (number of frames processed simultaneously)
    int tune;                    ///< Encoding tuning parameter
    
    std::string timing_log;      ///< Path to performance measurement log file
};


Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, Config config);
cv::Mat infer(InferVStreams &pipeline, cv::Mat input_img,Config config);

