/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_pipeline_example.cpp
 * This example demonstrates the basic data-path on HailoRT using the high level API - Virtual Stream Pipeline.
 * The program creates a virtual device, generates a random dataset,
 * and runs it through a Hailo device with virtual streams pipeline.
 **/

#include <opencv2/opencv.hpp>
#include <iostream>
#include <gst/gst.h>
#include <glib.h>
#include <iostream>
#include <string>
#include <cstdio>
#include <memory>
#include <array>

#include "hailo/hailort.hpp"
#include "hailo/hailort_common.hpp" 

const std::string HEF_FILE = "./hefs/Midas_v2_small_model.hef";
constexpr size_t FRAMES_COUNT = 1;
constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;

using namespace hailort;

Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice)
{
    //hailo model load
    auto hef = Hef::create(HEF_FILE);
    if (!hef) {
        return make_unexpected(hef.status());
    }

    auto& hef_values = hef.value();

    auto names = hef_values.get_network_groups_names();
    auto input_infos = hef_values.get_input_vstream_infos();
    if (input_infos) {
        for (const auto& info : input_infos.value()) {
            std::cout << "=== Input VStream ===" << std::endl;
            std::cout << "Name: " << info.name << std::endl;
            std::cout << "Format type: " << HailoRTCommon::get_format_type_str(info.format.type) << std::endl;
            std::cout << "Format order: " << HailoRTCommon::get_format_order_str(info.format.order) << std::endl;
            std::cout << "Shape - height: " << info.shape.height 
                    << ", width: " << info.shape.width 
                    << ", features: " << info.shape.features << std::endl;
            std::cout << std::endl;
        }
    }

    //Creates the default configure params for the Hef. 
    auto configure_params = vdevice.create_configure_params(hef.value());
    if (!configure_params) {
        return make_unexpected(configure_params.status());
    }

    auto network_groups = vdevice.configure(hef.value(), configure_params.value());
    if (!network_groups) {
        return make_unexpected(network_groups.status());
    }

    if (1 != network_groups->size()) {
        std::cerr << "Invalid amount of network groups" << std::endl;
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return std::move(network_groups->at(0));
}



hailo_status infer(InferVStreams &pipeline){
    // 1. CPU: input_data 생성 (Rasp RAM)
    // 2. CPU → NPU: PCIe write (데이터 복사)
    // 3. NPU: 연산 실행
    // 4. CPU ← NPU: PCIe read (결과 복사)
    // 5. CPU: output_data에 저장 (Rasp RAM)
    //load image file
    std::string image_path = "./Images/";
    std::string image_name = "street-view.jpg";
    std::cout << image_path + image_name << std::endl;
    cv::Mat image = cv::imread(image_path + image_name);
    if (image.empty()) {
        std::cout << "이미지 로드 실패" << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }
    cv::Size input_shape(256, 256);
    // 리사이즈
    cv::Mat resized;
    cv::resize(image, resized, 
            cv::Size(input_shape.width, input_shape.height));
    // BGR → RGB
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    const size_t frames_count = FRAMES_COUNT;
    //Creates input virtual stream params.
    auto input_vstreams = pipeline.get_input_vstreams();
    
    if (input_vstreams.empty()) {
        std::cerr << "No input vstreams found!" << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }
    
    //스트림 이름을 key로, 필요한 만큼의 버퍼를 value로 map에 저장
    std::map<std::string, std::vector<uint8_t>> input_data;
    for (const auto &input_vstream : input_vstreams) {
        input_data.emplace(
            input_vstream.get().name(), 
            std::vector<uint8_t>(input_vstream.get().get_frame_size() * frames_count)
        );
    }
    std::string stream_name = input_vstreams[0].get().name();
    auto input_vstream_info = input_vstreams[0].get().get_info();
    std::cout << "Input shape: " 
            << input_vstream_info.shape.height << "x"
            << input_vstream_info.shape.width << "x"
            << input_vstream_info.shape.features << std::endl;    

    std::cout << "Input stream name: " << stream_name << std::endl;
    std::cout << "Expected size: " << input_data[stream_name].size() << std::endl;
    std::cout << "Normalized size: " << rgb.total() * rgb.channels() << std::endl;
    
    auto input_info = input_vstreams[0].get().get_info();
    std::cout << "Format type: " << input_info.format.type << std::endl;
    std::cout << "Format order: " << input_info.format.order << std::endl;


    // 크기 검증
    if (input_data[stream_name].size() != rgb.total() * rgb.channels()) {
        std::cerr << "Size mismatch! Cannot copy data safely." << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }
    
    std::memcpy(
        input_data[stream_name].data(),  // 이미 만들어진 버퍼
        rgb.data,                  // 이미지 데이터
        input_data[stream_name].size()
    );
    //스트림 이름을 key로, 필요한 만큼의 버퍼를 value로 map에 저장
    //메모리에 있는값을 바로 헤일로의 NPU캐시로 보내라
    std::map<std::string, MemoryView> input_data_mem_views;
    for (const auto &input_vstream : input_vstreams) {
        auto &input_buffer = input_data[input_vstream.get().name()];
        input_data_mem_views.emplace(input_vstream.get().name(), MemoryView(input_buffer.data(), input_buffer.size()));
    }
    //Creates output virtual stream params.
    auto output_vstreams = pipeline.get_output_vstreams();
    
    if (output_vstreams.empty()) {
        std::cerr << "No output vstreams found!" << std::endl;
        return HAILO_INTERNAL_FAILURE;
    }
    
    std::map<std::string, std::vector<uint8_t>> output_data;
    for (const auto &output_vstream : output_vstreams) {
        output_data.emplace(output_vstream.get().name(), std::vector<uint8_t>(output_vstream.get().get_frame_size() * frames_count));
    }
    std::map<std::string, MemoryView> output_data_mem_views;
    for (const auto &output_vstream : output_vstreams) {
        auto &output_buffer = output_data[output_vstream.get().name()];
        output_data_mem_views.emplace(output_vstream.get().name(), MemoryView(output_buffer.data(), output_buffer.size()));
    }
    
    hailo_status status = pipeline.infer(input_data_mem_views, output_data_mem_views, frames_count);
    
    // Status 출력s
    std::cout << "\n=== Inference Status ===" << std::endl;
    std::cout << "Status code: " << status << std::endl;
    std::cout << "Status string: " << (status == HAILO_SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    
    if (status != HAILO_SUCCESS) {
        std::cerr << "Inference failed!" << std::endl;
        return status;
    }
    
    // Output data 정보 출력
    std::cout << "\n=== Output Data Info ===" << std::endl;

    for (const auto &output_vstream : output_vstreams) {
        std::string out_name = output_vstream.get().name();
        auto& result_buffer = output_data[out_name];
        
        if (result_buffer.empty()) {
            std::cerr << "Output buffer is empty!" << std::endl;
            continue;
        }
        
        std::cout << "\n[Output Stream: " << out_name << "]" << std::endl;
        std::cout << "Buffer size: " << result_buffer.size() << " bytes" << std::endl;
        std::cout << "Frame size: " << output_vstream.get().get_frame_size() << " bytes" << std::endl;
        std::cout << "Data type: int8_t" << std::endl;
        
        // 처음 10개 값 출력 (int8)
        std::cout << "First 10 values (int8): ";
        for (size_t i = 0; i < std::min(size_t(10), result_buffer.size()); i++) {
            std::cout << (int)static_cast<int8_t>(result_buffer[i]) << " ";
        }
        std::cout << std::endl;
        
        // ========== 시각화 ==========
        std::cout << "\n=== Visualizing Depth Map ===" << std::endl;
        
        // int8 데이터를 직접 사용 (256x256 예상)
        int height = 256;
        int width = 256;
        int expected_pixels = height * width;
        
        if (result_buffer.size() != expected_pixels) {
            std::cerr << "Warning: Expected " << expected_pixels << " pixels but got " << result_buffer.size() << std::endl;
            // 실제 크기로 재계산
            height = static_cast<int>(std::sqrt(result_buffer.size()));
            width = height;
            std::cout << "Using size: " << width << "x" << height << std::endl;
        }
        
        // int8 데이터를 Mat으로 변환 (CV_8SC1 = signed char 1채널)
        cv::Mat depth_map(height, width, CV_8SC1, result_buffer.data());
        
        // int8 (-128~127) → uint8 (0~255) 변환
        cv::Mat depth_uint8;
        depth_map.convertTo(depth_uint8, CV_8U, 1.0, 128);  // value * 1.0 + 128
        
        // 정규화 (0-255 stretch)
        cv::Mat depth_normalized;
        cv::normalize(depth_uint8, depth_normalized, 0, 255, cv::NORM_MINMAX);
        
        // 컬러맵 적용
        cv::Mat depth_colormap;
        cv::applyColorMap(depth_normalized, depth_colormap, cv::COLORMAP_MAGMA);
        
        // 원본 이미지 리사이즈
        cv::Mat original_resized;
        cv::resize(image, original_resized, cv::Size(width, height));
        
        // 화면에 표시
        cv::imshow("Original Image", original_resized);
        cv::imshow("Depth Map (Grayscale)", depth_normalized);
        cv::imshow("Depth Map (Colormap)", depth_colormap);
        
        std::cout << "Press any key to close windows..." << std::endl;
        cv::waitKey(0);
        cv::destroyAllWindows();
    }

    return status;
}

int main(){
    auto vdevice = VDevice::create();
    if (!vdevice) {
        std::cerr << "Failed to create vdevice, status = " << vdevice.status() << std::endl;
        return vdevice.status();
    }

    auto network_group = configure_network_group(*vdevice.value());
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



    auto status = infer(pipeline.value());
    if (HAILO_SUCCESS == status) {
        std::cout << "Inference finished successfully" << std::endl;
    }
    
    return status;
    
}

// int main()
// {

// }
