#pragma once

#include "hailo/hailort.hpp"
#include <opencv2/opencv.hpp>


using namespace hailort;

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
};



Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, Config config);
cv::Mat infer(InferVStreams &pipeline, cv::Mat input_img,Config config);

