#include <RainyCreek/Capture.hpp>
#include <fstream>
#include <limits>
#include <stdexcept>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace RainyCreek {
namespace {
void WriteBytes(void* context, void* data, int size) {
    static_cast<std::ostream*>(context)->write(static_cast<const char*>(data), size);
}
} // namespace

void WriteCapture(LuxEngine::GpuDevice& device, const std::filesystem::path& path) {
    LuxEngine::GpuFrameCapture image;
    if (!device.ReadFrameCapture(image) || image.width == 0 || image.height == 0 ||
        image.width > static_cast<unsigned>(std::numeric_limits<int>::max() / 4) ||
        image.height > static_cast<unsigned>(std::numeric_limits<int>::max()) ||
        image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4)
        throw std::runtime_error("GPU 화면 캡처를 읽지 못했습니다.");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        throw std::runtime_error("캡처 파일을 열지 못했습니다.");
    if (path.extension() == ".png") {
        const auto width = static_cast<int>(image.width);
        if (!stbi_write_png_to_func(WriteBytes, &stream, width, static_cast<int>(image.height), 4,
                                    image.rgba.data(), width * 4))
            throw std::runtime_error("PNG 캡처를 인코딩하지 못했습니다.");
    } else {
        stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
        for (size_t i = 0; i < image.rgba.size(); i += 4)
            stream.write(reinterpret_cast<const char*>(image.rgba.data() + i), 3);
    }
    stream.close();
    if (!stream)
        throw std::runtime_error("캡처 파일을 저장하지 못했습니다.");
}

} // namespace RainyCreek
