#pragma once

#include "hailo/hailort.hpp"
#include <opencv2/opencv.hpp>


using namespace hailort;



Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, std::string hef_file);
cv::Mat infer(InferVStreams &pipeline, cv::Mat input_img);

