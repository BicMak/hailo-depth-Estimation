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

#include "Hailoinfer.hpp"
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




Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(VDevice &vdevice, std::string hef_file)
{
    //hailo model load
    auto hef = Hef::create(hef_file);
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



cv::Mat infer(InferVStreams &pipeline, cv::Mat input_img){
    // 1. CPU: input_data 생성 (Rasp RAM)
    // 2. CPU → NPU: PCIe write (데이터 복사)
    // 3. NPU: 연산 실행
    // 4. CPU ← NPU: PCIe read (결과 복사)
    // 5. CPU: output_data에 저장 (Rasp RAM)
    //load image file
    const size_t frames_count = FRAMES_COUNT;
    //Creates input virtual stream params.
    auto input_vstreams = pipeline.get_input_vstreams();
    
    if (input_vstreams.empty()) {
        std::cerr << "No input vstreams found!" << std::endl;
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
    std::cout << "Normalized size: " << input_img.total() * input_img.channels() << std::endl;
    
    auto input_info = input_vstreams[0].get().get_info();
    std::cout << "Format type: " << input_info.format.type << std::endl;
    std::cout << "Format order: " << input_info.format.order << std::endl;


    // 크기 검증
    if (input_data[stream_name].size() != input_img.total() * input_img.channels()) {
        std::cerr << "Size mismatch! Cannot copy data safely." << std::endl;
    }
    
    std::memcpy(
        input_data[stream_name].data(),  // 이미 만들어진 버퍼
        input_img.data,                  // 이미지 데이터
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

    std::string out_name = output_vstreams[0].get().name();
    auto& result_buffer = output_data[out_name];

    if (result_buffer.empty()) {
        std::cerr << "Output buffer is empty!" << std::endl;
    }

    std::cout << "\n[Output Stream: " << out_name << "]" << std::endl;
    std::cout << "Buffer size: " << result_buffer.size() << " bytes" << std::endl;
    std::cout << "Frame size: " << output_vstreams[0].get().get_frame_size() << " bytes" << std::endl;
    std::cout << "Data type: int8_t" << std::endl;   

    cv::Mat depth_map(256, 256, CV_8SC1, result_buffer.data());
    cv::Mat depth_uint8;
    depth_map.convertTo(depth_uint8, CV_8U, 1.0, 128);  // value * 1.0 + 128
        
    
    return depth_uint8.clone();
}

