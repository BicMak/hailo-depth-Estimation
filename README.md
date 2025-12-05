# hailo-depth-Estimation
This project implements a real-time depth estimation inference application using the MiDaS v2 depth model. The model has been quantized for Hailo-8 NPU deployment in a previous project (https://github.com/BicMak/hailo-depth-quantization).

### **keypoint**
  - Deployed monocular depth estimation on Hailo NPU + Raspberry Pi platform
  - On-device monocular depth estimation with real-time inference
  - Integrated GStreamer-based inference pipeline for streamlined video processing

## each process time statics

| target | Preprocess | Infer | Postprocess | Total |
|:---:|---:|---:|---:|---:|
| **mean** | 0.13 ms | 38.64 ms | 2.46 ms | 42.57 ms |
| **median** | 0.00 ms | 39.00 ms | 2.00 ms | 43.00 ms |
| **min** | 0 ms | 28 ms | 2 ms | 34 ms |
| **max** | 6 ms | 49 ms | 14 ms | 61 ms |
| **std** | 0.54 ms | 1.56 ms | 1.18 ms | 1.83 ms |
- The bottleneck is the NPU inference stage
- NPU inference time is difficult to reduce further, as there is an inherent trade-off between model precision and inference speed

## Pipelin Structure
### 1. video input pipeline
<img width="1918" height="241" alt="v412src" src="https://github.com/user-attachments/assets/2bdbe730-f7f0-4d4a-9e24-844276cd774d" />

- v4l2src :capture video from v4l2 devices "/dev/video0".
- videoconvert : Convert captured YUV image to RGB format
- videoscale
  - Resize the input x-raw rgb image to 640×480 pixel
  - Inference input size is 256×256, but the depth map is visualized on the original 640×480 resolution
- queue : make a buffer to save inputs
- Appsink
  - return the image to application to infer the depth estimation
  - its visualize the depth map and make before after image

### 2. video output pipeline
<img width="1920" height="360" alt="Appsink" src="https://github.com/user-attachments/assets/842c3ecc-02b6-4e0e-b7b4-ec56e906877b" />

- app_src :Receive already concatenated 1280×480 image from application
- videoconvert 
  - Format conversion only: BGR → RGB
  - concat origial image and depthmap image in same image 
- autovideosink : Visualize the concatenated image on screen

## Reference
- [Gstreamer](https://gstreamer.freedesktop.org/documentation/tutorials/basic/index.html?gi-language=c)
- [HailoRT](https://github.com/hailo-ai/hailort)
- [HailoTappas](https://github.com/hailo-ai/tappas)
