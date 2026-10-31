#include <libheif/heif.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void checked(heif_error error)
{
    if (error.code != heif_error_Ok) throw std::runtime_error(error.message ? error.message : "HEIC encoding failed.");
}

heif_error writeOutput(heif_context*, void const* bytes, size_t count, void* user)
{
    auto& file = *static_cast<std::ofstream*>(user);
    file.write(static_cast<char const*>(bytes), std::streamsize(count));
    return {file ? heif_error_Ok : heif_error_Encoding_error, heif_suberror_Unspecified, file ? nullptr : "Cannot write the HEIC output."};
}

}

int wmain(int argc, wchar_t** argv)
{
    try {
        if (argc != 8) throw std::runtime_error("Usage: eclipse-heif width height bits rgba16-file output-file icc-file-or-dash quality");
        int const width = std::stoi(argv[1]), height = std::stoi(argv[2]), bits = std::stoi(argv[3]);
        if (width < 1 || height < 1 || int64_t(width) * height > 32'000'000 || (bits != 8 && bits != 10 && bits != 12)) throw std::runtime_error("Invalid HEIC image size or bit depth.");
        int const quality = std::clamp(std::stoi(argv[7]),0,100);
        std::ifstream input(std::filesystem::path(argv[4]),std::ios::binary);
        std::vector<uint16_t> pixels(size_t(width) * size_t(height) * 4);
        std::streamsize const bytes = std::streamsize(pixels.size() * sizeof(uint16_t));
        if (!input.read(reinterpret_cast<char*>(pixels.data()),bytes)) throw std::runtime_error("Cannot read HEIC input pixels.");
        checked(heif_init(nullptr));
        struct Library { ~Library() { heif_deinit(); } } library;
        std::unique_ptr<heif_context,decltype(&heif_context_free)> context(heif_context_alloc(),&heif_context_free);
        if (!context) throw std::runtime_error("Cannot allocate a HEIC context.");
        heif_encoder* encoderRaw = nullptr;
        checked(heif_context_get_encoder_for_format(context.get(),heif_compression_HEVC,&encoderRaw));
        std::unique_ptr<heif_encoder,decltype(&heif_encoder_release)> encoder(encoderRaw,&heif_encoder_release);
        checked(heif_encoder_set_lossy_quality(encoder.get(),quality));
        checked(heif_encoder_set_lossless(encoder.get(),quality==100));
        checked(heif_encoder_set_parameter_string(encoder.get(),"chroma","444"));
        checked(heif_encoder_set_parameter_string(encoder.get(),"preset","fast"));
        heif_image* imageRaw = nullptr;
        heif_chroma const chroma = bits==8 ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RRGGBBAA_LE;
        checked(heif_image_create(width,height,heif_colorspace_RGB,chroma,&imageRaw));
        std::unique_ptr<heif_image,decltype(&heif_image_release)> image(imageRaw,&heif_image_release);
        checked(heif_image_add_plane(image.get(),heif_channel_interleaved,width,height,bits));
        size_t stride = 0;
        uint8_t* const plane = heif_image_get_plane2(image.get(),heif_channel_interleaved,&stride);
        size_t const pixelBytes = bits==8 ? 4 : 8;
        if (!plane || stride < size_t(width)*pixelBytes) throw std::runtime_error("Invalid HEIC pixel plane.");
        for (int y=0;y<height;++y)
            for (int x=0;x<width*4;++x) {
                unsigned const value = (unsigned(pixels[size_t(y)*size_t(width)*4+size_t(x)]) * ((1u<<bits)-1) + 32767) / 65535;
                if (bits==8) plane[size_t(y)*stride+size_t(x)] = uint8_t(value);
                else reinterpret_cast<uint16_t*>(plane+size_t(y)*stride)[x] = uint16_t(value);
            }
        if (std::wstring(argv[6]) != L"-") {
            std::ifstream profileFile(std::filesystem::path(argv[6]),std::ios::binary);
            std::vector<char> const profile{std::istreambuf_iterator<char>(profileFile),std::istreambuf_iterator<char>()};
            if (profile.empty() || profile.size()>16*1024*1024) throw std::runtime_error("Invalid HEIC colour profile.");
            checked(heif_image_set_raw_color_profile(image.get(),"prof",profile.data(),profile.size()));
        }
        heif_image_handle* handleRaw = nullptr;
        checked(heif_context_encode_image(context.get(),image.get(),encoder.get(),nullptr,&handleRaw));
        std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)> handle(handleRaw,&heif_image_handle_release);
        std::ofstream output(std::filesystem::path(argv[5]),std::ios::binary|std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open the HEIC output.");
        heif_writer writer{1,&writeOutput};
        checked(heif_context_write(context.get(),&writer,&output));
        output.flush();
        if (!output) throw std::runtime_error("Cannot finish the HEIC output.");
        return 0;
    } catch (std::exception const& error) {
        std::fprintf(stderr,"%s\n",error.what());
        return 1;
    }
}
