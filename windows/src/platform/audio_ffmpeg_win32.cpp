#include "audio_ffmpeg_win32.h"
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
extern "C" {
#include "libavcodec/codec_par.h"
#include "libavutil/frame.h"
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string>

namespace dokodemo::audio {
// MSVC puts a source-path hash in RTTI for types in anonymous namespaces.
namespace ffmpeg_win32 {
// FFmpeg owns all C allocations. Only the two public types for decoder
// parameters and sample planes are inspected via the pinned SDK headers.
struct AVCodec;
struct AVCodecContext;

class Library {
public:
    explicit Library(const std::filesystem::path& path)
        : handle_(LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!handle_) throw std::runtime_error("Could not load FFmpeg DLL: " + path.string());
    }
    ~Library() { FreeLibrary(handle_); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    template<class F> F Get(const char* name) const {
        FARPROC proc = GetProcAddress(handle_, name);
        if (!proc) throw std::runtime_error(std::string("Missing FFmpeg function: ") + name);
        static_assert(sizeof(proc) == sizeof(F));
        F result;
        std::memcpy(&result, &proc, sizeof(result));
        return result;
    }
private:
    HMODULE handle_;
};

struct Api {
    Library util, codec;
    unsigned (*codec_version)() = codec.Get<decltype(codec_version)>("avcodec_version");
    unsigned (*util_version)() = util.Get<decltype(util_version)>("avutil_version");
    const AVCodec* (*find)(const char*) = codec.Get<decltype(find)>("avcodec_find_decoder_by_name");
    AVCodecContext* (*alloc)(const AVCodec*) = codec.Get<decltype(alloc)>("avcodec_alloc_context3");
    void (*free_context)(AVCodecContext**) = codec.Get<decltype(free_context)>("avcodec_free_context");
    int (*open)(AVCodecContext*, const AVCodec*, AVDictionary**) = codec.Get<decltype(open)>("avcodec_open2");
    AVCodecParameters* (*params_alloc)() = codec.Get<decltype(params_alloc)>("avcodec_parameters_alloc");
    void (*params_free)(AVCodecParameters**) = codec.Get<decltype(params_free)>("avcodec_parameters_free");
    int (*from_context)(AVCodecParameters*, const AVCodecContext*) = codec.Get<decltype(from_context)>("avcodec_parameters_from_context");
    int (*to_context)(AVCodecContext*, const AVCodecParameters*) = codec.Get<decltype(to_context)>("avcodec_parameters_to_context");
    int (*set)(void*, const char*, const char*, int) = util.Get<decltype(set)>("av_opt_set");
    int (*set_int)(void*, const char*, int64_t, int) = util.Get<decltype(set_int)>("av_opt_set_int");
    AVPacket* (*packet_alloc)() = codec.Get<decltype(packet_alloc)>("av_packet_alloc");
    void (*packet_free)(AVPacket**) = codec.Get<decltype(packet_free)>("av_packet_free");
    int (*from_data)(AVPacket*, uint8_t*, int) = codec.Get<decltype(from_data)>("av_packet_from_data");
    void* (*mallocz)(size_t) = util.Get<decltype(mallocz)>("av_mallocz");
    void (*free_buffer)(void*) = util.Get<decltype(free_buffer)>("av_free");
    AVFrame* (*frame_alloc)() = util.Get<decltype(frame_alloc)>("av_frame_alloc");
    void (*frame_free)(AVFrame**) = util.Get<decltype(frame_free)>("av_frame_free");
    int (*send)(AVCodecContext*, const AVPacket*) = codec.Get<decltype(send)>("avcodec_send_packet");
    int (*receive)(AVCodecContext*, AVFrame*) = codec.Get<decltype(receive)>("avcodec_receive_frame");

    Api(const std::filesystem::path& codec_path, const std::filesystem::path& util_path)
        : util(util_path), codec(codec_path) {
        if (codec_version() != AV_VERSION_INT(63,1,102) || util_version() != AV_VERSION_INT(61,1,102))
            throw std::runtime_error("Expected libavcodec 63.1.102 and libavutil 61.1.102");
    }
};

class Decoder final : public AtracDecoder {
public:
    Decoder(std::shared_ptr<Api> api, const AVCodec* codec, AtracCodec type)
        : api_(std::move(api)), codec_(codec), type_(type), context_(api_->alloc(codec_)) {}
    ~Decoder() override { Reset(); }
    bool Available() const { return context_ != nullptr; }
    void Reset() noexcept override {
        api_->frame_free(&frame_);
        api_->free_context(&context_);
        block_ = rate_ = 0;
    }
    bool DecodePlusPacket(const uint8_t* data, size_t size, unsigned sample_rate) override {
        if (type_ != AtracCodec::Plus || !data || !size || size > INT_MAX || !sample_rate) return false;
        if (!block_ && !Configure({type_,sample_rate,static_cast<unsigned>(size),{}})) return false;
        AtracPcmFrame ignored;
        return rate_ == sample_rate && DecodePacket(data,size,ignored);
    }
    bool Configure(const AtracFormat& fmt) override {
        if (fmt.codec != type_ || !fmt.sample_rate || !fmt.block_size || fmt.extra.size() > 64) return false;
        auto* next = api_->alloc(codec_);
        if (!next) return false;
        auto* params = api_->params_alloc();
        auto* frame = api_->frame_alloc();
        bool ready = params && frame && api_->from_context(params,next) >= 0;
        if (ready && !fmt.extra.empty()) {
            params->extradata = static_cast<uint8_t*>(api_->mallocz(fmt.extra.size()+AV_INPUT_BUFFER_PADDING_SIZE));
            ready = params->extradata != nullptr;
            if (ready) {
                params->extradata_size = static_cast<int>(fmt.extra.size());
                std::memcpy(params->extradata,fmt.extra.data(),fmt.extra.size());
            }
        }
        ready = ready && api_->to_context(next,params) >= 0 && api_->set(next,"ch_layout","stereo",0) >= 0 &&
                api_->set_int(next,"ar",fmt.sample_rate,0) >= 0 && api_->set_int(next,"block_align",fmt.block_size,0) >= 0 &&
                api_->open(next,codec_,nullptr) == 0;
        api_->params_free(&params);
        if (!ready) {
            api_->frame_free(&frame); api_->free_context(&next);
            return false;
        }
        Reset();
        frame_ = frame; context_ = next; rate_ = fmt.sample_rate; block_ = fmt.block_size;
        return true;
    }
    bool DecodePacket(const uint8_t* data, size_t size, AtracPcmFrame& out) override {
        if (!data || !frame_ || !size || size != block_) return false;

        auto* packet = api_->packet_alloc();
        if (!packet) return false;
        // AV_INPUT_BUFFER_PADDING_SIZE for these FFmpeg majors is 64 bytes.
        auto* raw = static_cast<uint8_t*>(api_->mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!raw) { api_->packet_free(&packet); return false; }
        std::memcpy(raw, data, size);
        if (api_->from_data(packet, raw, block_) < 0) {
            api_->free_buffer(raw);
            api_->packet_free(&packet);
            return false;
        }
        const int sent = api_->send(context_, packet);
        api_->packet_free(&packet);
        if (sent != 0 || api_->receive(context_,frame_) != 0 || frame_->format != AV_SAMPLE_FMT_FLTP ||
            frame_->ch_layout.nb_channels != 2 || frame_->sample_rate != static_cast<int>(rate_) ||
            frame_->nb_samples <= 0 || frame_->nb_samples > 2048 || !frame_->data[0] || !frame_->data[1]) return false;
        out.sample_rate = rate_; out.channels = 2; out.end = false;
        out.samples.resize(static_cast<size_t>(frame_->nb_samples)*2);
        for (size_t channel = 0; channel < 2; ++channel) {
            const auto* plane = reinterpret_cast<const float*>(frame_->data[channel]);
            for (int i = 0; i < frame_->nb_samples; ++i) {
                if (!std::isfinite(plane[i])) return false;
                const float scaled = std::clamp(plane[i]*32768.0f,-32768.0f,32767.0f);
                out.samples[2*size_t(i)+channel] = static_cast<int16_t>(std::nearbyint(scaled));
            }
        }
        return true;
    }
private:
    std::shared_ptr<Api> api_;
    const AVCodec* codec_;
    AtracCodec type_;
    AVCodecContext* context_;
    AVFrame* frame_ = nullptr;
    unsigned block_ = 0, rate_ = 0;
};
class Factory final : public AtracDecoderFactory {
public:
    explicit Factory(std::shared_ptr<Api> api) : api_(std::move(api)) {}
    std::unique_ptr<AtracDecoder> Create(AtracCodec type) override {
        const auto* codec = api_->find(type == AtracCodec::Plus ? "atrac3plus" : "atrac3");
        if (!codec) return {};
        auto decoder = std::make_unique<Decoder>(api_, codec, type);
        if (!decoder->Available()) return {};
        return decoder;
    }
private:
    std::shared_ptr<Api> api_;
};
}
std::unique_ptr<AtracDecoderFactory> LoadFfmpegAtracDecoder(
    const std::filesystem::path& avcodec, const std::filesystem::path& avutil) {
    return std::make_unique<ffmpeg_win32::Factory>(std::make_shared<ffmpeg_win32::Api>(avcodec, avutil));
}
}
