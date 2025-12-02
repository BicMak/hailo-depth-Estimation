#pragma once

#include "hailo/hailort.hpp"
#include <opencv2/opencv.hpp>


using namespace hailort;

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, std::string hef_file);
cv::Mat infer(InferVStreams &pipeline, cv::Mat input_img);

// int main(){
//     auto vdevice = VDevice::create();
//     if (!vdevice) {
//         std::cerr << "Failed to create vdevice, status = " << vdevice.status() << std::endl;
//         return vdevice.status();
//     }

//     auto network_group = configure_network_group(*vdevice.value());
//     if (!network_group) {
//         std::cerr << "Failed to configure network group " << HEF_FILE << std::endl;
//         return network_group.status();
//     }

//     auto input_params = network_group.value()->make_input_vstream_params({}, FORMAT_TYPE, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
//     if (!input_params) {
//         std::cerr << "Failed make_input_vstream_params " << input_params.status() << std::endl;
//         return input_params.status();
//     }

//     auto output_params = network_group.value()->make_output_vstream_params({}, FORMAT_TYPE, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
//     if (!output_params) {
//         std::cerr << "Failed make_output_vstream_params " << output_params.status() << std::endl;
//         return output_params.status();
//     }

//     auto pipeline = InferVStreams::create(*network_group.value(), input_params.value(), output_params.value());
//     if (!pipeline) {
//         std::cerr << "Failed to create inference pipeline " << pipeline.status() << std::endl;
//         return pipeline.status();
//     }

//     auto status = infer(pipeline.value());
//     if (HAILO_SUCCESS == status) {
//         std::cout << "Inference finished successfully" << std::endl;
//     }
    
//     return status;
    
// }

// int main()
// {

// }
